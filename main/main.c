#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "esp_http_server.h"
#include "esp_timer.h"

#include "led_controller.h"
#include "wifi_manager.h"
#include "ble_manager.h"
#include "camera_server.h"
#include "camera_config.h"
#include "esp_wifi.h"

// ==================== ОБЪЯВЛЕНИЯ ФУНКЦИЙ ====================
extern camera_fb_t* camera_capture(void);
extern void camera_return_fb(camera_fb_t *fb);
extern void camera_start(void);
extern void camera_stop(void);
// =====================================================================

static const char *TAG = "DRIVEBOT_MAIN";
static httpd_handle_t http_server = NULL;
static bool camera_started = false;

// ==================== HTTP START КАМЕРЫ ====================
static esp_err_t start_handler(httpd_req_t *req) {
    // CORS заголовки
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
    // CORS заголовки
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
                    "--frame\r\n"
                    "Content-Type: image/jpeg\r\n"
                    "Content-Length: %zu\r\n\r\n",
                    fb->len);
                
                if (httpd_resp_send_chunk(req, header, header_len) != ESP_OK) {
                    camera_return_fb(fb);
                    break;
                }
                if (httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len) != ESP_OK) {
                    camera_return_fb(fb);
                    break;
                }
                if (httpd_resp_send_chunk(req, "\r\n", 2) != ESP_OK) {
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
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ==================== HTTP CAPTURE (не используется, но оставим для совместимости) ====================
static esp_err_t capture_handler(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    if (req->method == HTTP_OPTIONS) {
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    if (!camera_started) {
        camera_server_init();
        camera_started = true;
    }
    ESP_LOGI(TAG, "📸 Запрос фото (не используется)");
    camera_fb_t *fb = camera_capture();
    if (fb) camera_return_fb(fb);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Not used");
    return ESP_FAIL;
}

static esp_err_t start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.lru_purge_enable = true;
    config.stack_size = 16384;
    config.task_priority = 10;
    
    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_uri_t start_uri = { .uri = "/start",   .method = HTTP_GET,  .handler = start_handler };
        httpd_uri_t stream_uri = { .uri = "/stream", .method = HTTP_GET,  .handler = stream_handler };
        httpd_uri_t capture_uri = { .uri = "/capture", .method = HTTP_GET, .handler = capture_handler };
        // Добавляем OPTIONS для предварительных запросов
        httpd_uri_t start_opt = { .uri = "/start",   .method = HTTP_OPTIONS, .handler = start_handler };
        httpd_uri_t stream_opt = { .uri = "/stream", .method = HTTP_OPTIONS, .handler = stream_handler };
        httpd_uri_t capture_opt= { .uri = "/capture", .method = HTTP_OPTIONS, .handler = capture_handler };
        httpd_register_uri_handler(http_server, &start_uri);
        httpd_register_uri_handler(http_server, &stream_uri);
        httpd_register_uri_handler(http_server, &capture_uri);
        httpd_register_uri_handler(http_server, &start_opt);
        httpd_register_uri_handler(http_server, &stream_opt);
        httpd_register_uri_handler(http_server, &capture_opt);
        ESP_LOGI(TAG, "✅ HTTP сервер запущен на порту 81");
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ==================== UDP КОМАНДЫ ====================
static int udp_cmd_sock = -1;
static bool udp_running = false;

static void udp_command_task(void *pvParameters) {
    struct sockaddr_in server_addr, client_addr;
    socklen_t socklen = sizeof(client_addr);
    char buffer[128];
    
    udp_cmd_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (udp_cmd_sock < 0) {
        ESP_LOGE(TAG, "❌ Не удалось создать UDP сокет");
        vTaskDelete(NULL);
        return;
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(udp_cmd_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "❌ Ошибка привязки UDP сокета");
        close(udp_cmd_sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "✅ UDP командный сервер запущен на порту 8080");
    
    while (udp_running) {
        int len = recvfrom(udp_cmd_sock, buffer, sizeof(buffer)-1, 0, (struct sockaddr *)&client_addr, &socklen);
        if (len > 0) {
            buffer[len] = '\0';
            buffer[strcspn(buffer, "\r\n")] = '\0';
            ESP_LOGI(TAG, "📱 Получена UDP команда: %s", buffer);
            // обработка команд...
            ble_manager_send_command(buffer);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    close(udp_cmd_sock);
    vTaskDelete(NULL);
}

static void start_udp_command_server(void) {
    udp_running = true;
    xTaskCreate(udp_command_task, "udp_cmd", 4096, NULL, 5, NULL);
}

// ==================== MAIN ====================
void app_main(void)
{
    printf("\n\n=== 🛠 DRIVEBOT WITH CAMERA ===\n\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    led_controller_init();
    ble_manager_init(NULL);
    wifi_manager_init_ap();
    esp_wifi_set_max_tx_power(78);
    
    start_udp_command_server();
    start_http_server();
    
    ESP_LOGI(TAG, "Камера будет запущена при первом подключении клиента");
    camera_started = false;

    ESP_LOGI(TAG, "=== СИСТЕМА ГОТОВА ===");
    ESP_LOGI(TAG, "🎮 UDP команды: порт 8080");
    ESP_LOGI(TAG, "🎥 HTTP видео: http://192.168.4.1:81/stream");
    ESP_LOGI(TAG, "📡 WiFi: DriveBot_CAM, IP: 192.168.4.1");
    ESP_LOGI(TAG, "🔵 BLE: DriveBot");

    while(1) vTaskDelay(pdMS_TO_TICKS(10000));
}