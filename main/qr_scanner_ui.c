#include "qr_scanner_ui.h"
#include "camera_module.h"
#include "qr_decoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "qr_scanner_ui";

static lv_obj_t *scanner_screen = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *scan_btn = NULL;
static lv_obj_t *preview_canvas = NULL;

static TaskHandle_t qr_scan_task_handle = NULL;
static TaskHandle_t preview_task_handle = NULL;
static QueueHandle_t qr_frame_queue = NULL;
static QueueHandle_t preview_frame_queue = NULL;

static struct quirc *qr_decoder = NULL;
static bool scanning_active = false;
static bool preview_active = false;

extern bool lvgl_lock(int timeout_ms);
extern void lvgl_unlock(void);

#define PREVIEW_WIDTH 160
#define PREVIEW_HEIGHT 120

static lv_color_t *preview_buf = NULL;

static void preview_task(void *param)
{
    ESP_LOGI(TAG, "Preview task started on core %d", xPortGetCoreID());

    while (preview_active) {
        camera_fb_t *fb = NULL;

        // Wait for frame from preview queue
        if (xQueueReceive(preview_frame_queue, &fb, pdMS_TO_TICKS(1000)) == pdTRUE && fb) {
            // Downsample and convert grayscale to LVGL format
            if (lvgl_lock(10)) {
                // Proper downsampling with mirroring correction
                // Camera frame is 320x240, preview is 160x120 (2:1 ratio)
                for (int y = 0; y < PREVIEW_HEIGHT; y++) {
                    for (int x = 0; x < PREVIEW_WIDTH; x++) {
                        // Mirror both horizontally and vertically
                        int src_x = (PREVIEW_WIDTH - 1 - x) * 2;
                        int src_y = (PREVIEW_HEIGHT - 1 - y) * 2;

                        // Ensure we don't exceed bounds
                        if (src_x >= fb->width) src_x = fb->width - 1;
                        if (src_y >= fb->height) src_y = fb->height - 1;

                        uint8_t pixel = fb->buf[src_y * fb->width + src_x];

                        // Convert grayscale to RGB565 (LVGL color format)
                        preview_buf[y * PREVIEW_WIDTH + x] = lv_color_make(pixel, pixel, pixel);
                    }
                }

                // Update canvas
                if (preview_canvas) {
                    lv_canvas_set_buffer(preview_canvas, preview_buf,
                                        PREVIEW_WIDTH, PREVIEW_HEIGHT, LV_IMG_CF_TRUE_COLOR);
                }

                lvgl_unlock();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33)); // ~30 FPS preview
    }

    ESP_LOGI(TAG, "Preview task finished");
    preview_task_handle = NULL;
    vTaskDelete(NULL);
}

static void qr_scan_task(void *param)
{
    ESP_LOGI(TAG, "QR scan task started on core %d", xPortGetCoreID());

    camera_fb_t *fb = NULL;
    qr_decode_result_t results[2];
    int decode_count = 0;
    int64_t scan_start_time = esp_timer_get_time();
    int frames_processed = 0;

    while (scanning_active) {
        int64_t frame_start = esp_timer_get_time();

        // Wait for frame from QR scanner queue
        if (xQueueReceive(qr_frame_queue, &fb, pdMS_TO_TICKS(1000)) != pdTRUE || !fb) {
            ESP_LOGW(TAG, "No frame received from queue");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int64_t capture_time = esp_timer_get_time() - frame_start;
        frames_processed++;

        ESP_LOGI(TAG, "Frame received: %dx%d, format: %d, size: %zu bytes (wait: %lld ms)",
                 fb->width, fb->height, fb->format, fb->len, capture_time / 1000);

        // Decode QR codes
        int64_t decode_start = esp_timer_get_time();
        decode_count = qr_decoder_decode(qr_decoder, fb->buf, fb->width, fb->height, results, 2);
        int64_t decode_time = esp_timer_get_time() - decode_start;

        int64_t total_frame_time = esp_timer_get_time() - frame_start;

        if (decode_count > 0) {
            int64_t total_scan_time = esp_timer_get_time() - scan_start_time;
            float fps = (frames_processed * 1000000.0f) / total_scan_time;

            ESP_LOGI(TAG, "=== QR CODE DECODED ===");
            ESP_LOGI(TAG, "  Decode time: %lld ms", decode_time / 1000);
            ESP_LOGI(TAG, "  Total frame time: %lld ms", total_frame_time / 1000);
            ESP_LOGI(TAG, "  Total scan time: %lld ms", total_scan_time / 1000);
            ESP_LOGI(TAG, "  Frames processed: %d", frames_processed);
            ESP_LOGI(TAG, "  Average FPS: %.2f", fps);
            ESP_LOGI(TAG, "  Data: %s", results[0].payload);
            ESP_LOGI(TAG, "=======================");

            // Update UI with results and benchmark info
            if (lvgl_lock(-1)) {
                char result_text[512];
                snprintf(result_text, sizeof(result_text),
                         "%.80s\n\n"
                         "Time: %lld ms | FPS: %.1f",
                         results[0].payload,
                         decode_time / 1000,
                         fps);
                lv_label_set_text(result_label, result_text);

                lv_label_set_text(status_label, "QR Code Decoded!");
                lvgl_unlock();
            }

            // Stop scanning after successful decode
            scanning_active = false;
        } else {
            // Update status with current performance
            if (lvgl_lock(-1)) {
                char status_text[64];
                snprintf(status_text, sizeof(status_text),
                         "Scanning... (%d)", frames_processed);
                lv_label_set_text(status_label, status_text);
                lvgl_unlock();
            }

            ESP_LOGI(TAG, "No QR code found (decode: %lld ms, total: %lld ms)",
                     decode_time / 1000, total_frame_time / 1000);
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Stop preview task as well
    preview_active = false;

    // Wait for preview to finish
    vTaskDelay(pdMS_TO_TICKS(100));

    // Unregister consumers and clean up queues
    if (qr_frame_queue) {
        camera_unregister_consumer(qr_frame_queue);
        vQueueDelete(qr_frame_queue);
        qr_frame_queue = NULL;
    }
    if (preview_frame_queue) {
        camera_unregister_consumer(preview_frame_queue);
        vQueueDelete(preview_frame_queue);
        preview_frame_queue = NULL;
    }

    // Update UI when scanning stops
    if (lvgl_lock(-1)) {
        if (decode_count == 0) {
            lv_label_set_text(status_label, "Scan Stopped");
            lv_label_set_text(result_label, "Press 'Start' to scan again");
        }
        lv_obj_clear_state(scan_btn, LV_STATE_DISABLED);
        lvgl_unlock();
    }

    ESP_LOGI(TAG, "QR scan task finished");
    qr_scan_task_handle = NULL;
    vTaskDelete(NULL);
}

static void scan_btn_event_handler(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Scan button clicked");

        if (!scanning_active && !preview_active) {
            // Create frame queues
            qr_frame_queue = xQueueCreate(2, sizeof(camera_fb_t *));
            preview_frame_queue = xQueueCreate(2, sizeof(camera_fb_t *));

            if (!qr_frame_queue || !preview_frame_queue) {
                ESP_LOGE(TAG, "Failed to create frame queues");
                return;
            }

            // Register both consumers
            camera_register_consumer(qr_frame_queue);
            camera_register_consumer(preview_frame_queue);

            scanning_active = true;
            preview_active = true;

            lv_label_set_text(result_label, "Point camera at QR code");
            lv_label_set_text(status_label, "Scanning...");
            lv_obj_add_state(scan_btn, LV_STATE_DISABLED);

            // Start preview task on Core 1 (with LVGL)
            xTaskCreatePinnedToCore(preview_task, "preview_task", 8192, NULL, 3,
                                   &preview_task_handle, 1);

            // Create scan task on Core 0 (to avoid conflict with LVGL on Core 1)
            xTaskCreatePinnedToCore(qr_scan_task, "qr_scan_task", 32768, NULL, 4,
                                   &qr_scan_task_handle, 0);
        }
    }
}

void qr_scanner_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing QR scanner UI");

    bool camera_ok = false;
    bool decoder_ok = false;

    // Check if camera is already initialized (it should be pre-initialized in app_main)
    // We just verify it's working by trying to get the sensor
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        camera_ok = true;
        ESP_LOGI(TAG, "Camera already initialized and working");
    } else {
        ESP_LOGE(TAG, "Camera not initialized or failed");
    }

    // Initialize QR decoder (QVGA resolution: 320x240)
    if (camera_ok) {
        qr_decoder = qr_decoder_init(320, 240);
        if (!qr_decoder) {
            ESP_LOGE(TAG, "Failed to initialize QR decoder");
        } else {
            decoder_ok = true;
            ESP_LOGI(TAG, "QR decoder initialized OK");
        }
    }

    // Allocate preview buffer
    if (camera_ok && decoder_ok) {
        preview_buf = heap_caps_malloc(PREVIEW_WIDTH * PREVIEW_HEIGHT * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (!preview_buf) {
            ESP_LOGE(TAG, "Failed to allocate preview buffer");
        }
    }

    // Create scanner screen (even if camera failed - show error message)
    if (lvgl_lock(-1)) {
        scanner_screen = lv_obj_create(NULL);

        // Create title label at the top
        lv_obj_t *title = lv_label_create(scanner_screen);
        lv_label_set_text(title, "QR Code Scanner");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

        // Create preview canvas centered horizontally, below title
        if (camera_ok && decoder_ok && preview_buf) {
            preview_canvas = lv_canvas_create(scanner_screen);
            lv_canvas_set_buffer(preview_canvas, preview_buf,
                                PREVIEW_WIDTH, PREVIEW_HEIGHT, LV_IMG_CF_TRUE_COLOR);
            lv_obj_align(preview_canvas, LV_ALIGN_TOP_MID, 0, 35);
            lv_obj_set_style_border_width(preview_canvas, 2, 0);
            lv_obj_set_style_border_color(preview_canvas, lv_color_hex(0x00AA00), 0);
            lv_obj_set_style_radius(preview_canvas, 4, 0);

            // Clear preview to black
            lv_canvas_fill_bg(preview_canvas, lv_color_black(), LV_OPA_COVER);
        }

        // Create status label centered below preview
        status_label = lv_label_create(scanner_screen);
        if (!camera_ok) {
            lv_label_set_text(status_label, "Camera Failed!");
        } else if (!decoder_ok) {
            lv_label_set_text(status_label, "Decoder Failed!");
        } else {
            lv_label_set_text(status_label, "Ready");
        }
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
        lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 165);

        // Create result label centered below status
        result_label = lv_label_create(scanner_screen);
        lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(result_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_CENTER, 0);

        if (!camera_ok) {
            lv_label_set_text(result_label,
                "Camera initialization failed\n"
                "Check camera connection");
        } else {
            lv_label_set_text(result_label, "Press 'Start' to scan");
        }
        lv_obj_set_width(result_label, 220);
        lv_obj_align(result_label, LV_ALIGN_TOP_MID, 0, 185);

        // Create scan button at the bottom
        scan_btn = lv_btn_create(scanner_screen);
        lv_obj_set_size(scan_btn, 120, 40);
        lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_MID, 0, -15);

        if (!camera_ok || !decoder_ok) {
            lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_event_cb(scan_btn, scan_btn_event_handler, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *btn_label = lv_label_create(scan_btn);
        lv_label_set_text(btn_label, camera_ok ? "Start Scanning" : "Error");
        lv_obj_center(btn_label);

        // Load the scanner screen
        lv_scr_load(scanner_screen);

        lvgl_unlock();
    }

    if (camera_ok && decoder_ok) {
        ESP_LOGI(TAG, "QR scanner UI initialized successfully");
    } else {
        ESP_LOGW(TAG, "QR scanner UI initialized with errors (camera_ok=%d, decoder_ok=%d)",
                 camera_ok, decoder_ok);
    }
}

void qr_scanner_ui_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing QR scanner UI");

    scanning_active = false;
    preview_active = false;

    // Wait for tasks to finish
    if (qr_scan_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    if (preview_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Unregister consumers
    if (qr_frame_queue) {
        camera_unregister_consumer(qr_frame_queue);
        vQueueDelete(qr_frame_queue);
        qr_frame_queue = NULL;
    }
    if (preview_frame_queue) {
        camera_unregister_consumer(preview_frame_queue);
        vQueueDelete(preview_frame_queue);
        preview_frame_queue = NULL;
    }

    if (qr_decoder) {
        qr_decoder_destroy(qr_decoder);
        qr_decoder = NULL;
    }

    if (preview_buf) {
        heap_caps_free(preview_buf);
        preview_buf = NULL;
    }

    camera_deinit();

    if (lvgl_lock(-1)) {
        if (scanner_screen) {
            lv_obj_del(scanner_screen);
            scanner_screen = NULL;
        }
        lvgl_unlock();
    }

    ESP_LOGI(TAG, "QR scanner UI deinitialized");
}
