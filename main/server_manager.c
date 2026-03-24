#include "server_manager.h"
#include "websocket_client.h"
#include "mode_switch.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdbool.h>

static const char *TAG = "SERVER_MGR";

// Настройки подключения
static char current_server_url[128] = {0};
static char current_robot_id[32] = {0};
static bool is_started = false;

// Таска для мониторинга соединения
static TaskHandle_t monitor_task_handle = NULL;

// Обработчик входящих сообщений от сервера
static void on_websocket_message(const char *data, int len)
{
    ESP_LOGI(TAG, "Получено от сервера: %s", data);
    
    // TODO: передавать в command_handler
    // command_handler_process_server_cmd(data);
}

// Таска мониторинга
static void server_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Мониторинг соединения запущен");
    
    while (is_started) {
        // Проверяем текущий режим
        robot_mode_t current_mode = mode_switch_get_current();
        (void)current_mode; // убираем предупреждение о неиспользуемой переменной
        
        if (current_mode == MODE_AUTO_SERVER) {
            // Временно отключаем проверку WiFi
            if (!websocket_client_is_connected()) {
                ESP_LOGW(TAG, "Режим AUTO_SERVER, подключаемся к серверу...");
                websocket_client_connect(current_server_url, current_robot_id);
            }
        } else {
            // Не в AUTO режиме - отключаемся если подключены
            if (websocket_client_is_connected()) {
                ESP_LOGI(TAG, "Выход из AUTO режима, отключаемся от сервера");
                websocket_client_disconnect();
            }
        }
        
        // Проверяем каждые 5 секунд
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    
    ESP_LOGI(TAG, "Мониторинг остановлен");
    monitor_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t server_manager_init(void)
{
    ESP_LOGI(TAG, "Инициализация менеджера сервера");
    
    // Инициализируем WebSocket клиент
    esp_err_t ret = websocket_client_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка инициализации WebSocket: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Регистрируем callback для входящих сообщений
    websocket_client_register_callback(on_websocket_message);
    
    return ESP_OK;
}

esp_err_t server_manager_start(const char *server_url, const char *robot_id)
{
    if (!server_url || !robot_id) {
        ESP_LOGE(TAG, "Неверные параметры");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Сохраняем настройки
    strncpy(current_server_url, server_url, sizeof(current_server_url) - 1);
    strncpy(current_robot_id, robot_id, sizeof(current_robot_id) - 1);
    
    is_started = true;
    
    // Создаем таску мониторинга
    if (monitor_task_handle == NULL) {
        xTaskCreate(server_monitor_task, "server_monitor", 4096, NULL, 5, &monitor_task_handle);
    }
    
    // Если мы уже в AUTO режиме - подключаемся сразу (проверка WiFi временно отключена)
    if (mode_switch_get_current() == MODE_AUTO_SERVER && true) {
        ESP_LOGI(TAG, "Уже в AUTO режиме, подключаемся...");
        websocket_client_connect(current_server_url, current_robot_id);
    }
    
    return ESP_OK;
}

void server_manager_stop(void)
{
    ESP_LOGI(TAG, "Остановка менеджера сервера");
    
    is_started = false;
    
    // Отключаемся от сервера
    websocket_client_disconnect();
    
    // Останавливаем таску мониторинга
    if (monitor_task_handle) {
        vTaskDelete(monitor_task_handle);
        monitor_task_handle = NULL;
    }
}

bool server_manager_is_connected(void)
{
    return websocket_client_is_connected();
}

esp_err_t server_manager_send_data(const char *data)
{
    if (!websocket_client_is_connected()) {
        ESP_LOGW(TAG, "Сервер не подключен");
        return ESP_ERR_INVALID_STATE;
    }
    
    return websocket_client_send_text(data);
}