# AIoT Phase 2 — Waste Container Occupancy Monitor

On-device TinyML system running on a **Freenove ESP32-S3**. The device captures images of a waste container, preprocesses them locally, runs TFLite inference on-chip, and serves results over WiFi.

Phase 1 used a remote server for inference. Phase 2 performs both preprocessing and inference on the ESP32-S3 itself, exposing `/snapshot` and `/compute` HTTP endpoints. A companion server (`wco_server/`) handles training data collection and optional Google Drive upload.

---

## Repository layout

```
AIoT_phase_2/
├── src/main.cpp               # Firmware entry point
├── include/
│   ├── config.h               # WiFi credentials, pins, LEDC — update before flashing
│   ├── config.h.example       # Template for config.h
│   ├── board_config.h         # Selects CAMERA_MODEL_ESP32S3_EYE
│   ├── camera_pins.h          # Camera pin definitions
│   └── wco_model_v2_data.h    # TFLite model weights (binary, ~350 KB)
├── lib/
│   ├── CameraManager/         # esp_camera init, capture, release
│   ├── LEDController/         # RGB LED via LEDC PWM (pins 19/20/21)
│   ├── UltrasonicSensor/      # HC-SR04 (TRIG=3, ECHO=46)
│   ├── PreprocessService/     # JPEG decode → crop → mask → resize → 192×192 JPEG
│   └── InferenceService/      # TFLite inference (contamination + colour scores)
├── platformio.ini
├── wco_model_v2_2.ipynb       # Colab training notebook
└── wco_server/                # FastAPI data-collection server (see wco_server/README.md)
```

---

## Build & Flash

This project uses **PlatformIO**. All commands assume `pio` is on your PATH.

```bash
# Build
pio run

# Flash to device
pio run --target upload

# Serial monitor (115200 baud)
pio device monitor

# Build + flash + monitor in one line
pio run --target upload && pio device monitor

# Clean build artifacts
pio run --target clean
```

The target environment is `freenove_esp32s3`. Update `upload_port` in `platformio.ini` if your serial port differs.

---

## Configuration

Edit `include/config.h` before flashing (use `include/config.h.example` as a template):

- WiFi SSID and password
- Server IP and port
- Pin assignments (TRIG, ECHO, LED)
- LEDC parameters

Credentials are stored in plain text — do not commit `config.h`.

---

## HTTP endpoints (ESP32)

### `GET /snapshot`
1. LED on → capture 800×600 JPEG → LED off
2. Read fill percentage from ultrasonic sensor
3. `preprocessJpeg()` → 192×192 JPEG
4. Returns JPEG + `X-Fill-Percentage` header

### `GET /compute`
1. LED on → capture 800×600 JPEG → LED off
2. Read fill percentage from ultrasonic sensor
3. `preprocessRgbAndJpeg()` → 192×192 RGB buffer + 192×192 JPEG
4. `runInference()` → contamination and colour float scores
5. Returns JPEG + `X-Fill-Percentage`, `X-Contamination`, `X-Colour` headers

### `GET /uploadTrainingImage?turbidity=<0|1>&particle=<0|1>&color=<0|1>[&size=<px>]`
1. Validates `turbidity`, `particle`, `color` query params (must be 0 or 1)
2. LED on → capture 800×600 JPEG → LED off
3. `preprocessJpeg()` → `size`×`size` JPEG (default 192)
4. Builds a multipart/form-data body with the JPEG and label fields
5. POSTs to `http://<SERVER_IP>:<SERVER_PORT>/uploadGoogleDrive` on the companion server
6. Returns the server's JSON response to the caller

`SERVER_IP` and `SERVER_PORT` are set in `include/config.h`.

---

## Firmware libraries

| Library | Responsibility |
|---|---|
| `CameraManager` | Wraps `esp_camera` init, `capturePhoto()`, and release |
| `LEDController` | RGB LED via LEDC PWM on pins 19/20/21; `ledOn/ledOff/setColor` |
| `UltrasonicSensor` | HC-SR04 on TRIG=3/ECHO=46; `getFillPercentage()` maps 17.5 cm (0%) → 3.5 cm (100%) |
| `PreprocessService` | `preprocessJpeg()` / `preprocessRgbAndJpeg()`: decode JPEG → crop 480×480 @ (362,284) → circular mask (r=240, outside=white) → bilinear resize → 192×192; all buffers via `ps_malloc` |
| `InferenceService` | `initInference()` / `runInference()`: loads model from `wco_model_v2_data.h` into 350 KB TFLite arena; input 192×192 RGB888; outputs contamination & colour float scores |

---

## Key constraints

- **PSRAM required** — all large image buffers use `ps_malloc`. Board is built with `BOARD_HAS_PSRAM` and `qio_opi` memory type.
- The preprocess pipeline allocates three PSRAM buffers sequentially (raw RGB ~1.4 MB, crop ~691 KB, resized ~110 KB); each is freed before the next allocation.
- TFLite tensor arena is 350 KB; `initInference()` tries internal SRAM first and falls back to PSRAM.
- `InferenceService` accesses MicroTFLite's global interpreter and input tensor via `extern` — keep this in mind if upgrading `johnosbb/MicroTFLite`.

---

## Companion server

`wco_server/` is a FastAPI server for training data collection. It auto-labels uploaded images using calibrated image metrics and can push labeled JPEGs directly to Google Drive.

See [`wco_server/README.md`](wco_server/README.md) for setup and API documentation.
