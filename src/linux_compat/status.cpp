#include "headers.h"
#include "status.h"

StatusDisplay statusdisplay;

StatusDisplay::StatusDisplay()
    : hwnd(nullptr),
      hwndparent(nullptr),
      timerid(0),
      height(0),
      showfdstat(false),
      updatemessage(false) {
  litstat[0] = litstat[1] = litstat[2] = 0;
  litcurrent[0] = litcurrent[1] = litcurrent[2] = 0;
}

StatusDisplay::~StatusDisplay() = default;

bool StatusDisplay::Init(HWND parent) {
  hwndparent = parent;
  return true;
}

void StatusDisplay::Cleanup() {}
bool StatusDisplay::Enable(bool) { return true; }
bool StatusDisplay::Disable() { return true; }
void StatusDisplay::DrawItem(void*) {}
void StatusDisplay::FDAccess(uint, bool, bool) {}
void StatusDisplay::UpdateDisplay() {}
bool StatusDisplay::Show(int, int, char*, ...) { return true; }
void StatusDisplay::Update() {}
