#pragma once

#include "linux_monotonic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// Emulated-time-master pacing: wall clock follows cumulative scheduler frame budget.
// Each frame adds twork ticks (0.01 ms units); sleep until
//   wall_epoch + sum(twork) * 10 us
// Audio ring fill feeds a slow integral bias (subordinate, +/-1 ms).

struct M88EmuTimePacer {
  struct AudioHint {
    int ring_avail = -1;
    int ring_size = 0;
    int sample_rate_hz = 0;
  };

  uint64_t wall_epoch_ns = 0;
  uint64_t emu_ticks_budget = 0;
  double audio_integral_ms = 0.0;

  void Reset(uint64_t now_ns = 0) {
    wall_epoch_ns = now_ns ? now_ns : M88MonotonicNowNs();
    emu_ticks_budget = 0;
    audio_integral_ms = 0.0;
  }

  template <typename StopFn>
  void SleepAfterFrame(int twork_ticks, StopFn stop, AudioHint audio = {}) {
    if (twork_ticks <= 0 || stop()) {
      return;
    }

    emu_ticks_budget += static_cast<uint64_t>(twork_ticks);
    uint64_t target_ns = wall_epoch_ns + emu_ticks_budget * 10'000ULL;

    if (audio.sample_rate_hz >= 8000 && audio.ring_size > 0 && audio.ring_avail >= 0) {
      const double avail_ms =
          static_cast<double>(audio.ring_avail) * 1000.0 /
          static_cast<double>(audio.sample_rate_hz);
      const double size_ms =
          static_cast<double>(audio.ring_size) * 1000.0 /
          static_cast<double>(audio.sample_rate_hz);
      const double target_ms = size_ms * 0.5;
      const double error = (avail_ms - target_ms) / std::max(1.0, size_ms);

      constexpr double kDeadband = 0.04;
      if (std::abs(error) >= kDeadband) {
        const double rate_scale =
            std::clamp(static_cast<double>(audio.sample_rate_hz) / 44100.0, 0.2, 1.0);
        audio_integral_ms = audio_integral_ms * 0.996 + error * 0.8 * rate_scale;
        audio_integral_ms = std::clamp(audio_integral_ms, -1.0, 1.0);
      }

      if (std::abs(audio_integral_ms) >= 0.05) {
        if (audio_integral_ms >= 0.0) {
          target_ns += static_cast<uint64_t>(audio_integral_ms * 1'000'000.0);
        } else {
          const uint64_t trim =
              static_cast<uint64_t>(-audio_integral_ms * 1'000'000.0);
          target_ns = target_ns > trim ? target_ns - trim : target_ns;
        }
      }
    }

    const uint64_t now_ns = M88MonotonicNowNs();
    const uint64_t frame_ns = static_cast<uint64_t>(twork_ticks) * 10'000ULL;
    if (now_ns >= target_ns) {
      if (now_ns - target_ns > frame_ns * 2) {
        Resync(now_ns);
      }
      return;
    }

    M88MonotonicDetail::SleepUntilAbs(target_ns, stop);
  }

  void Resync(uint64_t now_ns) {
    wall_epoch_ns = now_ns;
    emu_ticks_budget = 0;
    audio_integral_ms = 0.0;
  }
};
