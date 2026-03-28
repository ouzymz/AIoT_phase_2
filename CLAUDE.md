# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**AIoT Phase 2** is an embedded vision + IoT system running on an ESP32-S3 microcontroller. It captures images of containers during industrial mixing processes using an OV2640 camera, measures fill levels with an HC-SR04 ultrasonic sensor, and uploads data to a backend server for analysis. The project also embeds a TensorFlow Lite model (`src/wco_model_data.h`) for future on-device inference.

## Build & Flash Commands

This project uses PlatformIO (not a Makefile or npm).

```bash
pio build          # Compile firmware
pio upload         # Compile and flash to device
pio monitor        # Open serial monitor (115200 baud)
pio run -t upload && pio device monitor  # Flash then monitor
pio test           # Run unit tests (in test/ directory)
```

Build target: Freenove ESP32-S3 WROOM (`freenove_esp32s3_wroom`), platform `espressif32@6.10.0`, Arduino framework.

## Configuration

**WiFi credentials and server address are stored in `include/config.h`** (gitignored). Copy `include/config.h.example` to `include/config.h` and fill in:
- WiFi SSID and password
- Backend server IP (default: `192.168.1.70:8000`)
- GPIO pin assignments

**Camera model** is selected in `include/board_config.h` — currently `ESP32S3_EYE`. Pin mappings for each supported board are in `include/camera_pins.h`.

## Architecture

### Firmware (ESP32-S3)

**Request flow** (`GET /capture` HTTP endpoint):
1. LED turns on → camera captures JPEG (SVGA 800×600, quality=12) → LED off
2. Ultrasonic sensor reads distance → fill percentage calculated (logged only, not forwarded)
3. JPEG posted as `multipart/form-data` to `http://<SERVER_IP>:8000/upload`
4. Server response forwarded back to HTTP caller

**Custom Libraries** (`lib/`):
- `CameraManager` — OV2640 init, JPEG capture, frame buffer management (PSRAM-backed, 2 buffers)
- `LEDController` — Common-anode RGB LED via LEDC PWM (channels 5–7, 12kHz, 8-bit); note: PWM is **inverted** (255 = full on)
- `UltrasonicSensor` — HC-SR04 driver with temperature-compensated sound speed; calibrated range: EMPTY=17.5cm → FULL=3.5cm

**Key hardware pins** (defaults in `config.h`):
- Ultrasonic: TRIG=GPIO3, ECHO=GPIO46
- LED: R=GPIO21, G=GPIO20, B=GPIO19
- Camera: configured per `camera_pins.h` for ESP32S3_EYE

**Build flags** critical to the build:
- `-DBOARD_HAS_PSRAM` — required; camera frame buffers use external PSRAM
- `-DARDUINO_USB_MODE=0` — USB CDC mode for serial monitor

### Backend Server (`wco_server/`)

> Note: The `wco_server/` directory was deleted in the most recent commit ("Clear: unrelated remaining of phase_1 cleared"). The backend code will be created with endpoints: `/upload`.

## Data Labeling Convention

Image filenames follow the pattern `t{test}_p{phase}_c{collection}_{frame}.jpg`:
- `t` = test number (0-based)
- `p` = phase/condition within test
- `c` = camera/collection index
- Frame count is zero-padded 4 digits

Mixing states documented in `follow.txt` (Turkish): clean oil, add paint, add particles, add water — each capturing 15–20 photos per condition.

## TensorFlow Lite Model

`src/wco_model_data.h` contains ~345KB of embedded model weights (`g_wco_model_data[]`). The model has 3 outputs but is **not yet called** in `main.cpp` — integration is planned for a future phase.
