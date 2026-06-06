# Linux Native Porting Guide

This repository currently contains a Visual Studio solution (`M88_2008.sln`) and
Win32-oriented frontend code in `src/win32`.

This document tracks the Linux-native porting path.

## Current status

- `m88_core` static library builds on Linux (GCC/Clang).
- `m88` SDL2 frontend executable added (`src/linux/main.cpp`).
- `m88-qt` Qt6 frontend (`src/qt/`, option `M88_BUILD_QT_FRONTEND=ON`).
  - Audio: miniaudio (`qt_miniaudio_sound.cpp`, vendored header); no SDL2 dependency.
  - Frame pacing: `std::chrono::steady_clock` (nanosecond resolution).
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
# Qt frontend (optional; audio via vendored miniaudio, not SDL2):
sudo zypper install qt6-qtbase-gui-devel
```

Build core library and SDL2 frontend (Release is the default):

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

Debug build:

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j
```

Optional host CPU tuning (GCC/Clang):

```bash
cmake -S . -B build -DM88_NATIVE_ARCH=ON
```

Build Qt frontend (`m88-qt`):

```bash
cmake -S . -B build -DM88_BUILD_QT_FRONTEND=ON
cmake --build build -j --target m88-qt
```

Simple frame profiler (`Core.CPU`, `Screen`, wall-clock frame time).
Use `build/ys3demo.d88` (in-store demo) as the sample disk image.
Run about **2 minutes** so the demo loop settles; the ys3demo in-store demo is
lightweight — measured steady-state (last 30s avg, Release build):

```bash
timeout 120 env M88_LOADMON=1 ./build/m88 --rom-dir build -d0 build/ys3demo.d88
timeout 120 env M88_LOADMON=1 ./build/m88-qt --rom-dir build -d0 build/ys3demo.d88
# ~1s rolling average on stderr; check lines from the last 30–60s, e.g.:
# M88: profile (~1s): frame=17.08ms Core.CPU=0.55ms Screen=0.02ms Pace=16.40ms Draw.Present=0.05ms other=0.06ms (59 frames)
# Pace = intentional frame wait; Draw.Present = SDL texture upload + flip.
# Linux default RefreshTiming=2 (draw skip); override in m88.ini if needed.
# Startup logs INDEX8 vs ARGB fallback reason when SDL_SetTexturePalette is unavailable.
```

Run (requires `pc88.rom` in cwd or `--rom-dir`; `disk.rom` is optional):

```bash
./build/m88 --rom-dir build -d0 build/ys3demo.d88
./build/m88-qt --rom-dir build [--scale 2] -d0 build/ys3demo.d88
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

## Alternating freeze / disk-boot workarounds (removed)

Earlier Linux bring-up added many patches aimed at **交互停止** (main/sub CPU
appearing to alternate stall) and **`-d0` disk boot** deadlocks. They did not
prove effective as a fix and were removed to match the Windows Sequencer model:

one `Proceed(frame_period)` per wall-clock frame, `subcpucontrol` from ini, Z80
`Sync` slack of 1 on PIO ports FC–FF, no ROM/PIO assists.

### Root-cause analysis (what was actually going wrong)

1. **Frame loop vs Windows Sequencer**  
   Windows runs one monolithic `Proceed(texec)` per frame. Early Linux code split
   the frame into slices so CRTC/VRTC scheduler edges would fire between CPU
   bursts (BIOS `port40` polling). That **breaks main/sub PIO handshakes** on
   ports FC–FF (documented in the original `linux_frame_pace.h` comment): with
   `-d0`, the main CPU and sub CPU must execute `IN`/`OUT` on Sync ports in
   lockstep.

2. **Misread “freeze” as CPU hang**  
   gdb often showed the main thread in `Present` / EGL wait. That is **rendering
   back-pressure**, not proof that the Z80 pair is deadlocked. Treating it as a
   CPU bug led to more CPU-side hacks.

3. **Workaround cascade made timing worse**  
   Patches piled on without fixing the scheduler model:
   - `M_Read2` / `S_Read2` fake `IN (FEh)` responses  
   - `Base::In40` VRTC bit synthesis  
   - CRTC longer `vretrace`  
   - Z80 `Sync` always-allow or large slack  
   - `subcpucontrol` forced off in `M88ApplyConfig`  
   - `stopwhenidle` cleared when disk mounted  
   - Sub ROM patch / CPU kick to `00C1`  
   - Unconditional `Present` when disk mounted  

   These let one CPU **run ahead** of the other, so disk boot either deadlocked
   at `37dc`/`06e4` or fell through to ROM BASIC after retries.

4. **What actually helped disk boot (when it worked)**  
   Reverting to **monolithic Proceed with disk mounted** and removing fake PIO
   assists—not the ROM patches or VRTC kicks.

### Current Linux frame loop (baseline)

- `pc88.Proceed(GetFramePeriod(), clock, effclock)` once per frame  
- `M88DrawSkip` + `M88PaceFrame` for draw and realtime pacing (same idea as
  Windows Sequencer refresh timing)  
- No VRTC watchdog, no Proceed slices, no disk-specific CPU assists  

### If alternating stop or disk boot regresses

Debug in this order:

1. Confirm one full-frame `Proceed` (no slices).  
2. Compare `subcpucontrol` on/off with the same disk image.  
3. Log main/sub PC when stall happens (`cpu1`, `cpu2`); typical disk-boot
   waits: main `37dc` (`IN FE`), sub `06e4` (`7F16` handshake) or sub `00cc`
   (disk BIOS).  
4. Use gdb: if backtrace is in `Present`/EGL, investigate GPU/present rate before
   changing CPU emulation.
