#include "qr_decoder.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char *TAG = "qr_decoder";

struct quirc *qr_decoder_init(uint16_t width, uint16_t height)
{
    struct quirc *qr = quirc_new();
    if (!qr) {
        ESP_LOGE(TAG, "Failed to allocate quirc object");
        return NULL;
    }

    if (quirc_resize(qr, width, height) < 0) {
        ESP_LOGE(TAG, "Failed to resize quirc to %dx%d", width, height);
        quirc_destroy(qr);
        return NULL;
    }

    ESP_LOGI(TAG, "QR decoder initialized for %dx%d", width, height);
    return qr;
}

int qr_decoder_decode(struct quirc *qr, const uint8_t *image_data,
                      uint16_t width, uint16_t height,
                      qr_decode_result_t *results, int max_results)
{
    if (!qr || !image_data || !results) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Get quirc image buffer
    uint8_t *qr_image = quirc_begin(qr, NULL, NULL);
    if (!qr_image) {
        ESP_LOGE(TAG, "Failed to get quirc image buffer");
        return -1;
    }

    // Copy image data to quirc buffer
    memcpy(qr_image, image_data, width * height);

    // Finish processing
    quirc_end(qr);

    // Get number of detected QR codes
    int num_codes = quirc_count(qr);
    ESP_LOGI(TAG, "Detected %d QR code(s)", num_codes);

    if (num_codes == 0) {
        return 0;
    }

    int decoded_count = 0;
    for (int i = 0; i < num_codes && decoded_count < max_results; i++) {
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(qr, i, &code);

        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err) {
            ESP_LOGW(TAG, "Decoding failed for QR code %d: %s", i, quirc_strerror(err));
            continue;
        }

        // Copy decoded data to result
        results[decoded_count].version = data.version;
        results[decoded_count].ecc_level = data.ecc_level;
        results[decoded_count].data_type = data.data_type;
        results[decoded_count].payload_len = data.payload_len;

        memcpy(results[decoded_count].payload, data.payload,
               data.payload_len < sizeof(results[decoded_count].payload) ?
               data.payload_len : sizeof(results[decoded_count].payload));

        // Null-terminate the payload
        if (data.payload_len < sizeof(results[decoded_count].payload)) {
            results[decoded_count].payload[data.payload_len] = '\0';
        } else {
            results[decoded_count].payload[sizeof(results[decoded_count].payload) - 1] = '\0';
        }

        ESP_LOGI(TAG, "QR Code %d decoded successfully:", i);
        ESP_LOGI(TAG, "  Version: %d", data.version);
        ESP_LOGI(TAG, "  ECC Level: %d", data.ecc_level);
        ESP_LOGI(TAG, "  Data Type: %d", data.data_type);
        ESP_LOGI(TAG, "  Payload Length: %d", data.payload_len);
        ESP_LOGI(TAG, "  Payload: %s", results[decoded_count].payload);

        decoded_count++;
    }

    return decoded_count;
}

void qr_decoder_destroy(struct quirc *qr)
{
    if (qr) {
        quirc_destroy(qr);
        ESP_LOGI(TAG, "QR decoder destroyed");
    }
}

const char* qr_decoder_error_string(quirc_decode_error_t err)
{
    return quirc_strerror(err);
}
