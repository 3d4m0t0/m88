#pragma once

#include "types.h"

#include <cstdio>
#include <string>
#include <vector>

class FileIO {
public:
  enum Flags {
    open = 0x000001,
    readonly = 0x000002,
    create = 0x000004,
  };

  enum SeekMethod {
    begin = 0,
    current = 1,
    end = 2,
  };

  enum Error {
    success = 0,
    file_not_found,
    sharing_violation,
    unknown = -1
  };

  FileIO();
  FileIO(const char* filename, uint flg = 0);
  virtual ~FileIO();

  bool Open(const char* filename, uint flg = 0);
  bool CreateNew(const char* filename);
  bool Reopen(uint flg = 0);
  void Close();
  Error GetError() { return error; }

  int32 Read(void* dest, int32 len);
  int32 Write(const void* src, int32 len);
  bool Seek(int32 fpos, SeekMethod method);
  int32 Tellp();
  bool SetEndOfFile();

  uint GetFlags() { return flags; }
  bool IsOpen() const { return fp != nullptr; }
  void SetLogicalOrigin(int32 origin) { lorigin = origin; }

private:
  FILE* fp;
  uint flags;
  uint32 lorigin;
  Error error;
  std::string path;

  FileIO(const FileIO&);
  const FileIO& operator=(const FileIO&);
};

class FileFinder {
public:
  FileFinder() : index(-1), current_attr(0) {}
  ~FileFinder() = default;

  bool FindFile(char* szSearch);
  bool FindNext();

  const char* GetFileName() { return current_name.c_str(); }
  DWORD GetFileAttr() { return current_attr; }
  const char* GetAltName() { return ""; }

private:
  std::vector<std::string> matches;
  int index;
  std::string current_name;
  DWORD current_attr;
};
