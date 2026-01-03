#include "qr_scanner_ui.h"
#include "camera_module.h"
#include "qr_decoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "qr_scanner_ui";

static lv_obj_t *scanner_screen = NULL;
static lv_obj_t *result_label = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *scan_btn = NULL;
static TaskHandle_t qr_scan_task_handle = NULL;
static struct quirc *qr_decoder = NULL;
static bool scanning_active = false;

extern bool lvgl_lock(int timeout_ms);
extern void lvgl_unlock(void);

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

        // Capture frame from camera
        fb = camera_capture_frame();
        if (!fb) {
            ESP_LOGE(TAG, "Failed to capture frame");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int64_t capture_time = esp_timer_get_time() - frame_start;
        frames_processed++;

        ESP_LOGI(TAG, "Frame captured: %dx%d, format: %d, size: %zu bytes (capture: %lld ms)",
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
                         "QR Code Found!\n\n"
                         "Data: %.120s\n\n"
                         "--- Performance ---\n"
                         "Decode: %lld ms\n"
                         "Total: %lld ms\n"
                         "Frames: %d\n"
                         "FPS: %.1f",
                         results[0].payload,
                         decode_time / 1000,
                         total_scan_time / 1000,
                         frames_processed,
                         fps);
                lv_label_set_text(result_label, result_text);

                char status_text[128];
                snprintf(status_text, sizeof(status_text),
                         "Status: Decoded in %lld ms!", total_scan_time / 1000);
                lv_label_set_text(status_label, status_text);
                lvgl_unlock();
            }

            // Stop scanning after successful decode
            scanning_active = false;
        } else {
            // Update status with current performance
            if (lvgl_lock(-1)) {
                char status_text[128];
                snprintf(status_text, sizeof(status_text),
                         "Scanning... (frame %d, %lld ms)",
                         frames_processed, decode_time / 1000);
                lv_label_set_text(status_label, status_text);
                lvgl_unlock();
            }

            ESP_LOGI(TAG, "No QR code found (decode: %lld ms, total: %lld ms)",
                     decode_time / 1000, total_frame_time / 1000);
        }

        camera_return_frame(fb);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Update UI when scanning stops
    if (lvgl_lock(-1)) {
        if (decode_count == 0) {
            lv_label_set_text(status_label, "Status: Scan Stopped");
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

        if (!scanning_active) {
            scanning_active = true;
            lv_label_set_text(result_label, "Point camera at QR code...");
            lv_label_set_text(status_label, "Status: Starting scan...");
            lv_obj_add_state(scan_btn, LV_STATE_DISABLED);

            // Create scan task on Core 0 (to avoid conflict with LVGL on Core 1)
            // Stack size increased to 32KB to accommodate Quirc decoder
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

    // Create scanner screen (even if camera failed - show error message)
    if (lvgl_lock(-1)) {
        scanner_screen = lv_obj_create(NULL);

        // Create title label
        lv_obj_t *title = lv_label_create(scanner_screen);
        lv_label_set_text(title, "QR Code Scanner");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

        // Create status label
        status_label = lv_label_create(scanner_screen);
        if (!camera_ok) {
            lv_label_set_text(status_label, "Status: Camera Init Failed!");
        } else if (!decoder_ok) {
            lv_label_set_text(status_label, "Status: Decoder Init Failed!");
        } else {
            lv_label_set_text(status_label, "Status: Ready");
        }
        lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, 0);
        lv_obj_align(status_label, LV_ALIGN_TOP_LEFT, 5, 35);

        // Create result label
        result_label = lv_label_create(scanner_screen);
        lv_label_set_long_mode(result_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(result_label, &lv_font_montserrat_12, 0);

        if (!camera_ok) {
            lv_label_set_text(result_label,
                "Camera initialization failed.\n\n"
                "Possible causes:\n"
                "- Camera not connected\n"
                "- Wrong pin configuration\n"
                "- I2C/SCCB communication issue\n\n"
                "Check serial monitor for error code.");
        } else {
            lv_label_set_text(result_label, "Press 'Scan' to start");
        }
        lv_obj_set_width(result_label, 220);
        lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 10);

        // Create scan button
        scan_btn = lv_btn_create(scanner_screen);
        lv_obj_align(scan_btn, LV_ALIGN_BOTTOM_MID, 0, -20);

        if (!camera_ok || !decoder_ok) {
            lv_obj_add_state(scan_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_add_event_cb(scan_btn, scan_btn_event_handler, LV_EVENT_CLICKED, NULL);
        }

        lv_obj_t *btn_label = lv_label_create(scan_btn);
        lv_label_set_text(btn_label, camera_ok ? "Scan QR Code" : "Camera Error");
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

    if (qr_scan_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    if (qr_decoder) {
        qr_decoder_destroy(qr_decoder);
        qr_decoder = NULL;
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
