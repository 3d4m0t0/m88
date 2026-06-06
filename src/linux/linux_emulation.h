#pragma once

#include "linux_frame_pace.h"
#include "pc88/pc88.h"

#include <cstdio>

// Full machine (CPU/HW) reset on the first main-loop iteration (after config /
// disk / sound are ready). Call once when the emulation loop has started.
inline void M88PostStartupCpuReset(PC88& pc88, M88DrawSkip* draw_skip = nullptr,
                                    int refresh_timing = 1) {
  pc88.Reset();
  if (draw_skip) {
    draw_skip->ForceUpdateAfterReset(refresh_timing);
  }
  std::fprintf(stderr, "M88: CPU reset\n");
}

// User-initiated reset (menu / F5): match WinUI::Reset CPU path.
inline void M88UserCpuReset(PC88& pc88, M88DrawSkip* draw_skip, int refresh_timing) {
  pc88.Reset();
  if (draw_skip) {
    draw_skip->ForceUpdateAfterReset(refresh_timing);
  }
  std::fprintf(stderr, "M88: CPU reset (user)\n");
}
