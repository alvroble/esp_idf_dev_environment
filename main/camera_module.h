#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include "esp_camera.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the camera module
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t camera_init(void);

/**
 * @brief Capture a frame from the camera
 *
 * @return camera_fb_t* Pointer to frame buffer, NULL on failure
 */
camera_fb_t* camera_capture_frame(void);

/**
 * @brief Return the frame buffer to the camera driver
 *
 * @param fb Frame buffer to return
 */
void camera_return_frame(camera_fb_t *fb);

/**
 * @brief Deinitialize the camera module
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t camera_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // CAMERA_MODULE_H
