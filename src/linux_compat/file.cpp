#include "headers.h"
#include "file.h"

#include <cstdio>
#include <filesystem>
#include <strings.h>

namespace {

FILE* FopenCaseInsensitive(const char* filename, const char* mode) {
  if (!filename || !*filename) {
    return nullptr;
  }

  FILE* fp = std::fopen(filename, mode);
  if (fp) {
    return fp;
  }

  std::filesystem::path target(filename);
  std::filesystem::path dir = target.parent_path();
  if (dir.empty()) {
    dir = ".";
  }
  const std::string leaf = target.filename().string();
  if (leaf.empty()) {
    return nullptr;
  }

  std::error_code ec;
  if (!std::filesystem::is_directory(dir, ec)) {
    return nullptr;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
    if (ec || !entry.is_regular_file(ec)) {
      continue;
    }
    const std::string name = entry.path().filename().string();
    if (strcasecmp(name.c_str(), leaf.c_str()) != 0) {
      continue;
    }
    fp = std::fopen(entry.path().string().c_str(), mode);
    if (fp) {
      return fp;
    }
  }
  return nullptr;
}

}  // namespace

FileIO::FileIO() : fp(nullptr), flags(0), lorigin(0), error(success) {}

FileIO::FileIO(const char* filename, uint flg)
    : fp(nullptr), flags(0), lorigin(0), error(success) {
  Open(filename, flg);
}

FileIO::~FileIO() { Close(); }

bool FileIO::Open(const char* filename, uint flg) {
  Close();
  if (!filename) {
    error = unknown;
    return false;
  }

  path = filename;
  flags = flg;
  lorigin = 0;

  const bool ro = (flg & readonly) != 0;
  const bool cr = (flg & create) != 0;
  const char* mode = ro ? "rb" : (cr ? "wb+" : "rb+");

  fp = FopenCaseInsensitive(filename, mode);
  if (!fp && !ro && !cr) {
    fp = FopenCaseInsensitive(filename, "rb");
  }

  if (!fp) {
    error = file_not_found;
    return false;
  }
  flags = (flg & ~open) | open;
  error = success;
  return true;
}

bool FileIO::CreateNew(const char* filename) { return Open(filename, create); }

bool FileIO::Reopen(uint flg) { return Open(path.c_str(), flg ? flg : flags); }

void FileIO::Close() {
  if (fp) {
    std::fclose(fp);
    fp = nullptr;
  }
}

int32 FileIO::Read(void* dest, int32 len) {
  if (!fp || !dest || len <= 0) return 0;
  return static_cast<int32>(std::fread(dest, 1, static_cast<size_t>(len), fp));
}

int32 FileIO::Write(const void* src, int32 len) {
  if (!fp || !src || len <= 0) return 0;
  return static_cast<int32>(std::fwrite(src, 1, static_cast<size_t>(len), fp));
}

bool FileIO::Seek(int32 fpos, SeekMethod method) {
  if (!fp) return false;
  int whence = SEEK_SET;
  if (method == current) whence = SEEK_CUR;
  if (method == end) whence = SEEK_END;
  const off_t pos = static_cast<off_t>(fpos) + static_cast<off_t>(lorigin);
  return fseeko(fp, pos, whence) == 0;
}

int32 FileIO::Tellp() {
  if (!fp) return -1;
  off_t pos = ftello(fp);
  if (pos < 0) return -1;
  return static_cast<int32>(pos - static_cast<off_t>(lorigin));
}

bool FileIO::SetEndOfFile() { return true; }

bool FileFinder::FindFile(char* szSearch) {
  matches.clear();
  index = -1;
  current_name.clear();
  current_attr = 0;
  if (!szSearch || !*szSearch) return false;

  std::filesystem::path pattern(szSearch);
  std::filesystem::path dir = pattern.parent_path();
  if (dir.empty()) dir = ".";
  std::string leaf = pattern.filename().string();

  bool wildcard = leaf.find('*') != std::string::npos || leaf.find('?') != std::string::npos;
  if (!std::filesystem::exists(dir)) return false;

  for (const auto& e : std::filesystem::directory_iterator(dir)) {
    const std::string name = e.path().filename().string();
    if (!wildcard) {
      if (name == leaf) matches.push_back(name);
      continue;
    }
    if (leaf == "*" || leaf == "*.*") {
      matches.push_back(name);
    }
  }
  return !matches.empty();
}

bool FileFinder::FindNext() {
  if (index + 1 >= static_cast<int>(matches.size())) return false;
  ++index;
  current_name = matches[static_cast<size_t>(index)];
  current_attr = 0;
  return true;
}
