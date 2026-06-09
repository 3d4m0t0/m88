#pragma once

#include "draw.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace M88ScreenCapture {

// Build a 640x400 4bpp BMP (Windows M88 CaptureScreen format). Returns 0 on failure.
size_t BuildBmp4(const uint8_t* framebuffer, int bytes_per_line,
                 const Draw::Palette palette[256], std::vector<uint8>* out);

}  // namespace M88ScreenCapture
