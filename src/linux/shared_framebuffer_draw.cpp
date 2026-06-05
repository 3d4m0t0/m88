#include "shared_framebuffer_draw.h"

#include <cstring>

SharedFramebufferDraw::SharedFramebufferDraw() {
  std::memset(palette_, 0, sizeof(palette_));
  ime_preedit_[0] = '\0';
}

SharedFramebufferDraw::~SharedFramebufferDraw() { Cleanup(); }

bool SharedFramebufferDraw::Init(uint w, uint h, uint /*bpp*/) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  width_ = w;
  height_ = h;
  bpl_ = static_cast<int>(w);
  image_.assign(static_cast<size_t>(w) * h, 0x40);
  status_ = Draw::readytodraw | Draw::shouldrefresh;
  palette_dirty_ = true;
  frame_ready_ = true;
  InitDefaultPalette();
  return true;
}

bool SharedFramebufferDraw::Cleanup() {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  image_.clear();
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

void SharedFramebufferDraw::DrawScreen(const Region& /*region*/) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
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

bool SharedFramebufferDraw::CopyFrame(std::vector<uint8>* indices,
                                      std::vector<Palette>* palette,
                                      uint* width, uint* height,
                                      bool* palette_changed) {
  if (!indices || !palette || !width || !height || !palette_changed) {
    return false;
  }
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (image_.empty() || !frame_ready_) {
    return false;
  }
  if (indices->size() != image_.size()) {
    indices->resize(image_.size());
  }
  std::memcpy(indices->data(), image_.data(), image_.size());
  palette->assign(palette_, palette_ + 256);
  *width = width_;
  *height = height_;
  *palette_changed = palette_dirty_;
  palette_dirty_ = false;
  frame_ready_ = false;
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
