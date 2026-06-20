#pragma once

#include "linux_emu_time_pace.h"
#include "linux_sequencer.h"
#include "pc88/config.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>

class PC88;
class SharedFramebufferDraw;

namespace PC8801 {
class WinKeyIF;
}

// Windows Sequencer-style dedicated emulation thread.
// Proceed + internal frame sleep run here; the Qt controller thread handles input,
// reset, and disk operations while the emu thread is paused at a frame boundary.
class M88EmuThread {
 public:
  struct Params {
    PC88* vm = nullptr;
    M88Sequencer* seq = nullptr;
    const PC8801::Config* config = nullptr;
    SharedFramebufferDraw* draw = nullptr;
    PC8801::WinKeyIF* keyif = nullptr;
    std::atomic<int>* post_reset_frames = nullptr;
    std::atomic<int>* title_frame_count = nullptr;
    std::atomic<long>* title_exec_count = nullptr;
    std::atomic<bool>* running = nullptr;
    M88EmuTimePacer* emu_pacer = nullptr;
    std::function<M88EmuTimePacer::AudioHint()> audio_hint;
    std::function<void(int emu_ticks)> mix_audio_slice;
    std::function<void()> drain_audio;
    std::function<void(int emu_sleep_ticks)> prepare_audio_sleep;
    std::function<void(bool drew_screen)> on_frame;
    bool emu_realtime_priority = true;
  };

  M88EmuThread() = default;
  ~M88EmuThread();

  M88EmuThread(const M88EmuThread&) = delete;
  M88EmuThread& operator=(const M88EmuThread&) = delete;

  void Start(Params params);
  void Stop();
  void Join();

  // Block until the current frame finishes, then keep the emu loop idle.
  void Pause();
  void Resume();
  bool IsPaused() const { return !active_.load(std::memory_order_relaxed); }

 private:
  void ThreadMain();
  void SignalFrameBoundary();
  void WaitIfPaused();
  static void TryRaiseRealtimePriority();

  std::mutex join_mutex_;
  bool joined_ = false;

  Params params_{};
  std::thread thread_;
  std::mutex boundary_mutex_;
  std::condition_variable boundary_cv_;
  std::atomic<bool> active_{true};
  std::atomic<int> pause_depth_{0};
  std::atomic<bool> should_stop_{false};
  std::atomic<bool> at_frame_boundary_{false};
};
