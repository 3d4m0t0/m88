#pragma once

class LinuxDraw;
namespace PC8801 {
class Config;
class WinKeyIF;
}

// Host IME (romaji -> hiragana preedit -> commit) -> halfwidth kana key injection.
namespace LinuxIme {

bool Enabled();

// Host IME detected once at startup (fcitx/ibus/Qt input method, etc.).
bool HostAvailable();

// User preference (m88.ini ImeHalfKana=1).
bool UserEnabled();
void SetUserEnabled(bool enabled);

// Call once after the GUI toolkit is up; qt_input_method when using Qt.
void ProbeHostAvailability(bool qt_input_method);

// Recompute Enabled() from host, user pref, and M88_IME_KANA.
void InitHost();

void OnWindowShown(LinuxDraw* draw);
void OnWindowHidden();

// Returns 1 if handled, 2 if handled and IME strokes were queued (caller should drain).
int HandleSdlEvent(unsigned int type, const void* sdl_event, LinuxDraw* draw,
                   PC8801::WinKeyIF* keyif, const PC8801::Config* cfg);

void Pump(PC8801::WinKeyIF* keyif);

// Commit composed UTF-8 (Qt input method or other hosts).
// Enqueue half-kana key strokes (returns false if nothing to inject).
bool CommitUtf8(const char* utf8, PC8801::WinKeyIF* keyif, const PC8801::Config* cfg);

}  // namespace LinuxIme
