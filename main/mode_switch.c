#include "mode_switch.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "MODE_SWITCH";

// Текущий режим (по умолчанию ручной BLE)
static robot_mode_t current_mode = MODE_MANUAL_BLE;

// Имена режимов для логирования
static const char* mode_names[] = {
    "MANUAL_BLE",
    "AUTO_SERVER", 
    "AUTO_OFFLINE"
};

// Ключ для сохранения в NVS (чтобы запомнить режим после перезагрузки)
#define NVS_NAMESPACE "robot_config"
#define NVS_KEY_MODE "current_mode"

void mode_switch_init(void)
{
    ESP_LOGI(TAG, "Инициализация модуля переключения режимов");
    
    // Открываем хранилище NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        // Пытаемся прочитать сохраненный режим
        int8_t saved_mode = MODE_MANUAL_BLE;
        err = nvs_get_i8(nvs_handle, NVS_KEY_MODE, &saved_mode);
        
        if (err == ESP_OK) {
            // Проверяем, что значение корректно
            if (saved_mode >= MODE_MANUAL_BLE && saved_mode <= MODE_AUTO_OFFLINE) {
                current_mode = (robot_mode_t)saved_mode;
                ESP_LOGI(TAG, "Загружен сохраненный режим: %s", mode_names[current_mode]);
            }
        } else {
            ESP_LOGI(TAG, "Сохраненный режим не найден, используем MANUAL_BLE");
        }
        
        nvs_close(nvs_handle);
    } else {
        ESP_LOGE(TAG, "Ошибка открытия NVS: %s", esp_err_to_name(err));
    }
}

robot_mode_t mode_switch_get_current(void)
{
    return current_mode;
}

esp_err_t mode_switch_set(robot_mode_t new_mode)
{
    // Проверяем корректность режима
    if (new_mode < MODE_MANUAL_BLE || new_mode > MODE_AUTO_OFFLINE) {
        ESP_LOGE(TAG, "Неверный режим: %d", new_mode);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Если режим не меняется - ничего не делаем
    if (new_mode == current_mode) {
        ESP_LOGI(TAG, "Режим уже %s", mode_names[current_mode]);
        return ESP_OK;
    }
    
    // Логируем смену режима
    ESP_LOGI(TAG, "Смена режима: %s -> %s", 
             mode_names[current_mode], 
             mode_names[new_mode]);
    
    // Здесь потом будут колбэки для разных режимов
    // (остановка BLE при переходе в AUTO, отключение от сервера при переходе в MANUAL и т.д.)
    
    // Сохраняем новый режим
    current_mode = new_mode;
    
    // Сохраняем в NVS
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    
    if (err == ESP_OK) {
        err = nvs_set_i8(nvs_handle, NVS_KEY_MODE, (int8_t)current_mode);
        if (err == ESP_OK) {
            err = nvs_commit(nvs_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Режим сохранен в NVS");
            }
        }
        nvs_close(nvs_handle);
    }
    
    return ESP_OK;
}

const char* mode_switch_get_name(void)
{
    return mode_names[current_mode];
}

bool mode_switch_is_auto(void)
{
    return (current_mode == MODE_AUTO_SERVER || current_mode == MODE_AUTO_OFFLINE);
}
