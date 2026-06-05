# Linux Native Porting Guide

This repository currently contains a Visual Studio solution (`M88_2008.sln`) and
Win32-oriented frontend code in `src/win32`.

This document tracks the Linux-native porting path.

## Current status

- `m88_core` static library builds on Linux (GCC/Clang).
- `m88` SDL2 frontend executable added (`src/linux/main.cpp`).
- `m88-qt` Qt6 frontend (`src/qt/`, option `M88_BUILD_QT_FRONTEND=ON`).
  - 640x400 indexed framebuffer via `LinuxDraw`
  - Basic frame loop (Proceed / UpdateScreen / present)
  - CLI: `--scale N` (overrides ini), `--rom-dir`, `-d0`
  - `m88.ini` `ScreenScale=auto` (default) or integer; missing keys are merged on load
  - Startup prints `ScreenScale: N (auto|ini)` to stdout
- Still stubbed: keyboard input, audio, config file loading, module plugins.

## Build instructions (Linux)

Dependencies (openSUSE example):

```bash
sudo zypper install gcc-c++ cmake ninja pkg-config libSDL2-devel
# Qt frontend (optional):
sudo zypper install qt6-qtbase-gui-devel
```

Build core library and SDL2 frontend:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Build Qt frontend (`m88-qt`):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DM88_BUILD_QT_FRONTEND=ON
cmake --build build -j --target m88-qt
```

Run (requires `PC88.ROM` and `DISK.ROM` in cwd or `--rom-dir`):

```bash
./build/m88 --rom-dir /path/to/roms -d0 game.d88
./build/m88-qt --rom-dir build [--scale 2] [-d0 disk.d88]
```

## Porting phases

1. Core compile green
   - Keep `m88_core` warning-clean and buildable on GCC/Clang.
   - Prefer `src/devices/Z80c.cpp` path over `Z80_x86.cpp`.

2. Platform abstraction layer
   - Introduce interfaces for video, audio, input, timing, and file dialogs.
   - Move OS-specific code behind these interfaces.

3. Linux frontend
   - Implement Linux backend (recommended: SDL2).
   - Map keyboard/joystick/mouse and frame presentation.

4. Audio backend
   - Start with SDL2 audio callback or ALSA/Pulse backend.
   - Validate latency, sample format, and sync.

5. Plugin/module compatibility
   - Windows `.m88` module loading and DLL behavior must be redesigned for
     Linux (`.so`) or replaced by static integration.

6. Packaging/runtime assets
   - Define ROM/config search paths.
   - Add install rules and release packaging.

## Known risk areas

- Win32 message-loop assumptions in existing UI layer.
- DirectSound/winmm dependencies in sound path.
- DirectDraw/GDI/D2D rendering abstractions.
- ODBC dependency (`odbc32`) if still required in runtime.

## Next implementation tasks

- Add `src/platform/` interfaces (`platform_video.h`, `platform_audio.h`,
  `platform_input.h`).
- Create minimal Linux main entry (`src/linux/main.cpp`) that can initialize
  core and run one frame loop stub.
- Add `M88_BUILD_LINUX_FRONTEND` CMake option and SDL2 dependency wiring.
