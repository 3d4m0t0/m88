#pragma once

namespace PC8801 {
struct Config;
}

void M88LogConfigPath(const char* path, bool created);
void M88LogWorkingDirectory();
void M88LogMachine(const PC8801::Config* config);
void M88LogSound(const PC8801::Config* config);
void M88LogFdd(const PC8801::Config* config);
void M88LogImeHalfKana();
void M88LogSdlVideoIndexed8();
void M88LogSdlVideoArgb();
void M88LogSdlVideoArgbFallback(const char* reason);
