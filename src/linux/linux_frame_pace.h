#pragma once

#include <SDL.h>

#include <algorithm>
#include <cstdint>

// Wall-clock pacing: one Proceed per loop, never faster than realtime.
// Matches the post-revert m88 path (frame work + delay >= frame_ms).
inline uint32_t M88FramePeriodMs(int texec, int config_speed) {
  return static_cast<uint32_t>(
      std::max<int64_t>(1, (static_cast<int64_t>(texec) * 10) / config_speed));
}

inline void M88PaceFrame(uint32_t frame_begin_ms, uint32_t frame_ms) {
  const uint32_t work_ms = SDL_GetTicks() - frame_begin_ms;
  if (work_ms < frame_ms) {
    SDL_Delay(frame_ms - work_ms);
  }
}
