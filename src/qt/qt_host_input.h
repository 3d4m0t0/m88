#pragma once

#include "if/ifui.h"

#include <QObject>

#include <atomic>
#include <mutex>

class QWidget;

namespace QtHostInput {

class PadInput : public IPadInput {
 public:
  PadInput();
  ~PadInput();

  bool Init();
  void IFCALL GetState(PadState* state) override;

 private:
  void Poll();

  int fd_ = -1;
  int axis_x_ = 0;
  int axis_y_ = 0;
  uint8_t buttons_ = 0;
};

class MouseUI : public IMouseUI {
 public:
  MouseUI();

  long IFCALL QueryInterface(REFIID id, void** out) override;
  ulong IFCALL AddRef() override;
  ulong IFCALL Release() override;

  bool IFCALL Enable(bool enabled) override;
  bool IFCALL GetMovement(POINT* move) override;
  uint IFCALL GetButton() override;

  void PostMovement(int dx, int dy);
  void PostButtons(uint buttons);

 private:
  std::mutex mutex_;
  POINT pending_move_{0, 0};
  uint buttons_ = 0;
  bool enabled_ = false;
  std::atomic<ulong> refcount_{1};
};

class Host : public QObject {
  Q_OBJECT

 public:
  explicit Host(QObject* parent = nullptr);

  PadInput* pad() { return &pad_; }
  MouseUI* mouse() { return &mouse_; }

  bool InitPad();

 signals:
  void mouseCaptureChanged(bool enabled);

 public slots:
  void applyMouseCapture(bool enabled, QWidget* view);

 private:
  PadInput pad_;
  MouseUI mouse_;
};

}  // namespace QtHostInput
