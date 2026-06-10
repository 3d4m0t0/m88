#include "keyboard_vk.h"

#include "winkeys.h"

#include <linux/input-event-codes.h>

namespace M88Input {

bool g_host_at101 = false;

void SetHostAt101(bool enabled) { g_host_at101 = enabled; }

uint EvdevScancodeToVk(uint code) {
  switch (code) {
    case KEY_A:
      return 'A';
    case KEY_B:
      return 'B';
    case KEY_C:
      return 'C';
    case KEY_D:
      return 'D';
    case KEY_E:
      return 'E';
    case KEY_F:
      return 'F';
    case KEY_G:
      return 'G';
    case KEY_H:
      return 'H';
    case KEY_I:
      return 'I';
    case KEY_J:
      return 'J';
    case KEY_K:
      return 'K';
    case KEY_L:
      return 'L';
    case KEY_M:
      return 'M';
    case KEY_N:
      return 'N';
    case KEY_O:
      return 'O';
    case KEY_P:
      return 'P';
    case KEY_Q:
      return 'Q';
    case KEY_R:
      return 'R';
    case KEY_S:
      return 'S';
    case KEY_T:
      return 'T';
    case KEY_U:
      return 'U';
    case KEY_V:
      return 'V';
    case KEY_W:
      return 'W';
    case KEY_X:
      return 'X';
    case KEY_Y:
      return 'Y';
    case KEY_Z:
      return 'Z';

    case KEY_0:
      return '0';
    case KEY_1:
      return '1';
    case KEY_2:
      return '2';
    case KEY_3:
      return '3';
    case KEY_4:
      return '4';
    case KEY_5:
      return '5';
    case KEY_6:
      return '6';
    case KEY_7:
      return '7';
    case KEY_8:
      return '8';
    case KEY_9:
      return '9';

    case KEY_ENTER:
      return VK_RETURN;
    case KEY_ESC:
      return VK_ESCAPE;
    case KEY_BACKSPACE:
      return VK_BACK;
    case KEY_TAB:
      return VK_TAB;
    case KEY_SPACE:
      return VK_SPACE;

    case KEY_UP:
      return VK_UP;
    case KEY_DOWN:
      return VK_DOWN;
    case KEY_LEFT:
      return VK_LEFT;
    case KEY_RIGHT:
      return VK_RIGHT;
    case KEY_HOME:
      return VK_HOME;
    case KEY_END:
      return VK_END;
    case KEY_PAGEUP:
      return VK_PRIOR;
    case KEY_PAGEDOWN:
      return VK_NEXT;
    case KEY_INSERT:
      return VK_INSERT;
    case KEY_DELETE:
      return VK_DELETE;

    case KEY_F1:
      return VK_F1;
    case KEY_F2:
      return VK_F2;
    case KEY_F3:
      return VK_F3;
    case KEY_F4:
      return VK_F4;
    case KEY_F5:
      return VK_F5;
    case KEY_F6:
      return VK_F6;
    case KEY_F7:
      return VK_F7;
    case KEY_F8:
      return VK_F8;
    case KEY_F9:
      return VK_F9;
    case KEY_F10:
      return VK_F10;
    case KEY_F11:
      return VK_F11;
    case KEY_F12:
      return VK_F12;

    case KEY_LEFTSHIFT:
      return VK_LSHIFT;
    case KEY_RIGHTSHIFT:
      return VK_RSHIFT;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
      return VK_CONTROL;
    case KEY_COMPOSE:
      if (!g_host_at101) {
        return VK_MENU;  // JIS 106 GRPH on some layouts
      }
      return 0;
    case KEY_RIGHTALT:
      if (!g_host_at101) {
        return VK_MENU;
      }
      return 0;
    case KEY_LEFTALT:
      return 0;  // never PC-88 GRPH
    case KEY_CAPSLOCK:
      return VK_CAPITAL;
    case KEY_SCROLLLOCK:
      return VK_SCROLL;
    case KEY_PAUSE:
      return VK_PAUSE;

    case KEY_GRAVE:
      return VK_OEM_3;
    case KEY_MINUS:
      return VK_OEM_MINUS;
    case KEY_EQUAL:
      return VK_OEM_PLUS;
    case KEY_LEFTBRACE:
      return VK_OEM_4;
    case KEY_RIGHTBRACE:
      return VK_OEM_6;
    case KEY_BACKSLASH:
      return VK_OEM_5;
    case KEY_SEMICOLON:
      return VK_OEM_1;
    case KEY_APOSTROPHE:
      return VK_OEM_7;
    case KEY_COMMA:
      return VK_OEM_COMMA;
    case KEY_DOT:
      return VK_OEM_PERIOD;
    case KEY_SLASH:
      return VK_OEM_2;
    case KEY_102ND:
      return VK_OEM_102;
    case KEY_RO:
      return VK_OEM_3;  // JP 106 @ / hankaku
    case KEY_HENKAN:
      return VK_CONVERT;
    case KEY_MUHENKAN:
      return VK_NONCONVERT;
    case KEY_KATAKANAHIRAGANA:
      return VK_OEM_3;

    case KEY_KP0:
      return VK_NUMPAD0;
    case KEY_KP1:
      return VK_NUMPAD1;
    case KEY_KP2:
      return VK_NUMPAD2;
    case KEY_KP3:
      return VK_NUMPAD3;
    case KEY_KP4:
      return VK_NUMPAD4;
    case KEY_KP5:
      return VK_NUMPAD5;
    case KEY_KP6:
      return VK_NUMPAD6;
    case KEY_KP7:
      return VK_NUMPAD7;
    case KEY_KP8:
      return VK_NUMPAD8;
    case KEY_KP9:
      return VK_NUMPAD9;
    case KEY_KPASTERISK:
      return VK_MULTIPLY;
    case KEY_KPPLUS:
      return VK_ADD;
    case KEY_KPMINUS:
      return VK_SUBTRACT;
    case KEY_KPDOT:
      return VK_DECIMAL;
    case KEY_KPSLASH:
      return VK_DIVIDE;
    default:
      return 0;
  }
}

uint NativeScanToVk(uint scan) {
  if (scan == 0) {
    return 0;
  }
  // Wayland: nativeScanCode is a Linux evdev KEY_* code.
  return EvdevScancodeToVk(scan);
}

}  // namespace M88Input
