#pragma once

#include <cstdarg>
#include <cstdio>
#include <ctime>

class Diag {
public:
  explicit Diag(const char* logname);
  ~Diag();
  void Put(const char* fmt, ...);
  static int GetCPUTick();

private:
  FILE* file;
};

#if defined(_DEBUG) && defined(LOGNAME)
static Diag diag__(LOGNAME ".dmp");
#define LOG0 diag__.Put
#define LOG1 diag__.Put
#define LOG2 diag__.Put
#define LOG3 diag__.Put
#define LOG4 diag__.Put
#define LOG5 diag__.Put
#define LOG6 diag__.Put
#define LOG7 diag__.Put
#define LOG8 diag__.Put
#define LOG9 diag__.Put
#define DIAGINIT(z)
#define LOGGING
#define Log diag__.Put
#else
#define LOG0(...) void(0)
#define LOG1(...) void(0)
#define LOG2(...) void(0)
#define LOG3(...) void(0)
#define LOG4(...) void(0)
#define LOG5(...) void(0)
#define LOG6(...) void(0)
#define LOG7(...) void(0)
#define LOG8(...) void(0)
#define LOG9(...) void(0)
#define DIAGINIT(z)
#define Log 0 ? 0 :
#endif
