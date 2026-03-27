#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "config.h"
#include "CameraManager.h"
#include "LEDController.h"
#include "UltrasonicSensor.h"

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

void handleCapture() {
    Serial.println("[Server] GET /capture received");

    // 1. Capture
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

    // 2. Distance — Serial only, not sent to server
    float fillPercentage = getFillPercentage();
    Serial.printf("[Sensor] Fill level: %.1f%%\n", fillPercentage);

    // 3. POST to /upload
    String url = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) + "/upload";
    Serial.printf("[HTTP] POSTing to %s\n", url.c_str());

    String body;
    int code = postJpeg(url, fb->buf, fb->len, "photo.jpg", &body);
    releasePhoto(fb);

    // 4. Forward server response to caller
    if (code > 0) {
        Serial.printf("[HTTP] Response %d: %s\n", code, body.c_str());
        server.send(code, "application/json", body);
    } else {
        Serial.printf("[HTTP] POST failed: code %d\n", code);
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"code\":" + String(code) + "}");
    }
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

    server.on("/capture",   HTTP_GET, handleCapture);
    server.begin();

    String ip = WiFi.localIP().toString();
    Serial.println("[Boot] Ready.");
    Serial.println("  Capture:   GET http://" + ip + "/capture");
}

void loop() {
    server.handleClient();
}
