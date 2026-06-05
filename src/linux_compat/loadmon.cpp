#include "loadmon.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unordered_map>

namespace {
constexpr uint64_t kReportIntervalUs = 1000000;

bool Enabled() {
  static int on = -1;
  if (on < 0) {
    const char* env = getenv("M88_LOADMON");
    on = (env && env[0] && std::strcmp(env, "0") != 0) ? 1 : 0;
  }
  return on != 0;
}

uint64_t NowUs() {
  timespec ts {};
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(ts.tv_sec) * 1000000u +
         static_cast<uint64_t>(ts.tv_nsec) / 1000u;
}

struct Region {
  uint64_t frame_us = 0;
  uint64_t start_us = 0;
  int depth = 0;
};

std::unordered_map<std::string, Region> g_regions;
std::unordered_map<std::string, uint64_t> g_window_us;
uint64_t g_frame_start_us = 0;
uint64_t g_window_start_us = 0;
int g_frame_count = 0;

}  // namespace

void M88LoadmonBegin(const char* name) {
  if (!Enabled() || !name || !*name) {
    return;
  }
  Region& r = g_regions[name];
  if (r.depth++ == 0) {
    r.start_us = NowUs();
  }
}

void M88LoadmonEnd(const char* name) {
  if (!Enabled() || !name || !*name) {
    return;
  }
  auto it = g_regions.find(name);
  if (it == g_regions.end() || it->second.depth <= 0) {
    return;
  }
  Region& r = it->second;
  if (--r.depth == 0) {
    r.frame_us += NowUs() - r.start_us;
  }
}

void M88LoadmonFrameBegin() {
  if (!Enabled()) {
    return;
  }
  g_frame_start_us = NowUs();
  if (g_window_start_us == 0) {
    g_window_start_us = g_frame_start_us;
  }
}

void M88LoadmonFrameEnd() {
  if (!Enabled()) {
    return;
  }

  const uint64_t frame_us = NowUs() - g_frame_start_us;
  g_window_us["__frame__"] += frame_us;
  for (auto& kv : g_regions) {
    g_window_us[kv.first] += kv.second.frame_us;
    kv.second.frame_us = 0;
  }
  ++g_frame_count;

  const uint64_t window_us = NowUs() - g_window_start_us;
  if (window_us < kReportIntervalUs) {
    return;
  }

  const double frames = static_cast<double>(g_frame_count);
  const double frame_ms = (static_cast<double>(g_window_us["__frame__"]) / frames) / 1000.0;
  std::fprintf(stderr, "M88: profile (~1s): frame=%.2fms", frame_ms);

  uint64_t tagged_us = 0;
  for (const auto& kv : g_window_us) {
    if (kv.first == "__frame__") {
      continue;
    }
    tagged_us += kv.second;
    std::fprintf(stderr, " %s=%.2fms", kv.first.c_str(),
                 (static_cast<double>(kv.second) / frames) / 1000.0);
  }

  const double tagged_ms = (static_cast<double>(tagged_us) / frames) / 1000.0;
  const double other_ms = frame_ms - tagged_ms;
  if (other_ms >= 0.05) {
    std::fprintf(stderr, " other=%.2fms", other_ms);
  }
  std::fprintf(stderr, " (%d frames)\n", g_frame_count);
  std::fflush(stderr);

  g_window_us.clear();
  g_frame_count = 0;
  g_window_start_us = NowUs();
}
