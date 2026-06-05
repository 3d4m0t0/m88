#include "linux_startup_log.h"

#include <cstddef>

#include "linux_config.h"
#include "pc88/config.h"

#include <algorithm>
#include <cstdio>

void M88LogConfigPath(const char* path, bool created) {
  if (!path || !path[0]) {
    return;
  }
  if (created) {
    std::fprintf(stderr, "M88: created default config: %s\n", path);
  } else {
    std::fprintf(stderr, "M88: config: %s\n", path);
  }
}

void M88LogMachine(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  const int effclock = std::max(1, config->clock * (config->speed / 10) / 100);
  std::fprintf(stderr,
               "M88: BASICMode=%d (%s), CPUClock=%d (%.1f MHz), effclock=%d, "
               "speed %d%%\n",
               config->basicmode, M88BasicModeName(config->basicmode), config->clock,
               config->clock / 10.0, effclock, config->speed / 10);
}

void M88LogSound(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  std::fprintf(stderr, "M88: sound %u Hz, buffer %u ms\n", config->sound,
               config->soundbuffer);
}

void M88LogFdd(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  std::fprintf(stderr, "M88: FDD %s\n",
               (config->flag2 & PC8801::Config::fddnowait) ? "no wait" : "wait");
}

void M88LogImeHalfKana() {
  std::fprintf(stderr, "M88: IME half-kana uses AT106 matrix during injection\n");
}

void M88LogSdlVideoIndexed8() {
  std::fprintf(stderr,
               "M88: video SDL Indexed8 texture (SDL_SetTexturePalette)\n");
}

void M88LogSdlVideoArgb() {
  std::fprintf(stderr, "M88: video ARGB8888 texture (LUT)\n");
}
