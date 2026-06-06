#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

// Monotonic wall clock for frame pacing (std::chrono::steady_clock, nanosecond resolution).

inline uint64_t M88MonotonicNowNs() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

inline uint64_t M88FramePeriodNs(int texec, int config_speed) {
  const int64_t ms =
      std::max<int64_t>(1, (static_cast<int64_t>(texec) * 10) / config_speed);
  return static_cast<uint64_t>(ms) * 1'000'000ULL;
}

namespace M88MonotonicDetail {

constexpr uint64_t kSpinThresholdNs = 1'000'000;  // spin the last ~1ms

template <typename StopFn>
inline void SleepRemainingNs(uint64_t begin_ns, uint64_t period_ns, StopFn stop) {
  for (;;) {
    if (stop()) {
      return;
    }
    const uint64_t now_ns = M88MonotonicNowNs();
    const uint64_t elapsed_ns = now_ns - begin_ns;
    if (elapsed_ns >= period_ns) {
      return;
    }
    const uint64_t remain_ns = period_ns - elapsed_ns;
    if (remain_ns > kSpinThresholdNs) {
      std::this_thread::sleep_for(
          std::chrono::nanoseconds(remain_ns - kSpinThresholdNs));
    }
  }
}

}  // namespace M88MonotonicDetail
