#include "qt_host_input.h"

#include "../common/misc.h"
#include "if/ifguid.h"

#include <QCursor>
#include <QWidget>

#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace QtHostInput {

namespace {

constexpr int kJoyThreshold = 8192;

bool GuidEqual(REFIID a, REFIID b)
{
  return std::memcmp(&a, &b, sizeof(a)) == 0;
}

int OpenFirstJoystick()
{
  for (int i = 0; i < 8; ++i) {
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/input/js%d", i);
    const int fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) {
      return fd;
    }
  }
  return -1;
}

}  // namespace

PadInput::PadInput() = default;

PadInput::~PadInput()
{
  if (fd_ >= 0) {
    close(fd_);
    fd_ = -1;
  }
}

bool PadInput::Init()
{
  if (fd_ >= 0) {
    return true;
  }
  fd_ = OpenFirstJoystick();
  if (fd_ < 0) {
    std::fprintf(stderr, "M88: no joystick device found (/dev/input/js*)\n");
    return false;
  }
  return true;
}

void PadInput::Poll()
{
  if (fd_ < 0) {
    return;
  }
  js_event event {};
  while (read(fd_, &event, sizeof(event)) == static_cast<ssize_t>(sizeof(event))) {
    const int value = static_cast<int>(event.value);
    switch (event.type & ~JS_EVENT_INIT) {
      case JS_EVENT_AXIS:
        if (event.number == 0) {
          axis_x_ = value;
        } else if (event.number == 1) {
          axis_y_ = value;
        }
        break;
      case JS_EVENT_BUTTON:
        if (event.number < 8) {
          if (value) {
            buttons_ |= static_cast<uint8_t>(1u << event.number);
          } else {
            buttons_ &= static_cast<uint8_t>(~(1u << event.number));
          }
        }
        break;
      default:
        break;
    }
  }
}

void IFCALL PadInput::GetState(PadState* state)
{
  if (!state) {
    return;
  }
  state->direction = 0;
  state->button = 0;
  if (fd_ < 0) {
    return;
  }
  Poll();
  if (axis_y_ < -kJoyThreshold) {
    state->direction |= 1;
  }
  if (axis_y_ > kJoyThreshold) {
    state->direction |= 2;
  }
  if (axis_x_ < -kJoyThreshold) {
    state->direction |= 4;
  }
  if (axis_x_ > kJoyThreshold) {
    state->direction |= 8;
  }
  state->button = buttons_;
}

MouseUI::MouseUI() = default;

long IFCALL MouseUI::QueryInterface(REFIID id, void** out)
{
  if (!out) {
    return -1;
  }
  if (GuidEqual(id, ChIID_MouseUI)) {
    *out = this;
    AddRef();
    return 0;
  }
  *out = nullptr;
  return -1;
}

ulong IFCALL MouseUI::AddRef()
{
  return ++refcount_;
}

ulong IFCALL MouseUI::Release()
{
  const ulong refs = --refcount_;
  return refs > 0 ? refs : 0;
}

bool IFCALL MouseUI::Enable(bool enabled)
{
  std::lock_guard<std::mutex> lock(mutex_);
  enabled_ = enabled;
  pending_move_.x = 0;
  pending_move_.y = 0;
  if (!enabled) {
    buttons_ = 0;
  }
  return true;
}

bool IFCALL MouseUI::GetMovement(POINT* move)
{
  if (!move) {
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  move->x = pending_move_.x;
  move->y = pending_move_.y;
  pending_move_.x = 0;
  pending_move_.y = 0;
  return enabled_;
}

uint IFCALL MouseUI::GetButton()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return buttons_;
}

void MouseUI::PostMovement(int dx, int dy)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_) {
    return;
  }
  pending_move_.x += dx;
  pending_move_.y += dy;
  pending_move_.x = Limit(pending_move_.x, 127, -127);
  pending_move_.y = Limit(pending_move_.y, 127, -127);
}

void MouseUI::PostButtons(uint buttons)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!enabled_) {
    return;
  }
  buttons_ = buttons;
}

Host::Host(QObject* parent) : QObject(parent) {}

bool Host::InitPad()
{
  return pad_.Init();
}

void Host::applyMouseCapture(bool enabled, QWidget* view)
{
  if (!view) {
    return;
  }
  if (enabled) {
    view->setMouseTracking(true);
    view->grabMouse();
    view->setCursor(Qt::BlankCursor);
  } else {
    if (view->mouseGrabber() == view) {
      view->releaseMouse();
    }
    view->unsetCursor();
  }
}

}  // namespace QtHostInput
