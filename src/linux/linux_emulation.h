#pragma once

#include "linux_sequencer.h"
#include "pc88/pc88.h"

#include <cstdio>

// Full machine reset after ApplyConfig (WinUI::InitM88: ApplyConfig then Reset).
inline void M88PostStartupCpuReset(PC88& pc88, M88Sequencer* seq = nullptr,
                                    int refresh_timing = 1) {
  pc88.Reset();
  if (seq) {
    seq->ForceDrawAfterReset(refresh_timing);
  }
  std::fprintf(stderr, "M88: CPU reset\n");
}

// User-initiated reset (menu / F5): match WinUI::Reset CPU path.
inline void M88UserCpuReset(PC88& pc88, M88Sequencer* seq, int refresh_timing) {
  pc88.Reset();
  if (seq) {
    seq->ForceDrawAfterReset(refresh_timing);
  }
  std::fprintf(stderr, "M88: CPU reset\n");
}
