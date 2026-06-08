#include "headers.h"

#include "linux_reset_diag.h"
#include "linux_config.h"
#include "pc88/pc88.h"

namespace {

void LogProbe(const char* tag, const char* phase, int basicmode, int prev_basicmode,
              const PC88::M88CpuProbe& p) {
  if (prev_basicmode >= 0) {
    std::fprintf(stderr,
                 "M88: diag[%s] %s BASIC %d(%s)->%d(%s) PC1=%04x PC2=%04x "
                 "In30=%02x In31=%02x In40=%02x mem_p31=%02x crtc=%02x/%04x p53=%02x "
                 "exec1=%d exec2=%d\n",
                 tag, phase, prev_basicmode, M88BasicModeName(prev_basicmode), basicmode,
                 M88BasicModeName(basicmode), p.pc1, p.pc2, p.in30, p.in31, p.in40,
                 p.mem_port31, p.crtc_status, p.crtc_mode, p.scrn_port53, p.exec1,
                 p.exec2);
  } else {
    std::fprintf(stderr,
                 "M88: diag[%s] %s BASIC %d(%s) PC1=%04x PC2=%04x "
                 "In30=%02x In31=%02x In40=%02x mem_p31=%02x crtc=%02x/%04x p53=%02x "
                 "exec1=%d exec2=%d\n",
                 tag, phase, basicmode, M88BasicModeName(basicmode), p.pc1, p.pc2, p.in30,
                 p.in31, p.in40, p.mem_port31, p.crtc_status, p.crtc_mode, p.scrn_port53,
                 p.exec1, p.exec2);
  }
}

bool VrtcBitSet(uint in40) { return (in40 & 0x20) != 0; }

bool LooksLikeVrtcWait(uint pc) { return pc >= 0x6330 && pc <= 0x6360; }

void RunOneFrame(PC88& pc88, int host_clock, int effclock) {
  const int period = std::max(1, pc88.GetFramePeriod());
  const uint clk = static_cast<uint>(std::max(1, host_clock));
  const uint eff = static_cast<uint>(std::max(1, effclock));
  constexpr int kSlices = 16;
  const int slice = std::max(1, period / kSlices);
  pc88.TimeSync();
  for (int i = 0; i < kSlices; ++i) {
    pc88.Proceed(static_cast<uint>(slice), clk, eff);
    pc88.TimeSync();
  }
}

}  // namespace

void M88DiagAfterReset(PC88& pc88, const char* tag, int basicmode, int prev_basicmode,
                       int host_clock, int effclock) {
  if (!tag || !*tag) {
    tag = "reset";
  }

  const char* env = std::getenv("M88_DIAG_RESET");
  const bool deep = env && env[0] && env[0] != '0';
  const bool mode_switch = prev_basicmode >= 0 && prev_basicmode != basicmode;
  if (!deep && !mode_switch) {
    return;
  }

  PC88::M88CpuProbe before {};
  pc88.ProbeCpuState(&before);
  LogProbe(tag, "after_reset", basicmode, prev_basicmode, before);

  // Mode switch: snapshot only (no Proceed). Running frames here without
  // UpdateScreen desynced display vs F5 reset and caused immediate black screen.
  if (mode_switch && !deep) {
    return;
  }

  PC88::M88CpuProbe after1 {};
  RunOneFrame(pc88, host_clock, effclock);
  pc88.ProbeCpuState(&after1);
  LogProbe(tag, "after_1frame", basicmode, prev_basicmode, after1);

  const int delta_exec = after1.exec1 - before.exec1;
  std::fprintf(stderr,
               "M88: diag[%s] delta_exec1=%d In40_vrtc=%s\n", tag, delta_exec,
               VrtcBitSet(after1.in40) ? "set" : "clear");

  if (delta_exec <= 0 && after1.pc1 == before.pc1) {
    std::fprintf(stderr,
                 "M88: diag[%s] CPU_STALL suspected (PC1 frozen, exec1=%d)\n", tag,
                 delta_exec);
  }
  if (!VrtcBitSet(after1.in40) && LooksLikeVrtcWait(after1.pc1)) {
    std::fprintf(stderr,
                 "M88: diag[%s] VRTC_WAIT suspected (PC1=%04x, In40&20=0)\n", tag,
                 after1.pc1);
  }

  constexpr int kWatchFrames = 30;
  uint last_pc1 = after1.pc1;
  int same_pc_frames = 0;
  int vrtc_clear_frames = 0;
  for (int f = 2; f <= kWatchFrames; ++f) {
    RunOneFrame(pc88, host_clock, effclock);
    PC88::M88CpuProbe snap {};
    pc88.ProbeCpuState(&snap);
    if (snap.pc1 == last_pc1) {
      ++same_pc_frames;
    } else {
      same_pc_frames = 0;
      last_pc1 = snap.pc1;
    }
    if (!VrtcBitSet(snap.in40)) {
      ++vrtc_clear_frames;
    }
    if (same_pc_frames >= 5) {
      std::fprintf(stderr,
                   "M88: diag[%s] PC1 frozen %d frames at %04x "
                   "(frame %d) In40=%02x exec1=%d\n",
                   tag, same_pc_frames, snap.pc1, f, snap.in40, snap.exec1);
      if (!VrtcBitSet(snap.in40) && LooksLikeVrtcWait(snap.pc1)) {
        std::fprintf(stderr,
                     "M88: diag[%s] VRTC_WAIT confirmed at frame %d\n", tag, f);
      }
      break;
    }
  }
  if (vrtc_clear_frames >= kWatchFrames - 1) {
    std::fprintf(stderr,
                 "M88: diag[%s] In40 VRTC bit never set in %d frames "
                 "(CRTC/scheduler?)\n",
                 tag, kWatchFrames);
  }
}
