#pragma once

#include "draw.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

class SharedFramebufferDraw;

// Thread-safe RGBA staging for OpenGL presentation (TownsQt pattern).
class SharedRgbaFramebuffer {
 public:
  void StageFromIndexed8(const uint8* indices, int bpl, unsigned wid, unsigned hei,
                         const Draw::Palette* palette, unsigned palette_capacity);

  void StageFromDraw(SharedFramebufferDraw* draw);

  bool Acquire(const unsigned char** rgba, unsigned int* wid, unsigned int* hei,
               uint64_t* serial) const;

  uint64_t Serial() const;

  void SetPresentCallback(std::function<void()> callback);

 private:
  void NotifyPresent();

  std::function<void()> present_callback_;
  mutable std::mutex mutex_;
  std::vector<unsigned char> buffers_[2];
  unsigned int wid_ = 0;
  unsigned int hei_ = 0;
  int read_index_ = 0;
  uint64_t serial_ = 0;
};
