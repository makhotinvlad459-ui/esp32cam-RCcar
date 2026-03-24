#ifndef WIFI_STREAM_H
#define WIFI_STREAM_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Структура для статистики потока
typedef struct {
    uint32_t frames_sent;       // Всего отправлено кадров
    uint32_t frames_lost;        // Потеряно кадров
    uint32_t bytes_sent;         // Всего отправлено байт
    float bitrate_mbps;          // Текущий битрейт (Мбит/с)
    float fps;                    // Текущий FPS
    uint32_t packets_sent;        // Всего отправлено пакетов
    uint32_t packets_retransmitted; // Переотправлено пакетов (если используем ACK)
    int8_t rssi;                   // Уровень сигнала WiFi
} stream_stats_t;

// Настройки стримера
typedef struct {
    uint16_t port;                 // Порт для видеопотока
    uint16_t fragment_size;         // Размер фрагмента (для ESP-NOW или пакетной отправки)
    bool use_fec;                  // Использовать Forward Error Correction
    bool use_packet_injection;     // Использовать Packet Injection (без ACK)
    uint16_t max_queue_size;        // Максимальный размер очереди
    uint8_t priority;               // Приоритет задачи
} stream_config_t;

// Режимы передачи
typedef enum {
    STREAM_MODE_HTTP_MJPEG,        // Обычный HTTP MJPEG поток
    STREAM_MODE_UDP_MJPEG,          // UDP пакеты с MJPEG
    STREAM_MODE_ESPNOW,              // ESP-NOW протокол (если поддерживается)
    STREAM_MODE_FEC_UDP              // UDP + Forward Error Correction
} stream_mode_t;

// Инициализация стримера
esp_err_t wifi_stream_init(stream_mode_t mode, const stream_config_t *config);

// Запуск/остановка потока
esp_err_t wifi_stream_start(void);
esp_err_t wifi_stream_stop(void);

// Получение статистики
void wifi_stream_get_stats(stream_stats_t *stats);

// Регистрация клиента (для HTTP режима)
void wifi_stream_register_client(int sockfd);
void wifi_stream_unregister_client(int sockfd);

// Отправка кадра (низкоуровневая функция, вызывается из camera_server)
esp_err_t wifi_stream_send_frame(const uint8_t *data, size_t len);

// Проверка активности
bool wifi_stream_is_active(void);

// Управление битрейтом (автоматическое)
void wifi_stream_set_target_bitrate(float mbps);
float wifi_stream_get_current_bitrate(void);

// Обновление RSSI
void wifi_stream_update_rssi(int8_t rssi);

// Получение количества клиентов
int wifi_stream_get_client_count(void);

// Функции обратного вызова для уведомлений
typedef void (*stream_client_cb_t)(int client_id, bool connected);
void wifi_stream_register_callback(stream_client_cb_t cb);

#endif // WIFI_STREAM_H