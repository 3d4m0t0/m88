#pragma once

#include <QActionGroup>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QMenu>
#include <QPointer>
#include <QThread>
#include <QWidget>

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
                         bool n80v2_supported, bool cd_supported, bool burst_mode,
                         bool show_statusbar, bool show_fdc_status,
                         bool ask_before_reset, bool f12_as_reset, bool suppress_menu);
  bool confirmReset();
  bool confirmExit();
  void updateStatusUi(bool bar_enabled, bool show_fdc_lamps, int lamp0, int lamp1,
                      int lamp2, QString message, int message_ms);
  void updateTitleStats(int fps, int mhz_whole, int mhz_frac);
  void updateDiskMenu(QString drive0Path, int drive0NumDisks, int drive0Current,
                      QStringList drive0Titles, QString drive1Path,
                      int drive1NumDisks, int drive1Current,
                      QStringList drive1Titles);

private:
  void setupMenuBar();
  void applyViewScale(int scale);
  void stopEmulator();
  void focusEmuView();
  void openConfigureDialog();
  void openDiskImageDialog(int drive);
  void openBothDrivesDialog();
  void rebuildDriveMenu(int drive, const QString& path, int numDisks,
                        int currentDisk, const QStringList& titles);
  static QString DriveBaseName(const QString& path);

  static constexpr const char* kDiskImageFilter =
      "8801 disk image (*.d88);;All files (*)";

  int view_scale_ = 2;
  QMenu* control_menu_ = nullptr;
  QMenu* disk_menu_ = nullptr;
  QAction* drive_actions_[2] = {nullptr, nullptr};
  QMenu* drive_submenus_[2] = {nullptr, nullptr};
  QActionGroup* disk_select_groups_[2] = {nullptr, nullptr};
  QAction* change_both_action_ = nullptr;
  QActionGroup* clock_group_ = nullptr;
  QAction* clock_4mhz_ = nullptr;
  QAction* clock_8mhz_ = nullptr;
  QAction* burst_action_ = nullptr;
  QAction* reset_action_ = nullptr;
  bool ask_before_reset_ = false;
  bool f12_as_reset_ = true;
  QAction* show_status_action_ = nullptr;
  QAction* fdc_status_action_ = nullptr;
  QLabel* fdc_text_label_ = nullptr;
  QTimer* title_timer_ = nullptr;
  QWidget* fdc_lamp_panel_ = nullptr;
  QLabel* fdc_lamp_labels_[3] = {nullptr, nullptr, nullptr};
  QActionGroup* mode_group_ = nullptr;
  std::vector<QAction*> mode_actions_;
  SharedFramebufferDraw* draw_ = nullptr;
  EmuView* view_ = nullptr;
  QPointer<EmulatorController> controller_;
  QThread emu_thread_;
  bool emu_stopped_ = false;
  bool window_shown_ = false;
};
