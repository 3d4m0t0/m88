#pragma once

// Linux core-only build: load monitor UI is not available.

#ifndef LOADBEGIN
  #define LOADBEGIN(name) ((void)0)
#endif
#ifndef LOADEND
  #define LOADEND(name) ((void)0)
#endif
