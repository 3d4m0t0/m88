#include "linux_ime.h"

#include "half_kana_ime.h"
#include "../win32/WinKeyIF.h"

#ifndef M88_QT_FRONTEND
#include <SDL.h>
#include "linux_draw.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace LinuxIme {
namespace {

bool g_enabled = false;

bool EnvEnabled() {
  const char* e = std::getenv("M88_IME_KANA");
  if (!e || !*e) {
    return true;  // default on; set M88_IME_KANA=0 to disable
  }
  return e[0] != '0';
}

bool CommitText(const char* utf8, PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!utf8 || !*utf8 || !keyif) {
    return false;
  }
  std::vector<uint16_t> hw;
  if (!HalfKanaIme::CommitUtf8ToHalfKana(utf8, &hw)) {
    std::fprintf(stderr, "M88: IME commit skipped (not kana): %s\n", utf8);
    return false;
  }
  std::vector<HalfKanaIme::KeyStroke> strokes;
  HalfKanaIme::HalfKanaToKeyStrokes(hw, &strokes);
  if (strokes.empty()) {
    return false;
  }
  keyif->FlushGuestKeys();
  HalfKanaIme::InjectBeginSession(keyif, cfg);
  HalfKanaIme::InjectEnqueue(strokes);
  std::fprintf(stderr, "M88: IME commit queued (%zu strokes): %s\n", strokes.size(), utf8);
  return true;
}

}  // namespace

bool Enabled() {
  return g_enabled;
}

void InitHost() { g_enabled = EnvEnabled(); }

#ifndef M88_QT_FRONTEND
void OnWindowShown(LinuxDraw* draw) {
  InitHost();
  if (!g_enabled || !draw || !draw->GetSdlWindow()) {
    return;
  }
  SDL_StartTextInput();
  SDL_Rect r{0, static_cast<int>(draw->GetHeight()) - 32, static_cast<int>(draw->GetWidth()), 32};
  SDL_SetTextInputRect(&r);
  draw->SetImePreedit("", 0);
}

void OnWindowHidden() {
  if (g_enabled) {
    SDL_StopTextInput();
  }
}

int HandleSdlEvent(unsigned int type, const void* sdl_event, LinuxDraw* draw,
                   PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!g_enabled || !draw || !keyif || !sdl_event) {
    return 0;
  }
  const SDL_Event& ev = *static_cast<const SDL_Event*>(sdl_event);

  switch (type) {
    case SDL_TEXTEDITING:
      draw->SetImePreedit(ev.edit.text, ev.edit.start);
      if (draw->GetSdlWindow()) {
        char title[256];
        std::snprintf(title, sizeof(title), "M88  [%s]", ev.edit.text);
        SDL_SetWindowTitle(draw->GetSdlWindow(), title);
      }
      return 1;

    case SDL_TEXTINPUT:
      draw->SetImePreedit("", 0);
      if (draw->GetSdlWindow()) {
        SDL_SetWindowTitle(draw->GetSdlWindow(), "M88");
      }
      if (CommitText(ev.text.text, keyif, cfg)) {
        return 2;
      }
      return 1;

    default:
      break;
  }
  return 0;
}
#else
void OnWindowShown(LinuxDraw* /*draw*/) { InitHost(); }

void OnWindowHidden() {}

int HandleSdlEvent(unsigned int /*type*/, const void* /*sdl_event*/, LinuxDraw* /*draw*/,
                   PC8801::WinKeyIF* /*keyif*/, const PC8801::Config* /*cfg*/) {
  return 0;
}
#endif

void Pump(PC8801::WinKeyIF* keyif) {
  if (g_enabled && keyif) {
    HalfKanaIme::InjectPump(keyif);
  }
}

bool CommitUtf8(const char* utf8, PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (g_enabled && keyif) {
    return CommitText(utf8, keyif, cfg);
  }
  return false;
}

}  // namespace LinuxIme
