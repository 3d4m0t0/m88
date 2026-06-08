#include "headers.h"
#include "status.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>

StatusDisplay statusdisplay;

namespace {

uint64_t MonotonicNowNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

}  // namespace

StatusDisplay::StatusDisplay()
    : hwnd(nullptr),
      hwndparent(nullptr),
      timerid(0),
      height(0),
      showfdstat(false),
      bar_enabled(false),
      message_priority(-1),
      message_expire_ns(0),
      ui_dirty(false) {
  litstat[0] = litstat[1] = litstat[2] = 0;
  message[0] = '\0';
}

StatusDisplay::~StatusDisplay() = default;

bool StatusDisplay::Init(HWND parent) {
  hwndparent = parent;
  return true;
}

void StatusDisplay::Cleanup() {}

void StatusDisplay::MarkDirty() {
  ui_dirty = true;
}

bool StatusDisplay::Enable(bool sfs) {
  CriticalSection::Lock lock(cs);
  bar_enabled = true;
  showfdstat = sfs;
  MarkDirty();
  return true;
}

bool StatusDisplay::Disable() {
  CriticalSection::Lock lock(cs);
  bar_enabled = false;
  showfdstat = false;
  MarkDirty();
  return true;
}

void StatusDisplay::DrawItem(void*) {}
void StatusDisplay::UpdateDisplay() { Update(); }

void StatusDisplay::WaitSubSys() {
  CriticalSection::Lock lock(cs);
  litstat[2] = 9;
  MarkDirty();
}

void StatusDisplay::FDAccess(uint dr, bool hd, bool active) {
  (void)hd;
  if (dr > 1) {
    return;
  }
  CriticalSection::Lock lock(cs);
  litstat[dr] = active ? 9 : 0;
  MarkDirty();
}

bool StatusDisplay::Show(int priority, int duration, char* fmt, ...) {
  if (!fmt) {
    return false;
  }

  char buf[sizeof(message)] = {};
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  CriticalSection::Lock lock(cs);
  if (!bar_enabled) {
    return true;
  }

  const uint64_t now_ns = MonotonicNowNs();
  const bool expired = message_expire_ns != 0 && now_ns >= message_expire_ns;
  if (!expired && priority < message_priority && message[0] != '\0') {
    return true;
  }

  std::strncpy(message, buf, sizeof(message) - 1);
  message[sizeof(message) - 1] = '\0';
  message_priority = priority;
  if (duration > 0) {
    message_expire_ns = now_ns + static_cast<uint64_t>(duration) * 1'000'000ULL;
  } else {
    message_expire_ns = 0;
  }
  MarkDirty();
  return true;
}

void StatusDisplay::ExpireMessage() {
  if (message_expire_ns == 0) {
    return;
  }
  if (MonotonicNowNs() >= message_expire_ns) {
    message[0] = '\0';
    message_priority = -1;
    message_expire_ns = 0;
    MarkDirty();
  }
}

void StatusDisplay::Update() {
  CriticalSection::Lock lock(cs);
  for (int i = 0; i < 3; ++i) {
    if (litstat[i] > 0) {
      --litstat[i];
      MarkDirty();
    }
  }
  ExpireMessage();
}

bool StatusDisplay::PollUiSnapshot(StatusUiSnapshot* out) {
  if (!out) {
    return false;
  }
  CriticalSection::Lock lock(cs);
  if (!ui_dirty) {
    return false;
  }
  out->bar_enabled = bar_enabled;
  out->show_fdc_lamps = showfdstat;
  for (int i = 0; i < 3; ++i) {
    out->lamp_level[i] = litstat[i];
  }
  std::strncpy(out->message, message, sizeof(out->message) - 1);
  out->message[sizeof(out->message) - 1] = '\0';
  if (message_expire_ns != 0) {
    const int64_t remain_ns =
        static_cast<int64_t>(message_expire_ns) - static_cast<int64_t>(MonotonicNowNs());
    out->message_duration_ms =
        remain_ns > 0 ? static_cast<int>(remain_ns / 1'000'000LL) : 0;
  } else {
    out->message_duration_ms = 0;
  }
  ui_dirty = false;
  return true;
}
