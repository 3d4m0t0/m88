#include "shared_rgba_framebuffer.h"

#include "../linux/shared_framebuffer_draw.h"

#include <cstring>

namespace {

void Indexed8ToRgba(const uint8* indices, int bpl, unsigned wid, unsigned hei,
                    const Draw::Palette* palette, unsigned palette_capacity,
                    std::vector<unsigned char>* rgba) {
  if (!indices || !palette || !rgba || wid == 0 || hei == 0) {
    return;
  }
  rgba->resize(static_cast<size_t>(wid) * hei * 4);
  for (unsigned y = 0; y < hei; ++y) {
    const uint8* src_row = indices + static_cast<size_t>(y) * bpl;
    unsigned char* dst_row = rgba->data() + static_cast<size_t>(y) * wid * 4;
    for (unsigned x = 0; x < wid; ++x) {
      const unsigned idx = src_row[x];
      const Draw::Palette& p =
          idx < palette_capacity ? palette[idx] : palette[0];
      dst_row[x * 4 + 0] = p.red;
      dst_row[x * 4 + 1] = p.green;
      dst_row[x * 4 + 2] = p.blue;
      dst_row[x * 4 + 3] = 255;
    }
  }
}

}  // namespace

void SharedRgbaFramebuffer::SetPresentCallback(std::function<void()> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  present_callback_ = std::move(callback);
}

void SharedRgbaFramebuffer::NotifyPresent() {
  std::function<void()> callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = present_callback_;
  }
  if (callback) {
    callback();
  }
}

void SharedRgbaFramebuffer::StageFromIndexed8(const uint8* indices, int bpl, unsigned wid,
                                              unsigned hei, const Draw::Palette* palette,
                                              unsigned palette_capacity) {
  if (!indices || !palette || wid == 0 || hei == 0 || bpl <= 0) {
    return;
  }

  std::vector<unsigned char> rgba;
  Indexed8ToRgba(indices, bpl, wid, hei, palette, palette_capacity, &rgba);
  if (rgba.empty()) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    const int write_index = 1 - read_index_;
    buffers_[write_index] = std::move(rgba);
    wid_ = wid;
    hei_ = hei;
    read_index_ = write_index;
    ++serial_;
  }
  NotifyPresent();
}

void SharedRgbaFramebuffer::StageFromDraw(SharedFramebufferDraw* draw) {
  if (!draw) {
    return;
  }
  const uint8* data = nullptr;
  int bpl = 0;
  uint width = 0;
  uint height = 0;
  Draw::Palette palette[256]{};
  if (!draw->PeekUiFrame(&data, &bpl, &width, &height, palette, 256)) {
    return;
  }
  StageFromIndexed8(data, bpl, width, height, palette, 256);
}

bool SharedRgbaFramebuffer::Acquire(const unsigned char** rgba, unsigned int* wid,
                                    unsigned int* hei, uint64_t* serial) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (wid_ == 0 || hei_ == 0 || buffers_[read_index_].empty()) {
    return false;
  }
  if (rgba) {
    *rgba = buffers_[read_index_].data();
  }
  if (wid) {
    *wid = wid_;
  }
  if (hei) {
    *hei = hei_;
  }
  if (serial) {
    *serial = serial_;
  }
  return true;
}

uint64_t SharedRgbaFramebuffer::Serial() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return serial_;
}
