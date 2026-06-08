#ifdef M88_LINUX_PORT

#include "rom_log.h"
#include "path.h"

#include <cstdio>

namespace {

int g_step = 0;

}  // namespace

void M88RomLogBegin() {
  g_step = 0;
  std::fprintf(stdout, "M88: ROM load sequence (dir=%s)\n", m88dir);
  std::fflush(stdout);
}

void M88RomLogLoaded(const char* path, const char* detail) {
  ++g_step;
  if (detail && detail[0]) {
    std::fprintf(stdout, "M88 ROM[%02d] loaded: %s (%s)\n", g_step, path, detail);
  } else {
    std::fprintf(stdout, "M88 ROM[%02d] loaded: %s\n", g_step, path);
  }
  std::fflush(stdout);
}

void M88RomLogSkipped(const char* path, const char* reason) {
  ++g_step;
  std::fprintf(stdout, "M88 ROM[%02d] skip: %s -- %s\n", g_step, path, reason);
  std::fflush(stdout);
}

void M88RomLogFallback(const char* reason) {
  ++g_step;
  std::fprintf(stdout, "M88 ROM[%02d] fallback: %s\n", g_step, reason);
  std::fflush(stdout);
}

void M88RomLogEnd(bool ok) {
  std::fprintf(stdout, "M88: ROM load sequence %s (%d entries)\n",
               ok ? "complete" : "FAILED", g_step);
  std::fflush(stdout);
}

#endif
