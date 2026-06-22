#pragma once

#include <atomic>
#include <cstdint>

class PC88;

#if defined(__linux__)
#include <pthread.h>
#endif

// Debug-only: detects when RunFrame / Z80::ExecDual stops making progress and
// dumps diagnostic state to stderr (M88_STALL_WATCHDOG=1 at compile time).
// Runtime env: M88_STALL_WATCHDOG_MS (default 3000), M88_STALL_WATCHDOG_BT.

struct M88StallWatchdogEmuState {
  bool thread_active = true;
  bool should_stop = false;
  int pause_depth = 0;
  bool in_run_frame = false;
};

#if defined(M88_STALL_WATCHDOG) && M88_STALL_WATCHDOG

void M88StallWatchdogInit();
void M88StallWatchdogShutdown();

void M88StallWatchdogSetEmuThread(
#if defined(__linux__)
    pthread_t native_handle
#else
    void* native_handle
#endif
);

void M88StallWatchdogFrameBegin();
void M88StallWatchdogFrameEnd();
void M88StallWatchdogProceedSlice();

void M88StallWatchdogPoll(bool emu_running, const M88StallWatchdogEmuState& emu,
                          PC88* vm);

#else

inline void M88StallWatchdogInit() {}
inline void M88StallWatchdogShutdown() {}
inline void M88StallWatchdogSetEmuThread(
#if defined(__linux__)
    pthread_t
#else
    void*
#endif
) {}
inline void M88StallWatchdogFrameBegin() {}
inline void M88StallWatchdogFrameEnd() {}
inline void M88StallWatchdogProceedSlice() {}
inline void M88StallWatchdogPoll(bool, const M88StallWatchdogEmuState&, PC88*) {}

#endif
