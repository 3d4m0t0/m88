#include "headers.h"
#include "linux_config.h"
#include "linux_paths.h"

#include "display_scale.h"
#include "linux_draw.h"
#include "half_kana_ime.h"
#include "linux_ime.h"
#include "linux_input.h"
#include "linux_emulation.h"
#include "linux_emu_time_pace.h"
#include "linux_sound.h"
#include "linux_startup_log.h"
#include "loadmon.h"
#include "path.h"
#include "../win32/WinKeyIF.h"
#include "../linux_compat/winkeys.h"

#include "error.h"
#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "pc88/opnif.h"
#include "pc88/pc88.h"
#include "pc88/beep.h"
#include "pc88/tapemgr.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

void PrintUsage(const char* prog) {
  std::fprintf(stderr,
               "Usage: %s [options]\n"
               "  --scale N         Window integer scale (overrides m88.ini ScreenScale)\n"
               "  --rom-dir PATH    Directory containing pc88.rom or split ROMs (n88.rom, ...)\n"
               "                    (default: current working directory)\n"
               "  -d0 FILE          Mount disk image on drive 0 (optional)\n"
               "  --config FILE     Load settings from m88.ini-style file\n"
               "  --keyboard TYPE   Override auto-detected keyboard (101/us, 106/jp, 98/pc98)\n"
               "  --arrow-tenkey    Map arrow keys to ten-key (UseArrowForTenKey=1)\n"
               "  F5                Reset machine (host key, not sent to guest)\n"
               "  -h, --help        Show this help\n"
               "\n"
               "If ./m88.ini exists it is loaded automatically (same format as Windows M88).\n"
               "Keyboard matrix is detected at startup (setxkbmap / localectl / locale).\n"
               "Example m88.ini entries:\n"
               "  Flags=...            (bit19=OPNA a8h sound board 2, bit18=OPN a8h)\n",
               prog);
}

bool RomFileExists(const char* filename) {
  return M88RomExists(filename);
}

bool DiskFileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void MountDiskOptional(DiskManager& diskmgr, int drive, const char* path) {
  diskmgr.Unmount(drive);
  if (drive == 1) {
    return;
  }
  diskmgr.Unmount(1);
  if (!path || !*path) {
    return;
  }
  if (!DiskFileExists(path)) {
    std::fprintf(stderr, "Disk not found, starting without disk: %s\n", path);
    return;
  }
  if (!diskmgr.Mount(static_cast<uint>(drive), path, false, 0, false)) {
    diskmgr.Unmount(drive);
    std::fprintf(stderr, "Failed to mount disk, starting without disk: %s\n", path);
    return;
  }
  std::fprintf(stderr, "M88: mounted drive %d: %s\n", drive, path);
  // Matches WinUI::OpenDiskImage(): multi-disk D88 images mount index 1 on drive 1.
  if (diskmgr.GetNumDisks(0) > 1) {
    if (!diskmgr.Mount(1, path, false, 1, false)) {
      diskmgr.Unmount(1);
      std::fprintf(stderr,
                   "Warning: failed to mount second disk in image: %s\n", path);
    }
  }
}

struct AutoKeyEvent {
  uint vk = 0;
  uint32 delay_ms = 0;
  bool fired = false;
};

uint AutoKeyNameToVk(const char* name, size_t len) {
  if (len == 5 && std::strncmp(name, "space", 5) == 0) {
    return VK_SPACE;
  }
  if (len == 6 && std::strncmp(name, "return", 6) == 0) {
    return VK_RETURN;
  }
  if (len == 2 && name[0] == 'f' && name[1] >= '1' && name[1] <= '9') {
    return static_cast<uint>(VK_F1 + (name[1] - '1'));
  }
  return 0;
}

bool ParseAutoKeySpec(const char* spec, std::vector<AutoKeyEvent>* out) {
  if (!spec || !*spec || !out) {
    return false;
  }
  out->clear();
  const char* p = spec;
  while (*p) {
    const char* comma = std::strchr(p, ',');
    const size_t toklen = comma ? static_cast<size_t>(comma - p) : std::strlen(p);
    const char* at = static_cast<const char*>(std::memchr(p, '@', toklen));
    if (!at || at == p) {
      return false;
    }
    const size_t namelen = static_cast<size_t>(at - p);
    const uint vk = AutoKeyNameToVk(p, namelen);
    if (!vk) {
      return false;
    }
    char* end = nullptr;
    const long sec = std::strtol(at + 1, &end, 10);
    if (end == at + 1 || sec < 0) {
      return false;
    }
    AutoKeyEvent ev;
    ev.vk = vk;
    ev.delay_ms = static_cast<uint32>(sec) * 1000u;
    out->push_back(ev);
    if (!comma) {
      break;
    }
    p = comma + 1;
  }
  return !out->empty();
}

void FireAutoKeys(PC8801::WinKeyIF& keyif, std::vector<AutoKeyEvent>& keys,
                  uint32 elapsed_ms) {
  for (AutoKeyEvent& ev : keys) {
    if (!ev.fired && elapsed_ms >= ev.delay_ms) {
      keyif.KeyDown(ev.vk, 0);
      keyif.KeyUp(ev.vk, 0);
      ev.fired = true;
      std::fprintf(stderr, "m88: auto-key vk=0x%x at %ums\n", ev.vk, elapsed_ms);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  int scale = 0;
  bool scale_explicit = false;
  const char* rom_dir = nullptr;
  const char* disk0 = nullptr;
  const char* config_file = nullptr;
  const char* keyboard_type = nullptr;
  bool arrow_tenkey = false;
  bool keyboard_type_set = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "-h") == 0 || std::strcmp(argv[i], "--help") == 0) {
      PrintUsage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      scale = std::atoi(argv[++i]);
      scale_explicit = true;
      continue;
    }
    if (std::strcmp(argv[i], "--rom-dir") == 0 && i + 1 < argc) {
      rom_dir = argv[++i];
      continue;
    }
    if (std::strcmp(argv[i], "-d0") == 0 && i + 1 < argc) {
      disk0 = argv[++i];
      continue;
    }
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      config_file = argv[++i];
      continue;
    }
    if (std::strcmp(argv[i], "--keyboard") == 0 && i + 1 < argc) {
      keyboard_type = argv[++i];
      keyboard_type_set = true;
      continue;
    }
    if (std::strcmp(argv[i], "--arrow-tenkey") == 0) {
      arrow_tenkey = true;
      continue;
    }
    std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
    PrintUsage(argv[0]);
    return 1;
  }

  PC8801::Config config;
  char ini_path[512];
  bool ini_created = false;
  M88LoadStartupConfig(&config, config_file, ini_path, sizeof(ini_path), &ini_created);
  M88ApplyEnvOverrides(&config);
  M88LogConfigPath(ini_path, ini_created);
  M88LogDataPaths();

  M88InitRomPath(rom_dir);

  if (!M88HasRequiredRoms()) {
    std::fprintf(stderr,
                 "ROM not found in %s (expected pc88.rom or n88.rom).\n",
                 m88dir);
    return 1;
  }
  M88ApplyStartupDirectory(&config, ini_path, disk0 != nullptr);
  M88LogWorkingDirectory();

  std::vector<AutoKeyEvent> auto_keys;
  if (const char* aks = std::getenv("M88_AUTOKEY")) {
    if (!ParseAutoKeySpec(aks, &auto_keys)) {
      std::fprintf(stderr, "Warning: invalid M88_AUTOKEY (use name@sec,...): %s\n",
                   aks);
      auto_keys.clear();
    }
  }
  if (keyboard_type_set) {
    const int keytype = M88ParseKeyboardType(keyboard_type);
    if (keytype < 0) {
      std::fprintf(stderr, "Unknown keyboard type: %s\n", keyboard_type);
      PrintUsage(argv[0]);
      return 1;
    }
    config.keytype = static_cast<PC8801::Config::KeyType>(keytype);
    if (config.keytype == PC8801::Config::PC98) {
      config.keytype = PC8801::Config::AT106;
    }
    M88NoteKeyboardCliOverride();
  } else if (!M88IniHasHostKeyboard()) {
    M88ApplyDetectedKeyboard(&config);
  }
  M88LoadKeyFixup(ini_path, &config);
  if (arrow_tenkey) {
    config.flags |= PC8801::Config::usearrowfor10;
  }

  LinuxDraw draw;
  PC8801::LinuxSound sound;
  PC8801::WinKeyIF keyif;

  DiskManager diskmgr;
  if (!diskmgr.Init()) {
    std::fprintf(stderr, "Failed to initialize disk manager.\n");
    return 1;
  }

  TapeManager tapemgr;
  PC88 pc88;
  if (!pc88.Init(&draw, &diskmgr, &tapemgr)) {
    std::fprintf(stderr, "Failed to initialize PC-8801 core.\n");
    if (const char* detail = Error::GetErrorText()) {
      std::fprintf(stderr, "%s\n", detail);
    }
    return 1;
  }
  if (!keyif.Init()) {
    std::fprintf(stderr, "Failed to initialize keyboard interface.\n");
    return 1;
  }
  if (!pc88.ConnectKeyboard(&keyif)) {
    std::fprintf(stderr, "Failed to connect keyboard interface.\n");
    return 1;
  }
  {
    int desktop_w = 0;
    int desktop_h = 0;
    if (SDL_Init(SDL_INIT_VIDEO) == 0) {
      SDL_Rect desktop {};
      if (SDL_GetDisplayUsableBounds(0, &desktop) == 0) {
        desktop_w = desktop.w;
        desktop_h = desktop.h;
      }
    }
    scale = M88ResolveScreenScale(desktop_w, desktop_h, 0, 0, scale, scale_explicit);
    M88PrintScreenScale(scale, scale_explicit);
  }
  if (!draw.InitWindow("M88 (Linux)", scale)) {
    std::fprintf(stderr, "Failed to initialize SDL2 window: %s\n", SDL_GetError());
    return 1;
  }
  LinuxIme::OnWindowShown(&draw);
  if (const char* adpcm = std::getenv("M88_DEBUG_ADPCM");
      adpcm && adpcm[0] && adpcm[0] != '0') {
    const char* logpath = std::getenv("M88_ADPCM_LOG");
    std::fprintf(stderr,
                 "M88_DEBUG_ADPCM=1 (log: stderr%s%s)\n",
                 logpath ? ", file " : "",
                 logpath ? logpath : "");
    std::fflush(stderr);
  }

  if (!sound.Init(&pc88, 8000, 0)) {
    std::fprintf(stderr, "Warning: sound init failed, continuing without audio.\n");
  } else if (!pc88.GetOPN1()->Connect(&sound) || !pc88.GetOPN2()->Connect(&sound) ||
             !pc88.GetBEEP()->Connect(&sound)) {
    std::fprintf(stderr, "Warning: failed to connect sound sources.\n");
  }

  M88ApplyConfig(&pc88, &config);
  sound.ApplyConfig(&config);
  keyif.ApplyConfig(&config);
  keyif.Activate(true);
  draw.LogVideoBackendOnce();
  M88LogSound(&config);
  M88LogKeyboard(&config);
  M88LogKeyFix();
  if (LinuxIme::Enabled()) {
    M88LogImeHalfKana();
  }
  M88LogFdd(&config);
  M88LogMachine(&config);
  M88LogFrameTiming(pc88.GetFramePeriod(), config.speed);
  MountDiskOptional(diskmgr, 0, disk0);

  M88Sequencer seq;
  M88EmuTimePacer emu_time_pacer;
  seq.ApplyConfig(config);
  seq.ResetPacing();
  emu_time_pacer.Reset();
  // WinUI::InitM88: ApplyConfig() then core.Reset() before the message loop.
  M88PostStartupCpuReset(pc88, &seq, config.refreshtiming);
  pc88.UpdateScreen(true);
  if (draw.NeedsPresent()) {
    draw.Present();
  }

  bool running = true;
  bool reset_requested = false;
  const uint32 app_start = SDL_GetTicks();

  while (running) {
    const int effclock =
        std::max(1, config.clock * (config.speed / 10) / 100);

    FireAutoKeys(keyif, auto_keys, SDL_GetTicks() - app_start);
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      const int ime_handled = LinuxIme::HandleSdlEvent(ev.type, &ev, &draw, &keyif, &config);
      if (ime_handled == 2) {
        const uint effclock = static_cast<uint>(
            std::max(1, config.clock * (config.speed / 10) / 100));
        int guard = 0;
        while (HalfKanaIme::InjectBusy() && guard++ < 8192) {
          HalfKanaIme::InjectPump(&keyif);
          const int period = std::max(1, pc88.GetFramePeriod());
          pc88.Proceed(static_cast<uint>(period), static_cast<uint>(config.clock), effclock);
        }
        HalfKanaIme::InjectEndSession(&keyif, &config);
        continue;
      }
      if (ime_handled == 1) {
        continue;
      }
      if (ev.type == SDL_QUIT) {
        running = false;
      } else if (ev.type == SDL_KEYDOWN) {
        if (ev.key.keysym.sym == SDLK_ESCAPE && !ev.key.repeat) {
          running = false;
        } else if (ev.key.keysym.sym == SDLK_F5 && !ev.key.repeat) {
          reset_requested = true;
        } else {
          M88Input::HandleKeyDown(keyif, ev.key);
        }
      } else if (ev.type == SDL_KEYUP) {
        M88Input::HandleKeyUp(keyif, ev.key);
      }
    }

    if (reset_requested) {
      reset_requested = false;
      HalfKanaIme::InjectEndSession(&keyif, &config);
      keyif.FlushGuestKeys();
      keyif.ApplyConfig(&config);
      seq.ResetPacing();
      emu_time_pacer.Reset();
      M88UserCpuReset(pc88, &seq, config.refreshtiming);
      pc88.UpdateScreen(true);
      if (draw.NeedsPresent()) {
        draw.Present();
      }
    }

    M88LoadmonFrameBegin();
    seq.ApplyConfig(config);
    struct SdlFrameDrawCtx {
      PC88* pc88;
      LinuxDraw* draw;
    } draw_ctx{&pc88, &draw};
    M88EmuTimePacer::AudioHint audio {};
    if (sound.GetRingSize() > 0) {
      audio.ring_avail = sound.GetRingAvail();
      audio.ring_size = sound.GetRingSize();
      audio.sample_rate_hz = static_cast<int>(sound.GetOutputSampleRate());
    }
    seq.RunFrame(
        &pc88,
        +[](void* ctx, bool draw_flag) {
          auto* f = static_cast<SdlFrameDrawCtx*>(ctx);
          if (!f || !f->pc88 || !draw_flag) {
            return;
          }
          f->pc88->UpdateScreen(false);
          if (f->draw && f->draw->NeedsPresent()) {
            f->draw->Present();
          }
        },
        &draw_ctx, false, [&]() { return !running; }, emu_time_pacer, audio);
    LinuxIme::Pump(&keyif);
    M88LoadmonFrameEnd();
  }

  return 0;
}
