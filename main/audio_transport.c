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
#include "rom/gpio.h"

static const char *TAG = "AUDIO_TRANSPORT";

// === ПИНЫ ДЛЯ FREENOVE S3 ===
#define I2S_COMMON_SCK    47
    #define I2S_COMMON_WS  21
#define I2S_SPK_DOUT      3    // DIN усилителя MAX98357
#define I2S_MIC_DIN       38   

#define SAMPLE_RATE       16000
#define UDP_BUFFER_SIZE   1024

#define ESP_AUDIO_RX_PORT 8082
#define ESP_AUDIO_TX_PORT 8083

static bool i2s_is_initialized = false;
static bool tx_enabled = false;

i2s_chan_handle_t tx_chan = NULL;

// === УПРАВЛЕНИЕ КАНАЛОМ ДИНАМИКА ===
void audio_enable_tx(bool enable) {
    if (!tx_chan) return;
    if (enable && !tx_enabled) {
        esp_err_t err = i2s_channel_enable(tx_chan);
        if (err == ESP_OK) {
            tx_enabled = true;
            ESP_LOGI(TAG, "🔊 Динамик активирован");
        } else {
            ESP_LOGE(TAG, "❌ Ошибка включения динамика: %d", err);
        }
    } else if (!enable && tx_enabled) {
        esp_err_t err = i2s_channel_disable(tx_chan);
        if (err == ESP_OK) {
            tx_enabled = false;
            ESP_LOGI(TAG, "🔇 Динамик деактивирован");
        } else {
            ESP_LOGE(TAG, "❌ Ошибка выключения динамика: %d", err);
        }
    }
}

// === ИНИЦИАЛИЗАЦИЯ I2S (только TX) ===
esp_err_t audio_i2s_init(void) {
    if (i2s_is_initialized) {
        ESP_LOGW(TAG, "I2S уже инициализирован");
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, &tx_chan, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Ошибка создания канала I2S: %d", err);
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)I2S_COMMON_SCK,
            .ws   = (gpio_num_t)I2S_COMMON_WS,
            .dout = (gpio_num_t)I2S_SPK_DOUT,
            .din  = I2S_GPIO_UNUSED,
        },
    };

    err = i2s_channel_init_std_mode(tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ TX I2S инициализация не удалась: %d", err);
        return err;
    }
    ESP_LOGI(TAG, "✅ TX I2S канал инициализирован (DOUT=%d) на I2S_NUM_1", I2S_SPK_DOUT);

    i2s_is_initialized = true;
    return ESP_OK;
}

// === ФУНКЦИЯ ГУДКА ===
void audio_play_horn(void) {
    if (!tx_chan) {
        ESP_LOGE(TAG, "❌ tx_chan = NULL");
        return;
    }

    bool was_enabled = tx_enabled;
    if (!was_enabled) {
        ESP_LOGI(TAG, "Включаю динамик для гудка");
        audio_enable_tx(true);
        if (!tx_enabled) {
            ESP_LOGE(TAG, "❌ Не удалось включить динамик");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // даём стабилизацию
    } else {
        ESP_LOGI(TAG, "Динамик уже включён");
    }

    // Выделяем буфер во внутренней RAM (быстрее, не мешает PSRAM)
    int16_t *t_buf = heap_caps_malloc(2048, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!t_buf) {
        ESP_LOGE(TAG, "❌ Нет памяти для буфера");
        if (!was_enabled) audio_enable_tx(false);
        return;
    }

    // Генерация меандра (писк) – амплитуда 30000, частота ~400 Гц (период 40 отсчётов при 16 кГц)
    for (int i = 0; i < 1024; i += 2) {
        int16_t sample = (i % 40 < 20) ? 30000 : -30000;
        t_buf[i] = sample;
        t_buf[i+1] = sample;
    }

    // Отправляем буфер несколько раз, чтобы получить длинный гудок
    size_t bytes_written = 0;
    esp_err_t res = ESP_OK;
    int repeats = 100;   // количество повторов (около 6 секунд звука)
    for (int j = 0; j < repeats; j++) {
        res = i2s_channel_write(tx_chan, t_buf, 2048, &bytes_written, pdMS_TO_TICKS(100));
        if (res != ESP_OK || bytes_written != 2048) {
            ESP_LOGE(TAG, "Ошибка на повторе %d: %d, байт=%d", j, res, bytes_written);
            break;
        }
        // Небольшая задержка между отправками, чтобы DMA успевал (не обязательно, но безопасно)
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "🔊 Гудок завершён, отправлено %d повторов", repeats);

    heap_caps_free(t_buf);
    vTaskDelay(pdMS_TO_TICKS(500));

    if (!was_enabled) {
        audio_enable_tx(false);
        ESP_LOGI(TAG, "Динамик выключен");
    }
}

// Временно заглушки для остальных функций, чтобы линковщик не ругался
esp_err_t audio_start(void) { return ESP_OK; }
void audio_stop(void) {}
void audio_start_recording(void) {}
void audio_stop_recording(void) {}
void audio_enable_rx(bool enable) {}