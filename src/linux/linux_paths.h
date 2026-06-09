#pragma once

#include "types.h"

namespace PC8801 {
class Config;
}

struct M88Paths {
  char work_dir[MAX_PATH];
  char config_dir[MAX_PATH];
  char config_ini[MAX_PATH];
  char keyfix_ini[MAX_PATH];
  char rom_dir[MAX_PATH];
  char snapshot_dir[MAX_PATH];
  char capture_dir[MAX_PATH];
  bool local_mode = false;
};

// Resolve layout (cwd m88.ini vs ~/.config/m88), ensure roms/ and snapshot/,
// load or create m88.ini. Returns false only on fatal errors.
bool M88InitializePaths(M88Paths* paths, PC8801::Config* cfg, const char* explicit_config,
                        char* used_ini_path, size_t used_ini_path_sz, bool* created_new_ini);

const M88Paths* M88GetPaths();
const char* M88GetSnapshotDir();
const char* M88GetCaptureDir();
const char* M88GetKeyfixIniPath();

void M88LogDataPaths();
