#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool audio_is_initialized(void);

esp_err_t audio_start(void);
void audio_stop(void);
void audio_start_recording(void);
void audio_stop_recording(void);

#ifdef __cplusplus
}
#endif