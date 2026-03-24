#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include "esp_camera.h"

// Пины для Freenove ESP32-S3 (N16R8) - проверено
#define CAM_PIN_PWDN     -1
#define CAM_PIN_RESET    -1
#define CAM_PIN_XCLK     15
#define CAM_PIN_SIOD     4
#define CAM_PIN_SIOC     5
#define CAM_PIN_D7       16
#define CAM_PIN_D6       17
#define CAM_PIN_D5       18
#define CAM_PIN_D4       12
#define CAM_PIN_D3       10
#define CAM_PIN_D2       8
#define CAM_PIN_D1       9
#define CAM_PIN_D0       11
#define CAM_PIN_VSYNC    6
#define CAM_PIN_HREF     7
#define CAM_PIN_PCLK     13

typedef enum {
    CAM_QUALITY_LOW,
    CAM_QUALITY_MEDIUM,
    CAM_QUALITY_HIGH,
    CAM_QUALITY_AUTO
} camera_quality_t;

typedef struct {
    framesize_t frame_size;
    int jpeg_quality;
    int fb_count;
    camera_fb_location_t fb_location;
    int xclk_freq;
    int fps_target;
} camera_params_t;

#define DEFAULT_CAM_PARAMS { \
    .frame_size = FRAMESIZE_VGA, \
    .jpeg_quality = 10, \
    .fps_target = 15, \
    .xclk_freq = 20000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 2 \
}

#define CAM_PARAMS_QVGA { \
    .frame_size = FRAMESIZE_QVGA, \
    .jpeg_quality = 8, \
    .fps_target = 20, \
    .xclk_freq = 20000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 2 \
}

// ИЗМЕНЕНО: частота 10MHz, FPS 10
#define CAM_PARAMS_VGA { \
    .frame_size = FRAMESIZE_VGA, \
    .jpeg_quality = 8, \
    .fps_target = 12, \
    .xclk_freq = 10000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 2 \
}

#define CAM_PARAMS_HVGA { \
    .frame_size = FRAMESIZE_HVGA, \
    .jpeg_quality = 10, \
    .fps_target = 18, \
    .xclk_freq = 20000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 2 \
}

#define CAM_PARAMS_XGA { \
    .frame_size = FRAMESIZE_XGA, \
    .jpeg_quality = 8, \
    .fps_target = 10, \
    .xclk_freq = 20000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 2 \
}
#define CAM_PARAMS_SVGA { \
    .frame_size = FRAMESIZE_SVGA, \
    .jpeg_quality = 12, \
    .fps_target = 15, \
    .xclk_freq = 20000000, \
    .fb_location = CAMERA_FB_IN_PSRAM, \
    .fb_count = 1 \
}

#endif