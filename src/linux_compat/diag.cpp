#include "headers.h"
#include "diag.h"

#include <chrono>

Diag::Diag(const char* logname) : file(nullptr) {
  if (logname && *logname) {
    file = std::fopen(logname, "a");
  }
}

Diag::~Diag() {
  if (file) {
    std::fclose(file);
  }
}

void Diag::Put(const char* fmt, ...) {
  if (!file || !fmt) return;

  std::va_list ap;
  va_start(ap, fmt);
  std::vfprintf(file, fmt, ap);
  std::fprintf(file, "\n");
  std::fflush(file);
  va_end(ap);
}

int Diag::GetCPUTick() {
  using namespace std::chrono;
  return static_cast<int>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count() &
      0x7fffffff);
}
