#pragma once
#include <stdint.h>

// Call once in setup() before using runInference().
// Allocates tensor arena (PSRAM) and initialises the TFLite interpreter.
bool initInference();

// Run model inference on a pre-processed 192×192 RGB frame.
// rgb_buf  : 192*192*3 bytes, uint8 RGB888 (same layout PreprocessService produces)
// contamination : output score [0.0, 1.0]
// colour        : output score [0.0, 1.0]
// Returns false if inference fails.
bool runInference(const uint8_t* rgb_buf, float* contamination, float* colour);
