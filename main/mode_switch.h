#ifndef MODE_SWITCH_H
#define MODE_SWITCH_H

#include <stdbool.h>
#include "esp_err.h"

// Режимы работы машинки
typedef enum {
    MODE_MANUAL_BLE = 0,      // Ручной режим через BLE (текущий)
    MODE_AUTO_SERVER = 1,     // Автономный режим через сервер
    MODE_AUTO_OFFLINE = 2     // Автономный режим локально (для будущего)
} robot_mode_t;

// Инициализация модуля
void mode_switch_init(void);

// Получить текущий режим
robot_mode_t mode_switch_get_current(void);

// Установить новый режим
esp_err_t mode_switch_set(robot_mode_t new_mode);

// Получить имя режима для вывода в лог
const char* mode_switch_get_name(void);

// Проверка, находится ли машинка в автономном режиме (любом)
bool mode_switch_is_auto(void);

#endif
