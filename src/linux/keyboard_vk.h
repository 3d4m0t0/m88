#pragma once

#include "types.h"

namespace M88Input {

// When true (US/101 host), do not map host Alt/Compose scancodes to PC-88 GRPH (VK_MENU).
void SetHostAt101(bool enabled);

// Linux evdev scancode (input-event-codes.h KEY_* values).
uint EvdevScancodeToVk(uint code);

// Linux evdev KEY_* code (Wayland nativeScanCode). XCB uses QtNativeScanToVk in qt_input.
uint NativeScanToVk(uint scan);

}  // namespace M88Input
