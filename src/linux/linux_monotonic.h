#pragma once

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <ctime>
#include <time.h>

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

// Kernel sleep (clock_nanosleep) is often ~1ms granular; spin only the tail.
constexpr uint64_t kSpinThresholdNs = 500'000;

inline void CpuRelax() {
#if defined(__x86_64__) || defined(__i386__)
  __builtin_ia32_pause();
#elif defined(__aarch64__)
  __asm__ __volatile__("yield");
#endif
}

inline timespec NsToTimespec(uint64_t ns) {
  timespec ts {};
  ts.tv_sec = static_cast<time_t>(ns / 1'000'000'000ULL);
  ts.tv_nsec = static_cast<long>(ns % 1'000'000'000ULL);
  return ts;
}

template <typename StopFn>
inline void ClockNanosleepAbs(uint64_t deadline_ns, StopFn stop) {
  timespec ts = NsToTimespec(deadline_ns);
  while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr) == -1) {
    if (errno != EINTR || stop()) {
      return;
    }
  }
}

template <typename StopFn>
inline void SleepRemainingNs(uint64_t begin_ns, uint64_t period_ns, StopFn stop) {
  const uint64_t deadline_ns = begin_ns + period_ns;
  const uint64_t coarse_deadline_ns =
      deadline_ns > kSpinThresholdNs ? deadline_ns - kSpinThresholdNs : begin_ns;

  for (;;) {
    if (stop()) {
      return;
    }
    const uint64_t now_ns = M88MonotonicNowNs();
    if (now_ns >= deadline_ns) {
      return;
    }
    if (now_ns < coarse_deadline_ns) {
      ClockNanosleepAbs(coarse_deadline_ns, stop);
      continue;
    }
    CpuRelax();
  }
}

}  // namespace M88MonotonicDetail
