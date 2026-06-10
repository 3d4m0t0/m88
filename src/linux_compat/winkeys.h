#pragma once

// Virtual-key codes used by WinKeyIF (subset of Win32 winuser.h).
#define VK_LBUTTON 0x01
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_F13 0x7C
#define VK_F14 0x7D
#define VK_F15 0x7E
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_SCROLL 0x91
#define VK_CONVERT 0x1C
#define VK_NONCONVERT 0x1D
#define VK_ACCEPT 0x1E
#define VK_HELP 0x2F
#define VK_CLEAR 0x0C

// US keyboard OEM virtual-key codes (Win32 winuser.h).
#define VK_OEM_1 0xBA     // ;:
#define VK_OEM_PLUS 0xBB  // =+
#define VK_OEM_COMMA 0xBC // ,<
#define VK_OEM_MINUS 0xBD // -_
#define VK_OEM_PERIOD 0xBE  // .>
#define VK_OEM_2 0xBF     // /?
#define VK_OEM_3 0xC0     // `~
#define VK_OEM_4 0xDB     // [{
#define VK_OEM_5 0xDC     // \|
#define VK_OEM_6 0xDD     // ]}
#define VK_OEM_7 0xDE     // '"
#define VK_OEM_102 0xE2   // extra key (102-key / some layouts)

// WinKeyIF::KeyDown keydata bits (Qt/SDL frontends on Linux)
#define M88_KEYDATA_EXTENDED (1u << 24)
#define M88_KEYDATA_HOST_SHIFT (1u << 25)
#define M88_KEYDATA_FIXUP_MASK (1u << 26)  // keyfix mask: guest key is unshifted
#define M88_KEYDATA_GUEST_SHIFT (1u << 27)  // PC-88 row08 SHIFT + host LSHIFT (IME sutegana)
#define M88_KEYDATA_FH_SHIFT (1u << 28)     // PC-88 row08 SHIFT only (IME ゜ on row07 .)
