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
#include "driver/i2s_std.h"
#include "driver/gpio.h"

static const char *TAG = "AUDIO_TRANSPORT";

// === ПИНЫ ===
#define I2S_MIC_SCK   GPIO_NUM_41
#define I2S_MIC_WS    GPIO_NUM_42
#define I2S_MIC_DIN   GPIO_NUM_2

#define I2S_SPK_SCK   GPIO_NUM_1
#define I2S_SPK_WS    GPIO_NUM_48
#define I2S_SPK_DOUT  GPIO_NUM_47

#define SAMPLE_RATE     16000
#define UDP_BUFFER_SIZE 1024

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

static i2s_chan_handle_t tx_chan = NULL;
static i2s_chan_handle_t rx_chan = NULL;

static int rx_sock = -1;
static int tx_sock = -1;

static struct sockaddr_in client_dest_addr;
static bool client_addr_resolved = false;

bool audio_is_initialized(void) {
    return initialized;
}

// === ИНИЦИАЛИЗАЦИЯ ЖЕЛЕЗА ===
static esp_err_t audio_i2s_init(void) {
    if (initialized) return ESP_OK;

    ESP_LOGI(TAG, "🎙 Инициализация I2S каналов...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    
    // Пытаемся создать новые каналы
    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка создания каналов: %s", esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_SPK_SCK,
            .ws   = I2S_SPK_WS,
            .dout = I2S_SPK_DOUT,
            .din  = I2S_MIC_DIN,
        },
    };

    // Конфиг для TX (динамик)
    std_cfg.gpio_cfg.bclk = I2S_SPK_SCK;
    std_cfg.gpio_cfg.ws = I2S_SPK_WS;
    i2s_channel_init_std_mode(tx_chan, &std_cfg);

    // Конфиг для RX (микрофон)
    std_cfg.gpio_cfg.bclk = I2S_MIC_SCK;
    std_cfg.gpio_cfg.ws = I2S_MIC_WS;
    i2s_channel_init_std_mode(rx_chan, &std_cfg);

    i2s_channel_enable(tx_chan);
    i2s_channel_enable(rx_chan);

    initialized = true;
    return ESP_OK;
}

// === ЗАДАЧИ ===
static void audio_rx_task(void *pvParameters) {
    uint8_t *buffer = malloc(UDP_BUFFER_SIZE);
    if (!buffer) { vTaskDelete(NULL); return; }
    
    rx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(ESP_AUDIO_RX_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    bind(rx_sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    ESP_LOGI(TAG, "🔊 RX Task Ready");

    while (running) {
        struct sockaddr_in source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(rx_sock, buffer, UDP_BUFFER_SIZE, 0, (struct sockaddr *)&source_addr, &socklen);

        if (len > 0) {
            if (!client_addr_resolved) {
                client_dest_addr = source_addr;
                client_dest_addr.sin_port = htons(ESP_AUDIO_TX_PORT);
                client_addr_resolved = true;
                ESP_LOGI(TAG, "📱 Адрес телефона определен: %s", inet_ntoa(source_addr.sin_addr));
            }
            size_t bytes_written = 0;
            i2s_channel_write(tx_chan, buffer, len, &bytes_written, portMAX_DELAY);
        }
    }

    if (rx_sock >= 0) close(rx_sock);
    free(buffer);
    ESP_LOGI(TAG, "RX Task Deleted");
    vTaskDelete(NULL);
}

static void audio_tx_task(void *pvParameters) {
    uint8_t *buffer = malloc(UDP_BUFFER_SIZE);
    if (!buffer) { vTaskDelete(NULL); return; }
    
    tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    ESP_LOGI(TAG, "🎤 TX Task Ready");

    while (running) {
        if (recording_enabled && client_addr_resolved) {
            size_t bytes_read = 0;
            esp_err_t ret = i2s_channel_read(rx_chan, buffer, UDP_BUFFER_SIZE, &bytes_read, pdMS_TO_TICKS(100));
            if (ret == ESP_OK && bytes_read > 0) {
                sendto(tx_sock, buffer, bytes_read, 0, (struct sockaddr *)&client_dest_addr, sizeof(client_dest_addr));
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20)); // Чуть быстрее опрос для отзывчивости
        }
    }

    if (tx_sock >= 0) close(tx_sock);
    free(buffer);
    ESP_LOGI(TAG, "TX Task Deleted");
    vTaskDelete(NULL);
}

// === API ===
esp_err_t audio_start(void) {
    if (audio_state == AUDIO_RUNNING || audio_state == AUDIO_STARTING) return ESP_OK;

    audio_state = AUDIO_STARTING;
    if (audio_i2s_init() != ESP_OK) {
        audio_state = AUDIO_IDLE;
        return ESP_FAIL;
    }

    running = true;
    client_addr_resolved = false;
    
    xTaskCreate(audio_rx_task, "audio_rx", 4096, NULL, 5, NULL);
    xTaskCreate(audio_tx_task, "audio_tx", 4096, NULL, 5, NULL);

    audio_state = AUDIO_RUNNING;
    return ESP_OK;
}

void audio_stop(void) {
    if (audio_state == AUDIO_IDLE || audio_state == AUDIO_STOPPING) return;
    
    audio_state = AUDIO_STOPPING;
    running = false;
    recording_enabled = false;

    // Ждем, пока задачи завершатся сами
    vTaskDelay(pdMS_TO_TICKS(200));

    if (tx_chan) {
        i2s_channel_disable(tx_chan);
        i2s_del_channel(tx_chan); // ОСВОБОЖДАЕМ РЕСУРСЫ
        tx_chan = NULL;
    }
    if (rx_chan) {
        i2s_channel_disable(rx_chan);
        i2s_del_channel(rx_chan); // ОСВОБОЖДАЕМ РЕСУРСЫ
        rx_chan = NULL;
    }

    rx_sock = -1;
    tx_sock = -1;
    initialized = false;
    audio_state = AUDIO_IDLE;
    ESP_LOGI(TAG, "🛑 Audio Cleaned Up");
}

void audio_start_recording(void) { recording_enabled = true; }
void audio_stop_recording(void) { recording_enabled = false; }