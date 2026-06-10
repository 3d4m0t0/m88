#pragma once

#include "../pc88/config.h"

#include <cstdint>

namespace Pc88KeyFixup {

void SetEnabled(bool on);
bool IsEnabled();

void SetHostKeyboard(PC8801::Config::KeyType host);
void SetGuestKeyboard(PC8801::Config::KeyType guest);
PC8801::Config::KeyType GuestKeyboard();

struct KeyMap {
  uint8_t vk = 0;
  bool shift = false;
  bool mask_host_shift = false;
  bool guest_shift = false;  // set PC-88 Shift for this stroke (row07 ( ) etc.)
  bool swallow = false;  // guest_vk NONE: do not send any key to PC-88
};

bool MapKey(PC8801::Config::KeyType host, uint vk, bool shift, KeyMap* out);
bool MapKey(uint vk, bool shift, KeyMap* out);

// Load m88_keyfix.ini (see M88LoadKeyFixup). Returns true if a file was read.
bool LoadFromFile(const char* path);

// M88_KEYFIX, <dir-of-m88.ini>/m88_keyfix.ini, then ./m88_keyfix.ini. No rules if none found.
bool LoadStartup(const char* m88_ini_path);

// Default install locations (not M88_KEYFIX). Write path: beside m88.ini or ./m88_keyfix.ini.
// CreateDefaultIni writes embedded m88_keyfix.ini (repo standard); M88_KEYFIX_TEMPLATE overrides.
bool DefaultIniExists(const char* m88_ini_path);
bool ResolveDefaultIniPath(const char* m88_ini_path, char* out, size_t out_sz);
bool CreateDefaultIni(const char* dest_path, const char* m88_ini_path = nullptr);

// Suppress keyfix stderr until FlushDeferredLogs() (startup log ordering).
void SetDeferLogs(bool defer);
void LogMessage(const char* fmt, ...);
void FlushDeferredLogs();

}  // namespace Pc88KeyFixup
