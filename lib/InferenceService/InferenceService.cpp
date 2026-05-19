#include "InferenceService.h"
#include "g_model_int8_data.h"

#include <Arduino.h>
#include <MicroTFLite.h>
#include <tensorflow/lite/micro/micro_interpreter.h>

// Access MicroTFLite's internal globals directly for bulk tensor I/O.
// Defined at file scope in MicroTFLite.cpp.
extern TfLiteTensor*             tflInputTensor;
extern tflite::MicroInterpreter* tflInterpreter;

// V1 (128, a=0.25) ~280KB, V2 (160, a=0.35) ~420KB, B0 (224) ~1.2MB+ (won't fit on S3).
// 500KB covers V1+V2 comfortably; bump higher only if MicroTFLite logs request more.
#define TENSOR_ARENA_SIZE (500 * 1024)

static uint8_t* tensor_arena = nullptr;

bool initInference() {
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

    if (!ModelInit(g_model_int8_data, tensor_arena, TENSOR_ARENA_SIZE)) {
        Serial.println("[Inference] ModelInit failed");
        return false;
    }

    Serial.printf("[Inference] Ready — arena used: %d / %d bytes\n",
                  (int)tflInterpreter->arena_used_bytes(), TENSOR_ARENA_SIZE);

    if (tflInputTensor && tflInputTensor->dims && tflInputTensor->dims->size >= 4) {
        Serial.printf("[Inference] Input shape: [%d, %d, %d, %d]  outputs: %d\n",
                      tflInputTensor->dims->data[0],
                      tflInputTensor->dims->data[1],
                      tflInputTensor->dims->data[2],
                      tflInputTensor->dims->data[3],
                      (int)tflInterpreter->outputs_size());
    }
    ModelPrintTensorInfo();
    return true;
}

int getModelInputSize() {
    if (!tflInputTensor || !tflInputTensor->dims || tflInputTensor->dims->size < 4) {
        return 0;
    }
    return tflInputTensor->dims->data[1];  // assumes NHWC, H == W
}

bool runInference(const uint8_t* rgb_buf,
                  float* turbidity, float* particle, float* colour) {
    const int H = getModelInputSize();
    if (H <= 0) {
        Serial.println("[Inference] Input tensor not ready");
        return false;
    }
    const int INPUT_SIZE = H * H * 3;

    if (tflInputTensor->type == kTfLiteUInt8) {
        memcpy(tflInputTensor->data.uint8, rgb_buf, INPUT_SIZE);
    } else if (tflInputTensor->type == kTfLiteInt8) {
        for (int i = 0; i < INPUT_SIZE; i++)
            tflInputTensor->data.int8[i] = (int8_t)((int)rgb_buf[i] - 128);
    } else {
        for (int i = 0; i < INPUT_SIZE; i++)
            tflInputTensor->data.f[i] = rgb_buf[i] / 255.0f;
    }

    if (!ModelRunInference()) return false;

    auto dequant = [](TfLiteTensor* t, int idx) -> float {
        if (t->type == kTfLiteInt8)
            return ((float)t->data.int8[idx]  - t->params.zero_point) * t->params.scale;
        if (t->type == kTfLiteUInt8)
            return ((float)t->data.uint8[idx] - t->params.zero_point) * t->params.scale;
        return t->data.f[idx];
    };

    // Keras dict-output → TFLite alphabetical ordering:
    // output(0) = colour, output(1) = particle, output(2) = turbidity
    const int n_out = (int)tflInterpreter->outputs_size();
    if (n_out < 3) {
        Serial.printf("[Inference] Expected 3 outputs, got %d\n", n_out);
        return false;
    }
    *colour    = dequant(tflInterpreter->output(0), 0);
    *particle  = dequant(tflInterpreter->output(1), 0);
    *turbidity = dequant(tflInterpreter->output(2), 0);

    Serial.printf("[Inference] turbidity=%.4f  particle=%.4f  colour=%.4f\n",
                  *turbidity, *particle, *colour);
    return true;
}
