#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

#include "config.h"
#include "CameraManager.h"
#include "LEDController.h"
#include "UltrasonicSensor.h"
#include "PreprocessService.h"
#include "InferenceService.h"

WebServer server(80);

// ─── CORS ────────────────────────────────────────────────────────────────────
// Allow the Angular web client (different origin) to read custom X-* headers.
static void sendCorsHeaders() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.sendHeader("Access-Control-Expose-Headers",
        "X-Fill-Percentage, X-Turbidity, X-Particle, X-Colour, "
        "X-Input-Size, X-Inference-Ms, X-Model, X-Benchmark-Results");
}

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
    sendCorsHeaders();

    int model_size = 192;
    if (server.hasArg("size")) {
        int val = server.arg("size").toInt();
        if (val > 0) model_size = val;
    }
    Serial.printf("[Server] model_size=%d\n", model_size);

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

    // 3. Preprocess: crop 480x480 @ (362,284) + circle mask + bilinear resize → model_size x model_size JPEG
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessJpeg(fb->buf, fb->len, &out_jpg, &out_len, model_size);
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

void handleCompute() {
    Serial.println("[Server] GET /compute received");
    sendCorsHeaders();

    // Use whatever input size the loaded TFLite model declares.
    // ?size query param is ignored — preprocessing must match the model.
    int model_size = getModelInputSize();
    if (model_size <= 0) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"inference not initialised\"}");
        return;
    }
    if (server.hasArg("size")) {
        Serial.printf("[Server] ?size=%s ignored — using model's %dx%d\n",
                      server.arg("size").c_str(), model_size, model_size);
    }
    Serial.printf("[Server] model_size=%d\n", model_size);

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

    // 3. Preprocess → raw model_size x model_size RGB (for inference) + JPEG (for response)
    uint8_t* rgb_buf = nullptr;
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessRgbAndJpeg(fb->buf, fb->len, &rgb_buf, &out_jpg, &out_len, model_size);
    releasePhoto(fb);

    if (!ok || !rgb_buf || !out_jpg) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"preprocess failed\"}");
        return;
    }

    // 4. Run TFLite inference (3 heads)
    float turbidity = 0.0f;
    float particle  = 0.0f;
    float colour    = 0.0f;
    bool inferred = runInference(rgb_buf, &turbidity, &particle, &colour);
    free(rgb_buf);

    if (!inferred) {
        free(out_jpg);
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"inference failed\"}");
        return;
    }

    // 5. Respond: preprocessed JPEG body + model scores + fill level in headers
    server.sendHeader("X-Fill-Percentage", String(fillPct, 1));
    server.sendHeader("X-Turbidity",       String(turbidity, 4));
    server.sendHeader("X-Particle",        String(particle, 4));
    server.sendHeader("X-Colour",          String(colour, 4));
    server.sendHeader("X-Input-Size",      String(model_size));
    server.send_P(200, "image/jpeg", (const char*)out_jpg, out_len);

    free(out_jpg);
}

void handleUploadTrainingImage() {
    Serial.println("[Server] GET /uploadTrainingImage received");
    sendCorsHeaders();

    // 1. Parse label args
    if (!server.hasArg("turbidity") || !server.hasArg("particle") || !server.hasArg("color")) {
        server.send(400, "application/json",
                    "{\"status\":\"error\",\"message\":\"turbidity, particle and color are required\"}");
        return;
    }

    int turbidity = server.arg("turbidity").toInt();
    int particle  = server.arg("particle").toInt();
    int color     = server.arg("color").toInt();

    if ((turbidity != 0 && turbidity != 1) ||
        (particle  != 0 && particle  != 1) ||
        (color     != 0 && color     != 1)) {
        server.send(400, "application/json",
                    "{\"status\":\"error\",\"message\":\"turbidity, particle and color must be 0 or 1\"}");
        return;
    }

    int model_size = 480;
    if (server.hasArg("size")) {
        int val = server.arg("size").toInt();
        if (val > 0) model_size = val;
    }

    Serial.printf("[Server] labels t=%d p=%d c=%d  size=%d\n",
                  turbidity, particle, color, model_size);

    // 2. Capture
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

    // 3. Preprocess → model_size x model_size JPEG
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessJpeg(fb->buf, fb->len, &out_jpg, &out_len, model_size);
    releasePhoto(fb);

    if (!ok || !out_jpg) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"preprocess failed\"}");
        return;
    }

    // 4. Build multipart body with file + label fields and POST to upload server
    String filename = "t" + String(turbidity) +
                      "-p" + String(particle) +
                      "-c" + String(color) + ".jpeg";

    const String boundary = "----ESP32Boundary";
    auto field = [&](const String& name, const String& value) -> String {
        return "--" + boundary + "\r\n"
               "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" +
               value + "\r\n";
    };

    String partTurbidity = field("turbidity", String(turbidity));
    String partParticle  = field("particle",  String(particle));
    String partColor     = field("color",     String(color));
    String partFileHead  =
        "--" + boundary + "\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String partEnd = "\r\n--" + boundary + "--\r\n";

    size_t totalLen = partTurbidity.length() + partParticle.length() + partColor.length()
                    + partFileHead.length() + out_len + partEnd.length();

    uint8_t* payload = (uint8_t*)malloc(totalLen);
    if (!payload) {
        free(out_jpg);
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"malloc failed\"}");
        return;
    }

    size_t offset = 0;
    auto append = [&](const String& s) {
        memcpy(payload + offset, s.c_str(), s.length());
        offset += s.length();
    };
    append(partTurbidity);
    append(partParticle);
    append(partColor);
    append(partFileHead);
    memcpy(payload + offset, out_jpg, out_len); offset += out_len;
    append(partEnd);
    free(out_jpg);

    String url = "http://" + String(SERVER_IP) + ":" + String(SERVER_PORT) +
                 "/uploadGoogleDrive";

    HTTPClient http;
    http.begin(url);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    int code = http.POST(payload, totalLen);
    free(payload);
    String responseBody = (code > 0) ? http.getString() : "";
    http.end();

    Serial.printf("[Upload] POST %s → HTTP %d\n", url.c_str(), code);

    if (code != 200) {
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"message\":\"upload server error\",\"code\":" +
                    String(code) + "}");
        return;
    }

    server.send(200, "application/json", responseBody);
}

// ─── /computeCloud — capture, preprocess, forward to wco_phase_2 ───────────

// Extract a numeric value for `"key": ...` from a flat-ish JSON string.
// Returns the textual value (trimmed); empty string on miss.
static String jsonExtract(const String& body, const String& key) {
    String needle = "\"" + key + "\"";
    int idx = body.indexOf(needle);
    if (idx < 0) return "";
    int colon = body.indexOf(":", idx + needle.length());
    if (colon < 0) return "";
    int start = colon + 1;
    while (start < (int)body.length() &&
           (body[start] == ' ' || body[start] == '\t')) start++;
    int end = start;
    while (end < (int)body.length() &&
           body[end] != ',' && body[end] != '}' && body[end] != '\n') end++;
    String val = body.substring(start, end);
    val.trim();
    return val;
}

void handleComputeCloud() {
    Serial.println("[Server] GET /computeCloud received");
    sendCorsHeaders();

    if (!server.hasArg("model")) {
        server.send(400, "application/json",
                    "{\"status\":\"error\",\"message\":\"'model' query param is required "
                    "(EfficientNetB0 | MobileNetV2 | MobileNetV1)\"}");
        return;
    }
    String modelName = server.arg("model");

    int model_size = 480;
    if (server.hasArg("size")) {
        int val = server.arg("size").toInt();
        if (val > 0) model_size = val;
    }
    Serial.printf("[Server] model=%s size=%d\n", modelName.c_str(), model_size);

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

    // 2. Fill level
    float fillPct = getFillPercentage();
    Serial.printf("[Sensor] Fill level: %.1f%%\n", fillPct);

    // 3. Preprocess to model_size x model_size JPEG
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessJpeg(fb->buf, fb->len, &out_jpg, &out_len, model_size);
    releasePhoto(fb);

    if (!ok || !out_jpg) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"preprocess failed\"}");
        return;
    }

    // 4. Forward to wco_phase_2 /predict/{model_name}
    String url = "http://" + String(CLOUD_SERVER_IP) + ":" + String(CLOUD_SERVER_PORT) +
                 "/predict/" + modelName;
    String responseBody;
    int code = postJpeg(url, out_jpg, out_len, "compute.jpeg", &responseBody);

    Serial.printf("[Cloud] POST %s → HTTP %d\n", url.c_str(), code);

    if (code != 200) {
        free(out_jpg);
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"message\":\"cloud server error\",\"code\":" +
                    String(code) + "}");
        return;
    }

    // 5. Parse predictions from JSON body
    String turbidity   = jsonExtract(responseBody, "turbidity");
    String particle    = jsonExtract(responseBody, "particle");
    String colour      = jsonExtract(responseBody, "color");
    String inferenceMs = jsonExtract(responseBody, "inference_ms");
    String inputSize   = jsonExtract(responseBody, "input_size");
    inputSize.replace("\"", "");  // strip JSON quotes

    // 6. Respond: preprocessed JPEG body + scores in headers
    server.sendHeader("X-Model",           modelName);
    server.sendHeader("X-Input-Size",      inputSize);
    server.sendHeader("X-Inference-Ms",    inferenceMs);
    server.sendHeader("X-Turbidity",       turbidity);
    server.sendHeader("X-Particle",        particle);
    server.sendHeader("X-Colour",          colour);
    server.sendHeader("X-Fill-Percentage", String(fillPct, 1));
    server.send_P(200, "image/jpeg", (const char*)out_jpg, out_len);

    free(out_jpg);
}

// ─── /computeBenchmark — capture, preprocess, forward to wco_phase_2 /benchmark ─

void handleComputeBenchmark() {
    Serial.println("[Server] GET /computeBenchmark received");
    sendCorsHeaders();

    int model_size = 480;
    if (server.hasArg("size")) {
        int val = server.arg("size").toInt();
        if (val > 0) model_size = val;
    }
    Serial.printf("[Server] size=%d\n", model_size);

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

    // 2. Fill level
    float fillPct = getFillPercentage();
    Serial.printf("[Sensor] Fill level: %.1f%%\n", fillPct);

    // 3. Preprocess to model_size x model_size JPEG
    uint8_t* out_jpg = nullptr;
    size_t   out_len = 0;
    bool ok = preprocessJpeg(fb->buf, fb->len, &out_jpg, &out_len, model_size);
    releasePhoto(fb);

    if (!ok || !out_jpg) {
        server.send(500, "application/json",
                    "{\"status\":\"error\",\"message\":\"preprocess failed\"}");
        return;
    }

    // 4. Forward to wco_phase_2 /benchmark
    String url = "http://" + String(CLOUD_SERVER_IP) + ":" + String(CLOUD_SERVER_PORT) +
                 "/benchmark";
    String responseBody;
    int code = postJpeg(url, out_jpg, out_len, "benchmark.jpeg", &responseBody);

    Serial.printf("[Cloud] POST %s → HTTP %d\n", url.c_str(), code);

    if (code != 200) {
        free(out_jpg);
        server.send(502, "application/json",
                    "{\"status\":\"error\",\"message\":\"cloud server error\",\"code\":" +
                    String(code) + "}");
        return;
    }

    // 5. Respond: preprocessed JPEG body + full benchmark JSON in header + fill level
    //    The benchmark response (3 models, ~700 bytes JSON) is returned verbatim
    //    in X-Benchmark-Results so the caller can parse it client-side.
    server.sendHeader("X-Benchmark-Results", responseBody);
    server.sendHeader("X-Fill-Percentage",   String(fillPct, 1));
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

    // Inference FIRST so the tensor arena can claim internal SRAM
    // before the camera driver fragments PSRAM.
    if (!initInference()) {
        Serial.println("[Boot] Inference init failed — /compute will be unavailable");
    }

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

    server.on("/snapshot",            HTTP_GET, handleSnapshot);
    server.on("/compute",             HTTP_GET, handleCompute);
    server.on("/computeCloud",        HTTP_GET, handleComputeCloud);
    server.on("/computeBenchmark",    HTTP_GET, handleComputeBenchmark);
    server.on("/uploadTrainingImage", HTTP_GET, handleUploadTrainingImage);
    server.begin();

    String ip = WiFi.localIP().toString();
    Serial.println("[Boot] Ready.");
    Serial.println("  Snapshot:     GET http://" + ip + "/snapshot?size=192  (size optional, default 192)");
    Serial.println("  Compute:      GET http://" + ip + "/compute?size=192   (size optional, default 192)");
    Serial.println("  ComputeCloud: GET http://" + ip + "/computeCloud?model=MobileNetV1&size=480");
    Serial.println("  Benchmark:    GET http://" + ip + "/computeBenchmark?size=480  (size optional, default 480)");
    Serial.println("  Upload:       GET http://" + ip + "/uploadTrainingImage?turbidity=0&particle=0&color=0&size=480  (size optional)");
}

void loop() {
    server.handleClient();
}
