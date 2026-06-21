#pragma once

#include "types.h"
#include "critsect.h"

struct StatusUiSnapshot {
  bool bar_enabled = false;
  bool show_fdc_lamps = false;
  int lamp_level[3] = {0, 0, 0};
  char message[128] = {};
  int message_duration_ms = 0;
  bool watch_register = false;
  char register_line[32] = {};
};

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
  void WaitSubSys();

  bool Show(int priority, int duration, char* msg, ...);
  void UpdateRegisterWatch(bool enabled, const char* fmt = nullptr, ...);
  void Update();
  UINT_PTR GetTimerID() { return timerid; }

  HWND GetHWnd() { return hwnd; }

  bool PollUiSnapshot(StatusUiSnapshot* out);

private:
  void MarkDirty();
  void ExpireMessage();

  HWND hwnd;
  HWND hwndparent;
  UINT_PTR timerid;
  CriticalSection cs;
  int height;
  int litstat[3];
  bool showfdstat;
  bool bar_enabled;

  int message_priority;
  char message[128];
  uint64_t message_expire_ns;
  bool watch_register;
  char register_line[32];
  bool ui_dirty;
};

extern StatusDisplay statusdisplay;
