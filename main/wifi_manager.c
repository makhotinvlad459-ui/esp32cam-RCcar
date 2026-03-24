#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>

static const char *TAG = "WIFI_MGR";
static int connected_clients = 0;
static char ap_ip[16] = "192.168.4.1";
static bool client_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, 
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_AP_STACONNECTED: {
                wifi_event_ap_staconnected_t* event = 
                    (wifi_event_ap_staconnected_t*) event_data;
                connected_clients++;
                client_connected = true;
                esp_wifi_set_ps(WIFI_PS_NONE);
                ESP_LOGI(TAG, "📱 Телефон подключен по WiFi");
                ESP_LOGI(TAG, "📡 Видеопоток: http://%s:81/stream", ap_ip);
                (void)event; // убираем warning о неиспользуемой переменной
                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED: {
                wifi_event_ap_stadisconnected_t* event = 
                    (wifi_event_ap_stadisconnected_t*) event_data;
                if (connected_clients > 0) connected_clients--;
                if (connected_clients == 0) {
                    client_connected = false;
                }
                ESP_LOGI(TAG, "📱 Телефон отключился от WiFi (reason: %d)", event->reason);
                break;
            }
            default:
                break;
        }
    }
}

esp_err_t wifi_manager_init_ap(void) {
    ESP_LOGI(TAG, "🚀 Запуск WiFi точки доступа: %s (без пароля)", WIFI_AP_SSID);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_AP_SSID,
            .ssid_len = strlen(WIFI_AP_SSID),
            .channel = WIFI_AP_CHANNEL,
            .password = "",
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Получаем IP адрес точки доступа
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    snprintf(ap_ip, sizeof(ap_ip), IPSTR, IP2STR(&ip_info.ip));
    
    ESP_LOGI(TAG, "✅ WiFi точка доступа запущена: %s", WIFI_AP_SSID);
    ESP_LOGI(TAG, "📡 IP адрес ESP32: %s", ap_ip);
    ESP_LOGI(TAG, "📡 Видеопоток: http://%s:81/stream", ap_ip);
    ESP_LOGI(TAG, "📡 Максимум клиентов: 1 (только телефон)");
    
    return ESP_OK;
}

const char* wifi_manager_get_ap_ip(void) {
    return ap_ip;
}

bool wifi_manager_is_client_connected(void) {
    return client_connected;
}

int wifi_manager_get_client_count(void) {
    return connected_clients;
}