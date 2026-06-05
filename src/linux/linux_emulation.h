#pragma once

#include "linux_frame_pace.h"
#include "pc88/pc88.h"

#include <cstdio>

// Full machine (CPU/HW) reset on the first main-loop iteration (after config /
// disk / sound are ready). Call once when the emulation loop has started.
inline void M88PostStartupCpuReset(PC88& pc88, M88DrawSkip* draw_skip = nullptr) {
  pc88.Reset();
  if (draw_skip) {
    draw_skip->Reset();
  }
  std::fprintf(stderr, "M88: CPU reset\n");
}
