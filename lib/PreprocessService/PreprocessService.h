#pragma once
#include <stdint.h>
#include <stddef.h>

// Accepts an 800x600 JPEG, crops 480x480 centered at (CX=362, CY=284)
// with a circular mask, bilinear-resizes to 192x192, re-encodes to JPEG.
// out_jpg is heap-allocated (ps_malloc); caller must free() it.
// Returns true on success.
bool preprocessJpeg(const uint8_t* src_jpg, size_t src_len,
                    uint8_t** out_jpg, size_t* out_len);
