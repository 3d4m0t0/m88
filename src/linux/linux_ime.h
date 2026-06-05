#pragma once

class LinuxDraw;
namespace PC8801 {
class Config;
class WinKeyIF;
}

// Host IME (romaji -> hiragana preedit -> commit) -> halfwidth kana key injection.
namespace LinuxIme {

bool Enabled();

// Enable half-kana IME injection (respects M88_IME_KANA). Call once at host startup.
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
