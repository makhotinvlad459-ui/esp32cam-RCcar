#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "driver/i2s_std.h"   // для i2s_chan_handle_t

#ifdef __cplusplus
extern "C" {
#endif

bool audio_is_initialized(void);

esp_err_t audio_start(void);
void audio_stop(void);
void audio_start_recording(void);
void audio_stop_recording(void);

// ДОБАВИТЬ ЭТИ СТРОКИ:
extern i2s_chan_handle_t tx_chan;
esp_err_t audio_i2s_init(void);
void audio_enable_tx(bool enable);
void audio_play_horn(void);

#ifdef __cplusplus
}
#endif