#include "linux_ime.h"

#include "half_kana_ime.h"
#include "linux_config.h"
#include "../win32/WinKeyIF.h"

#ifndef M88_QT_FRONTEND
#include <SDL.h>
#include "linux_draw.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

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

enum class FcitxKind { None, Fcitx5, Fcitx4 };

struct FcitxSnapshot {
  bool valid = false;
  FcitxKind kind = FcitxKind::None;
  int state = 1;
  char im_name[64] = {};
};

FcitxSnapshot g_fcitx_snapshot;

bool RunShell(const char* cmd) {
  if (!cmd || !*cmd) {
    return false;
  }
  const int rc = std::system(cmd);
  return rc != -1;
}

bool Fcitx5Running() {
  return RunShell("fcitx5-remote --check >/dev/null 2>&1");
}

bool Fcitx4Running() {
  return RunShell("fcitx-remote -n >/dev/null 2>&1");
}

FcitxKind ActiveFcitxKind() {
  if (Fcitx5Running()) {
    return FcitxKind::Fcitx5;
  }
  if (Fcitx4Running()) {
    return FcitxKind::Fcitx4;
  }
  return FcitxKind::None;
}

int FcitxState(FcitxKind kind) {
  char buf[16] = {};
  if (kind == FcitxKind::Fcitx5) {
    if (!ReadCommandLine("fcitx5-remote 2>/dev/null", buf, sizeof(buf))) {
      return -1;
    }
  } else if (kind == FcitxKind::Fcitx4) {
    if (!ReadCommandLine("fcitx-remote 2>/dev/null", buf, sizeof(buf))) {
      return -1;
    }
  } else {
    return -1;
  }
  return std::atoi(buf);
}

bool FcitxCurrentIm(FcitxKind kind, char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  if (kind == FcitxKind::Fcitx5) {
    return ReadCommandLine("fcitx5-remote -n 2>/dev/null", out, out_sz);
  }
  if (kind == FcitxKind::Fcitx4) {
    return ReadCommandLine("fcitx-remote -n 2>/dev/null", out, out_sz);
  }
  return false;
}

void FcitxActivate(FcitxKind kind) {
  if (kind == FcitxKind::Fcitx5) {
    RunShell("fcitx5-remote -o >/dev/null 2>&1");
  } else if (kind == FcitxKind::Fcitx4) {
    RunShell("fcitx-remote -o >/dev/null 2>&1");
  }
}

void FcitxDeactivate(FcitxKind kind) {
  if (kind == FcitxKind::Fcitx5) {
    RunShell("fcitx5-remote -c >/dev/null 2>&1");
  } else if (kind == FcitxKind::Fcitx4) {
    RunShell("fcitx-remote -c >/dev/null 2>&1");
  }
}

void FcitxSetIm(FcitxKind kind, const char* im_name) {
  if (!im_name || !*im_name) {
    return;
  }
  char cmd[160];
  if (kind == FcitxKind::Fcitx5) {
    std::snprintf(cmd, sizeof(cmd), "fcitx5-remote -s '%s' >/dev/null 2>&1", im_name);
  } else if (kind == FcitxKind::Fcitx4) {
    std::snprintf(cmd, sizeof(cmd), "fcitx-remote -s '%s' >/dev/null 2>&1", im_name);
  } else {
    return;
  }
  RunShell(cmd);
}

bool IsKeyboardImName(const char* name) {
  return name && std::strncmp(name, "keyboard-", 9) == 0;
}

bool IsJapaneseImName(const char* name) {
  if (!name || !*name || IsKeyboardImName(name)) {
    return false;
  }
  static const char* kKnown[] = {"mozc", "anthy", "kkc", "skk", "wanaku"};
  for (const char* known : kKnown) {
    if (std::strcmp(name, known) == 0) {
      return true;
    }
  }
  return false;
}

void TrimLine(char* s) {
  if (!s) {
    return;
  }
  char* start = s;
  while (*start == ' ' || *start == '\t') {
    ++start;
  }
  if (start != s) {
    std::memmove(s, start, std::strlen(start) + 1);
  }
  size_t n = std::strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ' ||
                   s[n - 1] == '\t')) {
    s[--n] = '\0';
  }
}

bool ProfilePath(char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    std::snprintf(out, out_sz, "%s/fcitx5/profile", xdg);
    return true;
  }
  const char* home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return false;
  }
  std::snprintf(out, out_sz, "%s/.config/fcitx5/profile", home);
  return true;
}

bool ResolveJapaneseImFromProfile(char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  char path[512];
  if (!ProfilePath(path, sizeof(path))) {
    return false;
  }
  FILE* fp = std::fopen(path, "r");
  if (!fp) {
    return false;
  }

  char line[256];
  char default_im[64] = {};
  char item_im[64] = {};
  bool in_items = false;
  while (std::fgets(line, sizeof(line), fp)) {
    TrimLine(line);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (std::strncmp(line, "DefaultIM=", 10) == 0) {
      std::strncpy(default_im, line + 10, sizeof(default_im) - 1);
      continue;
    }
    if (std::strncmp(line, "[Groups/", 8) == 0 && std::strstr(line, "/Items/")) {
      in_items = true;
      item_im[0] = '\0';
      continue;
    }
    if (line[0] == '[') {
      in_items = false;
      continue;
    }
    if (in_items && std::strncmp(line, "Name=", 5) == 0) {
      std::strncpy(item_im, line + 5, sizeof(item_im) - 1);
      if (IsJapaneseImName(item_im)) {
        std::strncpy(out, item_im, out_sz - 1);
        out[out_sz - 1] = '\0';
        std::fclose(fp);
        return true;
      }
    }
  }
  std::fclose(fp);

  if (IsJapaneseImName(default_im)) {
    std::strncpy(out, default_im, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
  }
  return false;
}

bool ResolveJapaneseIm(char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  const char* configured = M88ImeFcitxMethod();
  if (configured && configured[0] != '\0') {
    std::strncpy(out, configured, out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
  }
  if (ResolveJapaneseImFromProfile(out, out_sz)) {
    return true;
  }
  std::strncpy(out, "mozc", out_sz - 1);
  out[out_sz - 1] = '\0';
  return true;
}

bool ResolveKeyboardImFromProfile(char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  char path[512];
  if (!ProfilePath(path, sizeof(path))) {
    std::strncpy(out, "keyboard-us", out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
  }
  FILE* fp = std::fopen(path, "r");
  if (!fp) {
    std::strncpy(out, "keyboard-us", out_sz - 1);
    out[out_sz - 1] = '\0';
    return true;
  }

  char line[256];
  char item_im[64] = {};
  bool in_items = false;
  while (std::fgets(line, sizeof(line), fp)) {
    TrimLine(line);
    if (line[0] == '\0' || line[0] == '#') {
      continue;
    }
    if (std::strncmp(line, "[Groups/", 8) == 0 && std::strstr(line, "/Items/")) {
      in_items = true;
      item_im[0] = '\0';
      continue;
    }
    if (line[0] == '[') {
      in_items = false;
      continue;
    }
    if (in_items && std::strncmp(line, "Name=", 5) == 0) {
      std::strncpy(item_im, line + 5, sizeof(item_im) - 1);
      if (IsKeyboardImName(item_im)) {
        std::strncpy(out, item_im, out_sz - 1);
        out[out_sz - 1] = '\0';
        std::fclose(fp);
        return true;
      }
    }
  }
  std::fclose(fp);
  std::strncpy(out, "keyboard-us", out_sz - 1);
  out[out_sz - 1] = '\0';
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

void SyncHostImeSession(bool active) {
  const FcitxKind kind = ActiveFcitxKind();
  if (kind == FcitxKind::None) {
    return;
  }

  if (active) {
    g_fcitx_snapshot.valid = true;
    g_fcitx_snapshot.kind = kind;
    g_fcitx_snapshot.state = FcitxState(kind);
    g_fcitx_snapshot.im_name[0] = '\0';
    FcitxCurrentIm(kind, g_fcitx_snapshot.im_name, sizeof(g_fcitx_snapshot.im_name));

    FcitxActivate(kind);
    char japanese_im[64] = {};
    if (ResolveJapaneseIm(japanese_im, sizeof(japanese_im))) {
      FcitxSetIm(kind, japanese_im);
    }
    return;
  }

  // Leaving m88 kana IME: deactivate first (drop mozc composition), then keyboard IM.
  FcitxDeactivate(kind);
  char keyboard_im[64] = {};
  ResolveKeyboardImFromProfile(keyboard_im, sizeof(keyboard_im));
  if (keyboard_im[0] != '\0') {
    FcitxSetIm(kind, keyboard_im);
  }
  g_fcitx_snapshot.valid = false;
}

}  // namespace LinuxIme
