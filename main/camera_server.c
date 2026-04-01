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

extern esp_err_t camera_init_with_params(const camera_params_t *params);  // изменено
extern camera_fb_t* camera_capture(void);
extern void camera_return_fb(camera_fb_t *fb);
extern void camera_start(void);
extern void camera_stop(void);

esp_err_t camera_server_init_advanced(stream_mode_t mode, const camera_params_t *cam_params) {
    if (camera_started) {
        ESP_LOGW(TAG, "Камера уже инициализирована");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "🚀 Инициализация камеры...");
    memcpy(&saved_params, cam_params, sizeof(camera_params_t));
    
    // Передаём параметры в функцию инициализации
    if (camera_init_with_params(cam_params) != ESP_OK) {
        ESP_LOGE(TAG, "❌ Ошибка инициализации камеры!");
        return ESP_FAIL;
    }
    
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_pixformat(s, PIXFORMAT_JPEG);
        ESP_LOGI(TAG, "Установлен формат JPEG");
    } else {
        ESP_LOGW(TAG, "Не удалось получить сенсор, формат не изменён");
    }
    
    camera_start();
    
    // Прогрев: захватить 3 кадра для стабилизации
    ESP_LOGI(TAG, "Прогрев камеры (3 кадра)...");
    for (int i = 0; i < 3; i++) {
        camera_fb_t *fb = camera_capture();
        if (fb) {
            ESP_LOGI(TAG, "Прогрев %d: %d байт", i+1, fb->len);
            camera_return_fb(fb);
        } else {
            ESP_LOGW(TAG, "Прогрев %d: кадр не получен", i+1);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    camera_started = true;
    ESP_LOGI(TAG, "✅ Камера запущена и работает");
    return ESP_OK;
}

esp_err_t camera_server_init(void) {
    if (camera_started) {
        ESP_LOGW(TAG, "Камера уже запущена");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Инициализация камеры с параметрами SVGA, качество 10, XCLK 15MHz");
    
    camera_params_t svga_params = {
        .frame_size = FRAMESIZE_SVGA,       // 800x600
        .jpeg_quality = 10,
        .fps_target = 15,
        .xclk_freq = 15000000,            // 15 MHz
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