#pragma once
#include <stdint.h>
#include <stddef.h>

// Accepts an 800x600 JPEG, crops 480x480 centered at (CX=362, CY=284)
// with a circular mask, bilinear-resizes to model_size x model_size, re-encodes to JPEG.
// out_jpg is heap-allocated (ps_malloc); caller must free() it.
// model_size defaults to 192. Returns true on success.
bool preprocessJpeg(const uint8_t* src_jpg, size_t src_len,
                    uint8_t** out_jpg, size_t* out_len,
                    int model_size = 192);

// Same pipeline as preprocessJpeg, but also returns the raw model_size×model_size RGB888
// pixels (model_size*model_size*3 bytes, ps_malloc'd) in *out_rgb.
// *out_rgb must be free()'d by the caller.
// If JPEG encoding fails, *out_rgb is freed internally and set to nullptr.
// model_size defaults to 192.
bool preprocessRgbAndJpeg(const uint8_t* src_jpg, size_t src_len,
                           uint8_t** out_rgb,
                           uint8_t** out_jpg, size_t* out_len,
                           int model_size = 192);
