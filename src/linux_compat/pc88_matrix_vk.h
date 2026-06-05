#pragma once

#include "winkeys.h"

// PC-8801 FH+ keyboard matrix indices used in WinKeyIF keystate / KeyTable*.
//
// Original M88 (Win32) reused Win32 VK_NUMPAD0..9 (0x60..0x69) for matrix rows 00
// and 01. That is NOT the host PC's numeric keypad; it is the scan-code slot M88
// assigned to each matrix cell. Use the VK_PC88_* names below in new code and INI.
//
// Matrix reference: http://www.maroon.dti.ne.jp/youkan/pc88/kbd.html
//   row 00: tenkey 0..7
//   row 01: tenkey upper 8, 9, *, +, = (not main-row ( ) ? see row 07)

// Row 00 ? numeric keypad block (matrix 00h, D0..D7)
#define VK_PC88_KP0 VK_NUMPAD0
#define VK_PC88_KP1 VK_NUMPAD1
#define VK_PC88_KP2 VK_NUMPAD2
#define VK_PC88_KP3 VK_NUMPAD3
#define VK_PC88_KP4 VK_NUMPAD4
#define VK_PC88_KP5 VK_NUMPAD5
#define VK_PC88_KP6 VK_NUMPAD6
#define VK_PC88_KP7 VK_NUMPAD7

// Row 01 ? tenkey upper strip (matrix 01h, D0..D4); not host VK_NUMPAD*
#define VK_PC88_R01_8 VK_NUMPAD8
#define VK_PC88_R01_9 VK_NUMPAD9
#define VK_PC88_R01_MUL VK_MULTIPLY
#define VK_PC88_R01_ADD VK_ADD
#define VK_PC88_R01_EQ 0x92

// Row 02 / 05 / 07 (AT101 KeyTable101; Win32 VK slot in keystate)
#define VK_PC88_AT 0xDB      // row02 @
#define VK_PC88_CIRC 0xBB    // row05 ^ (circumflex)
#define VK_PC88_LBRA 0xDD    // row05 [
#define VK_PC88_RBRA 0xC0    // row05 ]
#define VK_PC88_BSL 0xDC     // row05 backslash
#define VK_PC88_COLON 0xBA   // row07 :  (88_COL in INI; avoid macro name COL)
#define VK_PC88_SEMICOLON 0xDE  // row07 ;  (88_SEM)
#define VK_PC88_COMMA 0xBC   // row07 ,  (88_COMM)
#define VK_PC88_PERIOD 0xBE  // row07 .  (88_DOT)
#define VK_PC88_SLASH 0xBF   // row07 /  (88_SLASH)
#define VK_PC88_UNDERSCORE 0xE2  // row07 _  (88_USCR)
