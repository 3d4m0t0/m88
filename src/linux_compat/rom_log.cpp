#ifdef M88_LINUX_PORT

#include "rom_log.h"
#include "path.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr const char* kEntryIndent = "     ";

int g_rom_step = 0;
int g_wav_step = 0;
std::vector<std::string> g_wav_log_buffer;

void FlushWavLogBuffer() {
  if (g_wav_log_buffer.empty()) {
    return;
  }
  for (const auto& line : g_wav_log_buffer) {
    std::fputs(line.c_str(), stdout);
  }
  g_wav_log_buffer.clear();
  std::fflush(stdout);
}

}  // namespace

void M88RomLogBegin() {
  g_rom_step = 0;
  std::fprintf(stdout, "M88: ROM load sequence (dir=%s)\n", m88dir);
  std::fflush(stdout);
}

void M88RomLogLoaded(const char* path, const char* detail) {
  ++g_rom_step;
  if (detail && detail[0]) {
    std::fprintf(stdout, "%sROM[%02d] loaded: %s (%s)\n", kEntryIndent, g_rom_step, path,
                 detail);
  } else {
    std::fprintf(stdout, "%sROM[%02d] loaded: %s\n", kEntryIndent, g_rom_step, path);
  }
  std::fflush(stdout);
}

void M88RomLogSkipped(const char* path, const char* reason) {
  ++g_rom_step;
  std::fprintf(stdout, "%sROM[%02d] skip: %s -- %s\n", kEntryIndent, g_rom_step, path,
               reason);
  std::fflush(stdout);
}

void M88RomLogFallback(const char* reason) {
  ++g_rom_step;
  std::fprintf(stdout, "%sROM[%02d] fallback: %s\n", kEntryIndent, g_rom_step, reason);
  std::fflush(stdout);
}

void M88RomLogEnd(bool ok) {
  std::fprintf(stdout, "M88: ROM load sequence %s (%d entries)\n",
               ok ? "complete" : "FAILED", g_rom_step);
  FlushWavLogBuffer();
  std::fflush(stdout);
}

void M88LogRequiredRomMissing(const char* rom_dir) {
  std::fprintf(stdout,
               "M88: required ROM not found in %s (expected pc88.rom or n88.rom)\n",
               rom_dir && *rom_dir ? rom_dir : ".");
  std::fflush(stdout);
}

void M88WavLogBegin() {
  g_wav_step = 0;
  g_wav_log_buffer.clear();
  char line[512];
  std::snprintf(line, sizeof(line), "M88: WAV load sequence (dir=%s)\n", m88dir);
  g_wav_log_buffer.emplace_back(line);
}

void M88WavLogLoaded(const char* path, const char* detail) {
  ++g_wav_step;
  char line[1024];
  if (detail && detail[0]) {
    std::snprintf(line, sizeof(line), "%sWAV[%02d] loaded: %s (%s)\n", kEntryIndent,
                  g_wav_step, path, detail);
  } else {
    std::snprintf(line, sizeof(line), "%sWAV[%02d] loaded: %s\n", kEntryIndent, g_wav_step,
                  path);
  }
  g_wav_log_buffer.emplace_back(line);
}

void M88WavLogSkipped(const char* path, const char* reason) {
  ++g_wav_step;
  char line[1024];
  std::snprintf(line, sizeof(line), "%sWAV[%02d] skip: %s -- %s\n", kEntryIndent,
                g_wav_step, path, reason);
  g_wav_log_buffer.emplace_back(line);
}

void M88WavLogEnd(bool ok) {
  char line[128];
  std::snprintf(line, sizeof(line), "M88: WAV load sequence %s (%d entries)\n",
                ok ? "complete" : "FAILED", g_wav_step);
  g_wav_log_buffer.emplace_back(line);
}

#endif
