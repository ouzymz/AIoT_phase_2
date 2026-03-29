#pragma once
#include <stdint.h>
#include <stddef.h>

// Accepts an 800x600 JPEG, crops 480x480 centered at (CX=362, CY=284)
// with a circular mask, bilinear-resizes to 192x192, re-encodes to JPEG.
// out_jpg is heap-allocated (ps_malloc); caller must free() it.
// Returns true on success.
bool preprocessJpeg(const uint8_t* src_jpg, size_t src_len,
                    uint8_t** out_jpg, size_t* out_len);

// Same pipeline as preprocessJpeg, but also returns the raw 192×192 RGB888
// pixels (192*192*3 bytes, ps_malloc'd) in *out_rgb.
// *out_rgb must be free()'d by the caller.
// If JPEG encoding fails, *out_rgb is freed internally and set to nullptr.
bool preprocessRgbAndJpeg(const uint8_t* src_jpg, size_t src_len,
                           uint8_t** out_rgb,
                           uint8_t** out_jpg, size_t* out_len);
