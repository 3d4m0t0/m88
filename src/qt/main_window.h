#pragma once

#include <QMainWindow>
#include <QPointer>
#include <QThread>

#include "emulator_controller.h"

class EmuView;
class SharedFramebufferDraw;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(const EmulatorController::Options& options, int scale = 2,
                      QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  void setupMenuBar();
  void applyViewScale(int scale);
  void stopEmulator();
  void focusEmuView();

  int view_scale_ = 2;
  SharedFramebufferDraw* draw_ = nullptr;
  EmuView* view_ = nullptr;
  QPointer<EmulatorController> controller_;
  QThread emu_thread_;
  bool emu_stopped_ = false;
  bool window_shown_ = false;
};
