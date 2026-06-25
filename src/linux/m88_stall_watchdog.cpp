#if defined(M88_STALL_WATCHDOG) && M88_STALL_WATCHDOG

#include "m88_stall_watchdog.h"

#include "headers.h"
#include "linux_monotonic.h"
#include "pc88/pc88.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__linux__)
#include <execinfo.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace {

constexpr uint64_t kDefaultThresholdMs = 3000;

std::atomic<uint64_t> g_last_progress_ns{0};
std::atomic<bool> g_in_run_frame{false};
std::atomic<uint64_t> g_stall_active_since_ns{0};
std::atomic<bool> g_dumped_this_stall{false};

#if defined(__linux__)
pthread_t g_emu_thread = 0;
std::atomic<bool> g_bt_handler_installed{false};

void StallBacktraceHandler(int) {
  void* frames[64];
  const int count = backtrace(frames, static_cast<int>(sizeof(frames) / sizeof(frames[0])));
  std::fprintf(stderr, "M88: emu thread backtrace (%d frames):\n", count);
  backtrace_symbols_fd(frames, count, STDERR_FILENO);
  std::fflush(stderr);
}

void EnsureBacktraceHandler() {
  if (g_bt_handler_installed.exchange(true, std::memory_order_relaxed)) {
    return;
  }
  struct sigaction sa {};
  sa.sa_handler = StallBacktraceHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, nullptr);
}
#endif

bool Enabled() {
  static int on = -1;
  if (on < 0) {
    const char* env = std::getenv("M88_STALL_WATCHDOG");
    on = (env && env[0] && std::strcmp(env, "0") == 0) ? 0 : 1;
  }
  return on != 0;
}

uint64_t ThresholdNs() {
  static uint64_t threshold_ns = 0;
  if (threshold_ns == 0) {
    uint64_t ms = kDefaultThresholdMs;
    if (const char* env = std::getenv("M88_STALL_WATCHDOG_MS")) {
      const long parsed = std::strtol(env, nullptr, 10);
      if (parsed > 0) {
        ms = static_cast<uint64_t>(parsed);
      }
    }
    threshold_ns = ms * 1'000'000ULL;
  }
  return threshold_ns;
}

bool BacktraceOnStall() {
  static int on = -1;
  if (on < 0) {
    const char* env = std::getenv("M88_STALL_WATCHDOG_BT");
    on = (!env || env[0] == '\0' || std::strcmp(env, "0") != 0) ? 1 : 0;
  }
  return on != 0;
}

void TouchProgress() {
  const uint64_t now = M88MonotonicNowNs();
  g_last_progress_ns.store(now, std::memory_order_release);
  g_stall_active_since_ns.store(0, std::memory_order_release);
  g_dumped_this_stall.store(false, std::memory_order_release);
}

const char* FdcPhaseName(int phase) {
  static const char* kNames[] = {
      "idle",     "command",  "execute",   "execread", "execwrite",
      "result",   "tc",       "timer",     "execscan",
  };
  if (phase >= 0 && phase < static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) {
    return kNames[phase];
  }
  return "?";
}

void DumpVmState(PC88* vm, uint64_t stalled_ns) {
  if (!vm) {
    std::fprintf(stderr, "M88: exec stall (%.2fs): PC88 unavailable\n",
                 static_cast<double>(stalled_ns) / 1e9);
    return;
  }

  auto* cpu1 = vm->GetCPU1();
  auto* cpu2 = vm->GetCPU2();
  PC88::StallDiag diag {};
  vm->FillStallDiag(&diag);

  const uint main_pc = cpu1 ? cpu1->GetPC() : 0;
  const uint sub_pc = cpu2 ? cpu2->GetPC() : 0;
  const uint8 main_i = cpu1 ? cpu1->GetReg().ireg : 0;
  const uint8 sub_i = cpu2 ? cpu2->GetReg().ireg : 0;
  const int main_count = cpu1 ? cpu1->GetCount() : 0;
  const int sub_count = cpu2 ? cpu2->GetCount() : 0;

  std::fprintf(stderr,
               "M88: exec stall detected (no progress for %.2fs, RunFrame/ExecDual)\n",
               static_cast<double>(stalled_ns) / 1e9);
  std::fprintf(stderr,
               "  guest: main PC=%04X I=%02X count=%d | sub PC=%04X I=%02X count=%d\n",
               main_pc, main_i, main_count, sub_pc, sub_i, sub_count);
  std::fprintf(stderr, "  mode: cpumode=0x%X run_dual=%d ms11=%d stopwhenidle=%d\n",
               diag.cpumode, diag.run_dual ? 1 : 0, (diag.cpumode & 1) != 0 ? 1 : 0,
               (diag.cpumode & 4) != 0 ? 1 : 0);
  std::fprintf(stderr, "  fdc: busy=%d phase=%s(%d) status=0x%02X\n", diag.fdc_busy ? 1 : 0,
               FdcPhaseName(diag.fdc_phase), diag.fdc_phase,
               static_cast<unsigned>(diag.fdc_status));
  std::fprintf(stderr, "  subsys: main_fdif_active=%d isbusy=%d idlecount=%u\n",
               diag.main_fdif_active ? 1 : 0, diag.sub_isbusy ? 1 : 0, diag.sub_idlecount);
  std::fprintf(stderr, "  host: clock=%u eclock=%u dexc=%d\n", diag.clock, diag.eclock,
               diag.dexc);
  std::fflush(stderr);
}

void RequestEmuBacktrace() {
#if defined(__linux__)
  if (!BacktraceOnStall()) {
    return;
  }
  const pthread_t tid = g_emu_thread;
  if (tid == 0) {
    return;
  }
  EnsureBacktraceHandler();
  pthread_kill(tid, SIGUSR1);
#endif
}

}  // namespace

void M88StallWatchdogInit() {
  TouchProgress();
  if (Enabled()) {
    std::fprintf(stderr, "M88: stall watchdog enabled (threshold=%llums)\n",
                 static_cast<unsigned long long>(ThresholdNs() / 1'000'000ULL));
    std::fflush(stderr);
  }
}

void M88StallWatchdogShutdown() {
  g_in_run_frame.store(false, std::memory_order_release);
  TouchProgress();
}

void M88StallWatchdogSetEmuThread(
#if defined(__linux__)
    pthread_t native_handle
#else
    void* native_handle
#endif
) {
#if defined(__linux__)
  g_emu_thread = native_handle;
#else
  (void)native_handle;
#endif
}

void M88StallWatchdogFrameBegin() {
  if (!Enabled()) {
    return;
  }
  g_in_run_frame.store(true, std::memory_order_release);
}

void M88StallWatchdogFrameEnd() {
  if (!Enabled()) {
    return;
  }
  g_in_run_frame.store(false, std::memory_order_release);
  TouchProgress();
}

void M88StallWatchdogProceedSlice() {
  if (!Enabled()) {
    return;
  }
  TouchProgress();
}

void M88StallWatchdogPoll(bool emu_running, const M88StallWatchdogEmuState& emu,
                          PC88* vm) {
  if (!Enabled() || !emu_running) {
    TouchProgress();
    return;
  }
  if (!emu.in_run_frame && !g_in_run_frame.load(std::memory_order_acquire)) {
    TouchProgress();
    return;
  }
  if (emu.should_stop || !emu.thread_active) {
    TouchProgress();
    return;
  }
  if (emu.pause_depth > 0 && !g_in_run_frame.load(std::memory_order_acquire)) {
    TouchProgress();
    return;
  }

  const uint64_t now = M88MonotonicNowNs();
  const uint64_t last = g_last_progress_ns.load(std::memory_order_acquire);
  if (last == 0 || now <= last) {
    return;
  }
  const uint64_t stalled_ns = now - last;
  if (stalled_ns < ThresholdNs()) {
    return;
  }

  uint64_t since = g_stall_active_since_ns.load(std::memory_order_acquire);
  if (since == 0) {
    g_stall_active_since_ns.store(now, std::memory_order_release);
    since = now;
  }

  if (g_dumped_this_stall.load(std::memory_order_acquire)) {
    return;
  }
  g_dumped_this_stall.store(true, std::memory_order_release);

  DumpVmState(vm, stalled_ns);
  std::fprintf(stderr,
               "  emu_thread: active=%d pause_depth=%d should_stop=%d in_run_frame=%d\n",
               emu.thread_active ? 1 : 0, emu.pause_depth, emu.should_stop ? 1 : 0,
               g_in_run_frame.load(std::memory_order_acquire) ? 1 : 0);
  std::fflush(stderr);
  RequestEmuBacktrace();
}

#endif  // M88_STALL_WATCHDOG
