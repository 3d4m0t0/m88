#pragma once

#include "linux_emu_time_pace.h"
#include "linux_monotonic.h"
#include "m88_stall_watchdog.h"
#include "pc88/config.h"
#include "pc88/pc88.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <thread>

// Windows Sequencer / TimeKeeper pacing model for Linux frontends.
// See src/win32/sequence.cpp and src/win32/timekeep.h.

struct M88TimeKeeper {
  static constexpr int kUnit = 100;  // 0.01 ms per tick

  uint64_t base_ns = 0;
  int64_t time = 0;

  void Reset() {
    base_ns = M88MonotonicNowNs();
    time = 0;
  }

  int64_t GetTime() {
    const uint64_t now = M88MonotonicNowNs();
    if (base_ns == 0) {
      base_ns = now;
    }
    const uint64_t delta_ns = now - base_ns;
    base_ns = now;
    time += static_cast<int64_t>(delta_ns / 10'000ULL);
    return time;
  }
};

struct M88Sequencer {
  M88TimeKeeper keeper;

  int clock = 40;       // host 0.1 MHz; 0 = fullspeed; <0 = burst (-host_clock)
  int speed = 100;      // config.speed / 10 (100 = 100%)
  int effclock = 100;
  int64_t time = 0;

  uint skippedframe = 0;
  uint refreshcount = 0;
  uint refreshtiming = 1;
  bool drawnextframe = true;

  int64_t fast_window_begin = 0;
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
                       M88EmuTimePacer::AudioHint audio_hint,
                       uint64_t fast_slice_ns = 20'000'000ULL) {
    return RunFrame(vm, draw_fn, draw_ctx, force_draw, stop, emu_pacer,
                    [audio_hint]() { return audio_hint; },
                    std::function<void(int)>{}, std::function<void()>{},
                    std::function<void(int)>{}, fast_slice_ns);
  }

  template <typename StopFn>
  FrameResult RunFrame(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, bool force_draw,
                       StopFn stop, M88EmuTimePacer& emu_pacer,
                       const std::function<M88EmuTimePacer::AudioHint()>& audio_hint = {},
                       const std::function<void(int emu_ticks)>& mix_audio_slice = {},
                       const std::function<void()>& drain_audio = {},
                       const std::function<void(int emu_sleep_ticks)>& prepare_audio_sleep = {},
                       uint64_t fast_slice_ns = 20'000'000ULL) {
    if (!vm) {
      return {};
    }
    if (IsFastMode()) {
      return RunFastSlice(vm, draw_fn, draw_ctx, stop, fast_slice_ns, mix_audio_slice,
                          drain_audio);
    }
    return RunNormal(vm, draw_fn, draw_ctx, force_draw, stop, emu_pacer, audio_hint,
                     mix_audio_slice, drain_audio, prepare_audio_sleep);
  }

  long TakeExecCount() {
    const long n = execcount;
    execcount = 0;
    return n;
  }

  void SetProceedSliceTicks(int ticks) {
    proceed_slice_ticks_ = std::max(1, ticks);
  }
  int ProceedSliceTicks() const { return proceed_slice_ticks_; }

 private:
  int proceed_slice_ticks_ = 10;

  template <typename StopFn, typename MixFn>
  void Execute(PC88* vm, long clk, long length, long eff, StopFn stop, MixFn mix_fn) {
    long remaining = length;
    while (remaining > 0 && !stop()) {
      const long slice = std::min<long>(remaining, proceed_slice_ticks_);
      const int ret = vm->Proceed(static_cast<uint>(slice), static_cast<uint>(clk),
                                  static_cast<uint>(std::max<long>(1, eff)));
      execcount += clk * ret;
      remaining -= slice;
      if (mix_fn) {
        mix_fn(static_cast<int>(slice));
      }
#ifdef M88_LINUX_PORT
      M88StallWatchdogProceedSlice();
      std::this_thread::yield();
#endif
    }
  }

  template <typename StopFn>
  void Execute(PC88* vm, long clk, long length, long eff, StopFn stop) {
    Execute(vm, clk, length, eff, stop, std::function<void()>{});
  }

  template <typename StopFn>
  FrameResult RunNormal(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, bool force_draw,
                        StopFn stop, M88EmuTimePacer& emu_pacer,
                        const std::function<M88EmuTimePacer::AudioHint()>& audio_hint,
                        const std::function<void(int emu_ticks)>& mix_audio_slice,
                        const std::function<void()>& drain_audio,
                        const std::function<void(int emu_sleep_ticks)>& prepare_audio_sleep) {
    FrameResult result;
    const int texec = vm->GetFramePeriod();
    const int64_t twork = static_cast<int64_t>(texec) * 100 / speed;
    vm->TimeSync();
    const int64_t frame_start = keeper.GetTime();

    int ticks_left = texec;
    const int slice_ticks = proceed_slice_ticks_;
    const int total_slices = std::max(1, (texec + slice_ticks - 1) / slice_ticks);

    auto ring_low = [&]() -> bool {
      if (!audio_hint) {
        return false;
      }
      const M88EmuTimePacer::AudioHint h = audio_hint();
      return h.ring_avail >= 0 && h.min_ring_frames > 0 &&
             h.ring_avail < h.min_ring_frames;
    };

    auto run_emu_slice = [&]() -> bool {
      if (ticks_left <= 0 || stop()) {
        return false;
      }
      const long slice = std::min<long>(slice_ticks, ticks_left);
      const int ret = vm->Proceed(static_cast<uint>(slice), static_cast<uint>(clock),
                                  static_cast<uint>(std::max<long>(1, clock * speed / 100)));
      execcount += clock * ret;
      ticks_left -= static_cast<int>(slice);
      if (mix_audio_slice) {
        mix_audio_slice(static_cast<int>(slice));
      }
      if (drain_audio) {
        drain_audio();
      }
#ifdef M88_LINUX_PORT
      M88StallWatchdogProceedSlice();
      std::this_thread::yield();
#endif
      return true;
    };

    // Spread emulation across the frame wall budget so audio Mix/Drain continues
    // while the callback consumes SPSC, instead of burst-then-sleep starvation.
    int slices_done = 0;
    while (!stop() && ticks_left > 0) {
      if (ring_low()) {
        run_emu_slice();
        ++slices_done;
        continue;
      }

      const int64_t now = keeper.GetTime();
      const int64_t scheduled =
          frame_start + (static_cast<int64_t>(slices_done) * twork) / total_slices;
      if (now < scheduled) {
        const uint64_t target_ns = M88MonotonicNowNs() +
                                   static_cast<uint64_t>(scheduled - now) * 10'000ULL;
        while (!stop() && ticks_left > 0 && M88MonotonicNowNs() < target_ns) {
          if (ring_low()) {
            break;
          }
          if (drain_audio) {
            drain_audio();
          }
          const uint64_t poll_ns = M88MonotonicNowNs() + 1'000'000ULL;
          M88MonotonicDetail::SleepUntilAbs(std::min(target_ns, poll_ns), stop);
        }
        continue;
      }

      run_emu_slice();
      ++slices_done;
    }

    if (drain_audio) {
      drain_audio();
    }

    const auto poll_hint = [&]() {
      return audio_hint ? audio_hint() : M88EmuTimePacer::AudioHint{};
    };
    const auto sleep_produce = [&]() {
      if (ticks_left > 0) {
        run_emu_slice();
        return;
      }
      if (drain_audio) {
        drain_audio();
      }
    };

    int64_t tcpu = keeper.GetTime() - time;
    if (tcpu < 0 || tcpu > twork * 4) {
      time = keeper.GetTime();
      tcpu = 0;
    }
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

      const int64_t tdraw = keeper.GetTime() - frame_start;
      drawnextframe = tdraw <= twork;
      if (prepare_audio_sleep) {
        prepare_audio_sleep(twork);
      }
      emu_pacer.SleepAfterFrame(twork, stop, poll_hint, sleep_produce);
      while (!stop() && ticks_left > 0) {
        run_emu_slice();
      }
      time += twork;
      return result;
    }

    time += twork;
    if (prepare_audio_sleep) {
      prepare_audio_sleep(twork);
    }
    emu_pacer.SleepAfterFrame(twork, stop, poll_hint, sleep_produce);
    while (!stop() && ticks_left > 0) {
      run_emu_slice();
    }
    if (++skippedframe >= 20) {
      result.update_screen = true;
      skippedframe = 0;
      time = keeper.GetTime();
      emu_pacer.Resync(M88MonotonicNowNs());
      if (draw_fn) {
        draw_fn(draw_ctx, true);
      }
    }
    return result;
  }

  // Burst / fullspeed: Windows ExecuteAsynchronus clock<=0 branch, sliced for UI.
  template <typename StopFn, typename MixFn, typename DrainFn>
  FrameResult RunFastSlice(PC88* vm, DrawFrameFn draw_fn, void* draw_ctx, StopFn stop,
                           uint64_t slice_ns, MixFn mix_fn, DrainFn drain_fn) {
    FrameResult result;
    if (fast_window_begin == 0) {
      fast_window_begin = keeper.GetTime();
      fast_eclk = 0;
      vm->TimeSync();
    }

    const uint64_t slice_end_ns = M88MonotonicNowNs() + slice_ns;
    do {
      if (stop()) {
        return result;
      }
      if (clock) {
        Execute(vm, -clock, 500, effclock, stop, mix_fn);
      } else {
        Execute(vm, effclock, 500 * speed / 100, effclock, stop, mix_fn);
      }
      if (drain_fn) {
        drain_fn();
      }
      fast_eclk += 5;
    } while (M88MonotonicNowNs() < slice_end_ns &&
             keeper.GetTime() - fast_window_begin < 1000);

    const int64_t ms = keeper.GetTime() - fast_window_begin;
    if (ms < 1000) {
      return result;
    }

    result.update_screen = true;
    const int adapt =
        std::min(1000, fast_eclk) * effclock * 100 /
            static_cast<int>(std::max<int64_t>(1, ms)) +
        1;
    effclock = std::min(10000, adapt);
    fast_window_begin = 0;
    fast_eclk = 0;
    if (draw_fn) {
      draw_fn(draw_ctx, true);
    }
    return result;
  }
};
