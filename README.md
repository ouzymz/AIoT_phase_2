# data-collect — WCO TinyML Data Collection System

End-to-end data collection pipeline for a TinyML **Waste Cooking Oil (WCO) quality assessment** project.

An ESP32-S3 with an OV2640 camera captures images on demand and POSTs them to a FastAPI server,
which auto-labels each image using image metrics derived from a clean-oil calibration step.

---

## Repository structure

```
data-collect/
├── include/
│   ├── config.h             # NOT committed — copy from config.h.example
│   ├── config.h.example     # Template: WiFi credentials, server IP, pin definitions
│   ├── board_config.h       # Camera model selection
│   └── camera_pins.h        # OV2640 pin map for ESP32S3_EYE
├── lib/
│   ├── CameraManager/       # OV2640 init (FRAMESIZE_SVGA), capture, release
│   ├── LEDController/       # RGB LED via LEDC PWM
│   └── UltrasonicSensor/    # HC-SR04 distance via pulseIn
├── src/
│   └── main.cpp             # WebServer port 80: /capture, /calibrate, /validate
├── platformio.ini
└── wco_server/              # FastAPI data collection server
    └── README.md
```

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | Freenove ESP32-S3-WROOM-1 CAM |
| Camera | OV3660 — JPEG, SVGA (800×600) |
| Distance sensor | HC-SR04 — TRIG: GPIO 3, ECHO: GPIO 46 |
| LED | RGB via LEDC PWM — R: GPIO 21, G: GPIO 20, B: GPIO 19 |

> Distance is read on every capture but logged to Serial only — it is **not** sent to the server.

---

## Firmware setup

### 1. Configure credentials

```bash
cp include/config.h.example include/config.h
```

Edit `include/config.h`:

```c
#define WIFI_SSID       "your_ssid"
#define WIFI_PASSWORD   "your_password"
#define SERVER_IP       "192.168.1.xxx"   // machine running wco_server
#define SERVER_PORT     8000
```

> `config.h` is gitignored — it will never be committed.

### 2. Build and flash

```bash
pio run --target upload
```

Or open the project in VS Code with the PlatformIO extension.

Monitor serial output at 115200 baud:

```bash
pio device monitor
```

---

## ESP32 endpoints

Once connected to WiFi the ESP32 acts as a web server on **port 80**:

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/capture` | LED on → capture JPEG → LED off → POST to server `/upload` → return server JSON |
| `GET` | `/calibrate?n=20` | Capture *n* clean-oil images (default 20, max 50), POST each to `/calibrate/image`, then trigger `/calibrate/compute` |
| `GET` | `/validate?group=<g>&n=3` | Capture *n* images (default 3, max 9), POST each to `/validate?group=<g>`, then fetch and return `/validate/report` |

`group` must be one of: `clean`, `turbid`, `turbid_particle`.

Boot output example:
```
[Boot] Ready.
  Capture:   GET http://192.168.1.42/capture
  Calibrate: GET http://192.168.1.42/calibrate?n=20
  Validate:  GET http://192.168.1.42/validate?group=clean
             GET http://192.168.1.42/validate?group=turbid
             GET http://192.168.1.42/validate?group=turbid_particle
```

---

## Recommended workflow

```
1. Start wco_server  →  cd wco_server && uvicorn main:app --host 0.0.0.0 --port 8000
2. Flash ESP32       →  pio run --target upload
3. Calibrate         →  GET http://<ESP32-IP>/calibrate?n=20       (clean fresh oil)
4. Collect data      →  GET http://<ESP32-IP>/capture              (repeat per sample)
5. Validate          →  GET http://<ESP32-IP>/validate?group=clean
                        GET http://<ESP32-IP>/validate?group=turbid
6. Check stats       →  GET http://<server-IP>:8000/stats
7. Check accuracy    →  GET http://<server-IP>:8000/validate/report
```

---

## Server

The FastAPI server handles image storage, auto-labeling, calibration thresholds,
and CSV logging. See the dedicated documentation:

**[wco_server/README.md](wco_server/README.md)**

---

## PlatformIO config summary

| Key | Value |
|-----|-------|
| Environment | `freenove_esp32s3` |
| Board | `freenove_esp32_s3_wroom` |
| Framework | Arduino |
| Platform | espressif32 @ 6.10.0 |
| Memory type | `qio_opi` |
| Lib deps | `esp32-camera` |
| Build flags | `-DARDUINO_USB_MODE=0 -DBOARD_HAS_PSRAM` |
