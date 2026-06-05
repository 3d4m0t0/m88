#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace PC8801 {
class Config;
class WinKeyIF;
}

// PC-8801 half-width kana via host IME commit -> WinKeyIF key strokes (106-key kana mode).
namespace HalfKanaIme {

struct KeyStroke {
  uint8_t vk = 0;
  bool down = true;
  uint32_t keydata = 0;  // extended bit (1<<24) when needed
};

// Hiragana / fullwidth katakana commit -> halfwidth katakana (U+FF61..FF9F).
// Returns false if nothing to inject (e.g. ASCII-only).
bool CommitUtf8ToHalfKana(const char* utf8, std::vector<uint16_t>* out_hw);

// Halfwidth katakana code units -> PC-88 key strokes (AT106 matrix, kana lock).
void HalfKanaToKeyStrokes(const std::vector<uint16_t>& hw, std::vector<KeyStroke>* out);

// Queue strokes; call Pump() from the main thread each frame.
void InjectBeginSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg);
void InjectEnqueue(const std::vector<KeyStroke>& strokes);
void InjectPump(PC8801::WinKeyIF* keyif);
void InjectEndSession(PC8801::WinKeyIF* keyif, const PC8801::Config* cfg);
bool InjectBusy();

}  // namespace HalfKanaIme
