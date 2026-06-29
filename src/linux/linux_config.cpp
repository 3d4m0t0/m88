#include "headers.h"
#include "linux_config.h"
#include "linux_paths.h"
#include "path.h"

#include "display_scale.h"
#include "linux_sequencer.h"
#include "pc88_key_fixup.h"

#include "misc.h"
#include "pc88/config.h"
#include "pc88/pc88.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace PC8801;

bool g_keyfix_enabled = true;
bool g_screen_scale_auto = true;
int g_screen_scale = 2;
bool g_wayland_idle_inhibit = false;
bool g_ime_half_kana = true;
char g_ime_fcitx_method[64] = {};

char g_keyboard_log_source[64] = "(default)";
namespace {

// Must match VOLUME_BIAS and LOADVOLUMEENTRY defaults in src/win32/88config.cpp,
// and IDC_SOUND_RESETVOL / IDC_SOUND_RESETRHYTHM in src/win32/cfgpage.cpp.
constexpr int kVolumeBias = 100;

constexpr int kVolumeIniFM = kVolumeBias;
constexpr int kVolumeIniSSG = 97;
constexpr int kVolumeIniADPCM = kVolumeBias;
constexpr int kVolumeIniRhythm = kVolumeBias;
constexpr int kVolumeIniBD = kVolumeBias;
constexpr int kVolumeIniSD = kVolumeBias;
constexpr int kVolumeIniTOP = kVolumeBias;
constexpr int kVolumeIniHH = kVolumeBias;
constexpr int kVolumeIniTOM = kVolumeBias;
constexpr int kVolumeIniRIM = kVolumeBias;

constexpr char kIniSection[] = "M88p2 for Windows";

struct IniKeyFlags {
  bool flags = false;
  bool flag2 = false;
  bool cpuclock = false;
  bool speed = false;
  bool refreshtiming = false;
  bool basicmode = false;
  bool sound = false;
  bool switches = false;
  bool soundbuffer = false;
  bool mousesensibility = false;
  bool cpumode = false;
  bool erambank = false;
  bool opnclock = false;
  bool volumefm = false;
  bool volumessg = false;
  bool volumeadpcm = false;
  bool volumerhythm = false;
  bool volumebd = false;
  bool volumesd = false;
  bool volumetop = false;
  bool volumehh = false;
  bool volumetom = false;
  bool volumerim = false;
  bool keyfix = false;
  bool screen_scale = false;
  bool keyboardtype = false;
};

IniKeyFlags g_ini_keys;

void ResetIniKeyFlags() { g_ini_keys = {}; }

bool IniKeysComplete() {
  const IniKeyFlags& k = g_ini_keys;
  return k.flags && k.flag2 && k.cpuclock && k.speed && k.refreshtiming && k.basicmode &&
         k.sound && k.switches && k.soundbuffer && k.mousesensibility && k.cpumode &&
         k.erambank && k.opnclock && k.volumefm && k.volumessg && k.volumeadpcm &&
         k.volumerhythm && k.volumebd && k.volumesd && k.volumetop && k.volumehh &&
         k.volumetom && k.volumerim && k.keyfix && k.screen_scale;
}

void MergeMissingIniDefaults(Config* cfg, const char* path) {
  if (!cfg || !path || !*path || IniKeysComplete()) {
    return;
  }
  if (M88SaveConfigFile(cfg, path)) {
    std::fprintf(stderr, "M88: config updated with default keys: %s\n", path);
  }
}

void ApplyKeyFixEnabled() { Pc88KeyFixup::SetEnabled(g_keyfix_enabled); }

void ApplyDefaultVolumes(Config* cfg) {
  cfg->volfm = kVolumeIniFM - kVolumeBias;
  cfg->volssg = kVolumeIniSSG - kVolumeBias;
  cfg->voladpcm = kVolumeIniADPCM - kVolumeBias;
  cfg->volrhythm = kVolumeIniRhythm - kVolumeBias;
  cfg->volbd = kVolumeIniBD - kVolumeBias;
  cfg->volsd = kVolumeIniSD - kVolumeBias;
  cfg->voltop = kVolumeIniTOP - kVolumeBias;
  cfg->volhh = kVolumeIniHH - kVolumeBias;
  cfg->voltom = kVolumeIniTOM - kVolumeBias;
  cfg->volrim = kVolumeIniRIM - kVolumeBias;
}

char* TrimInPlace(char* s) {
  while (*s && std::isspace(static_cast<unsigned char>(*s))) {
    ++s;
  }
  if (!*s) {
    return s;
  }
  char* end = s + std::strlen(s) - 1;
  while (end > s && std::isspace(static_cast<unsigned char>(*end))) {
    *end-- = '\0';
  }
  return s;
}

bool ParseIniInt(const char* value, int* out) {
  if (!value || !*value) {
    return false;
  }
  char* end = nullptr;
  const long n = std::strtol(value, &end, 10);
  if (end == value) {
    return false;
  }
  *out = static_cast<int>(n);
  return true;
}

bool ParseKeyValueLine(const char* line, Config* cfg) {
  const char* eq = std::strchr(line, '=');
  if (!eq) {
    return false;
  }

  char key[64];
  const size_t keylen = static_cast<size_t>(eq - line);
  if (keylen >= sizeof(key)) {
    return false;
  }
  std::memcpy(key, line, keylen);
  key[keylen] = '\0';
  TrimInPlace(key);

  char value[256];
  std::strncpy(value, eq + 1, sizeof(value) - 1);
  value[sizeof(value) - 1] = '\0';
  TrimInPlace(value);

  int n = 0;
  if (std::strcmp(key, "KeyboardType") == 0 && ParseIniInt(value, &n)) {
    if (n == Config::AT106 || n == Config::AT101) {
      cfg->keytype = static_cast<Config::KeyType>(n);
    } else if (n == Config::PC98) {
      cfg->keytype = Config::AT106;
    }
    g_ini_keys.keyboardtype = true;
    std::strncpy(g_keyboard_log_source, "(ini)", sizeof(g_keyboard_log_source));
    g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
    return true;
  }
  if ((std::strcmp(key, "KeyFix") == 0 || std::strcmp(key, "HostKeyFix") == 0) &&
      ParseIniInt(value, &n)) {
    g_keyfix_enabled = (n != 0);
    g_ini_keys.keyfix = true;
    return true;
  }
  if (std::strcmp(key, "ScreenScale") == 0) {
    if (strcasecmp(value, "auto") == 0) {
      g_screen_scale_auto = true;
      g_ini_keys.screen_scale = true;
      return true;
    }
    if (ParseIniInt(value, &n) && n >= 1) {
      g_screen_scale_auto = false;
      g_screen_scale = n;
      g_ini_keys.screen_scale = true;
      return true;
    }
    return false;
  }
  if (std::strcmp(key, "UseArrowForTenKey") == 0 && ParseIniInt(value, &n)) {
    if (n) {
      cfg->flags |= Config::usearrowfor10;
    } else {
      cfg->flags &= ~Config::usearrowfor10;
    }
    return true;
  }
  if (std::strcmp(key, "WaylandIdleInhibit") == 0 && ParseIniInt(value, &n)) {
    g_wayland_idle_inhibit = (n != 0);
    return true;
  }
  if (std::strcmp(key, "ImeHalfKana") == 0 && ParseIniInt(value, &n)) {
    g_ime_half_kana = (n != 0);
    return true;
  }
  if (std::strcmp(key, "ImeFcitxMethod") == 0 && value) {
    std::strncpy(g_ime_fcitx_method, value, sizeof(g_ime_fcitx_method) - 1);
    g_ime_fcitx_method[sizeof(g_ime_fcitx_method) - 1] = '\0';
    return true;
  }
  if (std::strcmp(key, "Flags") == 0 && ParseIniInt(value, &n)) {
    cfg->flags = n;
    cfg->flags &= ~Config::specialpalette;
    g_ini_keys.flags = true;
    return true;
  }
  if (std::strcmp(key, "Flag2") == 0 && ParseIniInt(value, &n)) {
    cfg->flag2 = n;
    cfg->flag2 &= ~(Config::mask0 | Config::mask1 | Config::mask2);
    g_ini_keys.flag2 = true;
    return true;
  }
  if (std::strcmp(key, "Sound") == 0 && ParseIniInt(value, &n)) {
    static const uint16 srate[] = {0, 11025, 22050, 44100, 44100, 48000, 55467};
    if (n < 7) {
      cfg->sound = srate[n];
    } else {
      cfg->sound = Limit(n, 55467 * 2, 8000);
    }
    g_ini_keys.sound = true;
    return true;
  }
  if (std::strcmp(key, "SoundBuffer") == 0 && ParseIniInt(value, &n)) {
    cfg->soundbuffer = static_cast<uint>(Limit(n, 1000, 50));
    g_ini_keys.soundbuffer = true;
    return true;
  }
  if (std::strcmp(key, "AudioDevice") == 0) {
    std::strncpy(cfg->audiodevice, value ? value : "", sizeof(cfg->audiodevice) - 1);
    cfg->audiodevice[sizeof(cfg->audiodevice) - 1] = '\0';
    return true;
  }
  if (std::strcmp(key, "AudioBackend") == 0) {
    std::strncpy(cfg->audiobackend, value ? value : "", sizeof(cfg->audiobackend) - 1);
    cfg->audiobackend[sizeof(cfg->audiobackend) - 1] = '\0';
    return true;
  }
  if (std::strcmp(key, "CPUClock") == 0 && ParseIniInt(value, &n)) {
    cfg->clock = Limit(n, 1000, 1);
    g_ini_keys.cpuclock = true;
    return true;
  }
  if (std::strcmp(key, "Speed") == 0 && ParseIniInt(value, &n)) {
    cfg->speed = static_cast<int>(Limit(n, 2000, 500));
    g_ini_keys.speed = true;
    return true;
  }
  if (std::strcmp(key, "RefreshTiming") == 0 && ParseIniInt(value, &n)) {
    cfg->refreshtiming = static_cast<int>(Limit(n, 4, 1));
    g_ini_keys.refreshtiming = true;
    return true;
  }
  if (std::strcmp(key, "BASICMode") == 0 && ParseIniInt(value, &n)) {
    if (n == Config::N80 || n == Config::N88V1 || n == Config::N88V1H ||
        n == Config::N88V2 || n == Config::N802 || n == Config::N80V2 ||
        n == Config::N88V2CD) {
      cfg->basicmode = static_cast<Config::BASICMode>(n);
    }
    g_ini_keys.basicmode = true;
    return true;
  }
  if (std::strcmp(key, "Switches") == 0 && ParseIniInt(value, &n)) {
    cfg->dipsw = n;
    g_ini_keys.switches = true;
    return true;
  }
  if (std::strcmp(key, "ERAMBank") == 0 && ParseIniInt(value, &n)) {
    cfg->erambanks = static_cast<int>(Limit(n, 256, 0));
    g_ini_keys.erambank = true;
    return true;
  }
  if (std::strcmp(key, "CPUMode") == 0 && ParseIniInt(value, &n)) {
    cfg->cpumode = static_cast<int>(Limit(n, 2, 0));
    g_ini_keys.cpumode = true;
    return true;
  }
  if (std::strcmp(key, "MouseSensibility") == 0 && ParseIniInt(value, &n)) {
    cfg->mousesensibility = static_cast<uint>(Limit(n, 10, 1));
    g_ini_keys.mousesensibility = true;
    return true;
  }
  if (std::strcmp(key, "OPNClock") == 0 && ParseIniInt(value, &n)) {
    cfg->opnclock = static_cast<int>(Limit(n, 10000000, 1000000));
    g_ini_keys.opnclock = true;
    return true;
  }
  if (std::strcmp(key, "VolumeFM") == 0 && ParseIniInt(value, &n)) {
    cfg->volfm = n - kVolumeBias;
    g_ini_keys.volumefm = true;
    return true;
  }
  if (std::strcmp(key, "VolumeSSG") == 0 && ParseIniInt(value, &n)) {
    cfg->volssg = n - kVolumeBias;
    g_ini_keys.volumessg = true;
    return true;
  }
  if (std::strcmp(key, "VolumeADPCM") == 0 && ParseIniInt(value, &n)) {
    cfg->voladpcm = n - kVolumeBias;
    g_ini_keys.volumeadpcm = true;
    return true;
  }
  if (std::strcmp(key, "VolumeRhythm") == 0 && ParseIniInt(value, &n)) {
    cfg->volrhythm = n - kVolumeBias;
    g_ini_keys.volumerhythm = true;
    return true;
  }
  if (std::strcmp(key, "VolumeBD") == 0 && ParseIniInt(value, &n)) {
    cfg->volbd = n - kVolumeBias;
    g_ini_keys.volumebd = true;
    return true;
  }
  if (std::strcmp(key, "VolumeSD") == 0 && ParseIniInt(value, &n)) {
    cfg->volsd = n - kVolumeBias;
    g_ini_keys.volumesd = true;
    return true;
  }
  if (std::strcmp(key, "VolumeTOP") == 0 && ParseIniInt(value, &n)) {
    cfg->voltop = n - kVolumeBias;
    g_ini_keys.volumetop = true;
    return true;
  }
  if (std::strcmp(key, "VolumeHH") == 0 && ParseIniInt(value, &n)) {
    cfg->volhh = n - kVolumeBias;
    g_ini_keys.volumehh = true;
    return true;
  }
  if (std::strcmp(key, "VolumeTOM") == 0 && ParseIniInt(value, &n)) {
    cfg->voltom = n - kVolumeBias;
    g_ini_keys.volumetom = true;
    return true;
  }
  if (std::strcmp(key, "VolumeRIM") == 0 && ParseIniInt(value, &n)) {
    cfg->volrim = n - kVolumeBias;
    g_ini_keys.volumerim = true;
    return true;
  }
  if (std::strcmp(key, "WinPosX") == 0 && ParseIniInt(value, &n)) {
    cfg->winposx = n;
    return true;
  }
  if (std::strcmp(key, "WinPosY") == 0 && ParseIniInt(value, &n)) {
    cfg->winposy = n;
    return true;
  }
  return false;
}

bool ReadIniKeyString(const char* path, const char* key, char* out, size_t out_sz,
                      bool anywhere) {
  if (!path || !*path || !key || !*key || !out || out_sz == 0) {
    return false;
  }
  FILE* fp = std::fopen(path, "r");
  if (!fp) {
    return false;
  }

  const size_t keylen = std::strlen(key);
  bool in_section = false;
  bool found = false;
  char line[1024];
  while (std::fgets(line, sizeof(line), fp)) {
    char* p = TrimInPlace(line);
    if (!*p || *p == ';' || *p == '#') {
      continue;
    }
    if (*p == '[') {
      in_section = (std::strstr(p, kIniSection) != nullptr);
      continue;
    }
    if (!anywhere && !in_section) {
      continue;
    }
    if (std::strncmp(p, key, keylen) != 0 || p[keylen] != '=') {
      continue;
    }
    char* value = TrimInPlace(p + keylen + 1);
    if (*value == ';') {
      continue;
    }
    std::strncpy(out, value, out_sz - 1);
    out[out_sz - 1] = '\0';
    found = true;
    break;
  }
  std::fclose(fp);
  return found && out[0] != '\0';
}

bool ConfigFileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int SoundRateToIniValue(uint rate) {
  switch (rate) {
    case 11025:
      return 1;
    case 22050:
      return 2;
    case 44100:
      return 3;
    case 48000:
      return 5;
    case 55467:
      return 6;
    default:
      return static_cast<int>(rate);
  }
}

}  // namespace

void M88SetDefaultConfig(Config* cfg) {
  std::memset(cfg, 0, sizeof(Config));

  // Match Windows 88config.cpp default Flags (subcpucontrol ON).
  // FM $44h: OPNA (enableopna); FM $A8h: none (no opnona8/opnaona8).
  cfg->flags = Config::subcpucontrol | Config::savedirectory | Config::force480 |
               Config::enablewait | Config::enableopna | Config::precisemixing |
               Config::mixsoundalways;
  cfg->flags &= ~Config::specialpalette;

  cfg->flag2 = Config::genscrnshotname;
  cfg->flag2 &= ~(Config::mask0 | Config::mask1 | Config::mask2);

  cfg->clock = 40;
  cfg->speed = 1000;
  cfg->refreshtiming = 1;
  cfg->basicmode = Config::N88V2;

  cfg->sound = 55467;
  cfg->opnclock = 3993600;
  cfg->erambanks = 4;
  // Default host AT101; guest matrix follows host (see WinKeyIF::ApplyConfig).
  cfg->keytype = Config::AT101;
  cfg->dipsw = 1829;
  cfg->soundbuffer = 400;
  cfg->mousesensibility = 4;
  cfg->cpumode = Config::msauto;

  cfg->lpffc = 8000;
  cfg->lpforder = 4;
  cfg->romeolatency = 100;

  ApplyDefaultVolumes(cfg);

  cfg->winposx = 64;
  cfg->winposy = 64;

  g_keyfix_enabled = true;
  g_screen_scale_auto = true;
  g_screen_scale = 2;
  g_wayland_idle_inhibit = false;
  g_ime_half_kana = true;
}

void M88GetDefaultConfig(Config* cfg, bool* wayland_idle, bool* ime_kana) {
  if (!cfg) {
    return;
  }
  const bool save_keyfix = g_keyfix_enabled;
  const bool save_scale_auto = g_screen_scale_auto;
  const int save_scale = g_screen_scale;
  const bool save_wayland = g_wayland_idle_inhibit;
  const bool save_ime = g_ime_half_kana;
  M88SetDefaultConfig(cfg);
  if (wayland_idle) {
    *wayland_idle = g_wayland_idle_inhibit;
  }
  if (ime_kana) {
    *ime_kana = g_ime_half_kana;
  }
  g_keyfix_enabled = save_keyfix;
  g_screen_scale_auto = save_scale_auto;
  g_screen_scale = save_scale;
  g_wayland_idle_inhibit = save_wayland;
  g_ime_half_kana = save_ime;
}

int M88ParseKeyboardType(const char* name) {
  if (!name || !*name) {
    return -1;
  }

  if (std::strcmp(name, "101") == 0 || std::strcmp(name, "104") == 0 ||
      strcasecmp(name, "at101") == 0 || strcasecmp(name, "us") == 0) {
    return Config::AT101;
  }
  if (std::strcmp(name, "106") == 0 || strcasecmp(name, "at106") == 0 ||
      strcasecmp(name, "jp") == 0) {
    return Config::AT106;
  }
  if (std::strcmp(name, "98") == 0 || strcasecmp(name, "pc98") == 0) {
    return Config::PC98;
  }

  char* end = nullptr;
  const long n = std::strtol(name, &end, 10);
  if (end != name && *end == '\0' && n >= Config::AT106 && n <= Config::AT101) {
    return static_cast<int>(n);
  }
  return -1;
}

namespace {

bool RunShellCapture(const char* cmd, char* out, size_t out_sz) {
  if (!cmd || !out || out_sz == 0) {
    return false;
  }
  FILE* fp = popen(cmd, "r");
  if (!fp) {
    return false;
  }
  size_t n = 0;
  while (n + 1 < out_sz) {
    const size_t got = std::fread(out + n, 1, out_sz - 1 - n, fp);
    if (got == 0) {
      break;
    }
    n += got;
  }
  out[n] = '\0';
  pclose(fp);
  return n > 0;
}

bool LayoutTokenIsJapanese(const char* tok) {
  if (!tok || !*tok) {
    return false;
  }
  if (strncasecmp(tok, "jp", 2) == 0) {
    return true;
  }
  if (strcasecmp(tok, "jis") == 0) {
    return true;
  }
  if (strcasecmp(tok, "mac-jp") == 0) {
    return true;
  }
  if (std::strstr(tok, "OADG") != nullptr || std::strstr(tok, "109") != nullptr) {
    return true;
  }
  return false;
}

bool LayoutTokenIsPc98(const char* tok) {
  return tok && *tok && std::strstr(tok, "pc98") != nullptr;
}

bool KeyTypeFromLayoutToken(const char* tok, Config::KeyType* keytype) {
  if (!tok || !*tok || !keytype) {
    return false;
  }
  char layout[64];
  std::strncpy(layout, tok, sizeof(layout) - 1);
  layout[sizeof(layout) - 1] = '\0';
  char* comma = std::strchr(layout, ',');
  if (comma) {
    *comma = '\0';
  }
  TrimInPlace(layout);
  if (LayoutTokenIsPc98(layout)) {
    *keytype = Config::AT106;
    return true;
  }
  if (LayoutTokenIsJapanese(layout)) {
    *keytype = Config::AT106;
    return true;
  }
  *keytype = Config::AT101;
  return true;
}

bool KeyTypeFromXkbOutput(const char* text, Config::KeyType* keytype) {
  if (!text || !keytype) {
    return false;
  }
  for (const char* line = text; *line; line = std::strchr(line, '\n')) {
    if (*line == '\n') {
      ++line;
    }
    if (!*line) {
      break;
    }
    if (strncasecmp(line, "layout:", 7) == 0) {
      char tok[64];
      if (std::sscanf(line + 7, " %63s", tok) == 1) {
        return KeyTypeFromLayoutToken(tok, keytype);
      }
    }
  }
  return false;
}

bool KeyTypeFromLocalectlOutput(const char* text, Config::KeyType* keytype) {
  if (!text || !keytype) {
    return false;
  }
  for (const char* line = text; *line; line = std::strchr(line, '\n')) {
    if (*line == '\n') {
      ++line;
    }
    if (!*line) {
      break;
    }
    const char* colon = std::strchr(line, ':');
    if (!colon) {
      continue;
    }
    if (std::strstr(line, "Layout") == nullptr && std::strstr(line, "Keymap") == nullptr &&
        std::strstr(line, "layout") == nullptr && std::strstr(line, "keymap") == nullptr) {
      continue;
    }
    char tok[64];
    if (std::sscanf(colon + 1, " %63s", tok) == 1) {
      return KeyTypeFromLayoutToken(tok, keytype);
    }
  }
  return false;
}

bool LocaleIsJapanese() {
  static const char* kVars[] = {"LC_ALL", "LC_CTYPE", "LANG", nullptr};
  for (const char** name = kVars; *name; ++name) {
    const char* val = std::getenv(*name);
    if (!val || !*val) {
      continue;
    }
    if (std::strncmp(val, "ja", 2) == 0) {
      return true;
    }
    if (std::strstr(val, "_JP") != nullptr || std::strstr(val, ".JP") != nullptr) {
      return true;
    }
  }
  return false;
}

}  // namespace

bool M88KeyFixEnabled() { return g_keyfix_enabled; }

bool M88ScreenScaleAuto() { return g_screen_scale_auto; }

int M88ScreenScaleIniValue() { return std::max(1, g_screen_scale); }

bool M88WaylandIdleInhibitEnabled() { return g_wayland_idle_inhibit; }

void M88SetWaylandIdleInhibitEnabled(bool enabled) { g_wayland_idle_inhibit = enabled; }

bool M88ImeHalfKanaEnabled() { return g_ime_half_kana; }

void M88SetImeHalfKanaEnabled(bool enabled) { g_ime_half_kana = enabled; }

const char* M88ImeFcitxMethod() { return g_ime_fcitx_method; }

void M88SetImeFcitxMethod(const char* name) {
  if (!name) {
    g_ime_fcitx_method[0] = '\0';
    return;
  }
  std::strncpy(g_ime_fcitx_method, name, sizeof(g_ime_fcitx_method) - 1);
  g_ime_fcitx_method[sizeof(g_ime_fcitx_method) - 1] = '\0';
}

int M88ResolveScreenScale(int avail_w, int avail_h, int chrome_w, int chrome_h,
                          int cli_scale, bool cli_explicit) {
  if (cli_explicit) {
    return std::max(1, cli_scale);
  }
  if (!g_screen_scale_auto) {
    return M88ScreenScaleIniValue();
  }
  if (avail_w > 0 && avail_h > 0) {
    return M88AutoScreenScale(avail_w, avail_h, chrome_w, chrome_h);
  }
  return 2;
}

void M88PrintScreenScale(int scale, bool cli_explicit) {
  if (cli_explicit) {
    std::fprintf(stderr, "M88: ScreenScale: %d (command line)\n", scale);
  } else if (!g_screen_scale_auto) {
    std::fprintf(stderr, "M88: ScreenScale: %d (ini)\n", scale);
  } else {
    std::fprintf(stderr, "M88: ScreenScale: %d (auto)\n", scale);
  }
}

void M88LoadKeyFixup(const char* m88_ini_path, Config* cfg) {
  Pc88KeyFixup::SetDeferLogs(true);
  if (cfg) {
    Config::KeyType host = static_cast<Config::KeyType>(cfg->keytype);
    if (host == Config::PC98) {
      host = Config::AT106;
    }
    Pc88KeyFixup::SetHostKeyboard(host);
    Pc88KeyFixup::SetGuestKeyboard(host);
  }

  const char* keyfix_path = M88GetKeyfixIniPath();
  const char* env_keyfix = std::getenv("M88_KEYFIX");
  if (env_keyfix && *env_keyfix) {
    ApplyKeyFixEnabled();
    if (!g_keyfix_enabled) {
      Pc88KeyFixup::LogMessage("M88: keyfix: disabled (m88.ini KeyFix=0)\n");
      return;
    }
    if (!Pc88KeyFixup::LoadFromFile(env_keyfix)) {
      Pc88KeyFixup::LogMessage("M88: keyfix: failed to load M88_KEYFIX=%s\n", env_keyfix);
    }
    return;
  }

  struct stat keyfix_st {};
  const bool keyfix_exists =
      keyfix_path && *keyfix_path && stat(keyfix_path, &keyfix_st) == 0 &&
      S_ISREG(keyfix_st.st_mode);
  if (g_keyfix_enabled && !keyfix_exists) {
    if (Pc88KeyFixup::CreateDefaultIni(keyfix_path, m88_ini_path)) {
      Pc88KeyFixup::LogMessage("M88: keyfix: created default %s\n", keyfix_path);
      if (cfg && m88_ini_path && *m88_ini_path) {
        M88SaveConfigFile(cfg, m88_ini_path);
      }
    } else {
      Pc88KeyFixup::LogMessage("M88: keyfix: failed to create %s\n", keyfix_path);
    }
  }

  ApplyKeyFixEnabled();
  if (!g_keyfix_enabled) {
    Pc88KeyFixup::LogMessage("M88: keyfix: disabled (m88.ini KeyFix=0)\n");
    return;
  }
  if (!Pc88KeyFixup::LoadFromFile(keyfix_path)) {
    Pc88KeyFixup::LoadStartup(m88_ini_path && *m88_ini_path ? m88_ini_path : nullptr);
  }
}

const char* M88KeyboardTypeName(int keytype) {
  switch (keytype) {
    case Config::AT106:
      return "AT106 (JP 106-key host)";
    case Config::PC98:
      return "PC98 (legacy, treated as AT106 host)";
    case Config::AT101:
      return "AT101 (US/101-key host)";
    default:
      return "unknown";
  }
}

bool M88IniHasHostKeyboard() { return g_ini_keys.keyboardtype; }

void M88NoteKeyboardCliOverride() {
  std::strncpy(g_keyboard_log_source, "(--keyboard)", sizeof(g_keyboard_log_source));
  g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
}

void M88LogKeyboard(const Config* cfg) {
  if (!cfg) {
    return;
  }
  Config::KeyType guest = static_cast<Config::KeyType>(cfg->keytype);
  if (guest == Config::PC98) {
    guest = Config::AT106;
  }
  const char* matrix =
      (guest == Config::AT101) ? "AT101 (KeyTable101)" : "AT106 (KeyTable106)";
  std::fprintf(stderr, "M88: host keyboard=%s %s, guest matrix=%s\n",
               M88KeyboardTypeName(cfg->keytype), g_keyboard_log_source, matrix);
  if (g_keyfix_enabled && cfg->keytype == Config::AT101) {
    std::fprintf(stderr, "M88: keyfix active for US/101 shifted symbols\n");
  }
}

void M88LogKeyFix() { Pc88KeyFixup::FlushDeferredLogs(); }

void M88ApplyDetectedKeyboard(Config* cfg) {
  if (!cfg) {
    return;
  }

  const char* env = std::getenv("M88_KEYBOARD");
  if (env && *env) {
    const int k = M88ParseKeyboardType(env);
    if (k == Config::AT106 || k == Config::AT101) {
      cfg->keytype = static_cast<Config::KeyType>(k);
      std::strncpy(g_keyboard_log_source, "(M88_KEYBOARD)", sizeof(g_keyboard_log_source));
      g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
      return;
    }
  }

  char buf[512];
  Config::KeyType detected = Config::AT101;

  if (RunShellCapture("setxkbmap -query 2>/dev/null", buf, sizeof(buf)) &&
      KeyTypeFromXkbOutput(buf, &detected)) {
    cfg->keytype = detected;
    std::strncpy(g_keyboard_log_source, "(setxkbmap)", sizeof(g_keyboard_log_source));
    g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
    return;
  }

  if (RunShellCapture("localectl status 2>/dev/null", buf, sizeof(buf)) &&
      KeyTypeFromLocalectlOutput(buf, &detected)) {
    cfg->keytype = detected;
    std::strncpy(g_keyboard_log_source, "(localectl)", sizeof(g_keyboard_log_source));
    g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
    return;
  }

  if (LocaleIsJapanese()) {
    cfg->keytype = Config::AT106;
    std::strncpy(g_keyboard_log_source, "(locale)", sizeof(g_keyboard_log_source));
    g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
    return;
  }

  cfg->keytype = Config::AT101;
  std::strncpy(g_keyboard_log_source, "(default)", sizeof(g_keyboard_log_source));
  g_keyboard_log_source[sizeof(g_keyboard_log_source) - 1] = '\0';
}

bool M88LoadConfigFile(Config* cfg, const char* path) {
  if (!cfg || !path || !*path) {
    return false;
  }

  ResetIniKeyFlags();

  FILE* fp = std::fopen(path, "r");
  if (!fp) {
    return false;
  }

  bool in_section = false;
  char line[512];
  while (std::fgets(line, sizeof(line), fp)) {
    char* p = TrimInPlace(line);
    if (!*p || *p == ';' || *p == '#') {
      continue;
    }
    if (*p == '[') {
      char* end = std::strchr(p, ']');
      if (!end) {
        continue;
      }
      *end = '\0';
      in_section = (std::strcmp(p + 1, kIniSection) == 0);
      continue;
    }
    if (in_section) {
      ParseKeyValueLine(p, cfg);
    }
  }

  std::fclose(fp);
  return true;
}

void M88LoadDefaultConfigFile(Config* cfg) {
  char used[512];
  bool created = false;
  M88LoadStartupConfig(cfg, nullptr, used, sizeof(used), &created);
}

bool M88BasicModeFixesClock4MHz(int basicmode) {
  switch (basicmode) {
    case Config::N88V1:
    case Config::N88V1H:
    case Config::N80:
      return true;
    default:
      return false;
  }
}

int M88EffectiveClock(const Config* cfg) {
  if (!cfg) {
    return 40;
  }
  if (M88BasicModeFixesClock4MHz(static_cast<int>(cfg->basicmode))) {
    return 40;
  }
  return std::max(1, cfg->clock);
}

Config M88ConfigForHardware(const Config& cfg) {
  Config hw = cfg;
  hw.clock = M88EffectiveClock(&cfg);
  hw.mainsubratio =
      (hw.clock >= 60 || (hw.flags & Config::fullspeed)) ? 2 : 1;
  return hw;
}

void M88SeqApplyConfig(M88Sequencer& seq, const Config& cfg) {
  seq.ApplyConfig(M88ConfigForHardware(cfg));
}

const char* M88BasicModeName(int basicmode) {
  switch (basicmode) {
    case Config::N80:
      return "N";
    case Config::N802:
      return "N80";
    case Config::N80V2:
      return "N80-V2";
    case Config::N88V1:
      return "N88-V1";
    case Config::N88V1H:
      return "N88-V1H";
    case Config::N88V2:
      return "N88-V2";
    case Config::N88V2CD:
      return "N88-V2CD";
    default:
      return "unknown";
  }
}

void M88ApplyStartupDirectory(const Config* cfg, const char* ini_path,
                              bool skip_if_disk_on_cli) {
  if (!cfg || !ini_path || !*ini_path || skip_if_disk_on_cli) {
    return;
  }
  if ((cfg->flags & Config::savedirectory) == 0) {
    return;
  }

  char dir[1024];
  if (!ReadIniKeyString(ini_path, "Directory", dir, sizeof(dir), true)) {
    return;
  }
  if (dir[0] == ';' || dir[0] == '\0') {
    return;
  }
  if (chdir(dir) != 0) {
    std::fprintf(stderr, "M88: failed to change directory to %s\n", dir);
  }
}

bool M88SaveConfigFile(const Config* cfg, const char* path) {
  if (!cfg || !path || !*path) {
    return false;
  }

  FILE* fp = std::fopen(path, "w");
  if (!fp) {
    return false;
  }

  std::fprintf(fp, "[%s]\n", kIniSection);
  char cwd[1024];
  cwd[0] = '\0';
  if (getcwd(cwd, sizeof(cwd))) {
    std::fprintf(fp, "Directory=%s\n", cwd);
  }
  std::fprintf(fp, "Flags=%d\n", cfg->flags);
  std::fprintf(fp, "Flag2=%d\n", cfg->flag2);
  std::fprintf(fp, "CPUClock=%d\n", cfg->clock);
  std::fprintf(fp, "Speed=%d\n", cfg->speed);
  std::fprintf(fp, "RefreshTiming=%d\n", cfg->refreshtiming);
  std::fprintf(fp, "BASICMode=%d\n", cfg->basicmode);
  std::fprintf(fp, "Sound=%d\n", SoundRateToIniValue(cfg->sound));
  std::fprintf(fp, "Switches=%d\n", cfg->dipsw);
  std::fprintf(fp, "SoundBuffer=%u\n", cfg->soundbuffer);
  if (cfg->audiodevice[0] != '\0') {
    std::fprintf(fp, "AudioDevice=%s\n", cfg->audiodevice);
  }
  if (cfg->audiobackend[0] != '\0') {
    std::fprintf(fp, "AudioBackend=%s\n", cfg->audiobackend);
  }
  std::fprintf(fp, "MouseSensibility=%u\n", cfg->mousesensibility);
  std::fprintf(fp, "CPUMode=%d\n", cfg->cpumode);
  std::fprintf(fp, "ERAMBank=%d\n", cfg->erambanks);
  std::fprintf(fp, "OPNClock=%d\n", cfg->opnclock);
  std::fprintf(fp, "VolumeFM=%d\n", cfg->volfm + kVolumeBias);
  std::fprintf(fp, "VolumeSSG=%d\n", cfg->volssg + kVolumeBias);
  std::fprintf(fp, "VolumeADPCM=%d\n", cfg->voladpcm + kVolumeBias);
  std::fprintf(fp, "VolumeRhythm=%d\n", cfg->volrhythm + kVolumeBias);
  std::fprintf(fp, "VolumeBD=%d\n", cfg->volbd + kVolumeBias);
  std::fprintf(fp, "VolumeSD=%d\n", cfg->volsd + kVolumeBias);
  std::fprintf(fp, "VolumeTOP=%d\n", cfg->voltop + kVolumeBias);
  std::fprintf(fp, "VolumeHH=%d\n", cfg->volhh + kVolumeBias);
  std::fprintf(fp, "VolumeTOM=%d\n", cfg->voltom + kVolumeBias);
  std::fprintf(fp, "VolumeRIM=%d\n", cfg->volrim + kVolumeBias);
  std::fprintf(fp, "KeyboardType=%d\n", cfg->keytype);
  std::fprintf(fp, "KeyFix=%d\n", g_keyfix_enabled ? 1 : 0);
  std::fprintf(fp, "WaylandIdleInhibit=%d\n", g_wayland_idle_inhibit ? 1 : 0);
  std::fprintf(fp, "ImeHalfKana=%d\n", g_ime_half_kana ? 1 : 0);
  if (g_ime_fcitx_method[0] != '\0') {
    std::fprintf(fp, "ImeFcitxMethod=%s\n", g_ime_fcitx_method);
  }
  if (g_screen_scale_auto) {
    std::fprintf(fp, "ScreenScale=auto\n");
  } else {
    std::fprintf(fp, "ScreenScale=%d\n", M88ScreenScaleIniValue());
  }
  std::fprintf(fp, "WinPosX=%d\n", cfg->winposx);
  std::fprintf(fp, "WinPosY=%d\n", cfg->winposy);
  std::fclose(fp);
  return true;
}

bool M88LoadConfigAtPath(Config* cfg, const char* path) {
  if (!cfg || !path || !*path || !ConfigFileExists(path)) {
    return false;
  }
  M88SetDefaultConfig(cfg);
  M88LoadConfigFile(cfg, path);
  M88FinalizeConfig(cfg);
  MergeMissingIniDefaults(cfg, path);
  return true;
}

static bool LoadIniFromPath(Config* cfg, const char* path) {
  return M88LoadConfigAtPath(cfg, path);
}

void M88CanonicalConfigPath(const char* path, char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return;
  }
  out[0] = '\0';
  if (!path || !*path) {
    return;
  }

  char resolved[1024];
  if (realpath(path, resolved)) {
    std::strncpy(out, resolved, out_sz - 1);
    out[out_sz - 1] = '\0';
    return;
  }

  if (path[0] == '/') {
    std::strncpy(out, path, out_sz - 1);
    out[out_sz - 1] = '\0';
    return;
  }

  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd))) {
    std::snprintf(out, out_sz, "%s/%s", cwd, path);
    out[out_sz - 1] = '\0';
    return;
  }

  std::strncpy(out, path, out_sz - 1);
  out[out_sz - 1] = '\0';
}

void M88LoadStartupConfig(Config* cfg, const char* explicit_path, char* used_path,
                          size_t used_path_sz, bool* created_new_ini) {
  M88Paths paths {};
  if (!M88InitializePaths(&paths, cfg, explicit_path, used_path, used_path_sz,
                          created_new_ini)) {
    std::fprintf(stderr, "M88: failed to initialize data paths\n");
  }
}

void M88ApplyEnvOverrides(Config* cfg) {
  if (!cfg) {
    return;
  }
  const char* cc = std::getenv("M88_CPUCLOCK");
  if (cc && *cc) {
    char* end = nullptr;
    const long mhz = std::strtol(cc, &end, 10);
    if (end != cc && mhz > 0) {
      // M88 stores CPUClock in 0.1 MHz units (INI 90 = 9.0 MHz); env is real MHz.
      cfg->clock = static_cast<int>(Limit(mhz * 10, 1000, 10));
    }
  }
  const char* sp = std::getenv("M88_SPEED");
  if (sp && *sp) {
    char* end = nullptr;
    const long pct = std::strtol(sp, &end, 10);
    if (end != sp && pct > 0) {
      cfg->speed = static_cast<int>(Limit(pct * 10, 10000, 10));
    }
  }
}

void M88FinalizeConfig(Config* cfg) {
  if (!cfg) {
    return;
  }
  // SDL output is slightly quiet on ADPCM; boost unless VolumeADPCM came from ini.
  if (!g_ini_keys.volumeadpcm) {
    cfg->voladpcm = 3;
  }
  // Do not override Flags/volumes from a loaded M88.ini (match Windows behaviour).
}

void M88ApplyConfig(PC88* pc88, Config* cfg) {
  if (cfg->dipsw != 1) {
    cfg->flags &= ~Config::specialpalette;
    cfg->flag2 &= ~(Config::mask0 | Config::mask1 | Config::mask2);
  }
  if (!M88MouseInputAvailable()) {
    cfg->flags &= ~(Config::enablemouse | Config::mousejoymode);
  }

  Config hw = M88ConfigForHardware(*cfg);
  cfg->mainsubratio = hw.mainsubratio;
  pc88->ApplyConfig(&hw);
}
