#pragma once

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <strings.h>

#include "types.h"

#ifndef strnicmp
  #define strnicmp strncasecmp
#endif

#ifndef localtime_s
  #define localtime_s(tm_ptr, time_ptr) localtime_r((time_ptr), (tm_ptr))
#endif

#ifndef MulDiv
inline int MulDiv(int number, int numerator, int denominator) {
  if (denominator == 0) return 0;
  return static_cast<int>((static_cast<long long>(number) * numerator) / denominator);
}
#endif

using namespace std;
