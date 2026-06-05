#pragma once

#include "../pc88/config.h"
#include "half_kana_ime.h"

#include <cstdint>
#include <vector>

namespace Pc88HostChar {

void SetHostKeyboardType(PC8801::Config::KeyType host);
void SetGuestKeyboardType(PC8801::Config::KeyType guest);

struct JisTap {
  uint8_t vk = 0;
  bool shift = false;
  uint32_t keydata = 0;
};

// True for punctuation that must be remapped (not 0-9, A-Z, space, -, =).
bool UsesCharTapPath(uint32_t codepoint);

// Map OS character -> guest matrix key tap.
bool CharToGuestTap(uint32_t codepoint, JisTap* out);

bool HostKeyToGuestTap(uint vk, bool shift, JisTap* out);

void AppendTapDown(const JisTap& tap, std::vector<HalfKanaIme::KeyStroke>* strokes);
void AppendTapUp(const JisTap& tap, std::vector<HalfKanaIme::KeyStroke>* strokes);

}  // namespace Pc88HostChar
