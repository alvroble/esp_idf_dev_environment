#ifndef QR_DECODER_H
#define QR_DECODER_H

#include <stdint.h>
#include "quirc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QR_MAX_PAYLOAD 1024

typedef struct {
    int version;
    int ecc_level;
    int data_type;
    int payload_len;
    uint8_t payload[QR_MAX_PAYLOAD];
} qr_decode_result_t;

/**
 * @brief Initialize the QR decoder
 *
 * @param width Image width
 * @param height Image height
 * @return struct quirc* Pointer to quirc object, NULL on failure
 */
struct quirc* qr_decoder_init(uint16_t width, uint16_t height);

/**
 * @brief Decode QR codes from image data
 *
 * @param qr Quirc object
 * @param image_data Grayscale image data
 * @param width Image width
 * @param height Image height
 * @param results Array to store decode results
 * @param max_results Maximum number of results to decode
 * @return int Number of QR codes decoded, -1 on error
 */
int qr_decoder_decode(struct quirc *qr, const uint8_t *image_data,
                      uint16_t width, uint16_t height,
                      qr_decode_result_t *results, int max_results);

/**
 * @brief Destroy the QR decoder
 *
 * @param qr Quirc object to destroy
 */
void qr_decoder_destroy(struct quirc *qr);

/**
 * @brief Get error string for quirc decode error
 *
 * @param err Error code
 * @return const char* Error string
 */
const char* qr_decoder_error_string(quirc_decode_error_t err);

#ifdef __cplusplus
}
#endif

#endif // QR_DECODER_H
