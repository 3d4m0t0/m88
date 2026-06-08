#pragma once

#include <QActionGroup>
#include <QMainWindow>
#include <QMenu>
#include <QPointer>
#include <QThread>

#include <vector>

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

private slots:
  void updateControlMenu(int clock, int basicmode, bool n80_supported,
                         bool n80v2_supported, bool cd_supported);

private:
  void setupMenuBar();
  void applyViewScale(int scale);
  void stopEmulator();
  void focusEmuView();

  int view_scale_ = 2;
  QMenu* control_menu_ = nullptr;
  QActionGroup* clock_group_ = nullptr;
  QAction* clock_4mhz_ = nullptr;
  QAction* clock_8mhz_ = nullptr;
  QActionGroup* mode_group_ = nullptr;
  std::vector<QAction*> mode_actions_;
  SharedFramebufferDraw* draw_ = nullptr;
  EmuView* view_ = nullptr;
  QPointer<EmulatorController> controller_;
  QThread emu_thread_;
  bool emu_stopped_ = false;
  bool window_shown_ = false;
};
