#pragma once

#include "types.hpp"

#include <cstdint>
#include <vector>

namespace minitrans {

PixelBuffer resize_bgra(const PixelBuffer& image, int width, int height);

}  // namespace minitrans
