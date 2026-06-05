#pragma once

#include "types.h"

enum {
  PICCOLO_YMF288 = 0
};

class PiccoloChip {
public:
  void Reset(bool) {}
  void SetReg(uint, uint, uint) {}
};

class Piccolo {
public:
  static Piccolo* GetInstance() { return nullptr; }
  static void DeleteInstance() {}

  int GetChip(int, PiccoloChip**) { return -1; }
  int IsDriverBased() const { return 0; }
  uint32 GetCurrentTime() const { return 0; }
};
