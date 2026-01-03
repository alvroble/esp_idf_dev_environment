#ifndef CAMERA_MODULE_H
#define CAMERA_MODULE_H

#include "esp_camera.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_FRAME_CONSUMERS 4

/**
 * @brief Initialize the camera module
 *
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t camera_init(void);

/**
 * @brief Register a queue to receive camera frames
 *
 * The camera will broadcast frames to all registered queues.
 * Consumer is responsible for returning frames via camera_return_frame().
 *
 * @param queue Queue handle to receive frames
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t camera_register_consumer(QueueHandle_t queue);

/**
 * @brief Unregister a consumer queue
 *
 * @param queue Queue handle to unregister
 * @return esp_err_t ESP_OK on success, error code otherwise
 */
esp_err_t camera_unregister_consumer(QueueHandle_t queue);

/**
 * @brief Capture a frame from the camera (single consumer mode)
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
