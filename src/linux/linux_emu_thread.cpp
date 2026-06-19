#include "headers.h"
#include "linux_emu_thread.h"

#include "half_kana_ime.h"
#include "linux_ime.h"
#include "loadmon.h"
#include "pc88/pc88.h"
#include "shared_framebuffer_draw.h"

#include <chrono>
#include <cstdio>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#endif

namespace {

struct EmuDrawCtx {
  PC88* pc88 = nullptr;
  SharedFramebufferDraw* draw = nullptr;
  std::atomic<int>* post_reset_frames = nullptr;
};

void EmuStageFrame(void* ctx, bool draw_flag) {
  auto* frame = static_cast<EmuDrawCtx*>(ctx);
  if (!frame || !frame->pc88) {
    return;
  }
  const int post_reset = frame->post_reset_frames
                             ? frame->post_reset_frames->load(std::memory_order_relaxed)
                             : 0;
  if (!draw_flag && post_reset <= 0) {
    return;
  }
  frame->pc88->UpdateScreen(true);
  if (frame->draw) {
    frame->draw->StageUiFrame();
  }
}

}  // namespace

M88EmuThread::~M88EmuThread() {
  Stop();
  Join();
}

void M88EmuThread::TryRaiseRealtimePriority() {
#if defined(__linux__)
  sched_param sp {};
  sp.sched_priority = 1;
  if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
    if (setpriority(PRIO_PROCESS, 0, -5) != 0) {
      std::fprintf(stderr, "M88: emu thread realtime priority unavailable\n");
    }
  }
#endif
}

void M88EmuThread::Start(Params params) {
  Stop();
  Join();
  params_ = std::move(params);
  should_stop_ = false;
  active_ = true;
  at_frame_boundary_ = false;
  thread_ = std::thread([this]() { ThreadMain(); });
  joined_ = false;
}

void M88EmuThread::Stop() {
  should_stop_ = true;
  active_ = true;
  boundary_cv_.notify_all();
  // Break out of a long Z80::ExecDual slice running on this thread.
  if (params_.vm) {
    params_.vm->BreakExecution();
  }
}

void M88EmuThread::Join() {
  std::lock_guard<std::mutex> lock(join_mutex_);
  if (joined_) {
    return;
  }
  if (thread_.joinable()) {
    thread_.join();
  }
  joined_ = true;
}

void M88EmuThread::SignalFrameBoundary() {
  {
    std::lock_guard<std::mutex> lock(boundary_mutex_);
    at_frame_boundary_ = true;
  }
  boundary_cv_.notify_all();
}

void M88EmuThread::WaitIfPaused() {
  if (active_.load(std::memory_order_relaxed)) {
    return;
  }
  SignalFrameBoundary();
  std::unique_lock<std::mutex> lock(boundary_mutex_);
  boundary_cv_.wait(lock, [this]() {
    return active_.load(std::memory_order_relaxed) || should_stop_.load();
  });
  at_frame_boundary_ = false;
}

void M88EmuThread::Pause() {
  active_ = false;
  std::unique_lock<std::mutex> lock(boundary_mutex_);
  boundary_cv_.wait(lock, [this]() {
    return at_frame_boundary_.load() || should_stop_.load();
  });
}

void M88EmuThread::Resume() {
  active_ = true;
  boundary_cv_.notify_all();
}

void M88EmuThread::ThreadMain() {
  if (params_.emu_realtime_priority) {
    TryRaiseRealtimePriority();
  }

  EmuDrawCtx draw_ctx;
  draw_ctx.pc88 = params_.vm;
  draw_ctx.draw = params_.draw;
  draw_ctx.post_reset_frames = params_.post_reset_frames;

  while (!should_stop_.load(std::memory_order_relaxed)) {
    WaitIfPaused();
    if (should_stop_.load(std::memory_order_relaxed)) {
      break;
    }
    if (!params_.running || !params_.running->load(std::memory_order_relaxed)) {
      SignalFrameBoundary();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    if (!params_.vm || !params_.seq) {
      SignalFrameBoundary();
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }

    at_frame_boundary_ = false;
    if (params_.config) {
      params_.seq->ApplyConfig(*params_.config);
    }

    const bool force_draw = draw_ctx.post_reset_frames &&
                            draw_ctx.post_reset_frames->load(std::memory_order_relaxed) > 0;

    M88LoadmonFrameBegin();
    const auto stop = [this]() {
      return should_stop_.load(std::memory_order_relaxed) ||
             (params_.running &&
              !params_.running->load(std::memory_order_relaxed));
    };

    M88EmuTimePacer::AudioHint audio {};
    if (params_.audio_hint) {
      audio = params_.audio_hint();
    }
    if (!params_.emu_pacer) {
      M88LoadmonFrameEnd();
      SignalFrameBoundary();
      continue;
    }
    (void)audio;
    const auto result = params_.seq->RunFrame(
        params_.vm, EmuStageFrame, &draw_ctx, force_draw, stop, *params_.emu_pacer,
        params_.audio_hint, params_.mix_audio_slice, params_.drain_audio,
        params_.prepare_audio_sleep);

    if (params_.title_exec_count) {
      params_.title_exec_count->fetch_add(params_.seq->TakeExecCount(),
                                        std::memory_order_relaxed);
    }

    if (params_.keyif) {
      LinuxIme::Pump(params_.keyif);
    }

    bool drew = false;
    if (!result.update_screen && !force_draw && params_.seq->IsFastMode()) {
      drew = false;
    } else {
      if (result.update_screen || force_draw) {
        if (draw_ctx.post_reset_frames &&
            draw_ctx.post_reset_frames->load(std::memory_order_relaxed) > 0) {
          draw_ctx.post_reset_frames->fetch_sub(1, std::memory_order_relaxed);
        }
      }
      if (params_.title_frame_count) {
        params_.title_frame_count->fetch_add(1, std::memory_order_relaxed);
      }
      drew = result.update_screen || force_draw;
    }

    if (params_.on_frame) {
      params_.on_frame(drew);
    }
    M88LoadmonFrameEnd();
    SignalFrameBoundary();
  }

  SignalFrameBoundary();
}
