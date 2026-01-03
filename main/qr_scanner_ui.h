#ifndef QR_SCANNER_UI_H
#define QR_SCANNER_UI_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the QR scanner UI
 *
 * This function initializes the camera, QR decoder, and creates
 * the LVGL UI for QR code scanning.
 */
void qr_scanner_ui_init(void);

/**
 * @brief Deinitialize the QR scanner UI
 *
 * This function cleans up camera, QR decoder, and UI resources.
 */
void qr_scanner_ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // QR_SCANNER_UI_H
