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
    float distance = getDistance();
    Serial.printf("[Sensor] Distance: %.2f cm\n", distance);

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

void handleCalibrate() {
    Serial.println("[Server] GET /calibrate received");

    // Parse ?n= param (default 20, max 50)
    int n = 20;
    if (server.hasArg("n")) {
        int parsed = server.arg("n").toInt();
        if (parsed > 0) n = parsed;
        if (n > 50)     n = 50;
    }
    Serial.printf("[Calib] Collecting %d images\n", n);

    String baseUrl = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT);

    // Capture and upload each image individually
    for (int i = 1; i <= n; i++) {
        
        ledOn();
        delay(1000);
        camera_fb_t* fb = capturePhoto();
        delay(1000);
        ledOff();

        if (!fb) {
            Serial.printf("[Calib] %d/%d → capture failed, skipping\n", i, n);
            delay(500);
            continue;
        }

        char filename[20];
        snprintf(filename, sizeof(filename), "calib_%03d.jpg", i);

        int code = postJpeg(baseUrl + "/calibrate/image", fb->buf, fb->len, filename);
        releasePhoto(fb);

        Serial.printf("[Calib] %d/%d → HTTP %d\n", i, n, code);
        delay(1000);
    }

    // Trigger threshold computation on the server
    String computeUrl = baseUrl + "/calibrate/compute";
    Serial.printf("[Calib] GET %s\n", computeUrl.c_str());

    HTTPClient http;
    http.begin(computeUrl);
    int code = http.GET();

    if (code > 0) {
        String body = http.getString();
        Serial.printf("[Calib] Compute %d: %s\n", code, body.c_str());
        http.end();
        server.send(code, "application/json", body);
    } else {
        Serial.printf("[Calib] Compute GET failed: %d\n", code);
        http.end();
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"message\":\"compute failed\"}");
    }
}

void handleValidate() {
    Serial.println("[Server] GET /validate received");

    // Parse ?group= param — required
    if (!server.hasArg("group")) {
        server.send(400, "application/json",
                    "{\"status\":\"error\",\"message\":\"missing required param: group\"}");
        return;
    }
    String group = server.arg("group");

    // Parse ?n= param (default 3, max 9)
    int n = 3;
    if (server.hasArg("n")) {
        int parsed = server.arg("n").toInt();
        if (parsed > 0) n = parsed;
        if (n > 9)      n = 9;
    }
    Serial.printf("[Validate] group=%s  n=%d\n", group.c_str(), n);

    String baseUrl = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT);

    for (int i = 1; i <= n; i++) {
        ledOn();
        delay(800);
        camera_fb_t* fb = capturePhoto();
        ledOff();

        if (!fb) {
            Serial.printf("[Validate] %d/%d → capture failed, skipping\n", i, n);
            delay(800);
            continue;
        }

        char filename[16];
        snprintf(filename, sizeof(filename), "val_%03d.jpg", i);

        String url = baseUrl + "/validate?group=" + group;
        int code = postJpeg(url, fb->buf, fb->len, filename);
        releasePhoto(fb);

        Serial.printf("[Validate] %d/%d → HTTP %d\n", i, n, code);
        delay(800);
    }

    // Fetch validation report
    String reportUrl = baseUrl + "/validate/report";
    Serial.printf("[Validate] GET %s\n", reportUrl.c_str());

    HTTPClient http;
    http.begin(reportUrl);
    int code = http.GET();

    if (code > 0) {
        String body = http.getString();
        Serial.printf("[Validate] Report %d: %s\n", code, body.c_str());
        http.end();
        server.send(code, "application/json", body);
    } else {
        Serial.printf("[Validate] Report GET failed: %d\n", code);
        http.end();
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"message\":\"report fetch failed\"}");
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
    server.on("/calibrate", HTTP_GET, handleCalibrate);
    server.on("/validate",  HTTP_GET, handleValidate);
    server.begin();

    String ip = WiFi.localIP().toString();
    Serial.println("[Boot] Ready.");
    Serial.println("  Capture:   GET http://" + ip + "/capture");
    Serial.println("  Calibrate: GET http://" + ip + "/calibrate?n=20");
    Serial.println("  Validate:  GET http://" + ip + "/validate?group=clean");
    Serial.println("             GET http://" + ip + "/validate?group=turbid");
    Serial.println("             GET http://" + ip + "/validate?group=turbid_particle");
}

void loop() {
    server.handleClient();
}
