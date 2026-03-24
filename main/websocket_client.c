#include "websocket_client.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "string.h"
#include "cJSON.h"

static const char *TAG = "WEBSOCKET_CLIENT";

// WebSocket клиент
static esp_websocket_client_handle_t websocket_client = NULL;

// Статус подключения
static bool is_connected = false;

// Callback для входящих сообщений
static websocket_callback_t user_callback = NULL;

// События для синхронизации
static EventGroupHandle_t ws_event_group;
#define WS_CONNECTED_BIT BIT0
#define WS_DISCONNECTED_BIT BIT1

// Обработчик событий WebSocket
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WebSocket подключен к серверу");
            is_connected = true;
            xEventGroupSetBits(ws_event_group, WS_CONNECTED_BIT);
            xEventGroupClearBits(ws_event_group, WS_DISCONNECTED_BIT);
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "WebSocket отключен");
            is_connected = false;
            xEventGroupSetBits(ws_event_group, WS_DISCONNECTED_BIT);
            xEventGroupClearBits(ws_event_group, WS_CONNECTED_BIT);
            break;
            
        case WEBSOCKET_EVENT_DATA:
            ESP_LOGD(TAG, "Получены данные: длина=%d", data->data_len);
            
            // Если есть callback и данные не пустые
            if (user_callback != NULL && data->data_len > 0) {
                // Создаем копию данных с нуль-терминатором
                char *payload = malloc(data->data_len + 1);
                if (payload) {
                    memcpy(payload, data->data_ptr, data->data_len);
                    payload[data->data_len] = '\0';
                    user_callback(payload, data->data_len);
                    free(payload);
                }
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "Ошибка WebSocket");
            break;
            
        default:
            break;
    }
}

esp_err_t websocket_client_init(void)
{
    ESP_LOGI(TAG, "Инициализация WebSocket клиента");
    
    // Создаем группу событий
    ws_event_group = xEventGroupCreate();
    if (ws_event_group == NULL) {
        ESP_LOGE(TAG, "Не удалось создать группу событий");
        return ESP_ERR_NO_MEM;
    }
    
    return ESP_OK;
}

esp_err_t websocket_client_connect(const char *server_uri, const char *robot_id)
{
    if (websocket_client != NULL) {
        ESP_LOGW(TAG, "WebSocket клиент уже существует, отключаем...");
        websocket_client_disconnect();
    }
    
    ESP_LOGI(TAG, "Подключение к серверу: %s", server_uri);
    
    // Формируем URI с robot_id как query параметр (опционально)
    char full_uri[256];
    if (robot_id != NULL) {
        snprintf(full_uri, sizeof(full_uri), "%s?robot_id=%s", server_uri, robot_id);
    } else {
        snprintf(full_uri, sizeof(full_uri), "%s", server_uri);
    }
    
    // Конфигурация WebSocket клиента
    esp_websocket_client_config_t ws_cfg = {
        .uri = full_uri,
        .keep_alive_enable = true,
        .keep_alive_interval = 10,      // секунд
        .keep_alive_idle = 5,
        .keep_alive_count = 3,
        .disable_auto_reconnect = false,
        .reconnect_timeout_ms = 5000,
        .buffer_size = 2048,             // Размер буфера для сообщений
    };
    
    // Создаем клиент
    websocket_client = esp_websocket_client_init(&ws_cfg);
    if (websocket_client == NULL) {
        ESP_LOGE(TAG, "Не удалось создать WebSocket клиент");
        return ESP_FAIL;
    }
    
    // Регистрируем обработчик событий
    esp_err_t err = esp_websocket_register_events(websocket_client, 
                                                   WEBSOCKET_EVENT_ANY, 
                                                   websocket_event_handler, 
                                                   NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка регистрации событий: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(websocket_client);
        websocket_client = NULL;
        return err;
    }
    
    // Запускаем соединение
    err = esp_websocket_client_start(websocket_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка запуска WebSocket: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(websocket_client);
        websocket_client = NULL;
        return err;
    }
    
    ESP_LOGI(TAG, "Подключение инициировано, ожидаем...");
    
    return ESP_OK;
}

esp_err_t websocket_client_send_text(const char *data)
{
    if (!is_connected || websocket_client == NULL) {
        ESP_LOGW(TAG, "WebSocket не подключен");
        return ESP_ERR_INVALID_STATE;
    }
    
    int len = strlen(data);
    int sent = esp_websocket_client_send_text(websocket_client, data, len, portMAX_DELAY);
    
    if (sent == len) {
        ESP_LOGD(TAG, "Отправлено %d байт: %s", sent, data);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Ошибка отправки: отправлено %d из %d", sent, len);
        return ESP_FAIL;
    }
}

esp_err_t websocket_client_send_json(const char *json_data)
{
    return websocket_client_send_text(json_data);
}

bool websocket_client_is_connected(void)
{
    return is_connected;
}

void websocket_client_disconnect(void)
{
    if (websocket_client != NULL) {
        ESP_LOGI(TAG, "Отключение от сервера");
        esp_websocket_client_stop(websocket_client);
        esp_websocket_client_destroy(websocket_client);
        websocket_client = NULL;
        is_connected = false;
        xEventGroupClearBits(ws_event_group, WS_CONNECTED_BIT);
        xEventGroupSetBits(ws_event_group, WS_DISCONNECTED_BIT);
    }
}

void websocket_client_deinit(void)
{
    websocket_client_disconnect();
    
    if (ws_event_group) {
        vEventGroupDelete(ws_event_group);
        ws_event_group = NULL;
    }
    
    ESP_LOGI(TAG, "WebSocket клиент деинициализирован");
}

void websocket_client_register_callback(websocket_callback_t cb)
{
    user_callback = cb;
    ESP_LOGI(TAG, "Callback зарегистрирован");
}

