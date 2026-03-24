#ifndef SERVER_MANAGER_H
#define SERVER_MANAGER_H
#include <stdbool.h>

#include "esp_err.h"

// Инициализация менеджера сервера
esp_err_t server_manager_init(void);

// Запуск подключения к серверу
esp_err_t server_manager_start(const char *server_url, const char *robot_id);

// Остановка подключения
void server_manager_stop(void);

// Статус
bool server_manager_is_connected(void);

// Отправить данные на сервер
esp_err_t server_manager_send_data(const char *data);

#endif