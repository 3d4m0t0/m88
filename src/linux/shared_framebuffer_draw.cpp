#include "shared_framebuffer_draw.h"

#include <algorithm>
#include <cstring>

SharedFramebufferDraw::SharedFramebufferDraw() {
  std::memset(palette_, 0, sizeof(palette_));
  std::memset(ui_palette_, 0, sizeof(ui_palette_));
  ime_preedit_[0] = '\0';
}

SharedFramebufferDraw::~SharedFramebufferDraw() { Cleanup(); }

void SharedFramebufferDraw::EnsureUiBuffers() {
  const size_t bytes = static_cast<size_t>(width_) * height_;
  for (int i = 0; i < 2; ++i) {
    if (ui_image_[i].size() != bytes) {
      ui_image_[i].assign(bytes, 0x40);
    }
  }
}

bool SharedFramebufferDraw::Init(uint w, uint h, uint /*bpp*/) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  width_ = w;
  height_ = h;
  bpl_ = static_cast<int>(w);
  image_.assign(static_cast<size_t>(w) * h, 0x40);
  status_ = Draw::readytodraw | Draw::shouldrefresh;
  palette_dirty_ = true;
  frame_ready_ = true;
  ui_read_index_ = 0;
  ui_has_frame_ = false;
  ui_ready_ = false;
  EnsureUiBuffers();
  InitDefaultPalette();
  return true;
}

bool SharedFramebufferDraw::Cleanup() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  image_.clear();
  ui_image_[0].clear();
  ui_image_[1].clear();
  ui_ready_ = false;
  ui_has_frame_ = false;
  width_ = height_ = 0;
  bpl_ = 0;
  return true;
}

bool SharedFramebufferDraw::Lock(uint8** pimage, int* pbpl) {
  if (!pimage || !pbpl) {
    return false;
  }
  mutex_.lock();
  if (image_.empty()) {
    mutex_.unlock();
    return false;
  }
  *pimage = image_.data();
  *pbpl = bpl_;
  return true;
}

bool SharedFramebufferDraw::Unlock() {
  status_ |= Draw::shouldrefresh;
  frame_ready_ = true;
  mutex_.unlock();
  return true;
}

uint SharedFramebufferDraw::GetStatus() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return Draw::readytodraw | (status_ & Draw::shouldrefresh);
}

void SharedFramebufferDraw::Resize(uint w, uint h) {
  Init(w, h, 8);
}

void SharedFramebufferDraw::DrawScreen(const Region& region) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (region.top <= region.bottom) {
    last_region_ = region;
  }
  status_ &= ~Draw::shouldrefresh;
}

void SharedFramebufferDraw::SetPalette(uint index, uint nents, const Palette* pal) {
  if (!pal) {
    return;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  for (uint i = 0; i < nents && index + i < 256; ++i) {
    palette_[index + i] = pal[i];
  }
  palette_dirty_ = true;
  status_ |= Draw::shouldrefresh;
  frame_ready_ = true;
}

void SharedFramebufferDraw::InitDefaultPalette() {
  static const Palette kTextColors[8] = {
      {0, 0, 0},       {0, 0, 255},     {255, 0, 0},     {255, 0, 255},
      {0, 255, 0},     {0, 255, 255},   {255, 255, 0},   {255, 255, 255},
  };
  palette_[0x40] = {0, 0, 0};
  for (int i = 1; i < 8; ++i) {
    palette_[0x40 + i] = kTextColors[i];
  }
  for (int base = 0x48; base < 0xd0; base += 8) {
    for (int i = 0; i < 8; ++i) {
      palette_[base + i] = palette_[0x40 + i];
    }
  }
}

bool SharedFramebufferDraw::StageUiFrame() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (image_.empty() || !frame_ready_) {
    return false;
  }

  EnsureUiBuffers();
  const int write_index = 1 - ui_read_index_;
  std::vector<uint8>& dst = ui_image_[write_index];

  const bool partial = ui_has_frame_ && !palette_dirty_ &&
                       last_region_.top <= last_region_.bottom;
  if (partial) {
    std::memcpy(dst.data(), ui_image_[ui_read_index_].data(), dst.size());
    const int left = std::max(0, last_region_.left);
    const int top = std::max(0, last_region_.top);
    const int right = std::min(static_cast<int>(width_), last_region_.right + 1);
    const int bottom =
        std::min(static_cast<int>(height_), last_region_.bottom + 1);
    for (int y = top; y < bottom; ++y) {
      std::memcpy(dst.data() + static_cast<size_t>(y) * bpl_ + left,
                  image_.data() + static_cast<size_t>(y) * bpl_ + left,
                  static_cast<size_t>(right - left));
    }
  } else {
    std::memcpy(dst.data(), image_.data(), image_.size());
  }

  ui_region_ = last_region_;
  ui_palette_dirty_ = palette_dirty_;
  std::memcpy(ui_palette_, palette_, sizeof(palette_));

  ui_read_index_ = write_index;
  ui_has_frame_ = true;
  frame_ready_ = false;
  ui_ready_ = true;
  return true;
}

bool SharedFramebufferDraw::AcquireUiFrame(const uint8** out_data, int* out_bpl,
                                             uint* width, uint* height,
                                             bool* palette_changed,
                                             Palette* palette_out,
                                             uint palette_capacity) {
  if (!out_data || !out_bpl || !width || !height || !palette_changed ||
      !palette_out || palette_capacity < 256) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ui_ready_ || ui_image_[ui_read_index_].empty()) {
    return false;
  }

  *out_data = ui_image_[ui_read_index_].data();
  *out_bpl = bpl_;
  *width = width_;
  *height = height_;
  *palette_changed = ui_palette_dirty_;
  std::memcpy(palette_out, ui_palette_, sizeof(ui_palette_));
  ui_palette_dirty_ = false;
  ui_ready_ = false;
  return true;
}

void SharedFramebufferDraw::SetImePreedit(const char* utf8) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!utf8) {
    ime_preedit_[0] = '\0';
    return;
  }
  std::strncpy(ime_preedit_, utf8, sizeof(ime_preedit_) - 1);
  ime_preedit_[sizeof(ime_preedit_) - 1] = '\0';
}
