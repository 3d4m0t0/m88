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
  EnsureUiBuffers();
  InitDefaultPalette();
  return true;
}

bool SharedFramebufferDraw::Cleanup() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  image_.clear();
  ui_image_[0].clear();
  ui_image_[1].clear();
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
  ++ui_palette_serial_;
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

void SharedFramebufferDraw::InvalidateUiStaging() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  ui_has_frame_ = false;
  palette_dirty_ = true;
  frame_ready_ = true;
  last_region_ = {};
  ui_region_ = {};
  status_ |= Draw::shouldrefresh;
}

bool SharedFramebufferDraw::StageUiFrame() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (image_.empty() || !frame_ready_) {
    return false;
  }

  EnsureUiBuffers();
  const int write_index = 1 - ui_read_index_;
  std::vector<uint8>& dst = ui_image_[write_index];

  // Always copy the full emulator buffer. Partial updates read the displayed
  // ping-pong slot while the GUI may still paint it (QImage held the pointer).
  std::memcpy(dst.data(), image_.data(), image_.size());

  ui_region_ = last_region_;
  ui_palette_dirty_ = palette_dirty_;
  std::memcpy(ui_palette_, palette_, sizeof(palette_));

  ui_read_index_ = write_index;
  ui_has_frame_ = true;
  frame_ready_ = false;
  ++ui_frame_serial_;
  return true;
}

uint64_t SharedFramebufferDraw::UiFrameSerial() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return ui_frame_serial_;
}

uint64_t SharedFramebufferDraw::UiPaletteSerial() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return ui_palette_serial_;
}

bool SharedFramebufferDraw::ImeRepaintPending() const {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  return ui_ime_dirty_;
}

bool SharedFramebufferDraw::ConsumeImeRepaint() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ui_ime_dirty_) {
    return false;
  }
  ui_ime_dirty_ = false;
  return true;
}

bool SharedFramebufferDraw::AcquireUiFrame(const uint8** out_data, int* out_bpl,
                                             uint* width, uint* height,
                                             bool* palette_changed,
                                             Palette* palette_out,
                                             uint palette_capacity,
                                             Draw::Region* out_region) {
  if (!out_data || !out_bpl || !width || !height || !palette_changed ||
      !palette_out || palette_capacity < 256) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ui_has_frame_ || ui_image_[ui_read_index_].empty()) {
    return false;
  }

  *out_data = ui_image_[ui_read_index_].data();
  *out_bpl = bpl_;
  *width = width_;
  *height = height_;
  *palette_changed = ui_palette_dirty_;
  std::memcpy(palette_out, ui_palette_, sizeof(ui_palette_));
  if (out_region) {
    *out_region = ui_region_;
  }
  ui_palette_dirty_ = false;
  return true;
}

bool SharedFramebufferDraw::PeekUiFrame(const uint8** out_data, int* out_bpl, uint* width,
                                        uint* height, Palette* palette_out,
                                        uint palette_capacity) const {
  if (!out_data || !out_bpl || !width || !height || !palette_out ||
      palette_capacity < 256) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!ui_has_frame_ || ui_image_[ui_read_index_].empty()) {
    return false;
  }
  *out_data = ui_image_[ui_read_index_].data();
  *out_bpl = bpl_;
  *width = width_;
  *height = height_;
  std::memcpy(palette_out, ui_palette_, sizeof(ui_palette_));
  return true;
}

void SharedFramebufferDraw::SetImePreedit(const char* utf8) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (!utf8) {
    ime_preedit_[0] = '\0';
    ui_ime_dirty_ = true;
    return;
  }
  std::strncpy(ime_preedit_, utf8, sizeof(ime_preedit_) - 1);
  ime_preedit_[sizeof(ime_preedit_) - 1] = '\0';
  ui_ime_dirty_ = true;
}
