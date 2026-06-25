#include "linux_startup_log.h"

#include <cstddef>

#include "linux_config.h"
#include "pc88/config.h"

#include <algorithm>
#include <cstdio>
#include <unistd.h>

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

void M88LogWorkingDirectory() {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd))) {
    std::fprintf(stderr, "M88: working directory: %s\n", cwd);
  }
}

void M88LogMachine(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  const int effclock = std::max(1, config->clock * (config->speed / 10) / 100);
  std::fprintf(stderr,
               "M88: BASICMode=%d (%s), CPUClock=%d (%.1f MHz), effclock=%d, "
               "speed %d%%, subcpucontrol=%s\n",
               config->basicmode, M88BasicModeName(config->basicmode), config->clock,
               config->clock / 10.0, effclock, config->speed / 10,
               (config->flags & PC8801::Config::subcpucontrol) ? "on" : "off");
}

void M88LogBasicModeChange(int prev_mode, int new_mode, bool cold_start) {
  std::fprintf(stderr, "M88: BASICMode=%d (%s) <- %d (%s), %s start\n", new_mode,
               M88BasicModeName(new_mode), prev_mode, M88BasicModeName(prev_mode),
               cold_start ? "cold" : "warm");
}

void M88LogSoftReset() {
  std::fprintf(stderr, "M88: soft reset (RAM preserved)\n");
}

void M88LogFrameTiming(int texec, int config_speed) {
  if (texec <= 0 || config_speed <= 0) {
    return;
  }
  const double ms = static_cast<double>(texec) * 10.0 / static_cast<double>(config_speed);
  const double hz = ms > 0.0 ? 1000.0 / ms : 0.0;
  std::fprintf(stderr, "M88: frame period %d ticks (%.2f ms, ~%.1f Hz)\n", texec, ms,
               hz);
}

void M88LogSound(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  std::fprintf(stderr, "M88: sound buffer %u ms\n", config->soundbuffer);
}

void M88LogFdd(const PC8801::Config* config) {
  if (!config) {
    return;
  }
  std::fprintf(stderr, "M88: FDD %s\n",
               (config->flag2 & PC8801::Config::fddnowait) ? "no wait" : "wait");
}

void M88LogImeHalfKana() {
  std::fprintf(stderr, "M88: IME half-kana: momentary カナ + FH matrix table\n");
}

void M88LogSdlVideoIndexed8() {
  std::fprintf(stderr,
               "M88: video SDL Indexed8 texture (SDL_SetTexturePalette)\n");
}

void M88LogSdlVideoArgb() {
  std::fprintf(stderr, "M88: video ARGB8888 texture (LUT)\n");
}

void M88LogSdlVideoArgbFallback(const char* reason) {
  if (reason && *reason) {
    std::fprintf(stderr, "M88: video ARGB8888 texture (LUT); INDEX8 fallback: %s\n",
                 reason);
  } else {
    M88LogSdlVideoArgb();
  }
}
