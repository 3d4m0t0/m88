#pragma once

#include <QActionGroup>
#include <QLabel>
#include <QMainWindow>
#include <QShortcut>
#include <QRect>
#include <QTimer>
#include <QMenu>
#include <QPointer>
#include <QThread>
#include <QWidget>

#include <vector>

#include "emulator_controller.h"
#include "fcitx_status.h"
#include "qt_host_input.h"

class EmuView;
class FcitxStatus;
class SharedFramebufferDraw;

struct MainWindowStartup {
  bool save_position = false;
  int winpos_x = 64;
  int winpos_y = 64;
};

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(const EmulatorController::Options& options, int scale = 2,
                      const MainWindowStartup& startup = {},
                      QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void showEvent(QShowEvent* event) override;
  void hideEvent(QHideEvent* event) override;
  void changeEvent(QEvent* event) override;

private slots:
  void updateControlMenu(int clock, int basicmode, bool n80_supported,
                         bool n80v2_supported, bool cd_supported, bool burst_mode,
                         bool arrow_tenkey, bool show_statusbar, bool show_fdc_status,
                         bool watch_register, bool ask_before_reset, bool f12_as_reset,
                         bool suppress_menu);
  bool confirmReset();
  bool confirmExit();
  void updateStatusUi(bool bar_enabled, bool show_fdc_lamps, int lamp0, int lamp1,
                      int lamp2, QString message, int message_ms, bool watch_register,
                      QString register_text);
  void updateTitleStats(int fps, int mhz_whole, int mhz_frac);
  void updateDiskMenu(QString drive0Path, int drive0NumDisks, int drive0Current,
                      QStringList drive0Titles, QString drive1Path,
                      int drive1NumDisks, int drive1Current,
                      QStringList drive1Titles);
  void updateSnapshotState(int current_slot);
  void updateDisplayConfig(bool force480, bool sync_to_vsync);
  void onRequiredRomMissing();
  void toggleFullscreen();

private:
  void setupMenuBar();
  void applyViewScale(int scale);
  void syncRememberPrefsFromConfig(const PC8801::Config& config);
  void syncWaylandIdleInhibit();
  void syncImeKanaInput();
  void syncHostImeFromFcitx();
  void updateKanaStatusLabel();
  void showStatusMessage(const QString& message, int message_ms = 0);
  void applySavedWindowPosition();
  void saveWindowPositionOnExit();
  void applyFullscreenLayout();
  void applyWindowedLayout();
  void reapplyFullscreenScale();
  void ensureMenuBarDocked();
  void noteFullscreenMouseActivity(const QPoint& global_pos);
  void scheduleFullscreenChromeHide();
  void hideFullscreenChrome();
  bool isSignificantFullscreenMouseMove(const QPoint& global_pos) const;
  void showFullscreenCursor();
  void hideFullscreenCursor();
  void connectFullscreenMenuHooks();
  bool isMouseInsideWindow(const QPoint& global_pos) const;
  bool isAnyMenuVisible() const;
  void syncControllerFullscreenState();
  double screenRefreshHz() const;
  void stopEmulator();
  void focusEmuView();
  void openConfigureDialog();
  void openDiskImageDialog(int drive);
  void openBothDrivesDialog();
  void openRecentDiskImage(const QString& path);
  void rebuildRecentDiskMenu();
  void openMultiDiskEditorDialog();
  void captureScreen();
  void toggleRecordSound();
  void rebuildSnapshotMenu();
  void invokeSaveSnapshot(int slot);
  void invokeLoadSnapshot(int slot);
  void applyRomMissingUiState();
  void rebuildDriveMenu(int drive, const QString& path, int numDisks,
                        int currentDisk, const QStringList& titles);
  static QString DriveBaseName(const QString& path);
  static QString SnapshotBaseTitle(const QString& drive0_path);

  static constexpr const char* kDiskImageFilter =
      "8801 disk image (*.d88);;All files (*)";

  int view_scale_ = 2;
  int windowed_view_scale_ = 2;
  bool fullscreen_ = false;
  qint64 last_fullscreen_mouse_move_ms_ = 0;
  qint64 last_fullscreen_menubar_show_ms_ = 0;
  QPoint last_fullscreen_mouse_global_;
  bool have_last_fullscreen_mouse_global_ = false;
  bool fullscreen_cursor_hidden_ = false;
  static constexpr int kFullscreenChromeToggleMs = 250;
  static constexpr int kFullscreenMouseMoveThresholdPx = 5;
  bool force480_ = false;
  bool sync_to_vsync_ = false;
  QRect windowed_geometry_;
  quint64 last_display_toggle_ms_ = 0;
  bool toggling_display_ = false;
  bool save_position_ = false;
  int winpos_x_ = 64;
  int winpos_y_ = 64;
  QAction* fullscreen_action_ = nullptr;
  QAction* exit_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QMenu* control_menu_ = nullptr;
  QMenu* help_menu_ = nullptr;
  QMenu* disk_menu_ = nullptr;
  QAction* drive_actions_[2] = {nullptr, nullptr};
  QMenu* drive_submenus_[2] = {nullptr, nullptr};
  QActionGroup* disk_select_groups_[2] = {nullptr, nullptr};
  QAction* change_both_action_ = nullptr;
  QAction* recent_disk_action_ = nullptr;
  QMenu* recent_disk_submenu_ = nullptr;
  QAction* multi_disk_editor_action_ = nullptr;
  QActionGroup* clock_group_ = nullptr;
  QAction* clock_4mhz_ = nullptr;
  QAction* clock_8mhz_ = nullptr;
  QAction* burst_action_ = nullptr;
  QAction* arrow_tenkey_action_ = nullptr;
  QAction* reset_action_ = nullptr;
  bool ask_before_reset_ = false;
  bool f12_as_reset_ = true;
  QAction* show_status_action_ = nullptr;
  QAction* record_sound_action_ = nullptr;
  QMenu* tools_menu_ = nullptr;
  QAction* save_snapshot_action_ = nullptr;
  QAction* load_snapshot_action_ = nullptr;
  QMenu* save_snapshot_submenu_ = nullptr;
  QMenu* load_snapshot_submenu_ = nullptr;
  QString snapshot_drive0_path_;
  int current_snapshot_slot_ = 0;
  QAction* fdc_status_action_ = nullptr;
  QAction* watch_register_action_ = nullptr;
  QAction* dump_cpu1_log_action_ = nullptr;
  QAction* dump_cpu2_log_action_ = nullptr;
  bool watch_register_enabled_ = false;
  QLabel* kana_status_label_ = nullptr;
  QLabel* status_message_label_ = nullptr;
  QTimer* status_message_timer_ = nullptr;
  QLabel* register_label_ = nullptr;
  QLabel* fdc_text_label_ = nullptr;
  FcitxStatus* fcitx_status_ = nullptr;
  QTimer* fcitx_poll_timer_ = nullptr;
  QTimer* title_timer_ = nullptr;
  QTimer* fullscreen_chrome_hide_timer_ = nullptr;
  static constexpr int kFullscreenChromeHideMs = 5000;
  static constexpr int kDefaultStatusMessageMs = 5000;
  QWidget* fdc_lamp_panel_ = nullptr;
  QLabel* fdc_lamp_labels_[3] = {nullptr, nullptr, nullptr};
  QActionGroup* mode_group_ = nullptr;
  std::vector<QAction*> mode_actions_;
  SharedFramebufferDraw* draw_ = nullptr;
  EmuView* view_ = nullptr;
  QtHostInput::Host host_input_;
  QPointer<EmulatorController> controller_;
  QThread emu_thread_;
  bool emu_stopped_ = false;
  bool closing_ = false;
  bool window_shown_ = false;
  bool rom_missing_ = false;
  int current_basicmode_ = 0;
};
