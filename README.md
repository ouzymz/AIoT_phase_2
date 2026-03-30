# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AIoT Phase 2 — a waste container occupancy monitor running on a **Freenove ESP32-S3** board. The device captures images of a container, preprocesses them on-device, runs TFLite inference locally, and serves results over WiFi. Phase 1 used a remote server for inference; Phase 2 performs both preprocessing and inference on the ESP32-S3 itself, exposing `/snapshot` and `/compute` HTTP endpoints.

## Build & Flash Commands

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

The target environment is `freenove_esp32s3`. Upload/monitor port is `/dev/cu.usbmodem5AE70815351` (update in `platformio.ini` if your port differs).

## Architecture

### Entry point: `src/main.cpp`
Sets up WiFi, initializes all subsystems (camera, LED, ultrasonic, inference), registers the `/snapshot` and `/compute` HTTP routes, and runs `server.handleClient()` in the main loop.

### HTTP endpoint: `GET /snapshot`
1. Turns LED on, captures 800×600 JPEG, turns LED off
2. Reads fill percentage from the ultrasonic sensor
3. Calls `preprocessJpeg()` to produce a 192×192 JPEG
4. Returns the JPEG with the fill level in the `X-Fill-Percentage` response header

### HTTP endpoint: `GET /compute`
1. Turns LED on, captures 800×600 JPEG, turns LED off
2. Reads fill percentage from the ultrasonic sensor
3. Calls `preprocessRgbAndJpeg()` to produce both a 192×192 RGB buffer and a 192×192 JPEG
4. Runs TFLite inference via `runInference()` to get contamination and colour scores
5. Returns the JPEG with three response headers: `X-Fill-Percentage`, `X-Contamination`, `X-Colour`

### Libraries (`lib/`)

| Library | Responsibility |
|---|---|
| `CameraManager` | Wraps `esp_camera` init, capture (`capturePhoto`), and release |
| `LEDController` | RGB LED via LEDC PWM on pins 19/20/21; `ledOn/ledOff/setColor` |
| `UltrasonicSensor` | HC-SR04 on TRIG=3/ECHO=46; `getFillPercentage()` maps 17.5 cm (0%) → 3.5 cm (100%) |
| `PreprocessService` | `preprocessJpeg()` and `preprocessRgbAndJpeg()`: decode JPEG → crop 480×480 @ (362, 284) → circular mask (radius 240, outside=white) → bilinear resize → 192×192 JPEG; all buffers via `ps_malloc` (PSRAM) |
| `InferenceService` | `initInference()` / `runInference()`: loads `wco_model_v2_data.h` into a 350 KB TFLite arena; input is 192×192 RGB888; outputs contamination & colour float scores |

### Configuration: `include/config.h`
Contains WiFi credentials, server IP/port, pin assignments, and LEDC parameters. **Update this file before flashing** — credentials are stored in plain text. Use `include/config.h.example` as a template.

### Camera pin mapping: `include/board_config.h` + `include/camera_pins.h`
`CAMERA_MODEL_ESP32S3_EYE` is selected; other models are commented out.

## Key Constraints

- **PSRAM is required** — all large image buffers use `ps_malloc`. The board is built with `BOARD_HAS_PSRAM` and `qio_opi` memory type.
- The `preprocessJpeg` / `preprocessRgbAndJpeg` pipeline allocates three PSRAM buffers sequentially (raw RGB: ~1.4 MB, crop: ~691 KB, resized: ~110 KB); each is freed before the next allocation.
- The TFLite tensor arena is 350 KB; `initInference()` tries internal SRAM first and falls back to PSRAM.
- `include/wco_model_v2_data.h` contains the binary model weights and is present in the working tree. The Colab training notebook is `wco_model_v2_2.ipynb`.
- `InferenceService` accesses MicroTFLite's global interpreter and input tensor directly via `extern` — keep this in mind if upgrading the `johnosbb/MicroTFLite` dependency.
