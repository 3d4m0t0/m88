#pragma once

#include <algorithm>
#include <cstdint>

// Target ~12 ms per device callback; large periods at low sample rates cause
// bursty drain and audible tempo wobble when the ring-level pacer reacts.
inline uint M88AudioPeriodFrames(uint sample_rate_hz, int ring_samples) {
  if (sample_rate_hz < 8000) {
    return 512;
  }
  constexpr uint kTargetMs = 12;
  uint period = std::max(128u, (sample_rate_hz * kTargetMs) / 1000);
  period = std::min(period, 512u);
  if (ring_samples > 0) {
    period = std::min(period, static_cast<uint>(std::max(128, ring_samples / 8)));
  }
  return period;
}
