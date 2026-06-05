#pragma once

#include "draw.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Palette;
class LinuxDraw : public Draw {
 public:
  LinuxDraw();
  ~LinuxDraw() override;

  bool Init(uint width, uint height, uint bpp) override;
  bool Cleanup() override;

  bool Lock(uint8** pimage, int* pbpl) override;
  bool Unlock() override;

  uint GetStatus() override;
  void Resize(uint width, uint height) override;
  void DrawScreen(const Region& region) override;
  void SetPalette(uint index, uint nents, const Palette* pal) override;
  void Flip() override;
  bool SetFlipMode(bool) override { return true; }

  bool InitWindow(const char* title, int scale);
  void Present();

  SDL_Window* GetSdlWindow() const { return window; }
  uint GetWidth() const { return width; }
  uint GetHeight() const { return height; }

  void SetImePreedit(const char* utf8, int cursor);
  void DrawImeOverlay();

  static bool SdlSetTexturePaletteAvailable();
  void LogVideoBackendOnce();

 private:
  void EnsureTexture();
  void BlitRegion(const Region& region);
  void InitDefaultPalette();
  void SyncTexturePalette();
  void RebuildRgbaLut();

  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;
  SDL_Palette* sdl_palette_;

  uint width;
  uint height;
  int scale;
  int bpl;
  uint status;

  uint8* image;
  uint32* rgba_fallback_;
  uint32 rgba_lut_[256];
  Palette palette[256];
  bool palette_dirty;
  bool use_index8_texture_;
  bool cleaned;
  bool needs_present_;

  char ime_preedit[128];
  int ime_cursor;
  bool ime_active;
};
