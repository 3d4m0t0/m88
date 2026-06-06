#include "linux_input.h"

#include "../win32/WinKeyIF.h"
#include "keyboard_vk.h"
#include "keyboard_vk_sdl.h"
#include "winkeys.h"

namespace M88Input {

uint SdlEventToVk(const SDL_KeyboardEvent& key) {
  const uint vk_scancode = SdlScancodeToVk(key.keysym.scancode);
  if (vk_scancode) {
    return vk_scancode;
  }
  return SdlSymToVk(key.keysym.sym);
}

uint32 SdlKeyData(const SDL_KeyboardEvent& key) {
  uint32 data = 0;
  switch (key.keysym.scancode) {
    case SDL_SCANCODE_INSERT:
    case SDL_SCANCODE_DELETE:
    case SDL_SCANCODE_HOME:
    case SDL_SCANCODE_END:
    case SDL_SCANCODE_PAGEUP:
    case SDL_SCANCODE_PAGEDOWN:
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_RCTRL:
    case SDL_SCANCODE_RALT:
      data = M88_KEYDATA_EXTENDED;
      break;
    default:
      break;
  }
  if (key.keysym.mod & KMOD_SHIFT) {
    data |= M88_KEYDATA_HOST_SHIFT;
  }
  return data;
}

void HandleKeyDown(PC8801::WinKeyIF& keyif, const SDL_KeyboardEvent& key) {
  if (key.repeat) {
    return;
  }
  const uint vk = SdlEventToVk(key);
  if (!vk) {
    return;
  }
  keyif.KeyDown(vk, SdlKeyData(key));
}

void HandleKeyUp(PC8801::WinKeyIF& keyif, const SDL_KeyboardEvent& key) {
  const uint vk = SdlEventToVk(key);
  if (!vk) {
    return;
  }
  keyif.KeyUp(vk, SdlKeyData(key));
}

uint SdlSymToVk(SDL_Keycode sym) {
  if (sym >= SDLK_a && sym <= SDLK_z) {
    return static_cast<uint>('A' + (sym - SDLK_a));
  }
  if (sym >= SDLK_0 && sym <= SDLK_9) {
    return static_cast<uint>('0' + (sym - SDLK_0));
  }

  switch (sym) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      return VK_RETURN;
    case SDLK_ESCAPE:
      return VK_ESCAPE;
    case SDLK_BACKSPACE:
      return VK_BACK;
    case SDLK_TAB:
      return VK_TAB;
    case SDLK_SPACE:
      return VK_SPACE;
    case SDLK_UP:
      return VK_UP;
    case SDLK_DOWN:
      return VK_DOWN;
    case SDLK_LEFT:
      return VK_LEFT;
    case SDLK_RIGHT:
      return VK_RIGHT;
    case SDLK_HOME:
      return VK_HOME;
    case SDLK_END:
      return VK_END;
    case SDLK_PAGEUP:
      return VK_PRIOR;
    case SDLK_PAGEDOWN:
      return VK_NEXT;
    case SDLK_INSERT:
      return VK_INSERT;
    case SDLK_DELETE:
      return VK_DELETE;
    case SDLK_F1:
      return VK_F1;
    case SDLK_F2:
      return VK_F2;
    case SDLK_F3:
      return VK_F3;
    case SDLK_F4:
      return VK_F4;
    case SDLK_F5:
      return VK_F5;
    case SDLK_F6:
      return VK_F6;
    case SDLK_F7:
      return VK_F7;
    case SDLK_F8:
      return VK_F8;
    case SDLK_F9:
      return VK_F9;
    case SDLK_F10:
      return VK_F10;
    case SDLK_F11:
      return VK_F11;
    case SDLK_F12:
      return VK_F12;
    case SDLK_LSHIFT:
      return VK_LSHIFT;
    case SDLK_RSHIFT:
      return VK_RSHIFT;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
      return VK_CONTROL;
    case SDLK_LALT:
    case SDLK_RALT:
      return VK_MENU;
    case SDLK_CAPSLOCK:
      return VK_CAPITAL;
    case SDLK_SCROLLLOCK:
      return VK_SCROLL;
    case SDLK_PAUSE:
      return VK_PAUSE;
    case SDLK_KP_0:
      return VK_NUMPAD0;
    case SDLK_KP_1:
      return VK_NUMPAD1;
    case SDLK_KP_2:
      return VK_NUMPAD2;
    case SDLK_KP_3:
      return VK_NUMPAD3;
    case SDLK_KP_4:
      return VK_NUMPAD4;
    case SDLK_KP_5:
      return VK_NUMPAD5;
    case SDLK_KP_6:
      return VK_NUMPAD6;
    case SDLK_KP_7:
      return VK_NUMPAD7;
    case SDLK_KP_8:
      return VK_NUMPAD8;
    case SDLK_KP_9:
      return VK_NUMPAD9;
    case SDLK_KP_MULTIPLY:
      return VK_MULTIPLY;
    case SDLK_KP_PLUS:
      return VK_ADD;
    case SDLK_KP_MINUS:
      return VK_SUBTRACT;
    case SDLK_KP_PERIOD:
      return VK_DECIMAL;
    case SDLK_KP_DIVIDE:
      return VK_DIVIDE;
    case SDLK_BACKQUOTE:
      return VK_OEM_3;
    case SDLK_MINUS:
      return VK_OEM_MINUS;
    case SDLK_EQUALS:
      return VK_OEM_PLUS;
    case SDLK_LEFTBRACKET:
      return VK_OEM_4;
    case SDLK_RIGHTBRACKET:
      return VK_OEM_6;
    case SDLK_BACKSLASH:
      return VK_OEM_5;
    case SDLK_SEMICOLON:
      return VK_OEM_1;
    case SDLK_QUOTE:
      return VK_OEM_7;
    case SDLK_COMMA:
      return VK_OEM_COMMA;
    case SDLK_PERIOD:
      return VK_OEM_PERIOD;
    case SDLK_SLASH:
      return VK_OEM_2;
    default:
      break;
  }

  return 0;
}

}  // namespace M88Input
