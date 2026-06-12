#include "half_kana_ime.h"

#include "../linux_compat/pc88_matrix_vk.h"
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

PC8801::Config::KeyType g_ime_host_keytype = PC8801::Config::AT101;

uint8_t VkRow05D2() { return 'Z'; }
uint8_t VkRow05D3() {
  return (g_ime_host_keytype == PC8801::Config::AT101) ? 0xdd : 0xdb;
}
uint8_t VkRow05D5() {
  return (g_ime_host_keytype == PC8801::Config::AT101) ? 0xc0 : 0xdd;
}
uint8_t VkRow07D4() { return 0xbc; }
uint8_t VkRow07D5() { return 0xbe; }
uint8_t VkRow07D6() { return VK_PC88_SLASH; }
uint8_t VkRow07D3() {
  return (g_ime_host_keytype == PC8801::Config::AT101) ? 0xbb : 0xde;
}
uint8_t VkRow05D6() {
  return (g_ime_host_keytype == PC8801::Config::AT101) ? 0xde : 0xbb;
}

// PC-8801 FH matrix kana (N88-BASIC / カナ LOCK). See LookupVkMomentary for 101 IME.
bool LookupVkLock(uint16_t hw, VkStroke* out) {
  if (!out) {
    return false;
  }
  // FH matrix row slots (A,S,D,…); same on KeyTable101 and KeyTable106 for letters.
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

// PC-8801 FH matrix sutegana: momentary カナ + row08 SHIFT on shared keys (row06/07).
bool LookupVkSmall(uint16_t hw, VkStroke* out) {
  if (!out) {
    return false;
  }
  out->keydata = M88_KEYDATA_GUEST_SHIFT;
  switch (hw) {
    case 0xFF66:
      out->vk = '0';
      return true;  // ｦ row06 0/ヲ (+ shift; plain 0 → ワ)
    case 0xFF67:
      out->vk = '3';
      return true;  // ｧ row06 3/ァ
    case 0xFF68:
      out->vk = 'E';
      return true;  // ｨ row02 E/イ + shift
    case 0xFF69:
      out->vk = '4';
      return true;  // ｩ row06 4/ゥ
    case 0xFF6A:
      out->vk = '5';
      return true;  // ｪ row06 5/ェ
    case 0xFF6B:
      out->vk = '6';
      return true;  // ｫ row06 6/ォ
    case 0xFF6C:
      out->vk = '7';
      return true;  // ｬ row06 7/ャ
    case 0xFF6D:
      out->vk = '8';
      return true;  // ｭ row07 8/ュ
    case 0xFF6E:
      out->vk = '9';
      return true;  // ｮ row07 9/ョ
    case 0xFF6F:
      out->vk = VkRow05D2();
      return true;  // ｯ っ row05 D2
    default:
      return false;
  }
}

// PC-8801 FH keyboard matrix kana layer (momentary カナ / kana on key cap).
// Source: PC-8801 key matrix chart (address row x data col).
bool LookupVkMomentary(uint16_t hw, VkStroke* out, PC8801::WinKeyIF* keyif) {
  if (!out) {
    return false;
  }
  out->keydata = 0;
  switch (hw) {
    case 0xFF70:
      out->vk = 0xdc;
      return true;  // ｰ row05 ¥/ー
    case 0xFF71:
      out->vk = '3';
      return true;  // ｱ row06
    case 0xFF72:
      out->vk = 'E';
      return true;  // ｲ row02
    case 0xFF73:
      out->vk = '4';
      return true;  // ｳ row06
    case 0xFF74:
      out->vk = '5';
      return true;  // ｴ row06
    case 0xFF75:
      out->vk = '6';
      return true;  // ｵ row06
    case 0xFF76:
      out->vk = 'T';
      return true;  // ｶ row04
    case 0xFF77:
      out->vk = 'G';
      return true;  // ｷ row02
    case 0xFF78:
      out->vk = 'H';
      return true;  // ｸ row03
    case 0xFF79:
      out->vk = 0xba;
      return true;  // ｹ row07 :
    case 0xFF7A:
      out->vk = 'B';
      return true;  // ｺ row02
    case 0xFF7B:
      out->vk = 'X';
      return true;  // ｻ row05
    case 0xFF7C:
      out->vk = 'D';
      return true;  // ｼ row02
    case 0xFF7D:
      out->vk = 'R';
      return true;  // ｽ row04
    case 0xFF7E:
      out->vk = 'P';
      return true;  // ｾ row04
    case 0xFF7F:
      out->vk = 'C';
      return true;  // ｿ row02
    case 0xFF80:
      out->vk = 'Q';
      return true;  // ﾀ row04
    case 0xFF81:
      out->vk = 'A';
      return true;  // ﾁ row02
    case 0xFF82:
      out->vk = 'Z';
      return true;  // ﾂ row05
    case 0xFF83:
      out->vk = 'W';
      return true;  // ﾃ row04
    case 0xFF84:
      out->vk = 'S';
      return true;  // ﾄ row04
    case 0xFF85:
      out->vk = 'U';
      return true;  // ﾅ row04
    case 0xFF86:
      out->vk = 'I';
      return true;  // ﾆ row03
    case 0xFF87:
      out->vk = '1';
      return true;  // ﾇ row06
    case 0xFF88:
      out->vk = 0xbc;
      return true;  // ﾈ row07 ,
    case 0xFF89:
      out->vk = 'K';
      return true;  // ﾉ row03
    case 0xFF8A:
      out->vk = 'F';
      return true;  // ﾊ row02
    case 0xFF8B:
      out->vk = 'V';
      return true;  // ﾋ row04
    case 0xFF8C:
      out->vk = '2';
      return true;  // ﾌ row06
    case 0xFF8D:
      out->vk = keyif ? keyif->MatrixVk(0x05, 6) : VkRow05D6();
      return true;  // ﾍ row05 D6 ^
    case 0xFF8E:
      out->vk = 0xbd;
      return true;  // ﾎ row05 -
    case 0xFF8F:
      out->vk = 'J';
      return true;  // ﾏ row03
    case 0xFF90:
      out->vk = 'N';
      return true;  // ﾐ row03
    case 0xFF91:
      out->vk = VkRow05D5();
      return true;  // ﾑ row05 D5
    case 0xFF92:
      out->vk = 0xbf;
      return true;  // ﾒ row07 /
    case 0xFF93:
      out->vk = 'M';
      return true;  // ﾓ row03
    case 0xFF94:
      out->vk = '7';
      return true;  // ﾔ row06
    case 0xFF95:
      out->vk = '8';
      return true;  // ﾕ row07
    case 0xFF96:
      out->vk = '9';
      return true;  // ﾖ row07
    case 0xFF97:
      out->vk = 'O';
      return true;  // ﾗ row03
    case 0xFF98:
      out->vk = 'L';
      return true;  // ﾘ row03
    case 0xFF99:
      out->vk = 0xbe;
      return true;  // ﾙ row07 .
    case 0xFF9A:
      out->vk = keyif ? keyif->MatrixVk(0x07, 3) : VkRow07D3();
      return true;  // ﾚ レ row07 D3
    case 0xFF9B:
      out->vk = 0xe2;
      return true;  // ﾛ row07 _
    case 0xFF9C:
      out->vk = '0';
      return true;  // ﾜ row06 0/ワ
    case 0xFF9D:
      out->vk = 'Y';
      return true;  // ﾝ row05
    default:
      return false;
  }
}

bool IsCombiningHalfMark(uint16_t hw) { return hw == 0xFF9E || hw == 0xFF9F; }

bool LookupVkPunct(uint16_t hw, VkStroke* out) {
  if (!out) {
    return false;
  }
  out->keydata = 0;
  switch (hw) {
    case 0xFF61:
      out->vk = VkRow07D5();
      out->keydata = M88_KEYDATA_FH_SHIFT;
      return true;  // ｡ 。 row07 D5 + カナ+SHIFT
    case 0xFF64:
      out->vk = VkRow07D4();
      out->keydata = M88_KEYDATA_FH_SHIFT;
      return true;  // ､ 、 row07 D4 + カナ+SHIFT
    case 0xFF65:
      out->vk = VkRow07D6();
      out->keydata = M88_KEYDATA_FH_SHIFT;
      return true;  // ･ ・ row07 D6 + カナ+SHIFT
    case 0xFF62:
      out->vk = VkRow05D3();
      out->keydata = M88_KEYDATA_FH_SHIFT;
      return true;  // 「 row05 D3
    case 0xFF63:
      out->vk = VkRow05D5();
      out->keydata = M88_KEYDATA_FH_SHIFT;
      return true;  // 」 row05 D5
    case 0xFF40:
      out->vk = VK_SPACE;
      return true;  // 半角スペース
    default:
      return false;
  }
}

// ゛/゜ matrix slot depends on host keytable (AT101 row02 @ is 0xdb, not 0xc0).
bool LookupCombiningVk(uint16_t hw, VkStroke* out) {
  if (!out || !IsCombiningHalfMark(hw)) {
    return false;
  }
  out->keydata = 0;
  if (hw == 0xFF9E) {
    out->vk = (g_ime_host_keytype == PC8801::Config::AT101) ? 0xdb : 0xc0;
    return true;
  }
  // row05 D3 [ / ｡ / 「 — 半濁点 (実機 ] キー)
  out->vk = VkRow05D3();
  return true;
}

bool LookupVk(uint16_t hw, VkStroke* out, PC8801::WinKeyIF* keyif) {
  if (IsCombiningHalfMark(hw)) {
    return LookupCombiningVk(hw, out);
  }
  if (LookupVkPunct(hw, out)) {
    return true;
  }
  if (hw >= 0xFF66 && hw <= 0xFF6F) {
    return LookupVkSmall(hw, out);
  }
  return LookupVkMomentary(hw, out, keyif);
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
      return base(0xFF6F);
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
      return base(0xFF94);
    case 0x3085:
      return base(0xFF6D);
    case 0x3086:
      return base(0xFF95);
    case 0x3087:
      return base(0xFF6E);
    case 0x3088:
      return base(0xFF96);
    case 0x3089:
      return base(0xFF97);
    case 0x308A:
      return base(0xFF98);
    case 0x308B:
      return base(0xFF99);
    case 0x308C:
      return base(0xFF9A);
    case 0x308D:
      return base(0xFF9B);
    case 0x308F:
      return base(0xFF9C);
    case 0x3092:
      return base(0xFF66);
    case 0x3093:
      return base(0xFF9D);
    case 0x3001:
      return base(0xFF64);
    case 0x3002:
      return base(0xFF61);
    case 0x00B7:  // · middle dot (some IMEs)
    case 0x30FB:  // ・ katakana middle dot
    case 0x002F:  // / — IME often commits this for 中点 (matrix 07D6 kana+shift -> A5)
      return base(0xFF65);
    case 0x300C:
      return base(0xFF62);
    case 0x300D:
      return base(0xFF63);
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
constexpr int kStrokeGapFrames = 4;
constexpr int kDakutenGapFrames = 1;
constexpr int kHalfWidthSettleFrames = 5;

void KanaMatrixPush(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif || !cfg) {
    return;
  }
  keyif->PushImeKeyTable();
}

void KanaMatrixPop(PC8801::WinKeyIF* keyif) {
  if (!keyif) {
    return;
  }
  keyif->PopImeKeyTable();
}

void ApplyImeHostKeyType(PC8801::Config::KeyType host) { g_ime_host_keytype = host; }

}  // namespace

void SyncImeHostKeyType(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (keyif) {
    ApplyImeHostKeyType(keyif->HostKeyType());
  } else if (cfg) {
    ApplyImeHostKeyType(static_cast<PC8801::Config::KeyType>(cfg->keytype));
  }
}

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
    if (cp == ' ' || cp == 0x3000 || cp == 0xFF40) {
      out_hw->push_back(0xFF40);
      any = true;
      continue;
    }
    if (cp == '\n' || cp == '\r' || cp == '\t') {
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

void HalfKanaToKeyStrokes(PC8801::WinKeyIF* keyif, const std::vector<uint16_t>& hw,
                          std::vector<KeyStroke>* out) {
  if (!out) {
    return;
  }
  out->clear();
  for (size_t i = 0; i < hw.size(); ++i) {
    const uint16_t ch = hw[i];
    VkStroke vk{};
    if (!LookupVk(ch, &vk, keyif)) {
      continue;
    }
    const bool mark = IsCombiningHalfMark(ch);
    const bool mark_next =
        (i + 1 < hw.size()) && IsCombiningHalfMark(hw[i + 1]);
    InjectRoute down_route = mark ? InjectRoute::Dakuten : InjectRoute::Ime;
    const InjectRoute up_route = mark_next ? InjectRoute::Dakuten : down_route;
    out->push_back(KeyStroke{vk.vk, true, vk.keydata, down_route});
    out->push_back(KeyStroke{vk.vk, false, vk.keydata, up_route});
  }
}

void InjectEndSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif) {
    return;
  }
  (void)cfg;
  g_queue.clear();
  g_frames_until_next = 0;
  g_session_kana = false;
  // Drop inject-layer matrix state before host keys resume (SPACE vs SHIFT on D6).
  keyif->FinishImeInjectSession();
  KanaMatrixPop(keyif);
}

void InjectBeginSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg) {
  if (!keyif) {
    return;
  }
  g_queue.clear();
  g_frames_until_next = 0;
  g_session_kana = true;
  SyncImeHostKeyType(keyif, cfg);
  KanaMatrixPush(keyif, cfg);
  keyif->SetKanaLock(false);
  keyif->ClearHostModifiers();
  g_queue.push_back(KeyStroke{0, true, 0, InjectRoute::HalfWidthPulse});
}

void InjectEnqueue(const std::vector<KeyStroke>& strokes) {
  for (const KeyStroke& s : strokes) {
    g_queue.push_back(s);
  }
}

bool InjectBusy() { return !g_queue.empty() || g_frames_until_next > 0; }

bool SessionActive() { return g_session_kana; }

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
  int gap = kStrokeGapFrames;
  switch (s.route) {
    case InjectRoute::Host:
      if (s.down) {
        keyif->InjectKeyDown(s.vk, s.keydata);
      } else {
        keyif->InjectKeyUp(s.vk, s.keydata);
      }
      break;
    case InjectRoute::HalfWidthPulse:
      keyif->PulseHalfWidthKana();
      gap = kHalfWidthSettleFrames;
      break;
    case InjectRoute::ImeLock:
      if (s.down) {
        keyif->InjectImeLockKeyDown(s.vk, s.keydata);
      } else {
        keyif->InjectImeLockKeyUp(s.vk, s.keydata);
      }
      break;
    case InjectRoute::Dakuten:
      gap = kDakutenGapFrames;
      if (s.down) {
        keyif->InjectImeKeyDown(s.vk, s.keydata);
      } else {
        keyif->InjectImeKeyUp(s.vk, s.keydata);
      }
      break;
    case InjectRoute::Ime:
    default:
      if (s.down) {
        keyif->InjectImeKeyDown(s.vk, s.keydata);
      } else {
        keyif->InjectImeKeyUp(s.vk, s.keydata);
      }
      break;
  }
  g_frames_until_next = gap;
}

}  // namespace HalfKanaIme
