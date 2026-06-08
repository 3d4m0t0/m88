#include "qt_input.h"

#include "../linux/keyboard_vk.h"
#include "../linux_compat/winkeys.h"

#include <QKeyEvent>

namespace {

bool g_suppress_menu = false;

// US-101 style shifted character -> base key VK (Shift is sent separately).
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

uint ShiftedQtKeyToVk(int key) {
  switch (key) {
    case Qt::Key_AsciiTilde:
    case Qt::Key_QuoteLeft:
      return VK_OEM_3;
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
    case Qt::Key_Plus:
    case Qt::Key_Equal:
      return VK_OEM_PLUS;
    case Qt::Key_Colon:
    case Qt::Key_Semicolon:
      return VK_OEM_1;
    case Qt::Key_QuoteDbl:
    case Qt::Key_Apostrophe:
      return VK_OEM_7;
    case Qt::Key_Less:
    case Qt::Key_Comma:
      return VK_OEM_COMMA;
    case Qt::Key_Greater:
    case Qt::Key_Period:
      return VK_OEM_PERIOD;
    case Qt::Key_Question:
    case Qt::Key_Slash:
      return VK_OEM_2;
    case Qt::Key_BraceLeft:
    case Qt::Key_BracketLeft:
      return VK_OEM_4;
    case Qt::Key_BraceRight:
    case Qt::Key_BracketRight:
      return VK_OEM_6;
    case Qt::Key_Bar:
    case Qt::Key_Backslash:
      return VK_OEM_5;
    case Qt::Key_Underscore:
    case Qt::Key_Minus:
      return VK_OEM_MINUS;
    default:
      return 0;
  }
}

uint VkFromQtKey(int key) {
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
    case Qt::Key_Henkan:
    case Qt::Key_Muhenkan:
      return 0;
    case Qt::Key_Zenkaku_Hankaku:
    case Qt::Key_Zenkaku:
    case Qt::Key_Hankaku:
      return VK_OEM_3;
    case Qt::Key_yen:
      return VK_OEM_5;
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

// nativeScanCode is evdev on Wayland and usually (evdev+8) on X11; pick the VK that
// matches the Qt key when possible.
uint VkFromNativeScan(quint32 scan, int qt_key) {
  if (HostImeModifierKey(qt_key)) {
    return 0;
  }
  if (scan == 0) {
    return 0;
  }
  const uint vk_hi = M88Input::EvdevScancodeToVk(static_cast<uint>(scan));
  const uint vk_lo =
      scan >= 8 ? M88Input::EvdevScancodeToVk(static_cast<uint>(scan - 8)) : 0;

  uint vk_qt = 0;
  if (qt_key >= Qt::Key_A && qt_key <= Qt::Key_Z) {
    vk_qt = static_cast<uint>('A' + (qt_key - Qt::Key_A));
  } else if (qt_key >= Qt::Key_0 && qt_key <= Qt::Key_9) {
    vk_qt = static_cast<uint>('0' + (qt_key - Qt::Key_0));
  }

  if (vk_qt) {
    if (vk_lo == vk_qt || vk_hi == vk_qt) {
      return vk_lo == vk_qt ? vk_lo : vk_hi;
    }
    return vk_qt;
  }

  if (vk_lo && vk_lo != VK_MENU) {
    return vk_lo;
  }
  if (vk_hi && vk_hi != VK_MENU) {
    return vk_hi;
  }
  return 0;
}

}  // namespace

namespace QtInput {

bool IsHostImeModifierKey(int key) { return HostImeModifierKey(key); }

void SetSuppressMenu(bool enabled) { g_suppress_menu = enabled; }

bool IsHostModifierVk(uint vk) {
  switch (vk) {
    case VK_MENU:
    case VK_CONTROL:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_SHIFT:
    case VK_CONVERT:
    case VK_NONCONVERT:
      return true;
    default:
      return false;
  }
}

namespace {

uint ResolveHostVk(const QKeyEvent& ev, bool* shift_held) {
  const int key = ev.key();
  if (shift_held) {
    *shift_held = ev.modifiers().testFlag(Qt::KeyboardModifier::ShiftModifier);
  }

  if (g_suppress_menu && (key == Qt::Key_Alt || key == Qt::Key_AltGr)) {
    return VK_MENU;
  }

  if (HostImeModifierKey(key)) {
    return 0;
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

  if (*shift_held) {
    const QString text = ev.text();
    if (text.size() == 1) {
      const uint vk_text = VkFromShiftedText(text[0]);
      if (vk_text) {
        return vk_text;
      }
    }
    const uint shifted = ShiftedQtKeyToVk(key);
    if (shifted) {
      return shifted;
    }
  }

  const uint vk_qt = VkFromQtKey(key);
  if (vk_qt) {
    return vk_qt;
  }

  uint vk = VkFromNativeScan(ev.nativeScanCode(), key);
  if (vk == VK_MENU) {
    return g_suppress_menu ? VK_MENU : 0;
  }
  return vk;
}

}  // namespace

uint VkFromKeyEvent(const QKeyEvent& ev) {
  bool shift_held = false;
  return ResolveHostVk(ev, &shift_held);
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
        return 1u << 24;
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
      return 1u << 24;
    default:
      break;
  }

  return 0;
}

uint32 KeyDataFromEvent(const QKeyEvent& ev) {
  uint32 data = KeyExtended(ev);
  if (ev.modifiers().testFlag(Qt::KeyboardModifier::ShiftModifier)) {
    data |= M88_KEYDATA_HOST_SHIFT;
  }
  return data;
}

LetterShiftAdjust LetterShiftAdjustFor(const QKeyEvent& ev) {
  const int key = ev.key();
  if (key < Qt::Key_A || key > Qt::Key_Z) {
    return LetterShiftAdjust::None;
  }
  const QString text = ev.text();
  if (text.size() != 1 || !text[0].isLetter()) {
    return LetterShiftAdjust::None;
  }
  const bool shift_held = ev.modifiers().testFlag(Qt::KeyboardModifier::ShiftModifier);
  if (text[0].isUpper() && !shift_held) {
    return LetterShiftAdjust::AddShift;
  }
  if (text[0].isLower() && shift_held) {
    return LetterShiftAdjust::RemoveShift;
  }
  return LetterShiftAdjust::None;
}

}  // namespace QtInput
