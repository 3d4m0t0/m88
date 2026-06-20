#pragma once

#include "../win32/version.h"

// Linux/Qt port SemVer (UI, build, Linux-specific layers). Bump independently of core.
#define M88_PORT_VER_MAJOR 1
#define M88_PORT_VER_MINOR 1
#define M88_PORT_VER_PATCH 0
#define M88_PORT_VER_STRING "1.1.0"

// Full release id: core emulation version + Qt port version (e.g. 2.21-qt1.1.0).
#define M88_LINUX_QT_VER_STRING APP_VER_STRING "-qt" M88_PORT_VER_STRING
