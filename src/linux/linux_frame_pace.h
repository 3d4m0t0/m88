#pragma once

#include "linux_monotonic.h"
#include "loadmon.h"

#include <algorithm>
#include <atomic>
#include <cstdint>

// Wall-clock pacing: one Proceed per loop, never faster than realtime.
inline uint32_t M88FramePeriodMs(int texec, int config_speed) {
  return static_cast<uint32_t>(M88FramePeriodNs(texec, config_speed) / 1'000'000ULL);
}

inline void M88PaceFrame(uint64_t frame_begin_ns, uint64_t frame_period_ns,
                          const volatile bool* stop = nullptr) {
  LOADBEGIN("Pace");
  M88MonotonicDetail::SleepRemainingNs(
      frame_begin_ns, frame_period_ns,
      [&]() { return stop && !*stop; });
  LOADEND("Pace");
}

inline void M88PaceFrame(uint64_t frame_begin_ns, uint64_t frame_period_ns,
                          const std::atomic<bool>* stop) {
  LOADBEGIN("Pace");
  M88MonotonicDetail::SleepRemainingNs(
      frame_begin_ns, frame_period_ns,
      [&]() { return stop && !stop->load(std::memory_order_relaxed); });
  LOADEND("Pace");
}

// Windows Sequencer-style draw skip: Proceed every frame; UpdateScreen only when
// the frame budget allows (or after many consecutive skips).
class M88DrawSkip {
 public:
  void Reset() {
    draw_next_ = false;
    refresh_count_ = 0;
    skipped_frames_ = 0;
    proceed_on_time_ = true;
    frame_period_ns_ = 1'000'000ULL;
  }

  // After CPU reset: draw_skip::Reset() leaves draw_next_=false so RefreshTiming>1
  // can skip UpdateScreen for many frames (black/frozen screen while CPU runs).
  void ForceUpdateAfterReset(int refresh_timing) {
    Reset();
    const uint32_t timing = static_cast<uint32_t>(std::max(1, refresh_timing));
    draw_next_ = true;
    refresh_count_ = timing;
    skipped_frames_ = 0;
  }

  bool AfterProceed(uint64_t frame_begin_ns, int texec, int config_speed,
                    int refresh_timing) {
    frame_period_ns_ = M88FramePeriodNs(texec, config_speed);
    const uint32_t timing = static_cast<uint32_t>(std::max(1, refresh_timing));
    const uint64_t elapsed_ns = M88MonotonicNowNs() - frame_begin_ns;

    if (elapsed_ns < frame_period_ns_) {
      proceed_on_time_ = true;
      if (draw_next_ && ++refresh_count_ >= timing) {
        skipped_frames_ = 0;
        refresh_count_ = 0;
        return true;
      }
      return false;
    }

    proceed_on_time_ = false;
    if (++skipped_frames_ >= 20) {
      skipped_frames_ = 0;
      draw_next_ = true;
      return true;
    }
    return false;
  }

  void EndFrame(uint64_t frame_begin_ns) {
    if (!proceed_on_time_) {
      return;
    }
    const uint64_t elapsed_ns = M88MonotonicNowNs() - frame_begin_ns;
    draw_next_ = elapsed_ns <= frame_period_ns_;
  }

 private:
  bool draw_next_ = false;
  uint32_t refresh_count_ = 0;
  uint32_t skipped_frames_ = 0;
  bool proceed_on_time_ = true;
  uint64_t frame_period_ns_ = 1'000'000ULL;
};
