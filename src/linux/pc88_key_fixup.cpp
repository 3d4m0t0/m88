#include "pc88_key_fixup.h"

#include "linux_config.h"

#include "../linux_compat/pc88_matrix_vk.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <vector>

namespace Pc88KeyFixup {
namespace {

struct Rule {
  PC8801::Config::KeyType host = PC8801::Config::AT101;
  uint8_t host_vk = 0;
  bool host_shift = false;
  uint8_t guest_vk = 0;
  bool mask_host_shift = false;
  bool guest_shift = false;
  bool swallow = false;
};

PC8801::Config::KeyType g_host = PC8801::Config::AT101;
std::vector<Rule> g_rules;
bool g_enabled = false;

char* Trim(char* s) {
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

bool ParseShiftToken(const char* tok, bool* shift) {
  if (!tok || !*tok || !shift) {
    return false;
  }
  if (std::strcmp(tok, "1") == 0 || strcasecmp(tok, "on") == 0 ||
      strcasecmp(tok, "true") == 0 || strcasecmp(tok, "yes") == 0) {
    *shift = true;
    return true;
  }
  if (std::strcmp(tok, "0") == 0 || strcasecmp(tok, "off") == 0 ||
      strcasecmp(tok, "false") == 0 || strcasecmp(tok, "no") == 0) {
    *shift = false;
    return true;
  }
  return false;
}

bool ParseMaskToken(const char* tok, bool* mask) {
  if (!tok || !*tok || !mask) {
    return false;
  }
  if (strcasecmp(tok, "mask") == 0 || strcasecmp(tok, "mask_shift") == 0 ||
      std::strcmp(tok, "1") == 0) {
    *mask = true;
    return true;
  }
  return false;
}

bool ParseVkToken(const char* tok, uint* vk) {
  if (!tok || !*tok || !vk) {
    return false;
  }

  if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) {
    char* end = nullptr;
    const unsigned long n = std::strtoul(tok + 2, &end, 16);
    if (end != tok + 2 && n <= 0xFF) {
      *vk = static_cast<uint>(n);
      return true;
    }
    return false;
  }

  if (std::strlen(tok) == 1) {
    *vk = static_cast<unsigned char>(tok[0]);
    return true;
  }

  char* end = nullptr;
  const unsigned long n = std::strtoul(tok, &end, 16);
  if (end != tok && *end == '\0' && n <= 0xFF) {
    *vk = static_cast<uint>(n);
    return true;
  }

  struct NamedVk {
    const char* name;
    uint vk;
  };
  static const NamedVk kNames[] = {
      {"OEM_1", VK_OEM_1},       {"OEM_2", VK_OEM_2},       {"OEM_3", VK_OEM_3},
      {"OEM_4", VK_OEM_4},       {"OEM_5", VK_OEM_5},       {"OEM_6", VK_OEM_6},
      {"OEM_7", VK_OEM_7},       {"OEM_102", VK_OEM_102},   {"OEM_PLUS", VK_OEM_PLUS},
      {"OEM_COMMA", VK_OEM_COMMA}, {"OEM_MINUS", VK_OEM_MINUS},
      {"OEM_PERIOD", VK_OEM_PERIOD}, {"MULTIPLY", VK_MULTIPLY},
      {"ADD", VK_ADD},
      {"88_R01_EQ", VK_PC88_R01_EQ},
      {"88_R01_8", VK_PC88_R01_8},
      {"88_R01_9", VK_PC88_R01_9},
      {"88_ROW07_8", '8'},
      {"88_ROW07_9", '9'},
      {"ROW07_8", '8'},
      {"ROW07_9", '9'},
      {"88_R01_MUL", VK_PC88_R01_MUL},
      {"88_R01_ADD", VK_PC88_R01_ADD},
      {"88_AT", VK_PC88_AT},
      {"88_CIRC", VK_PC88_CIRC},
      {"88_LBRA", VK_PC88_LBRA},
      {"88_RBRA", VK_PC88_RBRA},
      {"88_BSL", VK_PC88_BSL},
      {"88_COL", VK_PC88_COLON},
      {"88_SEM", VK_PC88_SEMICOLON},
      {"88_COMM", VK_PC88_COMMA},
      {"88_DOT", VK_PC88_PERIOD},
      {"88_SLASH", VK_PC88_SLASH},
      {"88_USCR", VK_PC88_UNDERSCORE},
      {"R01_EQ", VK_PC88_R01_EQ},
      {"R01_8", VK_PC88_R01_8},
      {"R01_9", VK_PC88_R01_9},
      {"R01_MUL", VK_PC88_R01_MUL},
      {"R01_ADD", VK_PC88_R01_ADD},
      // Legacy names: row01 tenkey only (not Shift -> parenthesis; use 88_ROW07_8/9).
      {"PC88_8", VK_PC88_R01_8},
      {"PC88_9", VK_PC88_R01_9},
      {"SPACE", VK_SPACE},
      {"RETURN", VK_RETURN},     {"TAB", VK_TAB},
  };
  for (const NamedVk& entry : kNames) {
    if (strcasecmp(tok, entry.name) == 0) {
      *vk = entry.vk;
      return true;
    }
  }
  return false;
}

bool ParseGuestVkToken(const char* tok, uint* vk, bool* swallow) {
  if (!tok || !*tok || !vk || !swallow) {
    return false;
  }
  if (strcasecmp(tok, "NONE") == 0) {
    *swallow = true;
    *vk = 0;
    return true;
  }
  *swallow = false;
  return ParseVkToken(tok, vk);
}

bool ParseGuestShiftToken(const char* tok, bool* guest_shift) {
  if (!tok || !*tok || !guest_shift) {
    return false;
  }
  if (strcasecmp(tok, "guest_shift") == 0 || strcasecmp(tok, "gshift") == 0) {
    *guest_shift = true;
    return true;
  }
  return false;
}

void AddRule(PC8801::Config::KeyType host, uint host_vk, bool host_shift, uint guest_vk,
             bool mask, bool guest_shift, bool swallow) {
  Rule r{};
  r.host = host;
  r.host_vk = static_cast<uint8_t>(host_vk);
  r.host_shift = host_shift;
  r.guest_vk = static_cast<uint8_t>(guest_vk);
  r.mask_host_shift = mask;
  r.guest_shift = guest_shift;
  r.swallow = swallow;
  g_rules.push_back(r);
}

bool ParseRuleLine(const char* line, PC8801::Config::KeyType section_host) {
  char buf[256];
  std::strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* tokens[8]{};
  int count = 0;
  char* ctx = nullptr;
  for (char* t = strtok_r(buf, " \t,", &ctx); t && count < 8;
       t = strtok_r(nullptr, " \t,", &ctx)) {
    if (t[0] == ';' || t[0] == '#') {
      break;
    }
    tokens[count++] = Trim(t);
  }
  if (count < 3) {
    return false;
  }

  bool shift = false;
  if (!ParseShiftToken(tokens[0], &shift)) {
    return false;
  }

  uint host_vk = 0;
  uint guest_vk = 0;
  bool swallow = false;
  if (!ParseVkToken(tokens[1], &host_vk) ||
      !ParseGuestVkToken(tokens[2], &guest_vk, &swallow)) {
    return false;
  }

  bool mask = false;
  bool guest_shift = false;
  for (int i = 3; i < count; ++i) {
    if (ParseMaskToken(tokens[i], &mask)) {
      continue;
    }
    if (ParseGuestShiftToken(tokens[i], &guest_shift)) {
      continue;
    }
  }

  if (guest_shift) {
    mask = true;
  }
  AddRule(section_host, host_vk, shift, guest_vk, mask, guest_shift, swallow);
  return true;
}

bool HostTypeFromSection(const char* section, PC8801::Config::KeyType* host) {
  if (!section || !*section || !host) {
    return false;
  }
  char name[64];
  std::strncpy(name, section, sizeof(name) - 1);
  name[sizeof(name) - 1] = '\0';
  Trim(name);

  char* colon = std::strchr(name, ':');
  if (colon) {
    *colon = '\0';
    Trim(colon + 1);
    if (strcasecmp(name, "host") != 0 && strcasecmp(name, "Host") != 0) {
      return false;
    }
    memmove(name, colon + 1, std::strlen(colon + 1) + 1);
    Trim(name);
  }

  const int k = M88ParseKeyboardType(name);
  if (k < PC8801::Config::AT106 || k > PC8801::Config::AT101) {
    return false;
  }
  *host = static_cast<PC8801::Config::KeyType>(k);
  return true;
}

}  // namespace

void SetEnabled(bool on) {
  g_enabled = on;
  if (!on) {
    g_rules.clear();
  }
}

bool IsEnabled() { return g_enabled; }

void SetHostKeyboard(PC8801::Config::KeyType host) { g_host = host; }

bool MapKey(PC8801::Config::KeyType host, uint vk, bool shift, KeyMap* out) {
  if (!g_enabled || !out || !vk) {
    return false;
  }
  out->vk = static_cast<uint8_t>(vk);
  out->shift = shift;
  out->mask_host_shift = false;
  out->guest_shift = false;
  out->swallow = false;

  const uint8_t host_vk = static_cast<uint8_t>(vk);
  for (const Rule& rule : g_rules) {
    if (rule.host != host || rule.host_vk != host_vk || rule.host_shift != shift) {
      continue;
    }
    out->vk = rule.guest_vk;
    out->shift = false;
    out->mask_host_shift = rule.mask_host_shift;
    out->guest_shift = rule.guest_shift;
    out->swallow = rule.swallow;
    return true;
  }
  return false;
}

bool MapKey(uint vk, bool shift, KeyMap* out) { return MapKey(g_host, vk, shift, out); }

namespace detail {

bool FileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool CopyFileIfExists(const char* src, const char* dest) {
  if (!src || !*src || !dest || !*dest || !FileExists(src)) {
    return false;
  }
  if (std::strcmp(src, dest) == 0) {
    return true;
  }

  FILE* in = std::fopen(src, "rb");
  if (!in) {
    return false;
  }
  FILE* out = std::fopen(dest, "wb");
  if (!out) {
    std::fclose(in);
    return false;
  }

  char buf[4096];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) {
    if (std::fwrite(buf, 1, n, out) != n) {
      std::fclose(in);
      std::fclose(out);
      return false;
    }
  }
  const bool ok = !std::ferror(in);
  std::fclose(in);
  std::fclose(out);
  return ok;
}

// Shipped with the repo; used when no template file is found on disk.
static const char kDefaultKeyfixIni[] =
    "; m88_keyfix.ini - US AT101 host -> PC-8801 (AT101 guest) key remaps\n"
    ";\n"
    "; m88.ini:  KeyFix=1   (KeyFix=0 disables)\n"
    "; Load: M88_KEYFIX, then beside m88.ini, then ./m88_keyfix.ini\n"
    ";\n"
    "; Rule:  shift host_vk guest_vk [mask] [guest_shift]   ; comment\n"
    ";\n"
    ";   shift        1 = host Shift held (Qt modifier or VK_LSHIFT in keyif)\n"
    ";   mask         clear host Shift from keystate before guest key\n"
    ";   guest_shift  inject PC-88 Shift for this stroke (row05/06/07 shifted symbols)\n"
    ";                guest_shift implies mask (avoid host+guest Shift both ON)\n"
    ";\n"
    "; Guest VK aliases (src/linux_compat/pc88_matrix_vk.h):\n"
    ";   88_R01_EQ   0x92        row01  =  (tenkey upper row)\n"
    ";   88_R01_8    VK_NUMPAD8  row01  8  (tenkey; not Shift -> parenthesis)\n"
    ";   88_R01_9    VK_NUMPAD9  row01  9  (tenkey)\n"
    ";   88_R01_MUL  VK_MULTIPLY row01  *\n"
    ";   88_R01_ADD  VK_ADD      row01  +\n"
    ";   6 / 7 / 2       row06  Shift+6=&  Shift+7='  Shift+2=\"\n"
    ";   8 / 9 / 88_ROW07_8 / 88_ROW07_9   row07  Shift+8=(  Shift+9=)\n"
    ";   88_AT       0xDB        row02  @\n"
    ";   88_CIRC     0xBB        row05  ^\n"
    ";   88_LBRA     0xDD        row05  [\n"
    ";   88_RBRA     0xC0        row05  ]\n"
    ";   88_BSL      0xDC        row05  backslash\n"
    ";   88_COL      0xBA        row07  :\n"
    ";   88_SEM      0xDE        row07  ;\n"
    ";   88_COMM     0xBC        row07  ,\n"
    ";   88_DOT      0xBE        row07  .\n"
    ";   88_SLASH    0xBF        row07  /   Shift -> ?\n"
    ";   88_USCR     0xE2        row07  _\n"
    ";   88_CIRC     0xBB        row05  ^   Shift -> ~  (US Shift+` only; unshifted ` -> NONE)\n"
    ";\n"
    "; Matrix: http://www.maroon.dti.ne.jp/youkan/pc88/kbd.html\n"
    "\n"
    "[101]\n"
    "\n"
    "; --- row01 (tenkey upper): = * + ---\n"
    "0 OEM_PLUS   88_R01_EQ\n"
    "1 8          88_R01_MUL mask\n"
    "1 OEM_PLUS   88_R01_ADD mask\n"
    "\n"
    "; --- row06/07: shifted digit row (PC-8801 matrix manual) ---\n"
    "1 7          6 guest_shift    ; US Shift+7  -> PC-88 Shift+6 -> &\n"
    "0 OEM_7      7 guest_shift    ; US '        -> PC-88 Shift+7 -> '\n"
    "1 OEM_7      2 guest_shift    ; US Shift+'  -> PC-88 Shift+2 -> \"\n"
    "1 9          8 guest_shift    ; US Shift+9  -> PC-88 Shift+row07-8 -> (\n"
    "1 0          9 guest_shift    ; US Shift+0  -> PC-88 Shift+row07-9 -> )\n"
    "\n"
    "; --- row05: [ ] { }  and  US ~ (no ` on PC-88) ---\n"
    "1 OEM_4      88_LBRA guest_shift    ; US Shift+[ -> PC-88 Shift+[ -> {\n"
    "1 OEM_6      88_RBRA guest_shift    ; US Shift+] -> PC-88 Shift+] -> }\n"
    "0 OEM_3      NONE mask              ; US `        -> (no PC-88 key; swallow)\n"
    "1 OEM_3      88_CIRC guest_shift    ; US Shift+`  -> PC-88 Shift+^ -> ~\n"
    "\n"
    "; --- row07: , . / and shifted < > ? ---\n"
    "1 OEM_COMMA  88_COMM guest_shift    ; US Shift+, -> PC-88 Shift+, -> <\n"
    "1 OEM_PERIOD 88_DOT guest_shift    ; US Shift+. -> PC-88 Shift+. -> >\n"
    "1 OEM_2      88_SLASH guest_shift   ; US Shift+/ -> PC-88 Shift+/ -> ?\n"
    "\n"
    "; --- Shift + symbol (guest VK carries the character; mask drops host Shift) ---\n"
    "1 2          88_AT mask\n"
    "1 6          88_CIRC mask\n"
    "1 OEM_MINUS  NONE mask\n"
    "1 OEM_1      88_COL mask\n"
    "1 OEM_5      88_BSL mask\n"
    "\n"
    "; --- unshifted punctuation ---\n"
    "0 OEM_1      88_SEM\n"
    "0 OEM_2      88_SLASH\n"
    "0 OEM_4      88_LBRA\n"
    "0 OEM_6      88_RBRA\n"
    "0 OEM_5      88_BSL\n"
    "0 OEM_COMMA  88_COMM\n"
    "0 OEM_PERIOD 88_DOT\n";

bool WriteEmbeddedDefault(const char* dest) {
  if (!dest || !*dest) {
    return false;
  }
  FILE* out = std::fopen(dest, "wb");
  if (!out) {
    return false;
  }
  const size_t len = std::strlen(kDefaultKeyfixIni);
  const bool ok = std::fwrite(kDefaultKeyfixIni, 1, len, out) == len && !std::ferror(out);
  std::fclose(out);
  return ok;
}

bool TryCopyTemplate(const char* dest, const char* m88_ini_path) {
  const char* env = std::getenv("M88_KEYFIX_TEMPLATE");
  if (env && *env && CopyFileIfExists(env, dest)) {
    return true;
  }

  if (m88_ini_path && *m88_ini_path) {
    char dir_path[512];
    std::strncpy(dir_path, m88_ini_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char* slash = std::strrchr(dir_path, '/');
    if (slash) {
      *slash = '\0';
      char template_path[512];
      static const char* kRel[] = {"../m88_keyfix.ini", "../../m88_keyfix.ini",
                                   "m88_keyfix.ini"};
      for (const char* rel : kRel) {
        std::snprintf(template_path, sizeof(template_path), "%s/%s", dir_path, rel);
        if (CopyFileIfExists(template_path, dest)) {
          return true;
        }
      }
    }
  }

  static const char* kCwd[] = {"../m88_keyfix.ini", "m88_keyfix.ini", "M88_keyfix.ini"};
  for (const char* name : kCwd) {
    if (CopyFileIfExists(name, dest)) {
      return true;
    }
  }
  return false;
}

}  // namespace detail

bool DefaultIniExists(const char* m88_ini_path) {
  if (m88_ini_path && *m88_ini_path) {
    char dir_path[512];
    std::strncpy(dir_path, m88_ini_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char* slash = std::strrchr(dir_path, '/');
    if (slash) {
      *slash = '\0';
      char fix_path[512];
      std::snprintf(fix_path, sizeof(fix_path), "%s/m88_keyfix.ini", dir_path);
      if (detail::FileExists(fix_path)) {
        return true;
      }
    }
  }

  static const char* kLocal[] = {"m88_keyfix.ini", "M88_keyfix.ini"};
  for (const char* name : kLocal) {
    if (detail::FileExists(name)) {
      return true;
    }
  }
  return false;
}

bool ResolveDefaultIniPath(const char* m88_ini_path, char* out, size_t out_sz) {
  if (!out || out_sz == 0) {
    return false;
  }
  if (m88_ini_path && *m88_ini_path) {
    char dir_path[512];
    std::strncpy(dir_path, m88_ini_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char* slash = std::strrchr(dir_path, '/');
    if (slash) {
      *slash = '\0';
      std::snprintf(out, out_sz, "%s/m88_keyfix.ini", dir_path);
      return true;
    }
  }
  std::snprintf(out, out_sz, "m88_keyfix.ini");
  return true;
}

bool CreateDefaultIni(const char* dest_path, const char* m88_ini_path) {
  if (!dest_path || !*dest_path) {
    return false;
  }
  if (detail::TryCopyTemplate(dest_path, m88_ini_path)) {
    return true;
  }
  return detail::WriteEmbeddedDefault(dest_path);
}

bool LoadFromFile(const char* path) {
  if (!path || !*path) {
    return false;
  }

  FILE* fp = std::fopen(path, "r");
  if (!fp) {
    return false;
  }

  g_rules.clear();
  PC8801::Config::KeyType section = PC8801::Config::AT101;
  bool have_section = false;
  int line_no = 0;
  char line[512];

  while (std::fgets(line, sizeof(line), fp)) {
    ++line_no;
    char* p = Trim(line);
    if (!*p || *p == ';' || *p == '#') {
      continue;
    }
    if (*p == '[') {
      char* end = std::strchr(p, ']');
      if (!end) {
        continue;
      }
      *end = '\0';
      if (!HostTypeFromSection(p + 1, &section)) {
        std::fprintf(stderr, "M88: keyfix: ignore section [%s] (%s:%d)\n", p + 1, path,
                     line_no);
        have_section = false;
        continue;
      }
      have_section = true;
      continue;
    }
    if (!have_section) {
      continue;
    }
    if (!ParseRuleLine(p, section)) {
      std::fprintf(stderr, "M88: keyfix: ignore line (%s:%d): %s\n", path, line_no, p);
    }
  }

  std::fclose(fp);
  std::fprintf(stderr, "M88: keyfix: loaded %zu rules from %s\n", g_rules.size(), path);
  return true;
}

bool LoadStartup(const char* m88_ini_path) {
  if (!g_enabled) {
    g_rules.clear();
    return false;
  }

  const char* env = std::getenv("M88_KEYFIX");
  if (env && *env) {
    if (LoadFromFile(env)) {
      return true;
    }
    std::fprintf(stderr, "M88: keyfix: failed to load M88_KEYFIX=%s\n", env);
  }

  if (m88_ini_path && *m88_ini_path) {
    char dir_path[512];
    std::strncpy(dir_path, m88_ini_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char* slash = std::strrchr(dir_path, '/');
    if (slash) {
      *slash = '\0';
      char fix_path[512];
      std::snprintf(fix_path, sizeof(fix_path), "%s/m88_keyfix.ini", dir_path);
      if (LoadFromFile(fix_path)) {
        return true;
      }
    }
  }

  static const char* kLocal[] = {"m88_keyfix.ini", "M88_keyfix.ini"};
  for (const char* name : kLocal) {
    if (LoadFromFile(name)) {
      return true;
    }
  }

  g_rules.clear();
  std::fprintf(stderr,
               "M88: keyfix: enabled but no m88_keyfix.ini found (no remaps applied)\n");
  return false;
}

}  // namespace Pc88KeyFixup
