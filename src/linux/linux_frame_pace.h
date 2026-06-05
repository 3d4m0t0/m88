#pragma once

#include "loadmon.h"

#include <SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

// Wall-clock pacing: one Proceed per loop, never faster than realtime.
inline uint32_t M88FramePeriodMs(int texec, int config_speed) {
  return static_cast<uint32_t>(
      std::max<int64_t>(1, (static_cast<int64_t>(texec) * 10) / config_speed));
}

namespace M88FramePaceDetail {

// Hybrid wait: SDL_Delay for bulk, spin the last ~1ms.  Uses elapsed time from
// frame_begin so SDL_GetTicks wrap-around stays safe (deadline_ms = begin+period
// can break when ticks wrap).
inline void SleepRemainingMs(uint32_t frame_begin_ms, uint32_t frame_ms,
                             const volatile bool* stop = nullptr) {
  for (;;) {
    if (stop && !*stop) {
      return;
    }
    const uint32_t elapsed = SDL_GetTicks() - frame_begin_ms;
    if (elapsed >= frame_ms) {
      return;
    }
    const uint32_t remain = frame_ms - elapsed;
    if (remain > 2) {
      SDL_Delay(remain - 2);
    }
  }
}

}  // namespace M88FramePaceDetail

inline void M88PaceFrame(uint32_t frame_begin_ms, uint32_t frame_ms,
                          const volatile bool* stop = nullptr) {
  LOADBEGIN("Pace");
  M88FramePaceDetail::SleepRemainingMs(frame_begin_ms, frame_ms, stop);
  LOADEND("Pace");
}

inline void M88PaceFrame(uint32_t frame_begin_ms, uint32_t frame_ms,
                          const std::atomic<bool>* stop) {
  LOADBEGIN("Pace");
  for (;;) {
    if (stop && !stop->load(std::memory_order_relaxed)) {
      break;
    }
    const uint32_t elapsed = SDL_GetTicks() - frame_begin_ms;
    if (elapsed >= frame_ms) {
      break;
    }
    const uint32_t remain = frame_ms - elapsed;
    if (remain > 2) {
      SDL_Delay(remain - 2);
    }
  }
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
    frame_ms_ = 1;
  }

  bool AfterProceed(uint32_t frame_begin_ms, int texec, int config_speed,
                    int refresh_timing) {
    frame_ms_ = M88FramePeriodMs(texec, config_speed);
    const uint32_t timing = static_cast<uint32_t>(std::max(1, refresh_timing));
    const uint32_t elapsed = SDL_GetTicks() - frame_begin_ms;

    if (elapsed < frame_ms_) {
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

  void EndFrame(uint32_t frame_begin_ms) {
    if (!proceed_on_time_) {
      return;
    }
    const uint32_t elapsed = SDL_GetTicks() - frame_begin_ms;
    draw_next_ = elapsed <= frame_ms_;
  }

 private:
  bool draw_next_ = false;
  uint32_t refresh_count_ = 0;
  uint32_t skipped_frames_ = 0;
  bool proceed_on_time_ = true;
  uint32_t frame_ms_ = 1;
};
