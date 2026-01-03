# ESP32-S3 QR Code Scanner

Real-time QR code scanner implementation for ESP32-S3-Touch-LCD-2 development board with performance benchmarking.

## Features

- Queue-based camera capture architecture for consistent frame delivery
- Real-time QR code decoding using Quirc library
- LVGL touchscreen UI with live performance metrics
- Comprehensive benchmarking (decode time, FPS, total scan time)
- Dual-core processing: Core 0 for decoding, Core 1 for camera/UI
- Optimized for QVGA (320x240) grayscale images

## Hardware

**Board:** ESP32-S3-Touch-LCD-2
**Camera:** OV2640 (connected via onboard camera connector)

### Verified Pin Configuration

| Function | GPIO | Description |
|----------|------|-------------|
| PWDN     | 17   | Power down (CRITICAL!) |
| XCLK     | 8    | Camera clock |
| SIOD     | 21   | SCCB data (I2C SDA) |
| SIOC     | 16   | SCCB clock (I2C SCL) |
| D7       | 2    | Data bit 7 (MSB) |
| D6       | 7    | Data bit 6 |
| D5       | 10   | Data bit 5 |
| D4       | 14   | Data bit 4 |
| D3       | 11   | Data bit 3 |
| D2       | 15   | Data bit 2 |
| D1       | 13   | Data bit 1 |
| D0       | 12   | Data bit 0 (LSB) |
| VSYNC    | 6    | Vertical sync |
| HREF     | 4    | Horizontal reference |
| PCLK     | 9    | Pixel clock |

## Architecture

### Queue-Based Frame Processing

The implementation uses a producer-consumer pattern to prevent VSYNC overflow and ensure reliable frame capture:

```
Core 1 (APP CPU)                    Core 0 (PRO CPU)
┌─────────────────────┐            ┌──────────────────┐
│ Camera Capture Task │            │  QR Scan Task    │
│                     │            │                  │
│ esp_camera_fb_get() │            │ Wait for frame   │
│         │           │            │        │         │
│         ▼           │            │        ▼         │
│  xQueueSend()       │ ─────────▶ │ xQueueReceive()  │
│         │           │  FreeRTOS  │        │         │
│         │           │   Queue    │        ▼         │
│    Continuous       │   (2 bufs) │  QR Decode       │
│      Loop           │            │        │         │
│                     │            │        ▼         │
│                     │            │  Update UI       │
└─────────────────────┘            └──────────────────┘
```

**Why This Works:**
- Dedicated capture task continuously calls `esp_camera_fb_get()` to prevent buffer overflow
- Queue decouples capture from processing, allowing different frame rates
- Core 1 handles camera/LVGL (I2C-sensitive operations)
- Core 0 handles CPU-intensive QR decoding

### Task Configuration

```c
// Camera capture task (Core 1, Priority 4)
xTaskCreatePinnedToCore(camera_capture_task, "cam_capture", 4096, NULL, 4,
                        &capture_task_handle, 1);

// QR scan task (Core 0, Priority 4, 32KB stack for Quirc)
xTaskCreatePinnedToCore(qr_scan_task, "qr_scan_task", 32768, NULL, 4,
                        &qr_scan_task_handle, 0);
```

## Performance Benchmarking

The scanner provides comprehensive real-time metrics:

### On-Screen Display
```
QR Code Found!

Data: https://example.com

--- Performance ---
Decode: 156 ms
Total: 1240 ms
Frames: 8
FPS: 6.5
```

### Serial Log Output
```
I (12345) qr_scanner_ui: === QR CODE DECODED ===
I (12346) qr_scanner_ui:   Decode time: 156 ms
I (12347) qr_scanner_ui:   Total frame time: 178 ms
I (12348) qr_scanner_ui:   Total scan time: 1240 ms
I (12349) qr_scanner_ui:   Frames processed: 8
I (12350) qr_scanner_ui:   Average FPS: 6.45
I (12351) qr_scanner_ui:   Data: https://example.com
I (12352) qr_scanner_ui: =======================
```

**Metrics:**
- **Decode time:** Time spent in Quirc decoder (per frame)
- **Total frame time:** Capture + decode time (per frame)
- **Total scan time:** From button press to successful decode
- **Frames processed:** Number of frames captured during scan
- **Average FPS:** Frames per second throughout scan session

## Dependencies

Auto-managed via [idf_component.yml](main/idf_component.yml):

```yaml
dependencies:
  idf: ">=4.4"
  lvgl/lvgl: "~8.4.0"
  espressif/esp_lcd_touch_cst816s: "^1.0.3"
  espressif/esp32-camera: "^2.0.13"
  espressif/quirc: "^1.0.0"
```

## Build and Flash

```bash
# Install dependencies
idf.py reconfigure

# Build
idf.py build

# Flash and monitor (with 10-second boot delay for serial setup)
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Structure

```
main/
├── camera_config.h       # Camera pin definitions and config
├── camera_module.c/h     # Queue-based camera capture
├── qr_decoder.c/h        # Quirc wrapper
├── qr_scanner_ui.c/h     # LVGL UI + scan task + benchmarking
├── main.c                # Application entry point
└── idf_component.yml     # Dependencies
```

## Critical Configuration

### I2C Driver Compatibility

**Must use legacy I2C driver** to avoid conflict between camera SCCB and touch controller:

```
CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y
```

### Camera Settings ([camera_config.h](main/camera_config.h))

```c
.pixel_format = PIXFORMAT_GRAYSCALE,   // Faster QR detection
.frame_size = FRAMESIZE_QVGA,          // 320x240
.fb_count = 2,                         // Double buffering
.fb_location = CAMERA_FB_IN_PSRAM,     // Use external PSRAM
.grab_mode = CAMERA_GRAB_LATEST,       // Always get newest frame
.sccb_i2c_port = -1,                   // Auto I2C management
.xclk_freq_hz = 20000000,              // 20MHz clock
```

## Usage

1. Power on board (10-second boot delay for serial monitor setup)
2. Touchscreen displays "QR Code Scanner" interface
3. Press "Scan QR Code" button
4. Point camera at QR code
5. Results display with performance metrics
6. Button re-enables for next scan

## Troubleshooting

### Camera Init Failed
- Verify PWDN pin is GPIO 17 (critical!)
- Check camera module connection
- Ensure PSRAM enabled in menuconfig

### VSYNC Overflow / Frame Timeout
- Queue architecture should prevent this
- Verify capture task is running on Core 1
- Check `camera_running` flag is set

### Stack Overflow in qr_scan_task
- Stack size must be 32KB minimum for Quirc
- Default 16KB will cause overflow

### I2C Driver Conflict
```
E (848) i2c: CONFLICT! driver_ng is not allowed to be used with this old driver
```
- Set `CONFIG_SCCB_HARDWARE_I2C_DRIVER_LEGACY=y`
- Camera and touch both use I2C, must use same driver version

### QR Not Detected
- Ensure adequate lighting
- QR code should fill 30-60% of frame
- Try different distances (10-30cm typical)
- Check logs for decode attempts and timing

## API Reference

### Camera Module

```c
esp_err_t camera_init(void);              // Start camera + capture task
camera_fb_t* camera_capture_frame(void);  // Get frame from queue (blocks)
void camera_return_frame(camera_fb_t *fb);// Return frame to driver
esp_err_t camera_deinit(void);            // Stop capture task + camera
```

### QR Decoder

```c
struct quirc* qr_decoder_init(uint16_t width, uint16_t height);
int qr_decoder_decode(struct quirc *qr, const uint8_t *image_data,
                      uint16_t width, uint16_t height,
                      qr_decode_result_t *results, int max_results);
void qr_decoder_destroy(struct quirc *qr);
```

### QR Scanner UI

```c
void qr_scanner_ui_init(void);    // Create LVGL UI + start scanner
void qr_scanner_ui_deinit(void);  // Cleanup UI + stop tasks
```

## Performance Tips

1. **Lighting:** Bright, even lighting improves decode speed
2. **Distance:** 15-25cm from camera optimal for standard QR codes
3. **Stability:** Hold camera steady during scan
4. **Code Size:** Larger QR codes (version 3+) may decode slower
5. **Frame Rate:** ~6-8 FPS typical with QVGA grayscale

## License

- ESP-IDF: Apache 2.0
- LVGL: MIT
- Quirc: ISC
- ESP32-Camera: Apache 2.0

## References

- [ESP32-Camera Driver](https://github.com/espressif/esp32-camera)
- [Quirc Library](https://github.com/dlbeer/quirc)
- [LVGL Documentation](https://docs.lvgl.io/)
- [ESP32-S3 Technical Reference](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
