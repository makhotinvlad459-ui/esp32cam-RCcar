#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include <stdbool.h>

// SSID для открытой точки доступа
#define WIFI_AP_SSID      "DriveBot_CAM"  // Можно изменить на любое имя
// БЕЗ ПАРОЛЯ - открытая сеть!

#define WIFI_AP_CHANNEL    1
#define WIFI_MAX_CONN      1                // Только телефон

// Инициализация WiFi в режиме точки доступа (без пароля)
esp_err_t wifi_manager_init_ap(void);

// Получение IP адреса ESP32 в AP режиме
const char* wifi_manager_get_ap_ip(void);

// Проверка подключен ли клиент
bool wifi_manager_is_client_connected(void);

// Получение количества подключенных клиентов
int wifi_manager_get_client_count(void);

#endif