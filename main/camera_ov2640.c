#include "camera_config.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "CAM_OV2640";
static bool camera_active = false;
static bool camera_initialized = false;
static camera_params_t current_params;
static camera_quality_t current_quality = CAM_QUALITY_AUTO;
static int64_t last_frame_time = 0;
static float channel_bandwidth = 0;

static void optimize_jpeg_quality(void) {
    if (current_quality != CAM_QUALITY_AUTO) return;
    
    if (channel_bandwidth < 2.0f) {
        current_params.jpeg_quality = 20;
    } else if (channel_bandwidth < 3.5f) {
        current_params.jpeg_quality = 15;
    } else {
        current_params.jpeg_quality = 10;
    }
}

void camera_update_bandwidth(float mbps) {
    channel_bandwidth = mbps;
    if (current_quality == CAM_QUALITY_AUTO) {
        optimize_jpeg_quality();
    }
}

esp_err_t camera_init_with_params(const camera_params_t *params) {
    ESP_LOGI(TAG, "🚀 Инициализация OV2640...");
    
    memcpy(&current_params, params, sizeof(camera_params_t));
    
    camera_config_t config = {
        .pin_pwdn   = CAM_PIN_PWDN,
        .pin_reset  = CAM_PIN_RESET,
        .pin_xclk   = CAM_PIN_XCLK,
        .pin_sscb_sda = CAM_PIN_SIOD,
        .pin_sscb_scl = CAM_PIN_SIOC,
        .pin_d7      = CAM_PIN_D7,
        .pin_d6      = CAM_PIN_D6,
        .pin_d5      = CAM_PIN_D5,
        .pin_d4      = CAM_PIN_D4,
        .pin_d3      = CAM_PIN_D3,
        .pin_d2      = CAM_PIN_D2,
        .pin_d1      = CAM_PIN_D1,
        .pin_d0      = CAM_PIN_D0,
        .pin_vsync   = CAM_PIN_VSYNC,
        .pin_href    = CAM_PIN_HREF,
        .pin_pclk    = CAM_PIN_PCLK,
        
        .xclk_freq_hz = current_params.xclk_freq,
        .ledc_timer   = LEDC_TIMER_0,
        .ledc_channel = LEDC_CHANNEL_0,
        
        .pixel_format = PIXFORMAT_JPEG,
        .frame_size   = current_params.frame_size,
        .jpeg_quality = current_params.jpeg_quality,
        
        .fb_location  = current_params.fb_location,
        .fb_count     = current_params.fb_count,
        .grab_mode    = CAMERA_GRAB_LATEST,
    };
    
    ESP_LOGI(TAG, "XCLK частота: %d Hz", config.xclk_freq_hz);
    ESP_LOGI(TAG, "Настройка памяти: location=%s, buffers=%d", 
             (config.fb_location == CAMERA_FB_IN_PSRAM) ? "PSRAM" : "DRAM", 
             config.fb_count);
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Ошибка инициализации камеры: 0x%x", err);
        return err;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        // БЫСТРЫЕ ФИКСИРОВАННЫЕ НАСТРОЙКИ
        s->set_brightness(s, 2);
        s->set_contrast(s, 2);
        s->set_saturation(s, 2);
        s->set_whitebal(s, 1);
        s->set_awb_gain(s, 2);
        s->set_gain_ctrl(s, 1);
        s->set_agc_gain(s, 255);
        s->set_exposure_ctrl(s, 1);
        
        // Отключаем медленные автонастройки
            s->set_aec2(s, 1);
            s->set_ae_level(s, 2);
        
        s->set_hmirror(s, 0);
        s->set_vflip(s, 0);
        s->set_quality(s, current_params.jpeg_quality);
        s->set_special_effect(s, 0);
        s->set_sharpness(s, 1);
        s->set_dcw(s, 1);
        s->set_colorbar(s, 0);
        
        ESP_LOGI(TAG, "✅ Сенсор OV2640 настроен (быстрый старт)");
    }

    camera_initialized = true;
    camera_active = true;
    ESP_LOGI(TAG, "🎬 Камера готова! Разрешение: %d, Качество: %d, FPS: %d", 
             current_params.frame_size, current_params.jpeg_quality, current_params.fps_target);
    return ESP_OK;
}

esp_err_t camera_init(void) {
    camera_params_t params = DEFAULT_CAM_PARAMS;
    return camera_init_with_params(&params);
}

void camera_start(void) {
    camera_active = true;
    last_frame_time = esp_timer_get_time();
}

void camera_stop(void) {
    camera_active = false;
}

bool camera_is_active(void) {
    return camera_active;
}

// Быстрый захват кадра без лишних задержек
camera_fb_t* camera_capture(void) {
    if (!camera_active) return NULL;
    
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) {
        // Минимальная проверка валидности
        if (fb->len > 100 && 
            fb->buf[0] == 0xFF && fb->buf[1] == 0xD8) {
            return fb;
        }
        esp_camera_fb_return(fb);
        return NULL;
    }
    return NULL;
}

void camera_return_fb(camera_fb_t *fb) {
    if (fb) esp_camera_fb_return(fb);
}

void camera_set_quality(camera_quality_t quality) {
    current_quality = quality;
    sensor_t *s = esp_camera_sensor_get();
    if (!s) return;

    int q = 12;
    if (quality == CAM_QUALITY_HIGH) q = 8;
    if (quality == CAM_QUALITY_LOW) q = 20;
    
    s->set_quality(s, q);
    current_params.jpeg_quality = q;
}

void camera_set_fps(int fps) {
    if (fps > 0 && fps <= 60) current_params.fps_target = fps;
}

const camera_params_t* camera_get_params(void) {
    return &current_params;
}