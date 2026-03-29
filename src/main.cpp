#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "config.h"
#include "CameraManager.h"
#include "LEDController.h"
#include "UltrasonicSensor.h"
#include "PreprocessService.h"

WebServer server(80);

// ─── Helper ──────────────────────────────────────────────────────────────────

// POST a single JPEG as multipart/form-data (field name: "file").
// Returns HTTP status code, or negative on connection error.
// Writes server response text into *responseBody if non-null.
int postJpeg(const String& url, const uint8_t* buf, size_t len,
             const String& filename, String* responseBody = nullptr) {
    const String boundary = "----ESP32Boundary";

    String bodyStart =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String bodyEnd = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = bodyStart.length() + len + bodyEnd.length();
    uint8_t* payload = (uint8_t*)malloc(totalLen);
    if (!payload) {
        Serial.println("[HTTP] malloc failed for payload");
        return -1;
    }

    size_t offset = 0;
    memcpy(payload + offset, bodyStart.c_str(), bodyStart.length()); offset += bodyStart.length();
    memcpy(payload + offset, buf,               len);                 offset += len;
    memcpy(payload + offset, bodyEnd.c_str(),   bodyEnd.length());

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);

    int code = http.POST(payload, totalLen);
    free(payload);

    if (responseBody) {
        *responseBody = (code > 0) ? http.getString() : "";
    }
    http.end();
    return code;
}

// ─── Handlers ────────────────────────────────────────────────────────────────

void handleSnapshot() {
    Serial.println("[Server] GET /snapshot received");

    // 1. Capture JPEG (800x600)
    ledOn();
    delay(1000);
    camera_fb_t* fb = capturePhoto();
    delay(1000);
    ledOff();

    if (!fb) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"camera capture failed\"}");
        return;
    }

    // 2. Measure fill level
    float fillPct = getFillPercentage();
    Serial.printf("[Sensor] Fill level: %.1f%%\n", fillPct);

    // 3. Preprocess: crop 480x480 @ (362,284) + circle mask + bilinear resize → 192x192 JPEG
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessJpeg(fb->buf, fb->len, &out_jpg, &out_len);
    releasePhoto(fb);

    if (!ok || !out_jpg) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"preprocess failed\"}");
        return;
    }

    // 4. Send preprocessed 192x192 JPEG; fill percentage in custom header
    server.sendHeader("X-Fill-Percentage", String(fillPct, 1));
    server.send_P(200, "image/jpeg", (const char*)out_jpg, out_len);

    free(out_jpg);
}


// ─── Setup / Loop ────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("[Boot] Starting...");

    initLED();
    initUltrasonic();
    if (!initCamera()) {
        Serial.println("[Boot] Camera init failed, halting");
        while (true) { delay(1000); }
    }

    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Failed to connect after 20 attempts, halting");
        while (true) { delay(1000); }
    }

    Serial.printf("[WiFi] Connected. IP: %s\n", WiFi.localIP().toString().c_str());

    server.on("/snapshot", HTTP_GET, handleSnapshot);
    server.begin();

    String ip = WiFi.localIP().toString();
    Serial.println("[Boot] Ready.");

    Serial.println("  Snapshot:  GET http://" + ip + "/snapshot");
}

void loop() {
    server.handleClient();
}
