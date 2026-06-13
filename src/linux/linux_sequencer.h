#pragma once

#include "linux_emu_time_pace.h"
#include "linux_monotonic.h"
#include "pc88/config.h"
#include "pc88/pc88.h"

#include <algorithm>
#include <cstdint>

// Windows Sequencer / TimeKeeper pacing model for Linux frontends.
// See src/win32/sequence.cpp and src/win32/timekeep.h.

struct M88TimeKeeper {
  static constexpr int kUnit = 100;  // 0.01 ms per tick

  uint64_t base_ns = 0;
  uint32_t time = 0;

  void Reset() {
    base_ns = M88MonotonicNowNs();
    time = 0;
  }

  uint32_t GetTime() {
    const uint64_t now = M88MonotonicNowNs();
    if (base_ns == 0) {
      base_ns = now;
    }
    const uint64_t delta_ns = now - base_ns;
    base_ns = now;
    time += static_cast<uint32_t>(delta_ns / 10'000ULL);
    return time;
  }
};

struct M88Sequencer {
  M88TimeKeeper keeper;

  int clock = 40;       // host 0.1 MHz; 0 = fullspeed; <0 = burst (-host_clock)
  int speed = 100;      // config.speed / 10 (100 = 100%)
  int effclock = 100;
  int time = 0;

  uint skippedframe = 0;
  uint refreshcount = 0;
  uint refreshtiming = 1;
  bool drawnextframe = true;

  int fast_window_begin = 0;
  int fast_eclk = 0;

  long execcount = 0;

  void ApplyConfig(const PC8801::Config& cfg) {
    refreshtiming = static_cast<uint>(std::max(1, cfg.refreshtiming));
    speed = std::max(1, cfg.speed / 10);
    if (cfg.flags & PC8801::Config::fullspeed) {
      clock = 0;
    } else if (cfg.flags & PC8801::Config::cpuburst) {
      clock = -static_cast<int>(std::max(1, cfg.clock));
    } else {
      clock = static_cast<int>(std::max(1, cfg.clock));
    }
  }

  void ResetPacing() {
    keeper.Reset();
    time = keeper.GetTime();
    effclock = 100;
    skippedframe = 0;
    refreshcount = 0;
    drawnextframe = true;
    fast_window_begin = 0;
    fast_eclk = 0;
    execcount = 0;
  }

  void ForceDrawAfterReset(int refresh_timing) {
    refreshtiming = static_cast<uint>(std::max(1, refresh_timing));
    drawnextframe = true;
    refreshcount = refreshtiming;
    skippedframe = 0;
  }

  bool IsFastMode() const { return clock <= 0; }

  struct FrameResult {
    bool update_screen = false;
  };

  using DrawFrameFn = void (*)(void* ctx, bool draw);

  template <typename StopFn>
  FrameResult RunFrame(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, bool force_draw,
                       StopFn stop, M88EmuTimePacer& emu_pacer,
                       M88EmuTimePacer::AudioHint audio_hint = {},
                       uint64_t fast_slice_ns = 20'000'000ULL) {
    if (!vm) {
      return {};
    }
    if (IsFastMode()) {
      return RunFastSlice(vm, draw_fn, draw_ctx, stop, fast_slice_ns);
    }
    return RunNormal(vm, draw_fn, draw_ctx, force_draw, stop, emu_pacer, audio_hint);
  }

  long TakeExecCount() {
    const long n = execcount;
    execcount = 0;
    return n;
  }

 private:
  template <typename StopFn>
  void Execute(PC88* vm, long clk, long length, long eff, StopFn stop) {
    long remaining = length;
    while (remaining > 0 && !stop()) {
      const long slice = std::min<long>(remaining, 500);
      const int ret = vm->Proceed(static_cast<uint>(slice), static_cast<uint>(clk),
                                  static_cast<uint>(std::max<long>(1, eff)));
      execcount += clk * ret;
      remaining -= slice;
    }
  }

  template <typename StopFn>
  FrameResult RunNormal(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, bool force_draw,
                        StopFn stop, M88EmuTimePacer& emu_pacer,
                        M88EmuTimePacer::AudioHint audio_hint) {
    FrameResult result;
    const int texec = vm->GetFramePeriod();
    const int twork = texec * 100 / speed;
    vm->TimeSync();
    Execute(vm, clock, texec, clock * speed / 100, stop);

    const int32_t tcpu = static_cast<int32_t>(keeper.GetTime()) - time;
    if (tcpu < twork) {
      const bool draw_eligible = drawnextframe || refreshtiming <= 1;
      if (draw_eligible && ++refreshcount >= refreshtiming) {
        result.update_screen = true;
        skippedframe = 0;
        refreshcount = 0;
      }

      const bool should_draw = result.update_screen || force_draw;
      if (should_draw && draw_fn) {
        draw_fn(draw_ctx, true);
      }

      const int32_t tdraw = static_cast<int32_t>(keeper.GetTime()) - time;
      drawnextframe = tdraw <= twork;
      emu_pacer.SleepAfterFrame(twork, stop, audio_hint);
      time += twork;
      return result;
    }

    time += twork;
    emu_pacer.SleepAfterFrame(twork, stop, audio_hint);
    if (++skippedframe >= 20) {
      result.update_screen = true;
      skippedframe = 0;
      time = static_cast<int>(keeper.GetTime());
      emu_pacer.Resync(M88MonotonicNowNs());
      if (draw_fn) {
        draw_fn(draw_ctx, true);
      }
    }
    return result;
  }

  // Burst / fullspeed: Windows ExecuteAsynchronus clock<=0 branch, sliced for UI.
  template <typename StopFn>
  FrameResult RunFastSlice(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, StopFn stop,
                           uint64_t slice_ns) {
    FrameResult result;
    if (fast_window_begin == 0) {
      fast_window_begin = static_cast<int>(keeper.GetTime());
      fast_eclk = 0;
      vm->TimeSync();
    }

    const uint64_t slice_end_ns = M88MonotonicNowNs() + slice_ns;
    do {
      if (stop()) {
        return result;
      }
      if (clock) {
        Execute(vm, -clock, 500, effclock, stop);
      } else {
        Execute(vm, effclock, 500 * speed / 100, effclock, stop);
      }
      fast_eclk += 5;
    } while (M88MonotonicNowNs() < slice_end_ns &&
             static_cast<uint32_t>(keeper.GetTime() - fast_window_begin) < 1000);

    const uint32_t ms = keeper.GetTime() - static_cast<uint32_t>(fast_window_begin);
    if (ms < 1000) {
      return result;
    }

    result.update_screen = true;
    const int adapt = std::min(1000, fast_eclk) * effclock * 100 / static_cast<int>(std::max<uint32_t>(1, ms)) + 1;
    effclock = std::min(10000, adapt);
    fast_window_begin = 0;
    fast_eclk = 0;
    if (draw_fn) {
      draw_fn(draw_ctx, true);
    }
    return result;
  }
};
