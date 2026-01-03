#ifndef CAMERA_CONFIG_H
#define CAMERA_CONFIG_H

#include "esp_camera.h"
#include "driver/i2c.h"

// ESP32-S3-Touch-LCD-2 Camera Pin Configuration
// VERIFIED - These pins match the working sample_camera_module configuration
#define CAM_PIN_PWDN    17  // Power down - IMPORTANT: Must be 17!
#define CAM_PIN_RESET   -1  // Reset not used (pulled high)
#define CAM_PIN_XCLK    8
#define CAM_PIN_SIOD    21  // SDA
#define CAM_PIN_SIOC    16  // SCL

#define CAM_PIN_D7      2
#define CAM_PIN_D6      7
#define CAM_PIN_D5      10
#define CAM_PIN_D4      14
#define CAM_PIN_D3      11
#define CAM_PIN_D2      15
#define CAM_PIN_D1      13
#define CAM_PIN_D0      12

#define CAM_PIN_VSYNC   6
#define CAM_PIN_HREF    4
#define CAM_PIN_PCLK    9

// Camera configuration structure
camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000,              // 20MHz - matches working example
    .ledc_timer = LEDC_TIMER_3,            // Use timer 3 (LCD uses timer 0)
    .ledc_channel = LEDC_CHANNEL_0,        // Channel 0

    .pixel_format = PIXFORMAT_GRAYSCALE,   // Grayscale for QR code detection
    .frame_size = FRAMESIZE_QVGA,          // 320x240 for faster QR detection
    .jpeg_quality = 90,                    // 90% quality - matches working example
    .fb_count = 2,                         // 2 frame buffers - matches working example
    .fb_location = CAMERA_FB_IN_PSRAM,
    .grab_mode = CAMERA_GRAB_LATEST,       // 1=LATEST - matches working example

    .sccb_i2c_port = -1,                   // Let camera driver manage I2C - matches working example
};

#endif // CAMERA_CONFIG_H
