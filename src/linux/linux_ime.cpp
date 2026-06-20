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

bool g_host_available = false;
bool g_user_enabled = true;
bool g_enabled = false;

bool EnvAllows() {
  const char* e = std::getenv("M88_IME_KANA");
  if (!e || !*e) {
    return true;
  }
  return e[0] != '0';
}

bool ReadCommandLine(const char* cmd, char* out, size_t out_sz) {
  if (!cmd || !out || out_sz == 0) {
    return false;
  }
  out[0] = '\0';
  FILE* fp = popen(cmd, "r");
  if (!fp) {
    return false;
  }
  if (!fgets(out, out_sz, fp)) {
    pclose(fp);
    return false;
  }
  pclose(fp);
  size_t n = std::strlen(out);
  while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r')) {
    out[--n] = '\0';
  }
  return out[0] != '\0';
}

bool ProbeEnvImeHints() {
  const char* qt_im = std::getenv("QT_IM_MODULE");
  if (qt_im && std::strcmp(qt_im, "none") == 0) {
    return false;
  }
  if (qt_im && qt_im[0] != '\0') {
    return true;
  }

  const char* xmod = std::getenv("XMODIFIERS");
  if (xmod && std::strstr(xmod, "@im=") != nullptr &&
      std::strcmp(xmod, "@im=none") != 0) {
    return true;
  }

  const char* gtk_im = std::getenv("GTK_IM_MODULE");
  if (gtk_im && gtk_im[0] != '\0' && std::strcmp(gtk_im, "none") != 0) {
    return true;
  }

  const char* ibus = std::getenv("IBUS_ADDRESS");
  if (ibus && ibus[0] != '\0') {
    return true;
  }

  return false;
}

// fcitx5 on Wayland/KDE often omits QT_IM_MODULE; query the daemon directly.
bool ProbeFcitx5Active() {
  char name[128];
  return ReadCommandLine("fcitx5-remote -n 2>/dev/null", name, sizeof(name));
}

bool ProbeFcitx4Active() {
  char name[128];
  return ReadCommandLine("fcitx-remote -n 2>/dev/null", name, sizeof(name));
}

bool ProbeHostImeAtStartup() {
  if (ProbeEnvImeHints()) {
    return true;
  }
  if (ProbeFcitx5Active()) {
    return true;
  }
  if (ProbeFcitx4Active()) {
    return true;
  }
  return false;
}

void RecomputeEnabled() {
  g_enabled = g_host_available && g_user_enabled && EnvAllows();
}

bool CommitText(const char* utf8, PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!utf8 || !*utf8 || !keyif) {
    return false;
  }
  std::vector<uint16_t> hw;
  if (!HalfKanaIme::CommitUtf8ToHalfKana(utf8, &hw)) {
    return false;
  }
  keyif->FlushGuestKeys();
  HalfKanaIme::InjectBeginSession(keyif, cfg);
  std::vector<HalfKanaIme::KeyStroke> strokes;
  HalfKanaIme::HalfKanaToKeyStrokes(keyif, hw, &strokes);
  if (strokes.empty()) {
    HalfKanaIme::InjectEndSession(keyif, cfg);
    return false;
  }
  HalfKanaIme::InjectEnqueue(strokes);
  return true;
}

}  // namespace

bool Enabled() { return g_enabled; }

bool HostAvailable() { return g_host_available; }

bool UserEnabled() { return g_user_enabled; }

void SetUserEnabled(bool enabled) {
  g_user_enabled = enabled;
  RecomputeEnabled();
}

void ProbeHostAvailability(bool qt_input_method) {
#ifdef M88_QT_FRONTEND
  g_host_available = qt_input_method && ProbeHostImeAtStartup();
#else
  (void)qt_input_method;
  g_host_available = ProbeHostImeAtStartup();
#endif
  RecomputeEnabled();
}

void InitHost() { RecomputeEnabled(); }

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
  if (g_enabled && keyif && !HalfKanaIme::SessionActive()) {
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
