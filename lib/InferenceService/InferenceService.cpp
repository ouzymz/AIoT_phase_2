#include "InferenceService.h"
#include "wco_model_v2_data.h"

#include <Arduino.h>
#include <MicroTFLite.h>
#include <tensorflow/lite/micro/micro_interpreter.h>

// Access MicroTFLite's internal globals directly for bulk tensor I/O.
// Defined at file scope in MicroTFLite.cpp.
extern TfLiteTensor*             tflInputTensor;
extern tflite::MicroInterpreter* tflInterpreter;

// Start conservatively; the Serial log will print how many bytes were actually
// used so you can tune this down if memory is tight.
#define TENSOR_ARENA_SIZE (350 * 1024)

static uint8_t* tensor_arena = nullptr;

bool initInference() {
    // Prefer internal SRAM (faster); fall back to PSRAM if unavailable.
    tensor_arena = (uint8_t*)heap_caps_malloc(TENSOR_ARENA_SIZE,
                                              MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!tensor_arena) {
        Serial.println("[Inference] Internal RAM full, falling back to PSRAM");
        tensor_arena = (uint8_t*)ps_malloc(TENSOR_ARENA_SIZE);
    }
    if (!tensor_arena) {
        Serial.println("[Inference] Arena allocation failed");
        return false;
    }

    if (!ModelInit(g_wco_model_v2_data, tensor_arena, TENSOR_ARENA_SIZE)) {
        Serial.println("[Inference] ModelInit failed");
        return false;
    }

    Serial.printf("[Inference] Ready — arena used: %d / %d bytes\n",
                  (int)tflInterpreter->arena_used_bytes(), TENSOR_ARENA_SIZE);
    ModelPrintTensorInfo();
    return true;
}

bool runInference(const uint8_t* rgb_buf, float* contamination, float* colour) {
    constexpr int INPUT_SIZE = 192 * 192 * 3;

    // Write pixels directly into the input tensor buffer (avoids 110k
    // individual ModelSetInput() calls).
    if (tflInputTensor->type == kTfLiteUInt8) {
        memcpy(tflInputTensor->data.uint8, rgb_buf, INPUT_SIZE);
    } else if (tflInputTensor->type == kTfLiteInt8) {
        // uint8 [0,255] → int8 [-128,127]  (standard INT8 quantisation offset)
        for (int i = 0; i < INPUT_SIZE; i++)
            tflInputTensor->data.int8[i] = (int8_t)((int)rgb_buf[i] - 128);
    } else {
        // float32 — normalise to [0, 1]
        for (int i = 0; i < INPUT_SIZE; i++)
            tflInputTensor->data.f[i] = rgb_buf[i] / 255.0f;
    }

    if (!ModelRunInference()) return false;

    // Dequantise one scalar from a tensor (int8 / uint8 / float32).
    auto dequant = [](TfLiteTensor* t, int idx) -> float {
        if (t->type == kTfLiteInt8)
            return ((float)t->data.int8[idx]  - t->params.zero_point) * t->params.scale;
        if (t->type == kTfLiteUInt8)
            return ((float)t->data.uint8[idx] - t->params.zero_point) * t->params.scale;
        return t->data.f[idx];
    };

    // output(1) = contamination, output(0) = colour
    // (matches Python: preds[0] → contamination, preds[1] → colour)
    *contamination = dequant(tflInterpreter->output(1), 0);
    *colour        = dequant(tflInterpreter->output(0), 0);

    Serial.printf("[Inference] contamination=%.4f  colour=%.4f\n",
                  *contamination, *colour);
    return true;
}
