#pragma once

#include "soundsrc.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Lock-free SPSC ring: stereo int16 PCM @ device output rate.
// Producer: emu thread only (PumpSpsc). Consumer: audio callback only.

class M88SpscPcmRing {
 public:
  bool Init(size_t capacity_frames) {
    if (capacity_frames < 64) {
      return false;
    }
    cap_ = capacity_frames;
    buf_.assign(cap_ * 2, 0);
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    return true;
  }

  void Reset() {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  size_t Capacity() const { return cap_; }

  size_t Avail() const {
    const uint64_t t = tail_.load(std::memory_order_acquire);
    const uint64_t h = head_.load(std::memory_order_acquire);
    return static_cast<size_t>(t - h);
  }

  size_t Free() const {
    const uint64_t used = Avail();
    return used >= cap_ ? 0 : cap_ - used;
  }

  // Producer only.
  size_t Push(const Sample* stereo_frames, size_t frames) {
    if (!stereo_frames || frames == 0 || cap_ == 0) {
      return 0;
    }
    const uint64_t h = head_.load(std::memory_order_acquire);
    uint64_t t = tail_.load(std::memory_order_relaxed);
    const uint64_t used = t - h;
    if (used >= cap_) {
      return 0;
    }
    const size_t n = static_cast<size_t>(std::min<uint64_t>(frames, cap_ - used));
    for (size_t i = 0; i < n; ++i) {
      const uint64_t slot = (t + i) % cap_;
      buf_[slot * 2] = stereo_frames[i * 2];
      buf_[slot * 2 + 1] = stereo_frames[i * 2 + 1];
    }
    tail_.store(t + n, std::memory_order_release);
    return n;
  }

  // Consumer only.
  size_t Pop(Sample* stereo_frames, size_t frames) {
    if (!stereo_frames || frames == 0 || cap_ == 0) {
      return 0;
    }
    uint64_t h = head_.load(std::memory_order_relaxed);
    const uint64_t t = tail_.load(std::memory_order_acquire);
    const uint64_t avail = t - h;
    if (avail == 0) {
      return 0;
    }
    const size_t n = static_cast<size_t>(std::min<uint64_t>(frames, avail));
    for (size_t i = 0; i < n; ++i) {
      const uint64_t slot = (h + i) % cap_;
      stereo_frames[i * 2] = buf_[slot * 2];
      stereo_frames[i * 2 + 1] = buf_[slot * 2 + 1];
    }
    head_.store(h + n, std::memory_order_release);
    return n;
  }

 private:
  size_t cap_ = 0;
  std::vector<Sample> buf_;
  std::atomic<uint64_t> head_{0};
  std::atomic<uint64_t> tail_{0};
};
