#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "led_controller.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "camera_server.h" 
#include "camera_config.h"
#include "audio_transport.h" 
#include "driver/i2s_std.h"

// --- Внешние ресурсы и функции (согласованные типы) ---
extern i2s_chan_handle_t tx_chan;
extern camera_fb_t* camera_capture(void);
extern void camera_return_fb(camera_fb_t *fb);

// Явные объявления для линковщика (чтобы не было implicit declaration)
extern esp_err_t audio_i2s_init(void);
extern void audio_enable_tx(bool enable);
extern void audio_start_recording(void);
extern void audio_stop_recording(void);

static const char *TAG = "DRIVEBOT_MAIN";
static httpd_handle_t http_server = NULL;
static bool camera_started = false;

// ==================== HTTP START КАМЕРЫ ====================
static esp_err_t start_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "📷 Принудительный запуск камеры");
    if (!camera_started) {
        camera_server_init(); 
        camera_started = true;
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// ==================== HTTP MJPEG ВИДЕО (ПОРТ 81) ====================
static esp_err_t stream_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    if (!camera_started) {
        camera_server_init();
        camera_started = true;
    }

    ESP_LOGI(TAG, "📹 Клиент запросил видеопоток");
    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (true) {
        camera_fb_t *fb = camera_capture();
        if (fb && fb->len > 100) {
            if (fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
                char header[64];
                int header_len = snprintf(header, sizeof(header),
                    "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %zu\r\n\r\n", fb->len);

                if (httpd_resp_send_chunk(req, header, header_len) != ESP_OK ||
                    httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK ||
                    httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
                    camera_return_fb(fb);
                    break;
                }
            }
            camera_return_fb(fb);
            vTaskDelay(pdMS_TO_TICKS(66));
        } else {
            if (fb) camera_return_fb(fb);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.lru_purge_enable = true;
    config.stack_size = 16384;

    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_uri_t start_uri = { .uri = "/start", .method = HTTP_GET, .handler = start_handler };
        httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET, .handler = stream_handler };
        httpd_register_uri_handler(http_server, &start_uri);
        httpd_register_uri_handler(http_server, &stream_uri);
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ==================== UDP КОМАНДЫ (ПОЛНЫЙ СПИСОК) ====================
static int udp_cmd_sock = -1;
static bool udp_running = false;

static void udp_command_task(void *pvParameters) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t socklen = sizeof(client_addr);
    char buffer[128];

    udp_cmd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    bind(udp_cmd_sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    ESP_LOGI(TAG, "✅ UDP Сервер готов (порт 8080)");

    while (udp_running) {
        int len = recvfrom(udp_cmd_sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&client_addr, &socklen);
        if (len > 0) {
            buffer[len] = '\0';
            buffer[strcspn(buffer, "\r\n")] = '\0';
            ESP_LOGI(TAG, "📱 Команда: %s", buffer);

           // 1. АУДИО / РАЦИЯ
            if (strcmp(buffer, "RADIO_ON") == 0) { 
                ESP_LOGI(TAG, "📻 Включение РАЦИИ... tx_chan: %p", tx_chan);
                audio_start(); 
            } 
            else if (strcmp(buffer, "RADIO_OFF") == 0) { 
                ESP_LOGI(TAG, "📴 Выключение РАЦИИ");
                audio_stop(); 
            } 
            else if (strcmp(buffer, "PTT_START") == 0) { 
                ESP_LOGI(TAG, "🎤 Запись пошла (PTT)");
                audio_start_recording(); 
            } 
            else if (strcmp(buffer, "PTT_STOP") == 0) { 
                ESP_LOGI(TAG, "🔇 Запись стоп (PTT)");
                audio_stop_recording(); 
            }
            
            // 2. СИГНАЛ (HORN) — ГЛУБОКАЯ ПРОВЕРКА
            else if (strcmp(buffer, "HORN_ON") == 0) {
    ESP_LOGI(TAG, "🎺 СИГНАЛ: Запуск...");
    if (tx_chan != NULL) { 
        // Включаем канал только если он выключен
        i2s_chan_info_t chan_info;
        i2s_channel_get_info(tx_chan, &chan_info);
        if (chan_info.state != I2S_CHAN_STATE_RUNNING) {
            i2s_channel_enable(tx_chan);
        }

        // Выделяем память в быстрой внутренней RAM (для DMA)
        int16_t *t_buf = heap_caps_malloc(2048, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        if (t_buf) {
            // Генерируем "гудок" (меандр)
            for (int i = 0; i < 1024; i += 2) {
                int16_t sample = (i % 30 < 15) ? 20000 : -20000;
                t_buf[i] = sample;     // Left
                t_buf[i+1] = sample;   // Right
            }
            
            size_t w = 0;
            // Цикл для длительности звука (например, 0.5 секунды)
            for(int j = 0; j < 150; j++) {
                i2s_channel_write(tx_chan, t_buf, 2048, &w, pdMS_TO_TICKS(100));
            }
            
            heap_caps_free(t_buf);
            ESP_LOGI(TAG, "✅ Звук отправлен на GPIO 14");
        }
    }
}

            // 3. СВЕТ
            else if (strcmp(buffer, "LIGHTS_ON") == 0)    { ESP_LOGI(TAG, "💡 ФАРЫ ВКЛ"); } 
            else if (strcmp(buffer, "LIGHTS_OFF") == 0)   { ESP_LOGI(TAG, "🌑 ФАРЫ ВЫКЛ"); } 
            else if (strcmp(buffer, "LIGHTS_BLINK") == 0) { ESP_LOGI(TAG, "🚨 МИГАНИЕ"); }

            // 4. ДВИЖЕНИЕ
            else if (strcmp(buffer, "FORWARD") == 0)      { ESP_LOGI(TAG, "🚀 ВПЕРЕД"); }
            else if (strcmp(buffer, "BACKWARD") == 0)     { ESP_LOGI(TAG, "⬇️ НАЗАД"); }
            else if (strcmp(buffer, "STOP") == 0)         { ESP_LOGI(TAG, "🛑 СТОП"); }
            else if (strcmp(buffer, "LEFT") == 0)         { ESP_LOGI(TAG, "⬅️ ВЛЕВО"); }
            else if (strcmp(buffer, "RIGHT") == 0)        { ESP_LOGI(TAG, "➡️ ВПРАВО"); }

            // 5. ДИАГОНАЛИ
            else if (strcmp(buffer, "FORWARD_LEFT") == 0)  { ESP_LOGI(TAG, "🚀↖️ ВПЕРЕД-ВЛЕВО"); }
            else if (strcmp(buffer, "FORWARD_RIGHT") == 0) { ESP_LOGI(TAG, "🚀↗️ ВПЕРЕД-ВПРАВО"); }
            else if (strcmp(buffer, "BACKWARD_LEFT") == 0) { ESP_LOGI(TAG, "⬇️↙️ НАЗАД-ВЛЕВО"); }
            else if (strcmp(buffer, "BACKWARD_RIGHT") == 0){ ESP_LOGI(TAG, "⬇️↘️ НАЗАД-ВПРАВО"); }

            else { ble_manager_send_command(buffer); }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

static void start_udp_command_server(void) {
    udp_running = true;
    xTaskCreate(udp_command_task, "udp_cmd", 4096, NULL, 5, NULL);
}

// ==================== MAIN ENTRY ====================
void app_main(void) {
    printf("\n\n=== 🛠 DRIVEBOT STARTING ===\n\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    audio_i2s_init(); 
    led_controller_init();
    ble_manager_init(NULL);
    wifi_manager_init_ap();
    esp_wifi_set_max_tx_power(78);

    start_udp_command_server();
    start_http_server();

    camera_started = false;
    ESP_LOGI(TAG, "=== СИСТЕМА ГОТОВА ===");
    while (1) vTaskDelay(pdMS_TO_TICKS(10000));
}