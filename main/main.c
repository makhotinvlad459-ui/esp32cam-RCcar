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

// ==================== HTTP START ====================
static esp_err_t start_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "📷 Запрос /start");
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

// ==================== HTTP MJPEG ВИДЕО (ПОРТ 81) ====================
static esp_err_t stream_handler(httpd_req_t *req) {
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

// ==================== HTTP CAPTURE (ФОТО) ====================
static esp_err_t capture_handler(httpd_req_t *req) {
    ESP_LOGI(TAG, "📸 Запрос фото");
    
    camera_fb_t *fb = camera_capture();
    if (fb && fb->len > 100 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_send(req, (const char*)fb->buf, fb->len);
        camera_return_fb(fb);
        ESP_LOGI(TAG, "✅ Фото отправлено (%d байт)", fb->len);
        return ESP_OK;
    }
    if (fb) camera_return_fb(fb);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed");
    ESP_LOGE(TAG, "❌ Ошибка фото");
    return ESP_FAIL;
}

static esp_err_t start_http_video_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 81;
    config.lru_purge_enable = true;
    config.stack_size = 16384;
    config.task_priority = 10;
    
    if (httpd_start(&http_server, &config) == ESP_OK) {
        httpd_uri_t start_uri = {
            .uri       = "/start",
            .method    = HTTP_GET,
            .handler   = start_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(http_server, &start_uri);
        
        httpd_uri_t stream_uri = {
            .uri       = "/stream",
            .method    = HTTP_GET,
            .handler   = stream_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(http_server, &stream_uri);
        
        httpd_uri_t capture_uri = {
            .uri       = "/capture",
            .method    = HTTP_GET,
            .handler   = capture_handler,
            .user_ctx  = NULL
        };
        httpd_register_uri_handler(http_server, &capture_uri);
        
        ESP_LOGI(TAG, "✅ HTTP сервер запущен на порту 81");
        ESP_LOGI(TAG, "📡 Видеопоток: http://192.168.4.1:81/stream");
        ESP_LOGI(TAG, "📸 Фото: http://192.168.4.1:81/capture");
        ESP_LOGI(TAG, "▶️ /start: http://192.168.4.1:81/start");
        return ESP_OK;
    }
    return ESP_FAIL;
}

// ==================== UDP КОМАНДЫ (ПОРТ 8080) ====================
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
        int len = recvfrom(udp_cmd_sock, buffer, sizeof(buffer) - 1, 0, 
                           (struct sockaddr *)&client_addr, &socklen);
        if (len > 0) {
            buffer[len] = '\0';
            buffer[strcspn(buffer, "\r\n")] = '\0';
            ESP_LOGI(TAG, "📱 Получена UDP команда: %s", buffer);
            
            if (strcmp(buffer, "FORWARD") == 0) {
                ESP_LOGI(TAG, "🚗 FORWARD");
            } else if (strcmp(buffer, "BACKWARD") == 0) {
                ESP_LOGI(TAG, "🚗 BACKWARD");
            } else if (strcmp(buffer, "LEFT") == 0) {
                ESP_LOGI(TAG, "🚗 LEFT");
            } else if (strcmp(buffer, "RIGHT") == 0) {
                ESP_LOGI(TAG, "🚗 RIGHT");
            } else if (strcmp(buffer, "STOP") == 0) {
                ESP_LOGI(TAG, "🚗 STOP");
            } else if (strcmp(buffer, "HORN_ON") == 0) {
                ESP_LOGI(TAG, "🔊 HORN ON");
            } else if (strcmp(buffer, "HORN_OFF") == 0) {
                ESP_LOGI(TAG, "🔊 HORN OFF");
            } else if (strcmp(buffer, "LIGHTS_ON") == 0) {
                ESP_LOGI(TAG, "💡 LIGHTS ON");
            } else if (strcmp(buffer, "LIGHTS_OFF") == 0) {
                ESP_LOGI(TAG, "💡 LIGHTS OFF");
            } else if (strcmp(buffer, "LIGHTS_BLINK") == 0) {
                ESP_LOGI(TAG, "💡 LIGHTS BLINK");
            } else if (strcmp(buffer, "RADIO_ON") == 0) {
                ESP_LOGI(TAG, "📻 RADIO ON");
            } else if (strcmp(buffer, "RADIO_OFF") == 0) {
                ESP_LOGI(TAG, "📻 RADIO OFF");
            }
            
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

// ==================== ЗАДАЧА ЗАПУСКА КАМЕРЫ ====================
static void camera_launcher_task(void *pvParameters) {
    // Ждем подключения клиента WiFi
    while (!wifi_manager_is_client_connected()) {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    // Дополнительная задержка для стабилизации WiFi
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    ESP_LOGI(TAG, "📱 Телефон подключен, запускаем камеру...");
    camera_server_init();
    
    vTaskDelete(NULL);
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
    start_http_video_server();
    
    // Запускаем задачу, которая включит камеру после подключения телефона
    xTaskCreate(camera_launcher_task, "camera_launcher", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "=== СИСТЕМА ГОТОВА ===");
    ESP_LOGI(TAG, "🎮 UDP команды: порт 8080");
    ESP_LOGI(TAG, "🎥 HTTP видео: http://192.168.4.1:81/stream");
    ESP_LOGI(TAG, "📸 HTTP фото: http://192.168.4.1:81/capture");
    ESP_LOGI(TAG, "📡 WiFi: DriveBot_CAM, IP: 192.168.4.1");
    ESP_LOGI(TAG, "🔵 BLE: DriveBot");
    ESP_LOGI(TAG, "📷 Камера запустится автоматически при подключении телефона");

    while(1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}