#include "half_kana_ime.h"

#include "../linux_compat/winkeys.h"
#include "../win32/WinKeyIF.h"
#include "pc88/config.h"

#include <deque>

namespace HalfKanaIme {
namespace {

struct VkStroke {
  uint8_t vk = 0;
  uint32_t keydata = 0;
};

bool LookupVk(uint16_t hw, VkStroke* out) {
  if (!out) {
    return false;
  }
  out->keydata = 0;
  switch (hw) {
    case 0xFF67:
      out->vk = '2';
      return true;
    case 0xFF68:
      out->vk = '3';
      return true;
    case 0xFF69:
      out->vk = '4';
      return true;
    case 0xFF6A:
      out->vk = '5';
      return true;
    case 0xFF6B:
      out->vk = '6';
      return true;
    case 0xFF66:
      out->vk = '1';
      return true;
    case 0xFF71:
      out->vk = 'A';
      return true;
    case 0xFF72:
      out->vk = 'S';
      return true;
    case 0xFF73:
      out->vk = 'D';
      return true;
    case 0xFF74:
      out->vk = 'F';
      return true;
    case 0xFF75:
      out->vk = 'G';
      return true;
    case 0xFF76:
      out->vk = 'H';
      return true;
    case 0xFF77:
      out->vk = 'J';
      return true;
    case 0xFF78:
      out->vk = 'K';
      return true;
    case 0xFF79:
      out->vk = 'L';
      return true;
    case 0xFF7A:
      out->vk = 0xbb;
      return true;
    case 0xFF7B:
      out->vk = 'Z';
      return true;
    case 0xFF7C:
      out->vk = 'X';
      return true;
    case 0xFF7D:
      out->vk = 'C';
      return true;
    case 0xFF7E:
      out->vk = 'V';
      return true;
    case 0xFF7F:
      out->vk = 'B';
      return true;
    case 0xFF80:
      out->vk = 'N';
      return true;
    case 0xFF81:
      out->vk = 'M';
      return true;
    case 0xFF82:
      out->vk = 0xbc;
      return true;
    case 0xFF83:
      out->vk = 0xbe;
      return true;
    case 0xFF84:
      out->vk = 0xbf;
      return true;
    case 0xFF85:
      out->vk = 'T';
      return true;
    case 0xFF86:
      out->vk = 'Y';
      return true;
    case 0xFF87:
      out->vk = 'U';
      return true;
    case 0xFF88:
      out->vk = 'I';
      return true;
    case 0xFF89:
      out->vk = 'O';
      return true;
    case 0xFF8A:
      out->vk = 0xc0;
      return true;
    case 0xFF8B:
      out->vk = 0xdb;
      return true;
    case 0xFF8C:
      out->vk = 0xdc;
      return true;
    case 0xFF8D:
      out->vk = 0xdd;
      return true;
    case 0xFF8E:
      out->vk = 0xde;
      return true;
    case 0xFF8F:
      out->vk = 'P';
      return true;
    case 0xFF90:
      out->vk = 0xba;
      return true;
    case 0xFF91:
      out->vk = 0xbd;
      return true;
    case 0xFF92:
      out->vk = 'Q';
      return true;
    case 0xFF93:
      out->vk = 'R';
      return true;
    case 0xFF94:
      out->vk = 'W';
      return true;
    case 0xFF95:
      out->vk = 'E';
      return true;
    case 0xFF96:
      out->vk = 'Y';
      return true;
    case 0xFF97:
      out->vk = 'U';
      return true;
    case 0xFF98:
      out->vk = 'I';
      return true;
    case 0xFF99:
      out->vk = 'O';
      return true;
    case 0xFF9A:
      out->vk = 'P';
      return true;
    case 0xFF9B:
      out->vk = static_cast<uint8_t>('A');
      return true;
    case 0xFF9C:
      out->vk = 'S';
      return true;
    case 0xFF9D:
      out->vk = 'D';
      return true;
    case 0xFF9E:
      out->vk = 0xde;
      return true;
    case 0xFF9F:
      out->vk = 0xbd;
      return true;
    case 0xFF70:
      out->vk = 0xbd;
      return true;
    default:
      return false;
  }
}

bool HiraganaToHalf(uint32_t cp, std::vector<uint16_t>* out) {
  if (!out) {
    return false;
  }
  const uint16_t dak = 0xFF9E;
  const uint16_t han = 0xFF9F;

  auto base = [&](uint16_t hw) {
    out->push_back(hw);
    return true;
  };
  auto voiced = [&](uint16_t hw) {
    out->push_back(hw);
    out->push_back(dak);
    return true;
  };
  auto semi = [&](uint16_t hw) {
    out->push_back(hw);
    out->push_back(han);
    return true;
  };

  switch (cp) {
    case 0x3041:
      return base(0xFF67);
    case 0x3042:
      return base(0xFF71);
    case 0x3043:
      return base(0xFF68);
    case 0x3044:
      return base(0xFF72);
    case 0x3045:
      return base(0xFF69);
    case 0x3046:
      return base(0xFF73);
    case 0x3047:
      return base(0xFF6A);
    case 0x3048:
      return base(0xFF74);
    case 0x3049:
      return base(0xFF6B);
    case 0x304A:
      return base(0xFF75);
    case 0x304B:
      return base(0xFF76);
    case 0x304C:
      return voiced(0xFF76);
    case 0x304D:
      return base(0xFF77);
    case 0x304E:
      return voiced(0xFF77);
    case 0x304F:
      return base(0xFF78);
    case 0x3050:
      return voiced(0xFF78);
    case 0x3051:
      return base(0xFF79);
    case 0x3052:
      return voiced(0xFF79);
    case 0x3053:
      return base(0xFF7A);
    case 0x3054:
      return voiced(0xFF7A);
    case 0x3055:
      return base(0xFF7B);
    case 0x3056:
      return voiced(0xFF7B);
    case 0x3057:
      return base(0xFF7C);
    case 0x3058:
      return voiced(0xFF7C);
    case 0x3059:
      return base(0xFF7D);
    case 0x305A:
      return voiced(0xFF7D);
    case 0x305B:
      return base(0xFF7E);
    case 0x305C:
      return voiced(0xFF7E);
    case 0x305D:
      return base(0xFF7F);
    case 0x305E:
      return voiced(0xFF7F);
    case 0x305F:
      return base(0xFF80);
    case 0x3060:
      return voiced(0xFF80);
    case 0x3061:
      return base(0xFF81);
    case 0x3062:
      return voiced(0xFF81);
    case 0x3063:
      return base(0xFF82);
    case 0x3064:
      return base(0xFF82);
    case 0x3065:
      return voiced(0xFF82);
    case 0x3066:
      return base(0xFF83);
    case 0x3067:
      return voiced(0xFF83);
    case 0x3068:
      return base(0xFF84);
    case 0x3069:
      return voiced(0xFF84);
    case 0x306A:
      return base(0xFF85);
    case 0x306B:
      return base(0xFF86);
    case 0x306C:
      return base(0xFF87);
    case 0x306D:
      return base(0xFF88);
    case 0x306E:
      return base(0xFF89);
    case 0x306F:
      return base(0xFF8A);
    case 0x3070:
      return voiced(0xFF8A);
    case 0x3071:
      return semi(0xFF8A);
    case 0x3072:
      return base(0xFF8B);
    case 0x3073:
      return voiced(0xFF8B);
    case 0x3074:
      return semi(0xFF8B);
    case 0x3075:
      return base(0xFF8C);
    case 0x3076:
      return voiced(0xFF8C);
    case 0x3077:
      return semi(0xFF8C);
    case 0x3078:
      return base(0xFF8D);
    case 0x3079:
      return voiced(0xFF8D);
    case 0x307A:
      return semi(0xFF8D);
    case 0x307B:
      return base(0xFF8E);
    case 0x307C:
      return voiced(0xFF8E);
    case 0x307D:
      return semi(0xFF8E);
    case 0x307E:
      return base(0xFF8F);
    case 0x307F:
      return base(0xFF90);
    case 0x3080:
      return base(0xFF91);
    case 0x3081:
      return base(0xFF92);
    case 0x3082:
      return base(0xFF93);
    case 0x3083:
      return base(0xFF6C);
    case 0x3084:
      return base(0xFF96);
    case 0x3085:
      return base(0xFF6D);
    case 0x3086:
      return base(0xFF97);
    case 0x3087:
      return base(0xFF6E);
    case 0x3088:
      return base(0xFF98);
    case 0x3089:
      return base(0xFF99);
    case 0x308A:
      return base(0xFF9A);
    case 0x308B:
      return base(0xFF9B);
    case 0x308C:
      return base(0xFF9C);
    case 0x308D:
      return base(0xFF9D);
    case 0x308F:
      return base(0xFF9C);
    case 0x3092:
      return base(0xFF66);
    case 0x3093:
      return base(0xFF9D);
    case 0x30FC:
      return base(0xFF70);
    default:
      break;
  }

  if (cp >= 0x30A1 && cp <= 0x30F6) {
    return HiraganaToHalf(cp - 0x30A1 + 0x3041, out);
  }
  if (cp >= 0xFF61 && cp <= 0xFF9F) {
    out->push_back(static_cast<uint16_t>(cp));
    return true;
  }
  return false;
}

const unsigned char* Utf8Next(const unsigned char* p, uint32_t* cp) {
  if (!p || !*p) {
    *cp = 0;
    return p;
  }
  const unsigned char c = p[0];
  if (c < 0x80) {
    *cp = c;
    return p + 1;
  }
  if ((c & 0xE0) == 0xC0 && p[1]) {
    *cp = ((c & 0x1F) << 6) | (p[1] & 0x3F);
    return p + 2;
  }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2]) {
    *cp = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
    return p + 3;
  }
  *cp = '?';
  return p + 1;
}

std::deque<KeyStroke> g_queue;
bool g_session_kana = false;
int g_frames_until_next = 0;
int g_kana_matrix_refs = 0;
PC8801::Config g_saved_cfg{};
bool g_saved_cfg_valid = false;
constexpr int kStrokeGapFrames = 2;

void KanaMatrixPush(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif || !cfg || cfg->keytype == PC8801::Config::AT106 ||
      cfg->keytype == PC8801::Config::PC98) {
    return;
  }
  if (g_kana_matrix_refs++ == 0) {
    g_saved_cfg = *cfg;
    g_saved_cfg_valid = true;
    PC8801::Config kana = *cfg;
    kana.keytype = PC8801::Config::AT106;
    keyif->ApplyConfig(&kana);
  }
}

void KanaMatrixPop(PC8801::WinKeyIF* keyif) {
  if (!keyif) {
    return;
  }
  if (g_kana_matrix_refs > 0 && --g_kana_matrix_refs == 0 && g_saved_cfg_valid) {
    keyif->ApplyConfig(&g_saved_cfg);
    g_saved_cfg_valid = false;
  }
}

}  // namespace

bool CommitUtf8ToHalfKana(const char* utf8, std::vector<uint16_t>* out_hw) {
  if (!utf8 || !out_hw) {
    return false;
  }
  out_hw->clear();
  const unsigned char* p = reinterpret_cast<const unsigned char*>(utf8);
  bool any = false;
  while (*p) {
    uint32_t cp = 0;
    p = Utf8Next(p, &cp);
    if (!cp) {
      break;
    }
    if (cp == ' ' || cp == '\n' || cp == '\r' || cp == '\t') {
      continue;
    }
    if (cp >= 0xFF61 && cp <= 0xFF9F) {
      out_hw->push_back(static_cast<uint16_t>(cp));
      any = true;
      continue;
    }
    if (HiraganaToHalf(cp, out_hw)) {
      any = true;
    }
  }
  return any;
}

void HalfKanaToKeyStrokes(const std::vector<uint16_t>& hw, std::vector<KeyStroke>* out) {
  if (!out) {
    return;
  }
  out->clear();
  for (uint16_t ch : hw) {
    VkStroke vk{};
    if (!LookupVk(ch, &vk)) {
      continue;
    }
    out->push_back(KeyStroke{vk.vk, true, vk.keydata});
    out->push_back(KeyStroke{vk.vk, false, vk.keydata});
  }
}

void InjectEndSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif) {
    return;
  }
  g_queue.clear();
  g_frames_until_next = 0;
  keyif->SetKanaLock(false);
  keyif->ClearHostModifiers();
  if (g_session_kana) {
    g_session_kana = false;
    KanaMatrixPop(keyif);
  }
}

void InjectBeginSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif) {
    return;
  }
  g_queue.clear();
  g_frames_until_next = 0;
  KanaMatrixPush(keyif, cfg);
  keyif->ClearHostModifiers();
  keyif->SetKanaLock(true);
  g_session_kana = true;
}

void InjectEnqueue(const std::vector<KeyStroke>& strokes) {
  for (const KeyStroke& s : strokes) {
    g_queue.push_back(s);
  }
}

bool InjectBusy() { return !g_queue.empty() || g_frames_until_next > 0; }

void InjectPump(PC8801::WinKeyIF* keyif) {
  if (!keyif) {
    return;
  }
  if (g_frames_until_next > 0) {
    --g_frames_until_next;
    return;
  }
  if (g_queue.empty()) {
    return;
  }
  const KeyStroke s = g_queue.front();
  g_queue.pop_front();
  if (s.down) {
    keyif->KeyDown(s.vk, s.keydata);
  } else {
    keyif->KeyUp(s.vk, s.keydata);
  }
  g_frames_until_next = kStrokeGapFrames;
}

}  // namespace HalfKanaIme
