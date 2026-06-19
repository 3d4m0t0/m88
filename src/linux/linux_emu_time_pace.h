#pragma once

#include "linux_monotonic.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

// Emulated-time-master pacing with SPSC-driven production during sleep.
// Producer runs on emu thread only; callback is consumer-only.

struct M88EmuTimePacer {
  struct AudioHint {
    int ring_avail = -1;
    int ring_size = 0;
    int sample_rate_hz = 0;
    int min_ring_frames = 0;
  };

  struct PacingThresholds {
    double low_water;
    double target_fill;
    double high_water;
  };

  static constexpr PacingThresholds kPacingThresholds{0.50, 0.58, 0.72};

  uint64_t wall_epoch_ns = 0;
  uint64_t emu_ticks_budget = 0;

  void Reset(uint64_t now_ns = 0) {
    wall_epoch_ns = now_ns ? now_ns : M88MonotonicNowNs();
    emu_ticks_budget = 0;
  }

  template <typename StopFn, typename HintFn, typename ProduceFn>
  void SleepAfterFrame(int twork_ticks, StopFn stop, HintFn hint_fn,
                       ProduceFn produce_fn) {
    if (twork_ticks <= 0 || stop()) {
      return;
    }

    emu_ticks_budget += static_cast<uint64_t>(twork_ticks);
    const uint64_t target_ns = wall_epoch_ns + emu_ticks_budget * 10'000ULL;
    const uint64_t frame_ns = static_cast<uint64_t>(twork_ticks) * 10'000ULL;

    constexpr uint64_t kPollChunkNs = 1'000'000;
    constexpr uint64_t kMinSleepNs = 250'000;
    constexpr int kMaxProduceBurst = 512;

    int produce_burst = 0;

    while (!stop()) {
      const uint64_t now_ns = M88MonotonicNowNs();
      if (now_ns >= target_ns) {
        break;
      }

      const AudioHint audio = hint_fn();
      const PacingThresholds& thresholds = kPacingThresholds;
      const bool ring_valid =
          audio.ring_size > 0 && audio.ring_avail >= 0 && audio.sample_rate_hz >= 8000;

      if (ring_valid) {
        if (audio.min_ring_frames > 0 &&
            audio.ring_avail < audio.min_ring_frames &&
            produce_burst < kMaxProduceBurst) {
          produce_fn();
          ++produce_burst;
          continue;
        }

        const double fill =
            static_cast<double>(audio.ring_avail) / static_cast<double>(audio.ring_size);

        if (fill < thresholds.low_water && produce_burst < kMaxProduceBurst) {
          produce_fn();
          ++produce_burst;
          continue;
        }
        produce_burst = 0;

        if (fill >= thresholds.high_water) {
          M88MonotonicDetail::SleepUntilAbs(target_ns, stop);
          break;
        }

        uint64_t chunk_ns = kPollChunkNs;
        if (fill < thresholds.target_fill) {
          const double urgency =
              (thresholds.target_fill - fill) /
              (thresholds.target_fill - thresholds.low_water);
          const double scale = 1.0 - std::clamp(urgency, 0.0, 1.0) * 0.85;
          chunk_ns = static_cast<uint64_t>(static_cast<double>(kPollChunkNs) * scale);
          if (chunk_ns < kMinSleepNs) {
            chunk_ns = kMinSleepNs;
          }
        } else {
          chunk_ns = kPollChunkNs * 2;
        }

        const uint64_t remaining_ns = target_ns - now_ns;
        if (chunk_ns > remaining_ns) {
          chunk_ns = remaining_ns;
        }
        if (chunk_ns > 0) {
          M88MonotonicDetail::SleepUntilAbs(now_ns + chunk_ns, stop);
        }
        continue;
      }

      M88MonotonicDetail::SleepUntilAbs(target_ns, stop);
      break;
    }

    const uint64_t now_ns = M88MonotonicNowNs();
    if (now_ns >= target_ns && now_ns - target_ns > frame_ns * 2) {
      Resync(now_ns);
    }
  }

  template <typename StopFn>
  void SleepAfterFrame(int twork_ticks, StopFn stop, AudioHint = {}) {
    SleepAfterFrame(
        twork_ticks, stop, []() { return AudioHint{}; },
        []() {});
  }

  void Resync(uint64_t now_ns) {
    wall_epoch_ns = now_ns;
    emu_ticks_budget = 0;
  }
};
