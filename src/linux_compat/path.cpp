#include "headers.h"
#include "path.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

char m88dir[MAX_PATH] = ".";

void M88InitRomPath(const char* rom_dir) {
  if (!rom_dir || !*rom_dir) {
    if (!getcwd(m88dir, sizeof(m88dir))) {
      std::strncpy(m88dir, ".", sizeof(m88dir) - 1);
      m88dir[sizeof(m88dir) - 1] = '\0';
    }
    return;
  }
  std::strncpy(m88dir, rom_dir, sizeof(m88dir) - 1);
  m88dir[sizeof(m88dir) - 1] = '\0';
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
