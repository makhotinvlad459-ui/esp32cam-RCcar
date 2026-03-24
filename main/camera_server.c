#include <string.h>
#include "camera_server.h"
#include "camera_config.h"
#include "wifi_stream.h"
#include "esp_log.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "CAM_SRV";
static bool camera_started = false;
static camera_params_t saved_params;

extern esp_err_t camera_init(void);
extern camera_fb_t* camera_capture(void);
extern void camera_return_fb(camera_fb_t *fb);
extern void camera_start(void);
extern void camera_stop(void);

esp_err_t camera_server_init_advanced(stream_mode_t mode, const camera_params_t *cam_params) {
    ESP_LOGI(TAG, "🚀 Инициализация камеры...");
    
    memcpy(&saved_params, cam_params, sizeof(camera_params_t));
    
    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "❌ Ошибка инициализации камеры!");
        return ESP_FAIL;
    }
    
    camera_start();
    
    // Просто ждем 1 секунду для стабилизации
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    camera_started = true;
    ESP_LOGI(TAG, "✅ Камера запущена и работает");
    return ESP_OK;
}

esp_err_t camera_server_init(void) {
    ESP_LOGI(TAG, "Инициализация камеры");
    
    camera_params_t svga_params = {
        .frame_size = FRAMESIZE_SVGA,    // 800x600
        .jpeg_quality = 10,              
        .fps_target = 15,                
        .xclk_freq = 15000000,           // 15 MHz
        .fb_location = CAMERA_FB_IN_PSRAM,
        .fb_count = 1
    };
    
    return camera_server_init_advanced(STREAM_MODE_UDP_MJPEG, &svga_params);
}

esp_err_t camera_server_get_stats(stream_stats_t *stats) {
    if (stats) {
        stats->fps = 15;
        stats->frames_sent = 0;
    }
    return ESP_OK;
}