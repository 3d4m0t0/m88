#pragma once

#include "types.h"

extern char m88dir[MAX_PATH];

// rom_dir == nullptr or empty: use current working directory.
void M88InitRomPath(const char* rom_dir);

void M88RomPath(char* out, size_t outlen, const char* filename);
