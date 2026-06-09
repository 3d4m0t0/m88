// Snapshot save/load (ported from src/win32/wincore.cpp).

#include "headers.h"
#include "pc88/pc88.h"
#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "file.h"
#include "zlib/zlib.h"

using namespace PC8801;

namespace {

//                         0123456789abcdef
constexpr char kSnapshotId[] = "M88 SnapshotData";

enum {
  kSnapshotMajor = 1,
  kSnapshotMinor = 1,
};

struct SnapshotHeader {
  char id[16];
  uint8 major;
  uint8 minor;
  int8 disk[2];
  int datasize;
  Config::BASICMode basicmode;
  int16 clock;
  uint16 erambanks;
  uint16 cpumode;
  uint16 mainsubratio;
  uint flags;
  uint flag2;
};

}  // namespace

bool PC88::SaveShapshot(const char* filename, const Config* config) {
  if (!filename || !*filename || !config || !diskmgr) {
    return false;
  }

  const bool docomp = (config->flag2 & Config::compresssnapshot) != 0;
  const uint size = devlist.GetStatusSize();
  uint8* buf = new uint8[docomp ? size * 129 / 64 + 20 : size];
  if (!buf) {
    return false;
  }
  std::memset(buf, 0, size);

  bool ok = false;
  if (devlist.SaveStatus(buf)) {
    ulong esize = size * 129 / 64 + 20 - 4;
    if (docomp) {
      if (compress(buf + size + 4, &esize, buf, size) != Z_OK) {
        delete[] buf;
        return false;
      }
      *reinterpret_cast<int32*>(buf + size) = -static_cast<long>(esize);
      esize += 4;
    }

    SnapshotHeader ssh {};
    std::memcpy(ssh.id, kSnapshotId, 16);
    ssh.major = kSnapshotMajor;
    ssh.minor = kSnapshotMinor;
    ssh.datasize = static_cast<int>(size);
    ssh.basicmode = config->basicmode;
    ssh.clock = static_cast<int16>(config->clock);
    ssh.erambanks = static_cast<uint16>(config->erambanks);
    ssh.cpumode = static_cast<uint16>(config->cpumode);
    ssh.mainsubratio = static_cast<uint16>(config->mainsubratio);
    ssh.flags = config->flags;
    ssh.flag2 = config->flag2;
    for (uint i = 0; i < 2; ++i) {
      ssh.disk[i] = static_cast<int8>(diskmgr->GetCurrentDisk(i));
    }
    ssh.flags |= (esize < size) ? 0x80000000U : 0;

    FileIO file;
    if (file.Open(filename, FileIO::create)) {
      ok = file.Write(&ssh, sizeof(ssh)) == static_cast<int32>(sizeof(ssh));
      if (ok) {
        if (esize < size) {
          ok = file.Write(buf + size, static_cast<int32>(esize)) ==
               static_cast<int32>(esize);
        } else {
          ok = file.Write(buf, static_cast<int32>(size)) == static_cast<int32>(size);
        }
      }
    }
  }

  delete[] buf;
  return ok;
}

bool PC88::LoadShapshot(const char* filename, Config* config, const char* diskname) {
  if (!filename || !*filename || !config || !diskmgr) {
    return false;
  }

  FileIO file;
  if (!file.Open(filename, FileIO::readonly)) {
    return false;
  }

  SnapshotHeader ssh {};
  if (file.Read(&ssh, sizeof(ssh)) != static_cast<int32>(sizeof(ssh))) {
    return false;
  }
  if (std::memcmp(ssh.id, kSnapshotId, 16) != 0) {
    return false;
  }
  if (ssh.major != kSnapshotMajor || ssh.minor > kSnapshotMinor) {
    return false;
  }

  constexpr uint fl1a = Config::subcpucontrol | Config::fullspeed | Config::enableopna |
                        Config::enablepcg | Config::fv15k | Config::cpuburst |
                        Config::cpuclockmode | Config::digitalpalette | Config::opnona8 |
                        Config::opnaona8 | Config::enablewait;
  constexpr uint fl2a = Config::disableopn44;

  config->flags = (config->flags & ~fl1a) | (ssh.flags & fl1a);
  config->flag2 = (config->flag2 & ~fl2a) | (ssh.flag2 & fl2a);
  config->basicmode = ssh.basicmode;
  config->clock = ssh.clock;
  config->erambanks = ssh.erambanks;
  config->cpumode = ssh.cpumode;
  config->mainsubratio = ssh.mainsubratio;
  ApplyConfig(config);
  Reset();

  uint8* buf = new uint8[ssh.datasize];
  bool loaded = false;
  if (buf) {
    bool read = false;
    if (ssh.flags & 0x80000000U) {
      int32 csize = 0;
      if (file.Read(&csize, 4) == 4 && csize < 0) {
        csize = -csize;
        uint8* cbuf = new uint8[csize];
        if (cbuf) {
          ulong bufsize = static_cast<ulong>(ssh.datasize);
          if (file.Read(cbuf, csize) == csize) {
            read = uncompress(buf, &bufsize, cbuf, csize) == Z_OK;
          }
          delete[] cbuf;
        }
      }
    } else {
      read = file.Read(buf, ssh.datasize) == ssh.datasize;
    }

    if (read) {
      loaded = devlist.LoadStatus(buf);
      if (loaded && diskname && *diskname) {
        for (uint i = 0; i < 2; ++i) {
          diskmgr->Unmount(i);
          diskmgr->Mount(i, diskname, false, ssh.disk[i], false);
        }
      }
      if (!loaded) {
        Reset();
      }
    }
    delete[] buf;
  }
  return loaded;
}
