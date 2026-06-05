#pragma once

#include <SDL.h>

#include "types.h"

namespace PC8801 {
class WinKeyIF;
}

namespace M88Input {

uint SdlSymToVk(SDL_Keycode sym);
uint SdlEventToVk(const SDL_KeyboardEvent& key);
uint32 SdlKeyData(const SDL_KeyboardEvent& key);

void HandleKeyDown(PC8801::WinKeyIF& keyif, const SDL_KeyboardEvent& key);
void HandleKeyUp(PC8801::WinKeyIF& keyif, const SDL_KeyboardEvent& key);

}  // namespace M88Input
