#ifndef CAMERA_SERVER_H
#define CAMERA_SERVER_H

#include "esp_err.h"
#include "camera_config.h"
#include "wifi_stream.h"


// Инициализация сервера
esp_err_t camera_server_init(void);
esp_err_t camera_server_init_advanced(stream_mode_t mode, const camera_params_t *cam_params);

// Управление качеством
esp_err_t camera_server_set_quality(camera_quality_t quality);
esp_err_t camera_server_set_fps(int fps);

// Статистика
esp_err_t camera_server_get_stats(stream_stats_t *stats);

// Статус
bool camera_server_is_streaming(void);
int camera_server_get_clients(void);
void camera_server_stop(void);

#endif