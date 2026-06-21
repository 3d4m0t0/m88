#pragma once

#include "draw.h"
#include "linux_sequencer.h"
#include "pc88/config.h"
#include "pc88/pc88.h"
#include "../win32/WinKeyIF.h"

#include <cstdio>
#include <functional>

// WinCore::ApplyConfig (wincore.cpp) without joypad Connect.
inline void M88ApplyWinCoreConfig(
    PC88& pc88, M88Sequencer& seq, PC8801::Config* cfg, Draw* draw,
    const std::function<void(PC8801::Config*)>& apply_sound = {}) {
  seq.ApplyConfig(*cfg);
  pc88.ApplyConfig(cfg);
  if (apply_sound) {
    apply_sound(cfg);
  }
  if (draw) {
    draw->SetFlipMode(false);
  }
}

// WinUI::Reset (ui.cpp): keyif.ApplyConfig → core.ApplyConfig → core.Reset.
inline void M88ApplyWinReset(
    PC8801::WinKeyIF& keyif, PC88& pc88, M88Sequencer& seq, PC8801::Config* cfg,
    Draw* draw, const std::function<void(PC8801::Config*)>& apply_sound = {}) {
  keyif.ApplyConfig(cfg);
  M88ApplyWinCoreConfig(pc88, seq, cfg, draw, apply_sound);
  pc88.Reset();
}

// InitM88 startup: WinUI::ApplyConfig then core.Reset (no keyif path).
inline void M88PostStartupCpuReset(PC88& pc88, M88Sequencer* seq = nullptr,
                                    int refresh_timing = 1) {
  pc88.Reset();
  if (seq) {
    seq->ForceDrawAfterReset(refresh_timing);
  }
  std::fprintf(stderr, "M88: CPU reset\n");
}
