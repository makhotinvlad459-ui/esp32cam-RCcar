#include "command_handler.h"
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "led_controller.h"
#include "camera_server.h"
#include "camera_config.h"

static const char *TAG = "CMD_HANDLER";

// Новая функция для обработки команд от Flutter
void command_handler_process_udp_cmd(uint8_t cmd_id, uint8_t value) {
    command_t command = CMD_STOP;

    // Если значение 0 — это команда остановки
    if (value == 0 && cmd_id <= 4) {
        led_controller_execute(CMD_STOP);
        return;
    }

    switch (cmd_id) {
        case 1: command = CMD_FORWARD; break;
        case 2: command = CMD_BACKWARD; break;
        case 3: command = CMD_LEFT; break;
        case 4: command = CMD_RIGHT; break;
        case 5: command = (value == 1) ? CMD_LIGHTS_ON : CMD_LIGHTS_OFF; break;
        default: 
            ESP_LOGW(TAG, "Unknown UDP ID: %d", cmd_id);
            return;
    }

    ESP_LOGI(TAG, "Executing UDP CMD: %d (Val: %d)", cmd_id, value);
    led_controller_execute(command);
}

// --- Остальные функции оставляем как были ---

command_t command_parse_string(const char *str) {
    if (str == NULL) return CMD_STOP;
    if (strstr(str, "FORWARD")) return CMD_FORWARD;
    if (strstr(str, "BACKWARD")) return CMD_BACKWARD;
    if (strstr(str, "LEFT")) return CMD_LEFT;
    if (strstr(str, "RIGHT")) return CMD_RIGHT;
    return CMD_STOP;
}

esp_err_t command_handler_process_server_cmd(const char *json_cmd) {
    // Твоя логика JSON...
    return ESP_OK;
}

void command_handler_process_ble_cmd(const char *cmd) {
    if (!cmd) return;
    led_controller_execute(command_parse_string(cmd));
}

bool command_is_valid(const char *str) { return true; }
const char* command_get_name(command_t cmd) { return "CMD"; }