#include "camera_module.h"
#include "camera_config.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "camera_module";
static TaskHandle_t capture_task_handle = NULL;
static QueueHandle_t frame_queue = NULL;
static bool camera_running = false;

#define FRAME_QUEUE_SIZE 2

// Multi-consumer support
static QueueHandle_t consumer_queues[MAX_FRAME_CONSUMERS];
static int consumer_count = 0;
static SemaphoreHandle_t consumer_mutex = NULL;

typedef struct {
    camera_fb_t *fb;
    int ref_count;
} frame_ref_t;

// Dedicated capture task - continuously grabs frames and broadcasts to consumers
static void camera_capture_task(void *param)
{
    ESP_LOGI(TAG, "Camera capture task started on core %d", xPortGetCoreID());
    camera_fb_t *last_fb = NULL;

    while (camera_running) {
        camera_fb_t *fb = esp_camera_fb_get();
        if (fb) {
            // Return previous frame before broadcasting new one
            if (last_fb) {
                esp_camera_fb_return(last_fb);
            }
            last_fb = fb;

            // If we have registered consumers, broadcast to them
            if (xSemaphoreTake(consumer_mutex, portMAX_DELAY) == pdTRUE) {
                if (consumer_count > 0) {
                    // Broadcast frame to all registered consumers (overwrite old frames)
                    for (int i = 0; i < consumer_count; i++) {
                        if (consumer_queues[i] != NULL) {
                            // Try to overwrite - clear old frame first
                            camera_fb_t *old_fb = NULL;
                            while (xQueueReceive(consumer_queues[i], &old_fb, 0) == pdTRUE) {
                                // Just clearing the queue, old_fb points to old frame (already returned above)
                            }
                            // Send new frame
                            if (xQueueSend(consumer_queues[i], &fb, 0) != pdTRUE) {
                                ESP_LOGW(TAG, "Consumer %d queue send failed", i);
                            }
                        }
                    }
                    xSemaphoreGive(consumer_mutex);
                } else {
                    xSemaphoreGive(consumer_mutex);
                    // No consumers, use legacy single queue mode
                    if (xQueueSend(frame_queue, &fb, 0) != pdTRUE) {
                        camera_fb_t *old_fb = NULL;
                        if (xQueueReceive(frame_queue, &old_fb, 0) == pdTRUE) {
                            esp_camera_fb_return(old_fb);
                        }
                        if (xQueueSend(frame_queue, &fb, 0) != pdTRUE) {
                            esp_camera_fb_return(fb);
                            last_fb = NULL;
                        }
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // ~20 FPS capture rate
    }

    // Clean up - return last frame
    if (last_fb) {
        esp_camera_fb_return(last_fb);
    }

    // Clean up queue
    camera_fb_t *fb = NULL;
    while (xQueueReceive(frame_queue, &fb, 0) == pdTRUE) {
        // Frames already returned above
    }

    ESP_LOGI(TAG, "Camera capture task stopped");
    capture_task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t camera_init(void)
{
    ESP_LOGI(TAG, "Initializing camera...");

    // Initialize consumer mutex
    if (!consumer_mutex) {
        consumer_mutex = xSemaphoreCreateMutex();
        if (!consumer_mutex) {
            ESP_LOGE(TAG, "Failed to create consumer mutex");
            return ESP_FAIL;
        }
    }

    // Initialize consumer array
    memset(consumer_queues, 0, sizeof(consumer_queues));
    consumer_count = 0;

    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera initialization failed with error 0x%x", err);
        return err;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s == NULL) {
        ESP_LOGE(TAG, "Failed to get camera sensor");
        return ESP_FAIL;
    }

    // Set camera settings optimized for QR code detection
    s->set_brightness(s, 0);     // -2 to 2
    s->set_contrast(s, 0);       // -2 to 2
    s->set_saturation(s, 0);     // -2 to 2
    s->set_sharpness(s, 0);      // -2 to 2
    s->set_special_effect(s, 0); // 0 to 6 (0 - No Effect)
    s->set_wb_mode(s, 0);        // 0 to 4 - Auto
    s->set_awb_gain(s, 1);       // 0 = disable , 1 = enable
    s->set_exposure_ctrl(s, 1);  // 0 = disable , 1 = enable
    s->set_aec2(s, 0);           // 0 = disable , 1 = enable
    s->set_gain_ctrl(s, 1);      // 0 = disable , 1 = enable
    s->set_agc_gain(s, 0);       // 0 to 30
    s->set_gainceiling(s, (gainceiling_t)0); // 0 to 6
    s->set_bpc(s, 0);            // 0 = disable , 1 = enable
    s->set_wpc(s, 1);            // 0 = disable , 1 = enable
    s->set_raw_gma(s, 1);        // 0 = disable , 1 = enable
    s->set_lenc(s, 1);           // 0 = disable , 1 = enable
    s->set_hmirror(s, 0);        // 0 = disable , 1 = enable
    s->set_vflip(s, 0);          // 0 = disable , 1 = enable
    s->set_dcw(s, 1);            // 0 = disable , 1 = enable
    s->set_colorbar(s, 0);       // 0 = disable , 1 = enable

    ESP_LOGI(TAG, "Camera initialized successfully");
    camera_sensor_info_t *info = esp_camera_sensor_get_info(&s->id);
    if (info) {
        ESP_LOGI(TAG, "Camera sensor: %s (PID=0x%x)", info->name, s->id.PID);
    }

    ESP_LOGI(TAG, "Camera configuration:");
    ESP_LOGI(TAG, "  Frame size: %d x %d", camera_config.frame_size, camera_config.pixel_format);
    ESP_LOGI(TAG, "  XCLK: GPIO%d @ %d MHz", camera_config.pin_xclk, camera_config.xclk_freq_hz / 1000000);
    ESP_LOGI(TAG, "  PCLK: GPIO%d, VSYNC: GPIO%d, HREF: GPIO%d",
             camera_config.pin_pclk, camera_config.pin_vsync, camera_config.pin_href);

    // Create frame queue
    frame_queue = xQueueCreate(FRAME_QUEUE_SIZE, sizeof(camera_fb_t *));
    if (!frame_queue) {
        ESP_LOGE(TAG, "Failed to create frame queue");
        esp_camera_deinit();
        return ESP_FAIL;
    }

    // Start dedicated capture task
    camera_running = true;
    BaseType_t task_created = xTaskCreatePinnedToCore(
        camera_capture_task,
        "cam_capture",
        4096,
        NULL,
        4,  // Priority 4 - same as scan task
        &capture_task_handle,
        1   // Run on Core 1 (APP CPU) - matches working example
    );

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create camera capture task");
        camera_running = false;
        vQueueDelete(frame_queue);
        frame_queue = NULL;
        esp_camera_deinit();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Camera capture task started - using queue-based architecture");

    return ESP_OK;
}

esp_err_t camera_register_consumer(QueueHandle_t queue)
{
    if (!queue) {
        ESP_LOGE(TAG, "Invalid queue handle");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(consumer_mutex, portMAX_DELAY) == pdTRUE) {
        if (consumer_count >= MAX_FRAME_CONSUMERS) {
            ESP_LOGE(TAG, "Maximum consumers reached");
            ret = ESP_ERR_NO_MEM;
        } else {
            consumer_queues[consumer_count] = queue;
            consumer_count++;
            ESP_LOGI(TAG, "Registered consumer %d (total: %d)", consumer_count - 1, consumer_count);
            ret = ESP_OK;
        }
        xSemaphoreGive(consumer_mutex);
    }
    return ret;
}

esp_err_t camera_unregister_consumer(QueueHandle_t queue)
{
    if (!queue) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(consumer_mutex, portMAX_DELAY) == pdTRUE) {
        for (int i = 0; i < consumer_count; i++) {
            if (consumer_queues[i] == queue) {
                // Shift remaining consumers down
                for (int j = i; j < consumer_count - 1; j++) {
                    consumer_queues[j] = consumer_queues[j + 1];
                }
                consumer_queues[consumer_count - 1] = NULL;
                consumer_count--;
                ESP_LOGI(TAG, "Unregistered consumer (remaining: %d)", consumer_count);
                ret = ESP_OK;
                break;
            }
        }
        xSemaphoreGive(consumer_mutex);
    }
    return ret;
}

camera_fb_t* camera_capture_frame(void)
{
    if (!frame_queue) {
        ESP_LOGE(TAG, "Frame queue not initialized");
        return NULL;
    }

    camera_fb_t *fb = NULL;
    // Wait up to 4 seconds for a frame (legacy single consumer mode)
    if (xQueueReceive(frame_queue, &fb, pdMS_TO_TICKS(4000)) != pdTRUE) {
        ESP_LOGE(TAG, "Timeout waiting for frame from queue");
        return NULL;
    }

    return fb;
}

void camera_return_frame(camera_fb_t *fb)
{
    if (fb) {
        esp_camera_fb_return(fb);
    }
}

esp_err_t camera_deinit(void)
{
    // Stop the capture task
    camera_running = false;

    // Wait for task to finish
    while (capture_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGI(TAG, "Camera capture task stopped");

    // Delete queue
    if (frame_queue) {
        vQueueDelete(frame_queue);
        frame_queue = NULL;
    }

    return esp_camera_deinit();
}
