#include "linux_draw.h"

#include "linux_startup_log.h"

#include <SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace {

using SetTexturePaletteFn = int (*)(SDL_Texture*, SDL_Palette*);

SetTexturePaletteFn ResolveSetTexturePalette() {
#if defined(__linux__)
  void* sym = dlsym(RTLD_DEFAULT, "SDL_SetTexturePalette");
  return sym ? reinterpret_cast<SetTexturePaletteFn>(sym) : nullptr;
#else
  return nullptr;
#endif
}

}  // namespace

bool LinuxDraw::SdlSetTexturePaletteAvailable() {
  return ResolveSetTexturePalette() != nullptr;
}

void LinuxDraw::LogVideoBackendOnce() {
  static bool logged = false;
  if (logged) {
    return;
  }
  logged = true;
  EnsureTexture();
  if (use_index8_texture_) {
    M88LogSdlVideoIndexed8();
  } else {
    M88LogSdlVideoArgb();
  }
}

LinuxDraw::LinuxDraw()
    : window(nullptr),
      renderer(nullptr),
      texture(nullptr),
      sdl_palette_(nullptr),
      width(0),
      height(0),
      scale(2),
      bpl(0),
      status(0),
      image(nullptr),
      rgba_fallback_(nullptr),
      palette_dirty(true),
      use_index8_texture_(true),
      cleaned(false),
      needs_present_(true),
      ime_cursor(0),
      ime_active(false) {
  std::memset(palette, 0, sizeof(palette));
  std::memset(rgba_lut_, 0, sizeof(rgba_lut_));
  ime_preedit[0] = '\0';
}

LinuxDraw::~LinuxDraw() { Cleanup(); }

bool LinuxDraw::InitWindow(const char* title, int window_scale) {
  scale = std::max(1, window_scale);
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
    return false;
  }

  const int w = width > 0 ? static_cast<int>(width) : 640;
  const int h = height > 0 ? static_cast<int>(height) : 400;
  window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            w * scale, h * scale,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    return false;
  }
  SDL_ShowWindow(window);
  SDL_RaiseWindow(window);

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer) {
    return false;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
  SDL_RenderSetLogicalSize(renderer, static_cast<int>(width), static_cast<int>(height));
  return true;
}

bool LinuxDraw::Init(uint w, uint h, uint /*bpp*/) {
  cleaned = false;
  width = w;
  height = h;
  bpl = static_cast<int>(width);

  delete[] image;
  delete[] rgba_fallback_;
  image = new uint8[width * height];
  rgba_fallback_ = new uint32[width * height];
  std::memset(image, 0x40, width * height);

  status = Draw::readytodraw | Draw::shouldrefresh;
  palette_dirty = true;
  InitDefaultPalette();
  return image != nullptr && rgba_fallback_ != nullptr;
}

bool LinuxDraw::Cleanup() {
  if (cleaned) {
    return true;
  }
  cleaned = true;

  const bool had_sdl = window != nullptr || renderer != nullptr;

  if (sdl_palette_) {
    SDL_FreePalette(sdl_palette_);
    sdl_palette_ = nullptr;
  }
  if (texture) {
    SDL_DestroyTexture(texture);
    texture = nullptr;
  }
  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    window = nullptr;
  }
  if (had_sdl) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
  }

  delete[] image;
  delete[] rgba_fallback_;
  image = nullptr;
  rgba_fallback_ = nullptr;
  return true;
}

bool LinuxDraw::Lock(uint8** pimage, int* pbpl) {
  if (!image) {
    return false;
  }
  *pimage = image;
  *pbpl = bpl;
  return true;
}

bool LinuxDraw::Unlock() {
  status |= Draw::shouldrefresh;
  return true;
}

uint LinuxDraw::GetStatus() {
  return Draw::readytodraw | (status & Draw::shouldrefresh);
}

void LinuxDraw::Resize(uint w, uint h) {
  if (w == width && h == height) {
    return;
  }
  Init(w, h, 8);
  if (texture) {
    SDL_DestroyTexture(texture);
    texture = nullptr;
  }
  if (renderer) {
    SDL_RenderSetLogicalSize(renderer, static_cast<int>(width), static_cast<int>(height));
  }
}

void LinuxDraw::SetPalette(uint index, uint nents, const Palette* pal) {
  if (!pal) {
    return;
  }
  for (uint i = 0; i < nents && index + i < 256; ++i) {
    palette[index + i] = pal[i];
  }
  palette_dirty = true;
  RebuildRgbaLut();
  status |= Draw::shouldrefresh;
}

void LinuxDraw::RebuildRgbaLut() {
  for (int i = 0; i < 256; ++i) {
    const Palette& p = palette[i];
    rgba_lut_[i] = 0xFF000000u | (static_cast<uint32>(p.red) << 16) |
                   (static_cast<uint32>(p.green) << 8) |
                   static_cast<uint32>(p.blue);
  }
}

void LinuxDraw::InitDefaultPalette() {
  static const Palette kTextColors[8] = {
      {0, 0, 0},       {0, 0, 255},     {255, 0, 0},     {255, 0, 255},
      {0, 255, 0},     {0, 255, 255},   {255, 255, 0},   {255, 255, 255},
  };
  palette[0x40] = {0, 0, 0};
  for (int i = 1; i < 8; ++i) {
    palette[0x40 + i] = kTextColors[i];
  }
  for (int base = 0x48; base < 0xd0; base += 8) {
    for (int i = 0; i < 8; ++i) {
      palette[base + i] = palette[0x40 + i];
    }
  }
  RebuildRgbaLut();
}

void LinuxDraw::EnsureTexture() {
  if (texture || !renderer) {
    return;
  }

  if (use_index8_texture_ && ResolveSetTexturePalette()) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_INDEX8,
                                SDL_TEXTUREACCESS_STREAMING,
                                static_cast<int>(width), static_cast<int>(height));
    if (!texture) {
      use_index8_texture_ = false;
    }
  } else {
    use_index8_texture_ = false;
  }

  if (!texture) {
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING,
                                static_cast<int>(width), static_cast<int>(height));
  }

  if (use_index8_texture_ && texture && !sdl_palette_) {
    sdl_palette_ = SDL_AllocPalette(256);
    SyncTexturePalette();
    ResolveSetTexturePalette()(texture, sdl_palette_);
  }
}

void LinuxDraw::SyncTexturePalette() {
  if (!sdl_palette_) {
    return;
  }
  SDL_Color colors[256];
  for (int i = 0; i < 256; ++i) {
    colors[i].r = palette[i].red;
    colors[i].g = palette[i].green;
    colors[i].b = palette[i].blue;
    colors[i].a = 255;
  }
  SDL_SetPaletteColors(sdl_palette_, colors, 0, 256);
  if (texture && use_index8_texture_) {
    if (const SetTexturePaletteFn set_palette = ResolveSetTexturePalette()) {
      set_palette(texture, sdl_palette_);
    }
  }
}

void LinuxDraw::BlitRegion(const Region& region) {
  if (!renderer || !image) {
    return;
  }

  const bool region_valid = region.top <= region.bottom;
  if (!palette_dirty && !region_valid) {
    return;
  }

  int left = 0;
  int top = 0;
  int right = static_cast<int>(width);
  int bottom = static_cast<int>(height);
  if (!palette_dirty && region_valid) {
    left = std::max(0, region.left);
    top = std::max(0, region.top);
    right = std::min(static_cast<int>(width), region.right + 1);
    bottom = std::min(static_cast<int>(height), region.bottom + 1);
    if (left >= right || top >= bottom) {
      return;
    }
  }

  EnsureTexture();
  if (!texture) {
    return;
  }

  if (palette_dirty && use_index8_texture_) {
    SyncTexturePalette();
  }

  const SDL_Rect tex_rect{left, top, right - left, bottom - top};
  if (use_index8_texture_) {
    SDL_UpdateTexture(texture, &tex_rect,
                      image + static_cast<size_t>(top) * bpl + left, bpl);
  } else if (rgba_fallback_) {
    for (int y = top; y < bottom; ++y) {
      const uint8* src_row = image + static_cast<size_t>(y) * bpl;
      uint32* dst_row = rgba_fallback_ + static_cast<size_t>(y) * width;
      for (int x = left; x < right; ++x) {
        dst_row[x] = rgba_lut_[src_row[x]];
      }
    }
    SDL_UpdateTexture(texture, &tex_rect,
                      rgba_fallback_ + static_cast<size_t>(top) * width + left,
                      static_cast<int>(width) * static_cast<int>(sizeof(uint32)));
  }

  needs_present_ = true;
  palette_dirty = false;
}

void LinuxDraw::DrawScreen(const Region& region) {
  BlitRegion(region);
  status &= ~Draw::shouldrefresh;
}

void LinuxDraw::Flip() { Present(); }

void LinuxDraw::SetImePreedit(const char* utf8, int cursor) {
  if (!utf8) {
    if (ime_active) {
      needs_present_ = true;
    }
    ime_preedit[0] = '\0';
    ime_active = false;
    ime_cursor = 0;
    return;
  }
  std::strncpy(ime_preedit, utf8, sizeof(ime_preedit) - 1);
  ime_preedit[sizeof(ime_preedit) - 1] = '\0';
  ime_cursor = cursor;
  const bool was_active = ime_active;
  ime_active = ime_preedit[0] != '\0';
  if (ime_active || was_active) {
    needs_present_ = true;
  }
  status |= Draw::shouldrefresh;
}

void LinuxDraw::DrawImeOverlay() {
  if (!renderer || !ime_active) {
    return;
  }
  constexpr int bar_h = 28;
  const int y = static_cast<int>(height) - bar_h;
  SDL_Rect bar{0, y, static_cast<int>(width), bar_h};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 20, 24, 40, 220);
  SDL_RenderFillRect(renderer, &bar);
  SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255);
  SDL_RenderDrawRect(renderer, &bar);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

void LinuxDraw::Present() {
  if (renderer && image && (palette_dirty || (status & Draw::shouldrefresh))) {
    Region full;
    full.left = 0;
    full.top = 0;
    full.right = width > 0 ? static_cast<int>(width) - 1 : 0;
    full.bottom = height > 0 ? static_cast<int>(height) - 1 : 0;
    BlitRegion(full);
  }
  if (!renderer || !needs_present_) {
    return;
  }
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  DrawImeOverlay();
  SDL_RenderPresent(renderer);
  needs_present_ = ime_active;
}
