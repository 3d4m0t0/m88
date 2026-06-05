#pragma once

#include "types.h"
#include "critsect.h"

class StatusDisplay {
public:
  StatusDisplay();
  ~StatusDisplay();

  bool Init(HWND hwndparent);
  void Cleanup();

  bool Enable(bool sfs = false);
  bool Disable();
  int GetHeight() { return height; }
  void DrawItem(void* dis);
  void FDAccess(uint dr, bool hd, bool active);
  void UpdateDisplay();
  void WaitSubSys() { litstat[2] = 9; }

  bool Show(int priority, int duration, char* msg, ...);
  void Update();
  UINT_PTR GetTimerID() { return timerid; }

  HWND GetHWnd() { return hwnd; }

private:
  HWND hwnd;
  HWND hwndparent;
  UINT_PTR timerid;
  CriticalSection cs;
  int height;
  int litstat[3];
  int litcurrent[3];
  bool showfdstat;
  bool updatemessage;
};

extern StatusDisplay statusdisplay;
