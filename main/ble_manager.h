#ifndef BLE_MANAGER_H
#define BLE_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// Определения
#define BLE_DEVICE_NAME "DriveBot"
#define BLE_SERVICE_UUID 0xffe0
#define BLE_CHARACTERISTIC_UUID 0xffe1

// Тип колбэка для команд
typedef void (*ble_command_callback_t)(const char *command, uint16_t len);

// Инициализация BLE
esp_err_t ble_manager_init(ble_command_callback_t cmd_callback);

// Проверка подключения
bool ble_manager_is_connected(void);

// Отправить ответ (если нужно)
esp_err_t ble_manager_send_response(const char *response);

// ========== ДОБАВЬ ЭТУ ФУНКЦИЮ ==========
// Отправить команду через BLE
void ble_manager_send_command(const char *command);
// ========================================

// Информация
void ble_manager_print_info(void);

#endif // BLE_MANAGER_H