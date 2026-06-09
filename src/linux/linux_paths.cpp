#include "linux_paths.h"

#include "linux_config.h"
#include "path.h"

#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace {

M88Paths g_paths {};
bool g_paths_ready = false;

bool FileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool DirExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool MkdirOne(const char* path) {
  if (!path || !*path) {
    return false;
  }
  if (mkdir(path, 0755) == 0) {
    return true;
  }
  return errno == EEXIST && DirExists(path);
}

bool EnsureDirRecursive(char* path) {
  if (!path || !*path) {
    return false;
  }
  if (DirExists(path)) {
    return true;
  }

  char* p = path;
  if (*p == '/') {
    ++p;
  }
  for (; *p; ++p) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (!DirExists(path) && !MkdirOne(path)) {
      *p = '/';
      return false;
    }
    *p = '/';
  }
  return MkdirOne(path);
}

bool JoinPath(char* out, size_t out_sz, const char* dir, const char* leaf) {
  if (!out || out_sz == 0 || !dir || !leaf) {
    return false;
  }
  const int n = std::snprintf(out, out_sz, "%s/%s", dir, leaf);
  return n > 0 && static_cast<size_t>(n) < out_sz;
}

bool PathsEqual(const char* a, const char* b) {
  if (!a || !b) {
    return false;
  }
  char ra[MAX_PATH];
  char rb[MAX_PATH];
  if (!realpath(a, ra) || !realpath(b, rb)) {
    return std::strcmp(a, b) == 0;
  }
  return std::strcmp(ra, rb) == 0;
}

bool IniExistsInDir(const char* dir, char* out_ini, size_t out_ini_sz) {
  static const char* kNames[] = {"m88.ini", "M88.ini", "M88.INI"};
  for (const char* name : kNames) {
    char path[MAX_PATH];
    if (!JoinPath(path, sizeof(path), dir, name)) {
      continue;
    }
    if (FileExists(path)) {
      std::strncpy(out_ini, path, out_ini_sz - 1);
      out_ini[out_ini_sz - 1] = '\0';
      return true;
    }
  }
  return false;
}

bool DirnameCopy(const char* path, char* out, size_t out_sz) {
  if (!path || !out || out_sz == 0) {
    return false;
  }
  std::strncpy(out, path, out_sz - 1);
  out[out_sz - 1] = '\0';
  char* slash = std::strrchr(out, '/');
  if (!slash) {
    out[0] = '.';
    out[1] = '\0';
    return true;
  }
  if (slash == out) {
    out[1] = '\0';
  } else {
    *slash = '\0';
  }
  return true;
}

bool LoadOrCreateConfig(PC8801::Config* cfg, const char* ini_path, bool* created_new_ini) {
  if (created_new_ini) {
    *created_new_ini = false;
  }
  if (!cfg || !ini_path || !*ini_path) {
    return false;
  }

  if (M88LoadConfigAtPath(cfg, ini_path)) {
    return true;
  }

  M88FinalizeConfig(cfg);
  if (!M88SaveConfigFile(cfg, ini_path)) {
    std::fprintf(stderr, "M88: failed to create config: %s\n", ini_path);
    return false;
  }
  if (created_new_ini) {
    *created_new_ini = true;
  }
  return true;
}

bool SetupSubdirs(M88Paths* paths) {
  char rom_path[MAX_PATH];
  char snap_path[MAX_PATH];
  if (!JoinPath(rom_path, sizeof(rom_path), paths->config_dir, "roms") ||
      !JoinPath(snap_path, sizeof(snap_path), paths->config_dir, "snapshot")) {
    return false;
  }
  if (!EnsureDirRecursive(rom_path) || !EnsureDirRecursive(snap_path)) {
    std::fprintf(stderr, "M88: failed to create data directories under %s\n",
                 paths->config_dir);
    return false;
  }
  std::strncpy(paths->rom_dir, rom_path, sizeof(paths->rom_dir) - 1);
  paths->rom_dir[sizeof(paths->rom_dir) - 1] = '\0';
  std::strncpy(paths->snapshot_dir, snap_path, sizeof(paths->snapshot_dir) - 1);
  paths->snapshot_dir[sizeof(paths->snapshot_dir) - 1] = '\0';
  return true;
}

bool SetupCaptureDir(M88Paths* paths) {
  if (paths->local_mode) {
    std::strncpy(paths->capture_dir, paths->work_dir, sizeof(paths->capture_dir) - 1);
    paths->capture_dir[sizeof(paths->capture_dir) - 1] = '\0';
    return true;
  }
  const char* home = getenv("HOME");
  if (!home || !*home) {
    std::strncpy(paths->capture_dir, paths->config_dir, sizeof(paths->capture_dir) - 1);
    paths->capture_dir[sizeof(paths->capture_dir) - 1] = '\0';
    return true;
  }
  std::strncpy(paths->capture_dir, home, sizeof(paths->capture_dir) - 1);
  paths->capture_dir[sizeof(paths->capture_dir) - 1] = '\0';
  return true;
}

}  // namespace

bool M88InitializePaths(M88Paths* paths, PC8801::Config* cfg, const char* explicit_config,
                        char* used_ini_path, size_t used_ini_path_sz, bool* created_new_ini) {
  if (!paths || !cfg) {
    return false;
  }
  if (created_new_ini) {
    *created_new_ini = false;
  }
  if (used_ini_path && used_ini_path_sz > 0) {
    used_ini_path[0] = '\0';
  }

  *paths = {};
  if (!getcwd(paths->work_dir, sizeof(paths->work_dir))) {
    std::strncpy(paths->work_dir, ".", sizeof(paths->work_dir) - 1);
    paths->work_dir[sizeof(paths->work_dir) - 1] = '\0';
  }

  M88SetDefaultConfig(cfg);

  char config_ini[MAX_PATH] = {};
  bool local_mode = false;

  if (explicit_config && *explicit_config) {
    char dir[MAX_PATH];
    if (!DirnameCopy(explicit_config, dir, sizeof(dir))) {
      return false;
    }
    std::strncpy(paths->config_dir, dir, sizeof(paths->config_dir) - 1);
    paths->config_dir[sizeof(paths->config_dir) - 1] = '\0';
    std::strncpy(config_ini, explicit_config, sizeof(config_ini) - 1);
    config_ini[sizeof(config_ini) - 1] = '\0';
    local_mode = PathsEqual(paths->config_dir, paths->work_dir);
  } else if (IniExistsInDir(paths->work_dir, config_ini, sizeof(config_ini))) {
    local_mode = true;
    std::strncpy(paths->config_dir, paths->work_dir, sizeof(paths->config_dir) - 1);
    paths->config_dir[sizeof(paths->config_dir) - 1] = '\0';
  } else {
    const char* home = getenv("HOME");
    if (!home || !*home) {
      std::fprintf(stderr, "M88: HOME is not set; cannot use ~/.config/m88\n");
      return false;
    }
    char global_dir[MAX_PATH];
    std::snprintf(global_dir, sizeof(global_dir), "%s/.config/m88", home);
    global_dir[sizeof(global_dir) - 1] = '\0';
    if (!EnsureDirRecursive(global_dir)) {
      std::fprintf(stderr, "M88: failed to create %s\n", global_dir);
      return false;
    }
    local_mode = false;
    std::strncpy(paths->config_dir, global_dir, sizeof(paths->config_dir) - 1);
    paths->config_dir[sizeof(paths->config_dir) - 1] = '\0';
    if (!JoinPath(config_ini, sizeof(config_ini), paths->config_dir, "m88.ini")) {
      return false;
    }
  }

  paths->local_mode = local_mode;
  if (!LoadOrCreateConfig(cfg, config_ini, created_new_ini)) {
    return false;
  }

  std::strncpy(paths->config_ini, config_ini, sizeof(paths->config_ini) - 1);
  paths->config_ini[sizeof(paths->config_ini) - 1] = '\0';
  if (!JoinPath(paths->keyfix_ini, sizeof(paths->keyfix_ini), paths->config_dir,
                "m88_keyfix.ini")) {
    return false;
  }

  if (!SetupSubdirs(paths) || !SetupCaptureDir(paths)) {
    return false;
  }

  if (used_ini_path && used_ini_path_sz > 0) {
    M88CanonicalConfigPath(paths->config_ini, used_ini_path, used_ini_path_sz);
  }

  g_paths = *paths;
  g_paths_ready = true;
  return true;
}

const M88Paths* M88GetPaths() { return g_paths_ready ? &g_paths : nullptr; }

const char* M88GetSnapshotDir() {
  return g_paths_ready ? g_paths.snapshot_dir : ".";
}

const char* M88GetCaptureDir() {
  return g_paths_ready ? g_paths.capture_dir : ".";
}

const char* M88GetKeyfixIniPath() {
  return g_paths_ready ? g_paths.keyfix_ini : "m88_keyfix.ini";
}

void M88LogDataPaths() {
  if (!g_paths_ready) {
    return;
  }
  std::fprintf(stderr, "M88: data layout: %s\n",
               g_paths.local_mode ? "working directory" : "~/.config/m88");
  std::fprintf(stderr, "M88: rom directory: %s\n", g_paths.rom_dir);
  std::fprintf(stderr, "M88: snapshot directory: %s\n", g_paths.snapshot_dir);
  std::fprintf(stderr, "M88: capture directory: %s\n", g_paths.capture_dir);
}
