#pragma once
#include <stdint.h>

// Call once in setup() before using runInference().
// Allocates tensor arena and initialises the TFLite interpreter.
bool initInference();

// Returns the model's expected square input edge length (H = W), read from the
// TFLite input tensor's dims at runtime. Returns 0 if the interpreter is not
// initialised. Channels are assumed to be 3 (RGB888).
int getModelInputSize();

// Run model inference on a pre-processed square RGB888 frame.
// rgb_buf   : N*N*3 bytes where N = getModelInputSize()
// turbidity : output score [0.0, 1.0]
// particle  : output score [0.0, 1.0]
// colour    : output score [0.0, 1.0]
// Output ordering assumes alphabetical Keras dict-output serialisation
// (colour=0, particle=1, turbidity=2). Returns false if inference fails.
bool runInference(const uint8_t* rgb_buf,
                  float* turbidity, float* particle, float* colour);
