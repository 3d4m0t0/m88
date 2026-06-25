#pragma once

namespace PC8801 {
class Config;
}

class PC88;

// Defaults from src/win32/88config.cpp (LoadConfig with applydefault=true).
void M88SetDefaultConfig(PC8801::Config* cfg);

// Snapshot factory defaults without changing the running process globals.
void M88GetDefaultConfig(PC8801::Config* cfg, bool* wayland_idle = nullptr,
                         bool* ime_kana = nullptr);

// Applies WinUI::ApplyConfig() core settings, then PC88::ApplyConfig().
void M88ApplyConfig(PC88* pc88, PC8801::Config* cfg);

// Serial / bus mouse UI is not verified on the Linux Qt port yet.
inline bool M88MouseInputAvailable() { return false; }

// Parse keyboard layout name: 101/104/at101, 106/at106, 98/pc98. Returns -1 on failure.
int M88ParseKeyboardType(const char* name);

// Detect host keyboard layout (setxkbmap / localectl / locale). Sets cfg->keytype.
void M88ApplyDetectedKeyboard(PC8801::Config* cfg);

// True when KeyboardType was loaded from m88.ini (skip auto-detection).
bool M88IniHasHostKeyboard();

// Call when --keyboard overrides auto-detection (defers the keyboard log line).
void M88NoteKeyboardCliOverride();

const char* M88KeyboardTypeName(int keytype);

// Deferred startup lines (keyboard / keyfix); emit after machine/video/sound.
void M88LogKeyboard(const PC8801::Config* cfg);
void M88LogKeyFix();

// Host->PC-88 key remaps (m88.ini KeyFix=1, rules in m88_keyfix.ini). m88_ini_path may be "".
// If m88_keyfix.ini is missing, creates it and sets KeyFix=1 in m88.ini (cfg + path required to save).
bool M88KeyFixEnabled();
void M88LoadKeyFixup(const char* m88_ini_path, PC8801::Config* cfg = nullptr);

// Load KeyboardType / UseArrowForTenKey from an M88 Windows-compatible INI file.
// Returns true if the file was opened and parsed (missing keys keep current values).
bool M88LoadConfigFile(PC8801::Config* cfg, const char* path);

// Load ./m88.ini from the current working directory when present.
void M88LoadDefaultConfigFile(PC8801::Config* cfg);

// Load INI if present; otherwise write defaults (see M88LoadStartupConfig).
bool M88SaveConfigFile(const PC8801::Config* cfg, const char* path);

// Load a specific INI path (defaults + parse + finalize). Returns false if missing.
bool M88LoadConfigAtPath(PC8801::Config* cfg, const char* path);

// Resolve relative config paths to an absolute path (for stable save/load after chdir).
void M88CanonicalConfigPath(const char* path, char* out, size_t out_sz);

// Windows LoadConfigDirectory: chdir to INI Directory= when savedirectory is set.
void M88ApplyStartupDirectory(const PC8801::Config* cfg, const char* ini_path,
                              bool skip_if_disk_on_cli);

// explicit_path: --config value or nullptr. Sets used_path and created_new_ini.
void M88LoadStartupConfig(PC8801::Config* cfg, const char* explicit_path,
                          char* used_path, size_t used_path_sz, bool* created_new_ini);

const char* M88BasicModeName(int basicmode);
bool M88BasicModeFixesClock4MHz(int basicmode);

// config.clock / INI CPUClock: V2 operating clock (40 or 80). V1/N modes run at
// 4 MHz without overwriting the stored value.
int M88EffectiveClock(const PC8801::Config* cfg);
PC8801::Config M88ConfigForHardware(const PC8801::Config& cfg);

class M88Sequencer;
void M88SeqApplyConfig(M88Sequencer& seq, const PC8801::Config& cfg);

// Apply Linux-port defaults that should survive partial Windows INI imports.
void M88FinalizeConfig(PC8801::Config* cfg);

// Optional debug overrides: M88_CPUCLOCK (MHz, e.g. 9 for 9MHz), M88_SPEED (percent).
void M88ApplyEnvOverrides(PC8801::Config* cfg);

// m88.ini ScreenScale: auto (default) or integer N>=1. --scale overrides when passed.
bool M88ScreenScaleAuto();
int M88ScreenScaleIniValue();
int M88ResolveScreenScale(int avail_w, int avail_h, int chrome_w, int chrome_h,
                          int cli_scale, bool cli_explicit);
void M88PrintScreenScale(int scale, bool cli_explicit);

// m88.ini WaylandIdleInhibit=1 (Wayland idle-inhibit protocol; Qt frontend only).
bool M88WaylandIdleInhibitEnabled();
void M88SetWaylandIdleInhibitEnabled(bool enabled);

// m88.ini ImeHalfKana=1 (host IME -> PC-88 half-width kana).
bool M88ImeHalfKanaEnabled();
void M88SetImeHalfKanaEnabled(bool enabled);
