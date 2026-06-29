#include "qt_input.h"

#include "../linux/keyboard_vk.h"
#include "../linux_compat/winkeys.h"

#include <QKeyEvent>
#include <QtGlobal>

namespace {

bool g_suppress_menu = false;
bool g_host_at101 = false;

uint VkFromScan(quint32 scan) {
  if (scan == 0) {
    return 0;
  }
  const uint raw = static_cast<uint>(scan);
  uint vk = M88Input::EvdevScancodeToVk(raw);
  if (vk) {
    return vk;
  }
  if (raw >= 8) {
    vk = M88Input::EvdevScancodeToVk(raw - 8);
  }
  return vk;
}

// Qt may emit Key_Colon / Key_At without Qt::ShiftModifier (shift already in the key).
bool IsShiftedSymbolQtKey(int key) {
  switch (key) {
    case Qt::Key_Exclam:
    case Qt::Key_At:
    case Qt::Key_NumberSign:
    case Qt::Key_Dollar:
    case Qt::Key_Percent:
    case Qt::Key_AsciiCircum:
    case Qt::Key_Ampersand:
    case Qt::Key_Asterisk:
    case Qt::Key_ParenLeft:
    case Qt::Key_ParenRight:
    case Qt::Key_Underscore:
    case Qt::Key_Plus:
    case Qt::Key_BraceLeft:
    case Qt::Key_BraceRight:
    case Qt::Key_Bar:
    case Qt::Key_Colon:
    case Qt::Key_QuoteDbl:
    case Qt::Key_AsciiTilde:
    case Qt::Key_Less:
    case Qt::Key_Greater:
    case Qt::Key_Question:
      return true;
    default:
      return false;
  }
}

uint VkFromShiftedQtKey(int key) {
  switch (key) {
    case Qt::Key_Exclam:
      return '1';
    case Qt::Key_At:
      return '2';
    case Qt::Key_NumberSign:
      return '3';
    case Qt::Key_Dollar:
      return '4';
    case Qt::Key_Percent:
      return '5';
    case Qt::Key_AsciiCircum:
      return '6';
    case Qt::Key_Ampersand:
      return '7';
    case Qt::Key_Asterisk:
      return '8';
    case Qt::Key_ParenLeft:
      return '9';
    case Qt::Key_ParenRight:
      return '0';
    case Qt::Key_Underscore:
      return VK_OEM_MINUS;
    case Qt::Key_Plus:
      return VK_OEM_PLUS;
    case Qt::Key_BraceLeft:
      return VK_OEM_4;
    case Qt::Key_BraceRight:
      return VK_OEM_6;
    case Qt::Key_Bar:
      return VK_OEM_5;
    case Qt::Key_Colon:
      return VK_OEM_1;
    case Qt::Key_QuoteDbl:
      return VK_OEM_7;
    case Qt::Key_AsciiTilde:
      return VK_OEM_3;
    case Qt::Key_Less:
      return VK_OEM_COMMA;
    case Qt::Key_Greater:
      return VK_OEM_PERIOD;
    case Qt::Key_Question:
      return VK_OEM_2;
    default:
      return 0;
  }
}

uint VkFromShiftedText(QChar ch) {
  switch (ch.unicode()) {
    case '!':
      return '1';
    case '@':
      return '2';
    case '#':
      return '3';
    case '$':
      return '4';
    case '%':
      return '5';
    case '^':
      return '6';
    case '&':
      return '7';
    case '*':
      return '8';
    case '(':
      return '9';
    case ')':
      return '0';
    case '_':
      return VK_OEM_MINUS;
    case '+':
      return VK_OEM_PLUS;
    case '{':
      return VK_OEM_4;
    case '}':
      return VK_OEM_6;
    case '|':
      return VK_OEM_5;
    case ':':
      return VK_OEM_1;
    case '"':
      return VK_OEM_7;
    case '~':
      return VK_OEM_3;
    case '<':
      return VK_OEM_COMMA;
    case '>':
      return VK_OEM_PERIOD;
    case '?':
      return VK_OEM_2;
    default:
      if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
        return static_cast<uint>(ch.unicode());
      }
      if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) {
        return static_cast<uint>(ch.unicode() - 'a' + 'A');
      }
      return 0;
  }
}

uint VkFromAsciiChar(QChar ch) {
  const ushort u = ch.unicode();
  if (u >= 'A' && u <= 'Z') {
    return u;
  }
  if (u >= 'a' && u <= 'z') {
    return static_cast<uint>(u - 'a' + 'A');
  }
  if (u >= '0' && u <= '9') {
    return u;
  }
  const uint vk_shift = VkFromShiftedText(ch);
  if (vk_shift) {
    return vk_shift;
  }
  switch (u) {
    case ' ':
      return VK_SPACE;
    case '\t':
      return VK_TAB;
    case '-':
      return VK_OEM_MINUS;
    case '=':
      return VK_OEM_PLUS;
    case '[':
      return VK_OEM_4;
    case ']':
      return VK_OEM_6;
    case '\\':
      return VK_OEM_5;
    case ';':
      return VK_OEM_1;
    case '\'':
      return VK_OEM_7;
    case ',':
      return VK_OEM_COMMA;
    case '.':
      return VK_OEM_PERIOD;
    case '/':
      return VK_OEM_2;
    case '`':
      return VK_OEM_3;
    default:
      return 0;
  }
}

uint VkFromQtKey(int key) {
  if (key == Qt::Key_Space) {
    return VK_SPACE;
  }
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    return static_cast<uint>('A' + (key - Qt::Key_A));
  }
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    return static_cast<uint>('0' + (key - Qt::Key_0));
  }

  switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      return VK_RETURN;
    case Qt::Key_Escape:
      return VK_ESCAPE;
    case Qt::Key_Backspace:
      return VK_BACK;
    case Qt::Key_Tab:
      return VK_TAB;
    case Qt::Key_Space:
      return VK_SPACE;
    case Qt::Key_Up:
      return VK_UP;
    case Qt::Key_Down:
      return VK_DOWN;
    case Qt::Key_Left:
      return VK_LEFT;
    case Qt::Key_Right:
      return VK_RIGHT;
    case Qt::Key_Home:
      return VK_HOME;
    case Qt::Key_End:
      return VK_END;
    case Qt::Key_PageUp:
      return VK_PRIOR;
    case Qt::Key_PageDown:
      return VK_NEXT;
    case Qt::Key_Insert:
      return VK_INSERT;
    case Qt::Key_Delete:
      return VK_DELETE;
    case Qt::Key_F1:
      return VK_F1;
    case Qt::Key_F2:
      return VK_F2;
    case Qt::Key_F3:
      return VK_F3;
    case Qt::Key_F4:
      return VK_F4;
    case Qt::Key_F5:
      return VK_F5;
    case Qt::Key_F6:
      return VK_F6;
    case Qt::Key_F7:
      return VK_F7;
    case Qt::Key_F8:
      return VK_F8;
    case Qt::Key_F9:
      return VK_F9;
    case Qt::Key_F10:
      return VK_F10;
    case Qt::Key_F11:
      return VK_F11;
    case Qt::Key_F12:
      return VK_F12;
    case Qt::Key_Shift:
      return VK_LSHIFT;
    case Qt::Key_Control:
      return VK_CONTROL;
    case Qt::Key_CapsLock:
      return VK_CAPITAL;
    case Qt::Key_ScrollLock:
      return VK_SCROLL;
    case Qt::Key_Pause:
      return VK_PAUSE;
    case Qt::Key_QuoteLeft:
      return VK_OEM_3;
    case Qt::Key_Minus:
      return VK_OEM_MINUS;
    case Qt::Key_Equal:
      return VK_OEM_PLUS;
    case Qt::Key_BracketLeft:
      return VK_OEM_4;
    case Qt::Key_BracketRight:
      return VK_OEM_6;
    case Qt::Key_Backslash:
      return VK_OEM_5;
    case Qt::Key_Semicolon:
      return VK_OEM_1;
    case Qt::Key_Apostrophe:
      return VK_OEM_7;
    case Qt::Key_Comma:
      return VK_OEM_COMMA;
    case Qt::Key_Period:
      return VK_OEM_PERIOD;
    case Qt::Key_Slash:
      return VK_OEM_2;
    default:
      return 0;
  }
}

bool HostImeModifierKey(int key) {
  switch (key) {
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
      return true;
    default:
      return false;
  }
}

uint ResolveModifierVk(const QKeyEvent& ev) {
  const int key = ev.key();
  const quint32 scan = ev.nativeScanCode();
  if (key == Qt::Key_Shift || key == Qt::Key_unknown) {
    const uint vk = VkFromScan(scan);
    if (vk == VK_LSHIFT || vk == VK_RSHIFT) {
      return vk;
    }
    return VK_LSHIFT;
  }
  if (key == Qt::Key_Control) {
    const uint vk = VkFromScan(scan);
    if (vk == VK_CONTROL) {
      return vk;
    }
    return VK_CONTROL;
  }
  if (key == Qt::Key_CapsLock) {
    return VK_CAPITAL;
  }
  return 0;
}

// True when typist Shift should affect this stroke (never Caps Lock alone).
bool HostTypistShiftHeld(const QKeyEvent& ev) {
  if (ev.key() == Qt::Key_CapsLock) {
    return false;
  }
  if (ev.modifiers().testFlag(Qt::KeyboardModifier::ShiftModifier)) {
    return true;
  }
  return IsShiftedSymbolQtKey(ev.key());
}

uint ResolveHostVk(const QKeyEvent& ev) {
  const int key = ev.key();

  if (key == Qt::Key_Space) {
    return VK_SPACE;
  }

  if (g_suppress_menu && !g_host_at101 &&
      (key == Qt::Key_Alt || key == Qt::Key_AltGr)) {
    return VK_MENU;
  }
  if (HostImeModifierKey(key)) {
    return 0;
  }

  if (key == Qt::Key_Shift || key == Qt::Key_Control || key == Qt::Key_CapsLock) {
    return ResolveModifierVk(ev);
  }

  if (key == Qt::Key_unknown || key == Qt::Key_Any) {
    if (ev.modifiers().testFlag(Qt::ShiftModifier)) {
      return ResolveModifierVk(ev);
    }
    if (ev.modifiers().testFlag(Qt::ControlModifier)) {
      return VK_CONTROL;
    }
    return VkFromScan(ev.nativeScanCode());
  }

  if (ev.modifiers() & Qt::KeypadModifier) {
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
      return VK_NUMPAD0 + static_cast<uint>(key - Qt::Key_0);
    }
    switch (key) {
      case Qt::Key_Asterisk:
        return VK_MULTIPLY;
      case Qt::Key_Plus:
        return VK_ADD;
      case Qt::Key_Minus:
        return VK_SUBTRACT;
      case Qt::Key_Period:
        return VK_DECIMAL;
      case Qt::Key_Slash:
        return VK_DIVIDE;
      case Qt::Key_Enter:
      case Qt::Key_Return:
        return VK_RETURN;
      default:
        break;
    }
  }

  if (HostTypistShiftHeld(ev)) {
    const QString text = ev.text();
    if (text.size() == 1) {
      const uint vk_text = VkFromShiftedText(text[0]);
      if (vk_text) {
        return vk_text;
      }
    }
    const uint vk_shifted = VkFromShiftedQtKey(key);
    if (vk_shifted) {
      return vk_shifted;
    }
  }

  // Prefer Qt key (layout VK) before native scancode (6377df2 scan-first broke Space on
  // AT101 when g_host_at101 was unset: wrong scan mapped to F2, never reached WinKeyIF).
  // VK_SPACE (0x20) must use an explicit test; 0x20 is a valid VK but easy to mishandle.
  const uint vk_qt = VkFromQtKey(key);
  if (vk_qt != 0) {
    return vk_qt;
  }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  const uint nvk = static_cast<uint>(ev.nativeVirtualKey());
  if (nvk >= 0x20 && nvk < 0x100) {
    return nvk;
  }
#endif

  const uint vk_scan = VkFromScan(ev.nativeScanCode());
  if (vk_scan != 0) {
    return vk_scan;
  }

  const QString text = ev.text();
  if (text.size() == 1) {
    return VkFromAsciiChar(text[0]);
  }
  return 0;
}

}  // namespace

namespace QtInput {

bool IsHostImeModifierKey(int key) { return HostImeModifierKey(key); }

void SetSuppressMenu(bool enabled) { g_suppress_menu = enabled; }

void SetHostAt101(bool enabled) { g_host_at101 = enabled; }

bool IsHostModifierVk(uint vk) {
  switch (vk) {
    case VK_MENU:
    case VK_CONTROL:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
    case VK_CAPITAL:
    case VK_SCROLL:
      return true;
    default:
      return false;
  }
}

uint VkFromKeyEvent(const QKeyEvent& ev) {
  if (ev.key() == Qt::Key_Space) {
    return VK_SPACE;
  }
  uint vk = ResolveHostVk(ev);
  if (vk == 0) {
    return 0;
  }
  if (g_host_at101 && vk == VK_MENU) {
    return 0;
  }
  if (g_host_at101 && vk == VK_SHIFT) {
    vk = VK_LSHIFT;
  }
  return vk;
}

uint VkFromAsciiChar(QChar ch) {
  return ::VkFromAsciiChar(ch);
}

uint32 KeyDataForAsciiChar(QChar ch) {
  uint32 kd = 0;
  if (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) {
    kd |= M88_KEYDATA_HOST_SHIFT;
  } else if (VkFromAsciiChar(ch) != 0 && !(ch >= QLatin1Char('a') && ch <= QLatin1Char('z')) &&
             !(ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
    const uint vk_shift = VkFromShiftedText(ch);
    if (vk_shift && vk_shift != static_cast<uint>(ch.unicode())) {
      kd |= M88_KEYDATA_HOST_SHIFT;
    }
  }
  return kd;
}

uint32 KeyExtended(const QKeyEvent& ev) {
  if (ev.modifiers() & Qt::KeypadModifier) {
    switch (ev.key()) {
      case Qt::Key_Enter:
      case Qt::Key_Return:
      case Qt::Key_Insert:
      case Qt::Key_End:
      case Qt::Key_Down:
      case Qt::Key_PageDown:
      case Qt::Key_Left:
      case Qt::Key_Right:
      case Qt::Key_Home:
      case Qt::Key_Up:
      case Qt::Key_PageUp:
      case Qt::Key_Delete:
        return M88_KEYDATA_EXTENDED;
      default:
        break;
    }
  }

  switch (ev.key()) {
    case Qt::Key_Insert:
    case Qt::Key_Delete:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Left:
    case Qt::Key_Right:
      return M88_KEYDATA_EXTENDED;
    default:
      break;
  }

  return 0;
}

uint32 KeyDataFromEvent(const QKeyEvent& ev) {
  uint32 data = KeyExtended(ev);
  if (HostTypistShiftHeld(ev)) {
    data |= M88_KEYDATA_HOST_SHIFT;
  }
  return data;
}

LetterShiftAdjust LetterShiftAdjustFor(const QKeyEvent& /*ev*/) {
  return LetterShiftAdjust::None;
}

}  // namespace QtInput
