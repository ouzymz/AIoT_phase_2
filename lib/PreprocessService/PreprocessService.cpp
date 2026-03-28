#include "PreprocessService.h"
#include "img_converters.h"
#include <Arduino.h>

#define CAM_W      800
#define CAM_H      600
#define CX         362
#define CY         284
#define R          240
#define CROP_SIZE  480   // R * 2
#define MODEL_SIZE 192

#define CROP_X0    (CX - R)   // 122
#define CROP_Y0    (CY - R)   // 44

bool preprocessJpeg(const uint8_t* src_jpg, size_t src_len,
                    uint8_t** out_jpg, size_t* out_len) {

    // ── 1. Decode JPEG → RGB888 (800x600) ────────────────────────────────────
    uint8_t* rgb = (uint8_t*)ps_malloc(CAM_W * CAM_H * 3);
    if (!rgb) {
        Serial.println("[Preprocess] ps_malloc failed for rgb");
        return false;
    }

    if (!fmt2rgb888(src_jpg, src_len, PIXFORMAT_JPEG, rgb)) {
        Serial.println("[Preprocess] JPEG decode failed");
        free(rgb);
        return false;
    }

    // ── 2. Crop 480x480 centered at (CX, CY) + circle mask ───────────────────
    uint8_t* crop = (uint8_t*)ps_malloc(CROP_SIZE * CROP_SIZE * 3);
    if (!crop) {
        Serial.println("[Preprocess] ps_malloc failed for crop");
        free(rgb);
        return false;
    }

    const int r2 = R * R;
    for (int row = 0; row < CROP_SIZE; row++) {
        for (int col = 0; col < CROP_SIZE; col++) {
            int dx  = col - R;
            int dy  = row - R;
            int out = (row * CROP_SIZE + col) * 3;

            if (dx * dx + dy * dy > r2) {
                crop[out] = crop[out + 1] = crop[out + 2] = 255; // outside → white
            } else {
                int src_idx = ((CROP_Y0 + row) * CAM_W + (CROP_X0 + col)) * 3;
                crop[out]     = rgb[src_idx];
                crop[out + 1] = rgb[src_idx + 1];
                crop[out + 2] = rgb[src_idx + 2];
            }
        }
    }
    free(rgb);

    // ── 3. Bilinear resize 480x480 → 192x192 ─────────────────────────────────
    uint8_t* resized = (uint8_t*)ps_malloc(MODEL_SIZE * MODEL_SIZE * 3);
    if (!resized) {
        Serial.println("[Preprocess] ps_malloc failed for resized");
        free(crop);
        return false;
    }

    const float scale = (float)CROP_SIZE / (float)MODEL_SIZE;

    for (int y = 0; y < MODEL_SIZE; y++) {
        float fy  = (y + 0.5f) * scale - 0.5f;
        int   y0  = (int)fy;
        int   y1  = y0 + 1;
        float wy1 = fy - y0;
        float wy0 = 1.0f - wy1;
        if (y0 < 0)          y0 = 0;
        if (y1 >= CROP_SIZE) y1 = CROP_SIZE - 1;

        for (int x = 0; x < MODEL_SIZE; x++) {
            float fx  = (x + 0.5f) * scale - 0.5f;
            int   x0  = (int)fx;
            int   x1  = x0 + 1;
            float wx1 = fx - x0;
            float wx0 = 1.0f - wx1;
            if (x0 < 0)          x0 = 0;
            if (x1 >= CROP_SIZE) x1 = CROP_SIZE - 1;

            const uint8_t* p00 = &crop[(y0 * CROP_SIZE + x0) * 3];
            const uint8_t* p01 = &crop[(y0 * CROP_SIZE + x1) * 3];
            const uint8_t* p10 = &crop[(y1 * CROP_SIZE + x0) * 3];
            const uint8_t* p11 = &crop[(y1 * CROP_SIZE + x1) * 3];

            int o = (y * MODEL_SIZE + x) * 3;
            for (int c = 0; c < 3; c++) {
                float v = wy0 * (wx0 * p00[c] + wx1 * p01[c])
                        + wy1 * (wx0 * p10[c] + wx1 * p11[c]);
                resized[o + c] = (uint8_t)(v + 0.5f);
            }
        }
    }
    free(crop);

    // ── 4. Encode 192x192 RGB888 → JPEG ──────────────────────────────────────
    bool ok = fmt2jpg(resized, MODEL_SIZE * MODEL_SIZE * 3,
                      MODEL_SIZE, MODEL_SIZE,
                      PIXFORMAT_RGB888, 80,
                      out_jpg, out_len);
    free(resized);

    if (!ok) Serial.println("[Preprocess] JPEG encode failed");
    else     Serial.printf("[Preprocess] Done — output %zu bytes\n", *out_len);

    return ok;
}
