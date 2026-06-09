#pragma once

#include "draw.h"

#include <cstdint>
#include <mutex>
#include <vector>

// Thread-safe 8bpp framebuffer for Draw::Lock/Unlock (no platform blit).
class SharedFramebufferDraw : public Draw {
 public:
  SharedFramebufferDraw();
  ~SharedFramebufferDraw() override;

  bool Init(uint width, uint height, uint bpp) override;
  bool Cleanup() override;

  bool Lock(uint8** pimage, int* pbpl) override;
  bool Unlock() override;

  uint GetStatus() override;
  void Resize(uint width, uint height) override;
  void DrawScreen(const Region& region) override;
  void SetPalette(uint index, uint nents, const Palette* pal) override;
  bool SetFlipMode(bool) override { return true; }

  // Drop staged UI state so the next StageUiFrame copies the full emulator buffer.
  void InvalidateUiStaging();

  // Emulator thread: snapshot live framebuffer into the ping-pong UI buffer.
  bool StageUiFrame();

  // GUI thread: acquire the displayed buffer (no pixel copy).
  bool AcquireUiFrame(const uint8** out_data, int* out_bpl, uint* width, uint* height,
                      bool* palette_changed, Palette* palette_out, uint palette_capacity,
                      Draw::Region* out_region = nullptr);

  void SetImePreedit(const char* utf8);
  const char* GetImePreedit() const { return ime_preedit_; }
  bool ImeRepaintPending() const;
  bool ConsumeImeRepaint();
  uint64_t UiFrameSerial() const;
  uint64_t UiPaletteSerial() const;

 private:
  void InitDefaultPalette();
  void EnsureUiBuffers();

  // recursive: UpdateScreen holds Lock() while Screen::UpdatePalette calls SetPalette().
  mutable std::recursive_mutex mutex_;
  std::vector<uint8> image_;
  Palette palette_[256];
  uint width_ = 0;
  uint height_ = 0;
  int bpl_ = 0;
  uint status_ = 0;
  bool palette_dirty_ = true;
  bool frame_ready_ = false;
  Draw::Region last_region_{};
  char ime_preedit_[128];

  std::vector<uint8> ui_image_[2];
  Palette ui_palette_[256];
  Draw::Region ui_region_{};
  bool ui_palette_dirty_ = true;
  bool ui_has_frame_ = false;
  int ui_read_index_ = 0;
  bool ui_ime_dirty_ = false;
  uint64_t ui_frame_serial_ = 0;
  uint64_t ui_palette_serial_ = 0;
};
