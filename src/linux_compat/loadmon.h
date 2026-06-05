#pragma once

// Lightweight frame profiler for Linux (stderr, M88_LOADMON=1).
// Existing LOADBEGIN/LOADEND markers in pc88.cpp (Core.CPU, Screen) are aggregated.

void M88LoadmonBegin(const char* name);
void M88LoadmonEnd(const char* name);
void M88LoadmonFrameBegin();
void M88LoadmonFrameEnd();

#ifndef LOADBEGIN
  #define LOADBEGIN(name) M88LoadmonBegin(name)
#endif
#ifndef LOADEND
  #define LOADEND(name) M88LoadmonEnd(name)
#endif
