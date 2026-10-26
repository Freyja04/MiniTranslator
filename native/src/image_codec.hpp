#pragma once

#include "types.hpp"

#include <cstdint>
#include <vector>

namespace minitrans {

PixelBuffer resize_bgra(const PixelBuffer& image, int width, int height);

// Stretches the luminance histogram over the full range with a small
// saturation clamp: washed-out screenshots separate into crisp text and
// background before OCR, which measurably improves recognition. Input and
// output are both straight BGRA.
PixelBuffer normalize_contrast_bgra(const PixelBuffer& image);

}  // namespace minitrans
