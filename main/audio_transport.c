#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include "audio_transport.h"

static const char *TAG = "AUDIO_TRANSPORT";

#define ESP_AUDIO_RX_PORT 8082
#define ESP_AUDIO_TX_PORT 8083

typedef enum {
    AUDIO_IDLE = 0,
    AUDIO_STARTING,
    AUDIO_RUNNING,
    AUDIO_STOPPING
} audio_state_t;

static audio_state_t audio_state = AUDIO_IDLE;
static bool running = false;
static bool recording_enabled = false;
static bool initialized = false;

bool audio_is_initialized(void) {
    return initialized;
}

static TaskHandle_t rx_task_handle = NULL;
static TaskHandle_t tx_task_handle = NULL;

static int rx_sock = -1;
static int tx_sock = -1;

static esp_err_t audio_udp_only_init(void) {
    if (initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "📡 UDP Audio сервер (I2S отключено)");
    ESP_LOGI(TAG, "📥 Прием аудио: порт %d (Flutter->ESP)", ESP_AUDIO_RX_PORT);
    ESP_LOGI(TAG, "📤 Отправка аудио: порт %d (ESP->Flutter)", ESP_AUDIO_TX_PORT);

    initialized = true;
    return ESP_OK;
}

static void audio_rx_task(void *pvParameters) {
    uint8_t buffer[1024];

    rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (rx_sock < 0) {
        ESP_LOGE(TAG, "RX socket create failed: %d", errno);
        rx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in rx_bind_addr;
    memset(&rx_bind_addr, 0, sizeof(rx_bind_addr));
    rx_bind_addr.sin_family = AF_INET;
    rx_bind_addr.sin_port = htons(ESP_AUDIO_RX_PORT);
    rx_bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(rx_sock, (struct sockaddr *)&rx_bind_addr, sizeof(rx_bind_addr)) < 0) {
        ESP_LOGE(TAG, "RX bind failed: %d", errno);
        close(rx_sock);
        rx_sock = -1;
        rx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "🎤 RX task started");

    while (running) {
        if (recording_enabled) {
            struct sockaddr_in source_addr;
            socklen_t socklen = sizeof(source_addr);

            int len = recvfrom(rx_sock, buffer, sizeof(buffer), MSG_DONTWAIT,
                               (struct sockaddr *)&source_addr, &socklen);

            if (len > 0) {
                ESP_LOGI(TAG, "📡 Audio from Flutter: %d bytes from %s:%d",
                         len, inet_ntoa(source_addr.sin_addr), ntohs(source_addr.sin_port));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    if (rx_sock >= 0) {
        close(rx_sock);
        rx_sock = -1;
    }

    rx_task_handle = NULL;
    ESP_LOGI(TAG, "RX task exited");
    vTaskDelete(NULL);
}

static void audio_tx_task(void *pvParameters) {
    uint8_t buffer[1024];

    tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (tx_sock < 0) {
        ESP_LOGE(TAG, "TX socket create failed: %d", errno);
        tx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in tx_bind_addr;
    memset(&tx_bind_addr, 0, sizeof(tx_bind_addr));
    tx_bind_addr.sin_family = AF_INET;
    tx_bind_addr.sin_port = htons(ESP_AUDIO_TX_PORT);
    tx_bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(tx_sock, (struct sockaddr *)&tx_bind_addr, sizeof(tx_bind_addr)) < 0) {
        ESP_LOGE(TAG, "TX bind failed: %d", errno);
        close(tx_sock);
        tx_sock = -1;
        tx_task_handle = NULL;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "🔊 TX task started");

    while (running) {
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        int len = recvfrom(tx_sock, buffer, sizeof(buffer), MSG_DONTWAIT,
                           (struct sockaddr *)&from_addr, &from_len);

        if (len > 0) {
            ESP_LOGI(TAG, "📡 Audio from Flutter: %d bytes from %s:%d",
                     len, inet_ntoa(from_addr.sin_addr), ntohs(from_addr.sin_port));
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (tx_sock >= 0) {
        close(tx_sock);
        tx_sock = -1;
    }

    tx_task_handle = NULL;
    ESP_LOGI(TAG, "TX task exited");
    vTaskDelete(NULL);
}

esp_err_t audio_start(void) {
    if (audio_state == AUDIO_RUNNING || audio_state == AUDIO_STARTING) {
        return ESP_OK;
    }

    audio_state = AUDIO_STARTING;
    running = true;
    recording_enabled = false;

    esp_err_t err = audio_udp_only_init();
    if (err != ESP_OK) {
        running = false;
        audio_state = AUDIO_IDLE;
        return err;
    }

    if (xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL, 5, &rx_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio_rx task");
        running = false;
        audio_state = AUDIO_IDLE;
        return ESP_FAIL;
    }

    if (xTaskCreate(audio_tx_task, "audio_tx", 4096, NULL, 5, &tx_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create audio_tx task");
        running = false;
        audio_state = AUDIO_IDLE;
        return ESP_FAIL;
    }

    audio_state = AUDIO_RUNNING;
    ESP_LOGI(TAG, "✅ Audio UDP started");
    return ESP_OK;
}

void audio_stop(void) {
    if (audio_state == AUDIO_IDLE || audio_state == AUDIO_STOPPING) {
        return;
    }

    audio_state = AUDIO_STOPPING;
    recording_enabled = false;
    running = false;

    ESP_LOGI(TAG, "Stopping audio...");

    vTaskDelay(pdMS_TO_TICKS(100));

    if (rx_sock >= 0) {
        shutdown(rx_sock, SHUT_RDWR);
        close(rx_sock);
        rx_sock = -1;
    }

    if (tx_sock >= 0) {
        shutdown(tx_sock, SHUT_RDWR);
        close(tx_sock);
        tx_sock = -1;
    }

    vTaskDelay(pdMS_TO_TICKS(50));

    rx_task_handle = NULL;
    tx_task_handle = NULL;
    initialized = false;
    audio_state = AUDIO_IDLE;

    ESP_LOGI(TAG, "Audio stopped");
}

void audio_start_recording(void) {
    if (audio_state != AUDIO_RUNNING) {
        return;
    }

    recording_enabled = true;
    ESP_LOGI(TAG, "🎤 Recording enabled");
}

void audio_stop_recording(void) {
    recording_enabled = false;
    ESP_LOGI(TAG, "🔇 Recording disabled");
}