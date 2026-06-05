#include "pc88_host_char.h"

#include "../linux_compat/winkeys.h"

namespace Pc88HostChar {
namespace {

PC8801::Config::KeyType g_host = PC8801::Config::AT101;
PC8801::Config::KeyType g_guest = PC8801::Config::AT101;

bool SetTap(uint8_t vk, bool shift, uint32_t keydata, JisTap* out) {
  if (!out || !vk) {
    return false;
  }
  out->vk = vk;
  out->shift = shift;
  out->keydata = keydata;
  return true;
}

// AT106 (JIS) guest: symbols only — digits/letters use direct VK in the UI.
bool CharToAt106Tap(uint32_t u, JisTap* out) {
  switch (u) {
    case '!':
      return SetTap('1', true, 0, out);
    case '"':
      return SetTap('2', true, 0, out);
    case '#':
      return SetTap('3', true, 0, out);
    case '$':
      return SetTap('4', true, 0, out);
    case '%':
      return SetTap('5', true, 0, out);
    case '&':
      return SetTap('7', true, 0, out);
    case '\'':
      return SetTap('7', true, 0, out);
    case '(':
      return SetTap('9', true, 0, out);
    case ')':
      return SetTap('0', true, 0, out);
    case '*':
      return SetTap('8', true, 0, out);
    case '+':
      return SetTap(0xbb, true, 0, out);
    case ',':
      return SetTap(0xbc, false, 0, out);
    case '.':
      return SetTap(0xbe, false, 0, out);
    case '/':
      return SetTap(0xbf, false, 0, out);
    case ':':
      return SetTap(0xba, false, 0, out);
    case ';':
      return SetTap(0xbb, false, 0, out);
    case '<':
      return SetTap(0xbc, true, 0, out);
    case '>':
      return SetTap(0xbe, true, 0, out);
    case '?':
      return SetTap(0xbf, true, 0, out);
    case '@':
      return SetTap(0xc0, false, 0, out);
    case '[':
      return SetTap(0xdb, false, 0, out);
    case '\\':
      return SetTap(0xdc, false, 0, out);
    case ']':
      return SetTap(0xdd, false, 0, out);
    case '^':
      return SetTap(0xde, false, 0, out);
    case '_':
      return SetTap(0xe2, false, 0, out);
    case '`':
      return SetTap(0xde, true, 0, out);
    case '{':
      return SetTap(0xdb, true, 0, out);
    case '|':
      return SetTap(0xdc, true, 0, out);
    case '}':
      return SetTap(0xdd, true, 0, out);
    case '~':
      return SetTap(0xbd, true, 0, out);
    default:
      return false;
  }
}

// AT101 (US) guest: WinKeyIF KeyTable101 OEM positions.
bool CharToAt101Tap(uint32_t u, JisTap* out) {
  switch (u) {
    case '!':
      return SetTap('1', true, 0, out);
    case '"':
      return SetTap('2', true, 0, out);
    case '#':
      return SetTap('3', true, 0, out);
    case '$':
      return SetTap('4', true, 0, out);
    case '%':
      return SetTap('5', true, 0, out);
    case '&':
      return SetTap('7', true, 0, out);
    case '\'':
      return SetTap('7', true, 0, out);
    case '(':
      return SetTap('9', true, 0, out);
    case ')':
      return SetTap('0', true, 0, out);
    case '*':
      return SetTap('8', true, 0, out);
    case '+':
      return SetTap(VK_OEM_PLUS, true, 0, out);
    case ',':
      return SetTap(0xbc, false, 0, out);
    case '.':
      return SetTap(0xbe, false, 0, out);
    case '/':
      return SetTap(0xbf, false, 0, out);
    case ':':
      return SetTap(0xba, true, 0, out);
    case ';':
      return SetTap(0xde, false, 0, out);
    case '<':
      return SetTap(0xbc, true, 0, out);
    case '>':
      return SetTap(0xbe, true, 0, out);
    case '?':
      return SetTap(0xbf, true, 0, out);
    case '@':
      return SetTap(0xdb, false, 0, out);
    case '[':
      return SetTap(0xdd, false, 0, out);
    case '\\':
      return SetTap(0xdc, false, 0, out);
    case ']':
      return SetTap(0xc0, false, 0, out);
    case '^':
      return SetTap(0xbb, false, 0, out);
    case '_':
      return SetTap(0xe2, false, 0, out);
    case '`':
      return SetTap(VK_OEM_3, false, 0, out);
    case '{':
      return SetTap(0xdd, true, 0, out);
    case '|':
      return SetTap(0xdc, true, 0, out);
    case '}':
      return SetTap(0xc0, true, 0, out);
    case '~':
      return SetTap(VK_OEM_3, true, 0, out);
    default:
      return false;
  }
}

bool Host101VkToChar(uint vk, bool shift, uint32_t* cp) {
  if (!cp) {
    return false;
  }
  if (vk >= 'A' && vk <= 'Z') {
    *cp = shift ? static_cast<uint32_t>(vk) : static_cast<uint32_t>(vk - 'A' + 'a');
    return true;
  }
  if (vk >= '0' && vk <= '9') {
    if (!shift) {
      *cp = static_cast<uint32_t>(vk);
      return true;
    }
    switch (vk) {
      case '1':
        *cp = '!';
        return true;
      case '2':
        *cp = '@';
        return true;
      case '3':
        *cp = '#';
        return true;
      case '4':
        *cp = '$';
        return true;
      case '5':
        *cp = '%';
        return true;
      case '6':
        *cp = '^';
        return true;
      case '7':
        *cp = '&';
        return true;
      case '8':
        *cp = '*';
        return true;
      case '9':
        *cp = '(';
        return true;
      case '0':
        *cp = ')';
        return true;
      default:
        return false;
    }
  }

  if (!shift) {
    switch (vk) {
      case VK_SPACE:
        *cp = ' ';
        return true;
      case VK_OEM_1:
        *cp = ';';
        return true;
      case VK_OEM_PLUS:
        *cp = '=';
        return true;
      case VK_OEM_COMMA:
        *cp = ',';
        return true;
      case VK_OEM_MINUS:
        *cp = '-';
        return true;
      case VK_OEM_PERIOD:
        *cp = '.';
        return true;
      case VK_OEM_2:
        *cp = '/';
        return true;
      case VK_OEM_3:
        *cp = '`';
        return true;
      case VK_OEM_4:
        *cp = '[';
        return true;
      case VK_OEM_5:
        *cp = '\\';
        return true;
      case VK_OEM_6:
        *cp = ']';
        return true;
      case VK_OEM_7:
        *cp = '\'';
        return true;
      default:
        return false;
    }
  }

  switch (vk) {
    case VK_OEM_1:
      *cp = ':';
      return true;
    case VK_OEM_PLUS:
      *cp = '+';
      return true;
    case VK_OEM_COMMA:
      *cp = '<';
      return true;
    case VK_OEM_MINUS:
      *cp = '_';
      return true;
    case VK_OEM_PERIOD:
      *cp = '>';
      return true;
    case VK_OEM_2:
      *cp = '?';
      return true;
    case VK_OEM_3:
      *cp = '~';
      return true;
    case VK_OEM_4:
      *cp = '{';
      return true;
    case VK_OEM_5:
      *cp = '|';
      return true;
    case VK_OEM_6:
      *cp = '}';
      return true;
    case VK_OEM_7:
      *cp = '"';
      return true;
    default:
      return false;
  }
}

bool Host106VkToChar(uint vk, bool shift, uint32_t* cp) {
  if (!cp) {
    return false;
  }
  if (vk >= 'A' && vk <= 'Z') {
    *cp = shift ? static_cast<uint32_t>(vk) : static_cast<uint32_t>(vk - 'A' + 'a');
    return true;
  }
  if (vk >= '0' && vk <= '9') {
    if (!shift) {
      *cp = static_cast<uint32_t>(vk);
      return true;
    }
    switch (vk) {
      case '1':
        *cp = '!';
        return true;
      case '2':
        *cp = '"';
        return true;
      case '3':
        *cp = '#';
        return true;
      case '4':
        *cp = '$';
        return true;
      case '5':
        *cp = '%';
        return true;
      case '6':
        *cp = '&';
        return true;
      case '7':
        *cp = '\'';
        return true;
      case '8':
        *cp = '(';
        return true;
      case '9':
        *cp = ')';
        return true;
      case '0':
        *cp = '~';
        return true;
      default:
        return false;
    }
  }

  if (!shift) {
    switch (vk) {
      case VK_SPACE:
        *cp = ' ';
        return true;
      case 0xc0:
        *cp = '@';
        return true;
      case 0xdb:
        *cp = '[';
        return true;
      case 0xdc:
        *cp = '\\';
        return true;
      case 0xdd:
        *cp = ']';
        return true;
      case 0xde:
        *cp = '^';
        return true;
      case 0xbd:
        *cp = '-';
        return true;
      case 0xba:
        *cp = ':';
        return true;
      case 0xbb:
        *cp = ';';
        return true;
      case 0xbc:
        *cp = ',';
        return true;
      case 0xbe:
        *cp = '.';
        return true;
      case 0xbf:
        *cp = '/';
        return true;
      case 0xe2:
        *cp = '_';
        return true;
      default:
        return false;
    }
  }

  switch (vk) {
    case 0xdb:
      *cp = '{';
      return true;
    case 0xdc:
      *cp = '|';
      return true;
    case 0xdd:
      *cp = '}';
      return true;
    case 0xde:
      *cp = '`';
      return true;
    case 0xbd:
      *cp = '=';
      return true;
    case 0xba:
      *cp = '+';
      return true;
    case 0xbb:
      *cp = '*';
      return true;
    case 0xbc:
      *cp = '<';
      return true;
    case 0xbe:
      *cp = '>';
      return true;
    case 0xbf:
      *cp = '?';
      return true;
    default:
      return false;
  }
}

}  // namespace

void SetHostKeyboardType(PC8801::Config::KeyType host) { g_host = host; }

void SetGuestKeyboardType(PC8801::Config::KeyType guest) { g_guest = guest; }

bool UsesCharTapPath(uint32_t codepoint) {
  if (codepoint == '-' || codepoint == '=') {
    return false;
  }
  if (codepoint >= '0' && codepoint <= '9') {
    return false;
  }
  if (codepoint >= 'A' && codepoint <= 'Z') {
    return false;
  }
  if (codepoint >= 'a' && codepoint <= 'z') {
    return false;
  }
  if (codepoint == ' ') {
    return false;
  }
  return codepoint >= 0x21 && codepoint < 0x7f;
}

bool CharToGuestTap(uint32_t codepoint, JisTap* out) {
  if (!out || !UsesCharTapPath(codepoint)) {
    return false;
  }
  if (g_guest == PC8801::Config::AT106) {
    return CharToAt106Tap(codepoint, out);
  }
  if (g_guest == PC8801::Config::AT101) {
    return CharToAt101Tap(codepoint, out);
  }
  return false;
}

bool HostKeyToGuestTap(uint vk, bool shift, JisTap* out) {
  if (!out || !vk) {
    return false;
  }
  uint32_t cp = 0;
  if (g_host == PC8801::Config::AT106) {
    if (!Host106VkToChar(vk, shift, &cp)) {
      return false;
    }
  } else if (!Host101VkToChar(vk, shift, &cp)) {
    return false;
  }
  return CharToGuestTap(cp, out);
}

void AppendTapDown(const JisTap& tap, std::vector<HalfKanaIme::KeyStroke>* strokes) {
  if (!strokes) {
    return;
  }
  if (tap.shift) {
    strokes->push_back(HalfKanaIme::KeyStroke{VK_LSHIFT, true, 0});
  }
  strokes->push_back(HalfKanaIme::KeyStroke{tap.vk, true, tap.keydata});
}

void AppendTapUp(const JisTap& tap, std::vector<HalfKanaIme::KeyStroke>* strokes) {
  if (!strokes) {
    return;
  }
  strokes->push_back(HalfKanaIme::KeyStroke{tap.vk, false, tap.keydata});
  if (tap.shift) {
    strokes->push_back(HalfKanaIme::KeyStroke{VK_LSHIFT, false, 0});
  }
}

}  // namespace Pc88HostChar
