#pragma once

#include "../pc88/config.h"
#include "winkeys.h"

// PC-8801 FH+ keyboard matrix indices used in WinKeyIF keystate / KeyTable*.
//
// Original M88 (Win32) reused Win32 VK_NUMPAD0..9 (0x60..0x69) for matrix rows 00
// and 01. That is NOT the host PC's numeric keypad; it is the scan-code slot M88
// assigned to each matrix cell. Use Pc88MatrixVk:: for guest-aware code and INI.
//
// Matrix reference: http://www.maroon.dti.ne.jp/youkan/pc88/kbd.html

#define VK_PC88_KP0 VK_NUMPAD0
#define VK_PC88_KP1 VK_NUMPAD1
#define VK_PC88_KP2 VK_NUMPAD2
#define VK_PC88_KP3 VK_NUMPAD3
#define VK_PC88_KP4 VK_NUMPAD4
#define VK_PC88_KP5 VK_NUMPAD5
#define VK_PC88_KP6 VK_NUMPAD6
#define VK_PC88_KP7 VK_NUMPAD7

#define VK_PC88_R01_8 VK_NUMPAD8
#define VK_PC88_R01_9 VK_NUMPAD9
#define VK_PC88_R01_MUL VK_MULTIPLY
#define VK_PC88_R01_ADD VK_ADD
#define VK_PC88_R01_EQ 0x92

#define VK_PC88_AT_101 0xDB
#define VK_PC88_AT_106 0xC0
#define VK_PC88_CIRC_101 0xBB
#define VK_PC88_CIRC_106 0xDE
#define VK_PC88_LBRA_101 0xDD
#define VK_PC88_LBRA_106 0xDB
#define VK_PC88_RBRA_101 0xC0
#define VK_PC88_RBRA_106 0xDD
#define VK_PC88_BSL 0xDC
#define VK_PC88_COLON 0xBA
#define VK_PC88_SEM_101 0xDE
#define VK_PC88_SEM_106 0xBB
#define VK_PC88_COMMA 0xBC
#define VK_PC88_PERIOD 0xBE
#define VK_PC88_SLASH 0xBF
#define VK_PC88_UNDERSCORE 0xE2

// Default INI aliases (AT106); keyfix resolves per SetGuestKeyboard().
#define VK_PC88_AT VK_PC88_AT_106
#define VK_PC88_CIRC VK_PC88_CIRC_106
#define VK_PC88_LBRA VK_PC88_LBRA_106
#define VK_PC88_RBRA VK_PC88_RBRA_106
#define VK_PC88_SEMICOLON VK_PC88_SEM_106

namespace Pc88MatrixVk {

using KeyType = PC8801::Config::KeyType;

inline uint8_t At(KeyType guest) {
  return guest == KeyType::AT101 ? VK_PC88_AT_101 : VK_PC88_AT_106;
}
inline uint8_t Circ(KeyType guest) {
  return guest == KeyType::AT101 ? VK_PC88_CIRC_101 : VK_PC88_CIRC_106;
}
inline uint8_t Lbra(KeyType guest) {
  return guest == KeyType::AT101 ? VK_PC88_LBRA_101 : VK_PC88_LBRA_106;
}
inline uint8_t Rbra(KeyType guest) {
  return guest == KeyType::AT101 ? VK_PC88_RBRA_101 : VK_PC88_RBRA_106;
}
inline uint8_t Bsl(KeyType /*guest*/) { return VK_PC88_BSL; }
inline uint8_t Colon(KeyType /*guest*/) { return VK_PC88_COLON; }
inline uint8_t Semicolon(KeyType guest) {
  return guest == KeyType::AT101 ? VK_PC88_SEM_101 : VK_PC88_SEM_106;
}
inline uint8_t Comma(KeyType /*guest*/) { return VK_PC88_COMMA; }
inline uint8_t Period(KeyType /*guest*/) { return VK_PC88_PERIOD; }
inline uint8_t Slash(KeyType /*guest*/) { return VK_PC88_SLASH; }
inline uint8_t Underscore(KeyType /*guest*/) { return VK_PC88_UNDERSCORE; }

}  // namespace Pc88MatrixVk
