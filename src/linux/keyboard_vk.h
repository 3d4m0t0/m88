#pragma once

#include "types.h"

#include <SDL.h>

namespace M88Input {

// Linux evdev scancode (input-event-codes.h KEY_* values).
uint EvdevScancodeToVk(uint code);

// Linux evdev KEY_* code (Wayland nativeScanCode). XCB uses QtNativeScanToVk in qt_input.
uint NativeScanToVk(uint scan);

uint SdlScancodeToVk(SDL_Scancode sc);

}  // namespace M88Input
