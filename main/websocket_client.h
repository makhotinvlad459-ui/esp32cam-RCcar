#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include "esp_err.h"
#include <stdbool.h>

// Инициализация WebSocket клиента
esp_err_t websocket_client_init(void);

// Подключение к серверу
esp_err_t websocket_client_connect(const char *server_uri, const char *robot_id);

// Отправка текстового сообщения
esp_err_t websocket_client_send_text(const char *data);

// Отправка JSON сообщения
esp_err_t websocket_client_send_json(const char *json_data);

// Проверка статуса подключения
bool websocket_client_is_connected(void);

// Отключение от сервера
void websocket_client_disconnect(void);

// Деконструктор (освобождение ресурсов)
void websocket_client_deinit(void);

// Установка callback для обработки входящих сообщений
typedef void (*websocket_callback_t)(const char *data, int len);
void websocket_client_register_callback(websocket_callback_t cb);

#endif
