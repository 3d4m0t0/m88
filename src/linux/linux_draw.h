#pragma once

#include "draw.h"

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

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

private:
  void EnsureTexture();
  void BlitRegion(const Region& region);
  void InitDefaultPalette();

  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;

  uint width;
  uint height;
  int scale;
  int bpl;
  uint status;

  uint8* image;
  uint32* rgba;
  Palette palette[256];
  bool palette_dirty;
  bool cleaned;

  char ime_preedit[128];
  int ime_cursor;
  bool ime_active;
};
