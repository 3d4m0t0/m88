#include "headers.h"
#include "file.h"
#include "path.h"

#include "../linux/linux_paths.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

char m88dir[MAX_PATH] = ".";

void M88InitRomPath(const char* rom_dir) {
  if (rom_dir && *rom_dir) {
    std::strncpy(m88dir, rom_dir, sizeof(m88dir) - 1);
    m88dir[sizeof(m88dir) - 1] = '\0';
    return;
  }
  if (const M88Paths* paths = M88GetPaths(); paths && paths->rom_dir[0]) {
    std::strncpy(m88dir, paths->rom_dir, sizeof(m88dir) - 1);
    m88dir[sizeof(m88dir) - 1] = '\0';
    return;
  }
  if (!getcwd(m88dir, sizeof(m88dir))) {
    std::strncpy(m88dir, ".", sizeof(m88dir) - 1);
    m88dir[sizeof(m88dir) - 1] = '\0';
  }
}

void M88RomPath(char* out, size_t outlen, const char* filename) {
  if (!out || outlen == 0) {
    return;
  }
  if (!filename || !*filename) {
    out[0] = '\0';
    return;
  }
  if (filename[0] == '/') {
    std::snprintf(out, outlen, "%s", filename);
  } else {
    std::snprintf(out, outlen, "%s/%s", m88dir, filename);
  }
  out[outlen - 1] = '\0';
}

bool M88RomExists(const char* filename) {
  if (!filename || !*filename) {
    return false;
  }
  char path[MAX_PATH];
  M88RomPath(path, sizeof(path), filename);
  FileIO file;
  return file.Open(path, FileIO::readonly);
}

bool M88HasRequiredRoms() {
  return M88RomExists("pc88.rom") || M88RomExists("n88.rom");
}
