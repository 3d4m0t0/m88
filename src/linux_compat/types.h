#pragma once

#include <cstddef>
#include <cstdint>

#define ENDIAN_IS_SMALL

typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned long ulong;

typedef std::uint8_t uint8;
typedef std::uint16_t uint16;
typedef std::uint32_t uint32;

typedef std::int8_t sint8;
typedef std::int16_t sint16;
typedef std::int32_t sint32;

typedef std::int8_t int8;
typedef std::int16_t int16;
typedef std::int32_t int32;

typedef uint32 packed;
#define PACK(p) ((p) | ((p) << 8) | ((p) << 16) | ((p) << 24))

typedef std::intptr_t intpointer;

#undef PTR_IDBIT

#define ALLOWBOUNDARYACCESS

#define STATIC_CAST(t, o) ((t)(o))
#define REINTERPRET_CAST(t, o) (*(t*)(void*)&(o))

#if !defined(_WIN32) && !defined(__stdcall)
  #define __stdcall
#endif

#ifndef interface
  #define interface struct
#endif

typedef void* HWND;
typedef void* HANDLE;
typedef unsigned int UINT;
typedef unsigned long WPARAM;
typedef long LPARAM;
typedef unsigned long DWORD;
typedef unsigned long long UINT_PTR;
typedef void PROPSHEETPAGE;
typedef int LONG_PTR;

struct POINT {
  long x;
  long y;
};

struct GUID {
  std::uint32_t Data1;
  std::uint16_t Data2;
  std::uint16_t Data3;
  std::uint8_t Data4[8];
};

typedef const GUID& REFIID;

#ifndef MAX_PATH
  #define MAX_PATH 260
#endif

#ifndef DEFINE_GUID
  #define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}
#endif

#define MEMCALL
