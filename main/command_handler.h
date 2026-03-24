#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include "led_controller.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h> // Нужно для uint8_t

// Обработка команд от UDP (ID, Value)
void command_handler_process_udp_cmd(uint8_t cmd_id, uint8_t value);

// Преобразование строки в команду
command_t command_parse_string(const char *str);

// Обработка команд от сервера (JSON)
esp_err_t command_handler_process_server_cmd(const char *json_cmd);

// Обработка команд от BLE
void command_handler_process_ble_cmd(const char *cmd);

// Проверка, является ли строка командой
bool command_is_valid(const char *str);

// Получение имени команды для логирования
const char* command_get_name(command_t cmd);

#endif // COMMAND_HANDLER_H