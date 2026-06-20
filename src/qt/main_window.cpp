#include "main_window.h"

#include "config_dialog.h"
#include "about_dialog.h"
#include "confirm_dialog.h"
#include "multi_disk_editor_dialog.h"
#include "emu_view.h"
#include "qt_platform.h"
#include "../linux/shared_framebuffer_draw.h"

#include "../linux/display_scale.h"
#include "../linux/linux_config.h"
#include "../linux/linux_ime.h"
#include "../linux/linux_paths.h"
#include "../linux/m88_port_version.h"
#include "../linux/m88_wayland_idle_inhibit.h"
#include "../pc88/config.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QLabel>
#include <QMenuBar>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QMessageBox>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QShowEvent>
#include <QHideEvent>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {

void SetWorkingDirectory(const QString& dir) {
  if (dir.isEmpty()) {
    return;
  }
  const QByteArray utf8 = QFileInfo(dir).absoluteFilePath().toUtf8();
  if (chdir(utf8.constData()) == 0) {
    QDir::setCurrent(QString::fromLocal8Bit(utf8.constData()));
  }
}

QAction* AddPlaceholder(QMenu* menu, const QString& text, bool checkable = false,
                        bool checked = false) {
  QAction* action = menu->addAction(text);
  action->setEnabled(false);
  if (checkable) {
    action->setCheckable(true);
    action->setChecked(checked);
  }
  return action;
}

QKeySequence HostShortcut(int key) {
  return QKeySequence(Qt::META | Qt::SHIFT | key);
}

}  // namespace

void MainWindow::stopEmulator() {
  if (emu_stopped_) {
    return;
  }

  if (view_) {
    view_->releaseKeyboard();
    view_->attachFramebuffer(nullptr);
  }

  EmulatorController* controller_raw = controller_.data();
  if (controller_raw) {
    disconnect(controller_raw, nullptr, view_, nullptr);
    disconnect(controller_raw, nullptr, this, nullptr);
    disconnect(&emu_thread_, nullptr, controller_raw, nullptr);
    disconnect(controller_raw, nullptr, &emu_thread_, nullptr);
    disconnect(&emu_thread_, &QThread::finished, controller_raw, &QObject::deleteLater);
    if (QThread* ct = controller_raw->thread(); ct && ct != QThread::currentThread()) {
      QMetaObject::invokeMethod(controller_raw, "requestStop", Qt::BlockingQueuedConnection);
    } else {
      controller_raw->requestStop();
    }
  }
  if (view_) {
    disconnect(view_, nullptr, controller_, nullptr);
  }

  if (emu_thread_.isRunning()) {
    QEventLoop wait_loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&emu_thread_, &QThread::finished, &wait_loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &wait_loop, &QEventLoop::quit);
    timeout.start(30000);
    wait_loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (emu_thread_.isRunning()) {
      std::fprintf(stderr,
                   "M88: emulator thread did not stop in 30s; skipping teardown\n");
      return;
    }
    emu_thread_.wait();
  }

  emu_stopped_ = true;

  if (controller_raw) {
    controller_.clear();
    controller_raw->deleteLater();
    controller_raw = nullptr;
  }

  if (QCoreApplication* app = QCoreApplication::instance()) {
    app->processEvents(QEventLoop::AllEvents);
  }

  if (draw_) {
    draw_->Cleanup();
    delete draw_;
    draw_ = nullptr;
  }
}

void MainWindow::applyViewScale(int scale) {
  view_scale_ = std::max(1, scale);
  windowed_view_scale_ = view_scale_;
  if (view_) {
    view_->setScale(view_scale_);
    view_->setForce480Layout(false);
  }
  int chrome_h = kM88QtChromeH;
  if (menuBar()) {
    chrome_h += menuBar()->sizeHint().height();
  }
  resize(kM88EmuWidth * view_scale_ + kM88QtChromeW,
         kM88EmuHeight * view_scale_ + chrome_h);
}

double MainWindow::screenRefreshHz() const {
  const QScreen* screen =
      windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
  if (!screen) {
    return 60.0;
  }
  const double hz = screen->refreshRate();
  return hz > 0.0 ? hz : 60.0;
}

void MainWindow::syncControllerFullscreenState() {
  if (!controller_) {
    return;
  }
  QMetaObject::invokeMethod(controller_, "setFullscreenState", Qt::QueuedConnection,
                            Q_ARG(bool, fullscreen_),
                            Q_ARG(double, screenRefreshHz()));
}

void MainWindow::scheduleFullscreenChromeHide() {
  if (!fullscreen_ || !fullscreen_chrome_hide_timer_) {
    return;
  }
  fullscreen_chrome_hide_timer_->start(kFullscreenChromeHideMs);
}

bool MainWindow::isSignificantFullscreenMouseMove(const QPoint& global_pos) const {
  if (!have_last_fullscreen_mouse_global_) {
    return true;
  }
  const QPoint delta = global_pos - last_fullscreen_mouse_global_;
  return (delta.x() * delta.x() + delta.y() * delta.y()) >=
         kFullscreenMouseMoveThresholdPx * kFullscreenMouseMoveThresholdPx;
}

void MainWindow::showFullscreenCursor() {
  if (!fullscreen_cursor_hidden_) {
    return;
  }
  if (QGuiApplication::overrideCursor()) {
    QGuiApplication::restoreOverrideCursor();
  }
  fullscreen_cursor_hidden_ = false;
}

void MainWindow::hideFullscreenCursor() {
  if (!fullscreen_ || fullscreen_cursor_hidden_) {
    return;
  }
  QGuiApplication::setOverrideCursor(Qt::BlankCursor);
  fullscreen_cursor_hidden_ = true;
}

void MainWindow::ensureMenuBarDocked() {
  QMenuBar* mb = menuBar();
  if (!mb) {
    return;
  }
  if (mb->parentWidget() != this) {
    mb->hide();
    setMenuBar(mb);
  }
  mb->setMouseTracking(true);
  if (!fullscreen_) {
    mb->show();
  }
}

void MainWindow::noteFullscreenMouseActivity(const QPoint& global_pos) {
  if (!fullscreen_) {
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  last_fullscreen_mouse_move_ms_ = now;
  last_fullscreen_mouse_global_ = global_pos;
  have_last_fullscreen_mouse_global_ = true;
  showFullscreenCursor();

  QMenuBar* mb = menuBar();
  if (!mb) {
    return;
  }
  ensureMenuBarDocked();
  if (!mb->isVisible()) {
    mb->show();
    last_fullscreen_menubar_show_ms_ = now;
  }
  scheduleFullscreenChromeHide();
}

void MainWindow::hideFullscreenChrome() {
  if (!fullscreen_) {
    return;
  }
  if (isAnyMenuVisible()) {
    scheduleFullscreenChromeHide();
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - last_fullscreen_mouse_move_ms_ < kFullscreenChromeHideMs) {
    scheduleFullscreenChromeHide();
    return;
  }
  if (now - last_fullscreen_menubar_show_ms_ < kFullscreenChromeToggleMs) {
    scheduleFullscreenChromeHide();
    return;
  }

  QMenuBar* mb = menuBar();
  if (mb && mb->isVisible()) {
    mb->hide();
  }
  hideFullscreenCursor();
  if (fullscreen_chrome_hide_timer_) {
    fullscreen_chrome_hide_timer_->stop();
  }
}

bool MainWindow::isMouseInsideWindow(const QPoint& global_pos) const {
  if (!isVisible()) {
    return false;
  }
  return rect().contains(mapFromGlobal(global_pos));
}

bool MainWindow::isAnyMenuVisible() const {
  if (!menuBar()) {
    return false;
  }
  QList<const QMenu*> pending;
  for (QAction* action : menuBar()->actions()) {
    if (QMenu* menu = action->menu()) {
      pending.append(menu);
    }
  }
  while (!pending.isEmpty()) {
    const QMenu* menu = pending.takeFirst();
    if (menu->isVisible()) {
      return true;
    }
    for (const QAction* action : menu->actions()) {
      if (const QMenu* sub = action->menu()) {
        pending.append(sub);
      }
    }
  }
  return false;
}

void MainWindow::connectFullscreenMenuHooks() {
  if (!menuBar()) {
    return;
  }
  QList<QMenu*> pending;
  for (QAction* action : menuBar()->actions()) {
    if (QMenu* menu = action->menu()) {
      pending.append(menu);
    }
  }
  while (!pending.isEmpty()) {
    QMenu* menu = pending.takeFirst();
    connect(menu, &QMenu::aboutToHide, this, [this]() {
      if (fullscreen_) {
        scheduleFullscreenChromeHide();
      }
    });
    for (QAction* action : menu->actions()) {
      if (QMenu* sub = action->menu()) {
        pending.append(sub);
      }
    }
  }
}

void MainWindow::reapplyFullscreenScale() {
  const QScreen* screen =
      windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
  const QRect geo = screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
  const int scale = M88ComputeFullscreenScale(geo.width(), geo.height(), force480_);
  view_scale_ = scale;
  if (view_) {
    view_->setScale(scale);
    view_->setForce480Layout(force480_);
  }
  syncControllerFullscreenState();
}

void MainWindow::applyFullscreenLayout() {
  reapplyFullscreenScale();
  if (statusBar()) {
    statusBar()->hide();
  }
  if (fullscreen_chrome_hide_timer_) {
    fullscreen_chrome_hide_timer_->stop();
  }
  ensureMenuBarDocked();
  have_last_fullscreen_mouse_global_ = false;
  last_fullscreen_mouse_move_ms_ = 0;
  last_fullscreen_menubar_show_ms_ = 0;
  showFullscreenCursor();
  if (menuBar()) {
    menuBar()->hide();
  }
  showFullScreen();
  focusEmuView();
  scheduleFullscreenChromeHide();
}

void MainWindow::applyWindowedLayout() {
  if (fullscreen_chrome_hide_timer_) {
    fullscreen_chrome_hide_timer_->stop();
  }
  have_last_fullscreen_mouse_global_ = false;
  last_fullscreen_mouse_move_ms_ = 0;
  last_fullscreen_menubar_show_ms_ = 0;
  showFullscreenCursor();
  ensureMenuBarDocked();
  view_scale_ = windowed_view_scale_;
  if (view_) {
    view_->setScale(view_scale_);
    view_->setForce480Layout(false);
  }
  showNormal();
  if (!windowed_geometry_.isNull()) {
    setGeometry(windowed_geometry_);
  } else {
    applyViewScale(view_scale_);
  }
  if (controller_) {
    PC8801::Config cfg;
    if (QMetaObject::invokeMethod(controller_, "exportConfig",
                                  Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(PC8801::Config, cfg))) {
      if (cfg.flags & PC8801::Config::showstatusbar) {
        if (statusBar()) {
          statusBar()->show();
        }
      }
    }
  }
  syncControllerFullscreenState();
  focusEmuView();
}

void MainWindow::updateDisplayConfig(bool force480, bool sync_to_vsync) {
  const bool changed =
      (force480_ != force480) || (sync_to_vsync_ != sync_to_vsync);
  force480_ = force480;
  sync_to_vsync_ = sync_to_vsync;
  if (fullscreen_ && changed) {
    reapplyFullscreenScale();
  }
}

void MainWindow::toggleFullscreen() {
  const quint64 now = static_cast<quint64>(QDateTime::currentMSecsSinceEpoch());
  if (now - last_display_toggle_ms_ < 300) {
    return;
  }
  last_display_toggle_ms_ = now;

  toggling_display_ = true;
  if (!fullscreen_) {
    windowed_geometry_ = geometry();
    windowed_view_scale_ = view_scale_;
    fullscreen_ = true;
    applyFullscreenLayout();
  } else {
    fullscreen_ = false;
    applyWindowedLayout();
  }
  toggling_display_ = false;
  if (fullscreen_action_) {
    fullscreen_action_->setChecked(fullscreen_);
  }
}

QString MainWindow::DriveBaseName(const QString& path) {
  if (path.isEmpty()) {
    return QString();
  }
  QString name = QFileInfo(path).completeBaseName();
  return name.isEmpty() ? QFileInfo(path).fileName() : name;
}

void MainWindow::rebuildDriveMenu(int drive, const QString& path, int numDisks,
                                  int currentDisk, const QStringList& titles) {
  if (drive < 0 || drive > 1 || !disk_menu_ || !drive_actions_[drive]) {
    return;
  }

  QAction* drive_action = drive_actions_[drive];
  QMenu* submenu = drive_submenus_[drive];
  if (submenu) {
    submenu->clear();
    delete disk_select_groups_[drive];
    disk_select_groups_[drive] = nullptr;
  }

  if (numDisks <= 0) {
    drive_action->setMenu(nullptr);
    drive_action->setText(tr("Drive &%1...").arg(drive + 1));
    return;
  }

  if (!submenu) {
    submenu = new QMenu(this);
    drive_submenus_[drive] = submenu;
  }

  disk_select_groups_[drive] = new QActionGroup(this);
  disk_select_groups_[drive]->setExclusive(true);

  const int ndisks = std::min(numDisks, 60);
  for (int i = 0; i < ndisks; ++i) {
    QString label;
    if (i < 9) {
      label = tr("&%1 %2").arg(i + 1).arg(titles.value(i, QStringLiteral("(untitled)")));
    } else {
      label = tr("%1 %2").arg(i + 1).arg(titles.value(i, QStringLiteral("(untitled)")));
    }
    QAction* item = submenu->addAction(label);
    item->setCheckable(true);
    item->setChecked(i == currentDisk);
    disk_select_groups_[drive]->addAction(item);
    connect(item, &QAction::triggered, this, [this, drive, i]() {
      if (controller_) {
        QMetaObject::invokeMethod(controller_, "selectDisk", Qt::QueuedConnection,
                                  Q_ARG(int, drive), Q_ARG(int, i));
      }
    });
  }

  submenu->addSeparator();
  QAction* no_disk = submenu->addAction(tr("&N No disk"));
  no_disk->setCheckable(true);
  no_disk->setChecked(currentDisk < 0);
  disk_select_groups_[drive]->addAction(no_disk);
  connect(no_disk, &QAction::triggered, this, [this, drive]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "selectDisk", Qt::QueuedConnection,
                                Q_ARG(int, drive), Q_ARG(int, 63));
    }
  });

  QAction* change_disk = submenu->addAction(tr("&0 Change disk"));
  connect(change_disk, &QAction::triggered, this, [this, drive]() {
    openDiskImageDialog(drive);
  });

  const QString base = DriveBaseName(path);
  drive_action->setText(base.isEmpty() ? tr("Drive &%1...").arg(drive + 1)
                                       : tr("Drive &%1 - %2").arg(drive + 1).arg(base));
  drive_action->setMenu(submenu);
}

void MainWindow::updateDiskMenu(QString drive0Path, int drive0NumDisks,
                                int drive0Current, QStringList drive0Titles,
                                QString drive1Path, int drive1NumDisks,
                                int drive1Current, QStringList drive1Titles) {
  snapshot_drive0_path_ = drive0Path;
  rebuildDriveMenu(0, drive0Path, drive0NumDisks, drive0Current, drive0Titles);
  rebuildDriveMenu(1, drive1Path, drive1NumDisks, drive1Current, drive1Titles);
  rebuildSnapshotMenu();
}

void MainWindow::updateSnapshotState(int current_slot) {
  if (current_slot >= 0 && current_slot <= 9) {
    current_snapshot_slot_ = current_slot;
  }
  rebuildSnapshotMenu();
}

QString MainWindow::SnapshotBaseTitle(const QString& drive0_path) {
  if (!drive0_path.isEmpty()) {
    const QString base = QFileInfo(drive0_path).completeBaseName();
    if (!base.isEmpty()) {
      return base;
    }
  }
  return QStringLiteral("snapshot");
}

void MainWindow::invokeSaveSnapshot(int slot) {
  if (!controller_) {
    return;
  }
  QMetaObject::invokeMethod(controller_, "saveSnapshot", Qt::QueuedConnection,
                            Q_ARG(int, slot));
}

void MainWindow::invokeLoadSnapshot(int slot) {
  if (!controller_) {
    return;
  }
  QMetaObject::invokeMethod(controller_, "loadSnapshot", Qt::QueuedConnection,
                            Q_ARG(int, slot));
}

void MainWindow::rebuildSnapshotMenu() {
  if (!save_snapshot_submenu_ || !load_snapshot_submenu_) {
    return;
  }

  save_snapshot_submenu_->clear();
  load_snapshot_submenu_->clear();

  const QString base = SnapshotBaseTitle(snapshot_drive0_path_);
  int entries = 0;
  const QString snap_dir = QString::fromUtf8(M88GetSnapshotDir());
  for (int i = 0; i < 10; ++i) {
    const QString path = QDir(snap_dir).filePath(QStringLiteral("%1_%2.s88").arg(base).arg(i));
    if (QFile::exists(path)) {
      entries |= (1 << i);
    }
  }

  for (int i = 0; i < 10; ++i) {
    QAction* save_action =
        save_snapshot_submenu_->addAction(QStringLiteral("&%1").arg(i));
    save_action->setCheckable(true);
    save_action->setChecked((entries & (1 << i)) != 0);
    connect(save_action, &QAction::triggered, this, [this, i]() {
      invokeSaveSnapshot(i);
    });
  }

  if (entries != 0) {
    load_snapshot_action_->setMenu(load_snapshot_submenu_);
    for (int i = 0; i < 10; ++i) {
      if ((entries & (1 << i)) == 0) {
        continue;
      }
      QAction* load_action =
          load_snapshot_submenu_->addAction(QStringLiteral("&%1").arg(i));
      load_action->setCheckable(true);
      load_action->setChecked(i == current_snapshot_slot_);
      connect(load_action, &QAction::triggered, this, [this, i]() {
        invokeLoadSnapshot(i);
      });
    }
  } else {
    load_snapshot_action_->setMenu(nullptr);
  }
}

void MainWindow::openConfigureDialog() {
  if (!controller_) {
    return;
  }
  PC8801::Config cfg;
  if (!QMetaObject::invokeMethod(controller_, "exportConfig",
                                 Qt::BlockingQueuedConnection,
                                 Q_RETURN_ARG(PC8801::Config, cfg))) {
    return;
  }
  ConfigDialog dlg(cfg, this);
  connect(&dlg, &ConfigDialog::settingsApplied, this, [this](PC8801::Config config) {
    syncRememberPrefsFromConfig(config);
    updateDisplayConfig((config.flags & PC8801::Config::force480) != 0,
                        (config.flag2 & PC8801::Config::synctovsync) != 0);
    syncWaylandIdleInhibit();
    syncImeKanaInput();
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "importConfig", Qt::QueuedConnection,
                                Q_ARG(PC8801::Config, config));
    }
  });
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  const PC8801::Config config = dlg.config();
  syncRememberPrefsFromConfig(config);
  syncWaylandIdleInhibit();
  syncImeKanaInput();
  QMetaObject::invokeMethod(controller_, "importConfig", Qt::QueuedConnection,
                            Q_ARG(PC8801::Config, config));
}

void MainWindow::openDiskImageDialog(int drive) {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open disk image"), QDir::currentPath(), tr(kDiskImageFilter));
  if (path.isEmpty()) {
    return;
  }
  SetWorkingDirectory(QFileInfo(path).absolutePath());
  if (controller_) {
    QMetaObject::invokeMethod(controller_, "changeDiskImage", Qt::QueuedConnection,
                              Q_ARG(int, drive), Q_ARG(QString, path));
  }
}

void MainWindow::openBothDrivesDialog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open disk image"), QDir::currentPath(), tr(kDiskImageFilter));
  if (path.isEmpty()) {
    return;
  }
  SetWorkingDirectory(QFileInfo(path).absolutePath());
  if (controller_) {
    QMetaObject::invokeMethod(controller_, "changeBothDrives", Qt::QueuedConnection,
                              Q_ARG(QString, path));
  }
}

void MainWindow::openMultiDiskEditorDialog() {
  MultiDiskEditorDialog dlg(this);
  dlg.exec();
}

namespace {

QString LampGlyph(int level) {
  return level > 0 ? QStringLiteral("\u25cf") : QStringLiteral("\u25cb");
}

bool IsFdcStatusMessage(const QString& message) {
  return message.startsWith(QStringLiteral("Read")) ||
         message.startsWith(QStringLiteral("Write")) ||
         message.startsWith(QStringLiteral("Scan")) ||
         message.startsWith(QStringLiteral("ReadID")) ||
         message.startsWith(QStringLiteral("WriteID")) ||
         message.startsWith(QStringLiteral("ReadDiagnostic"));
}

}  // namespace

void MainWindow::updateStatusUi(bool bar_enabled, bool show_fdc_lamps, int lamp0,
                                int lamp1, int lamp2, QString message, int message_ms) {
  if (fullscreen_) {
    if (statusBar()) {
      statusBar()->hide();
    }
    return;
  }
  if (QStatusBar* sb = statusBar()) {
    if (bar_enabled) {
      sb->show();
    } else {
      sb->hide();
      return;
    }
  }
  if (fdc_lamp_panel_) {
    fdc_lamp_panel_->setVisible(show_fdc_lamps);
  }
  if (show_fdc_lamps) {
    if (fdc_lamp_labels_[0]) {
      fdc_lamp_labels_[0]->setText(LampGlyph(lamp0));
    }
    if (fdc_lamp_labels_[1]) {
      fdc_lamp_labels_[1]->setText(LampGlyph(lamp1));
    }
    if (fdc_lamp_labels_[2]) {
      fdc_lamp_labels_[2]->setText(LampGlyph(lamp2));
    }
  }
  if (!statusBar()) {
    return;
  }
  if (fdc_text_label_) {
    if (show_fdc_lamps && IsFdcStatusMessage(message)) {
      fdc_text_label_->setText(message);
      fdc_text_label_->show();
    } else {
      fdc_text_label_->clear();
      fdc_text_label_->hide();
    }
  }
  if (message.isEmpty() || (show_fdc_lamps && IsFdcStatusMessage(message))) {
    statusBar()->clearMessage();
    return;
  }
  statusBar()->showMessage(message, message_ms > 0 ? message_ms : 0);
}

void MainWindow::updateTitleStats(int fps, int mhz_whole, int mhz_frac) {
  if (!isActiveWindow()) {
    setWindowTitle(QStringLiteral("M88"));
    return;
  }
  setWindowTitle(tr("M88 - %1 fps.  %2.%3 MHz")
                     .arg(fps)
                     .arg(mhz_whole)
                     .arg(mhz_frac, 2, 10, QChar('0')));
}

bool MainWindow::confirmReset() {
  if (!ask_before_reset_) {
    return true;
  }
  return ConfirmDialog::Ask(ConfirmDialog::Kind::Reset, this);
}

bool MainWindow::confirmExit() {
  if (!ask_before_reset_) {
    return true;
  }
  return ConfirmDialog::Ask(ConfirmDialog::Kind::Exit, this);
}

void MainWindow::updateControlMenu(int clock, int basicmode, bool n80_supported,
                                    bool n80v2_supported, bool cd_supported,
                                    bool burst_mode, bool arrow_tenkey,
                                    bool show_statusbar, bool show_fdc_status,
                                    bool ask_before_reset, bool f12_as_reset,
                                    bool suppress_menu) {
  if (rom_missing_) {
    applyRomMissingUiState();
    return;
  }
  ask_before_reset_ = ask_before_reset;
  f12_as_reset_ = f12_as_reset;
  if (view_) {
    view_->setSuppressMenu(suppress_menu);
  }
  if (reset_action_) {
    if (f12_as_reset_) {
      reset_action_->setShortcut(QKeySequence(Qt::Key_F12));
    } else {
      reset_action_->setShortcut(QKeySequence());
    }
  }
  const bool clock_fixed_4mhz = M88BasicModeFixesClock4MHz(basicmode);
  if (clock_4mhz_) {
    clock_4mhz_->setChecked(clock_fixed_4mhz || clock == 40);
    clock_4mhz_->setEnabled(!clock_fixed_4mhz);
  }
  if (clock_8mhz_) {
    clock_8mhz_->setChecked(!clock_fixed_4mhz && clock == 80);
    clock_8mhz_->setEnabled(!clock_fixed_4mhz);
  }
  for (QAction* action : mode_actions_) {
    const int mode = action->data().toInt();
    action->setChecked(mode == basicmode);
    bool enabled = true;
    if (mode == static_cast<int>(PC8801::Config::N802)) {
      enabled = n80_supported;
    } else if (mode == static_cast<int>(PC8801::Config::N80V2)) {
      enabled = n80v2_supported;
    } else if (mode == static_cast<int>(PC8801::Config::N88V2CD)) {
      enabled = cd_supported;
    }
    action->setEnabled(enabled);
  }
  if (burst_action_) {
    burst_action_->setChecked(burst_mode);
  }
  if (arrow_tenkey_action_) {
    arrow_tenkey_action_->setChecked(arrow_tenkey);
  }
  if (show_status_action_) {
    show_status_action_->setChecked(show_statusbar);
  }
  if (fdc_status_action_) {
    fdc_status_action_->setEnabled(show_statusbar);
    fdc_status_action_->setChecked(show_fdc_status);
  }
  (void)clock;
}

void MainWindow::setupMenuBar() {
  // Match Win32 IDR_MENU_M88 (M88.rc); unimplemented items stay visible but disabled.
  control_menu_ = menuBar()->addMenu(tr("&Control"));

  reset_action_ = control_menu_->addAction(tr("&Reset"));
  reset_action_->setShortcut(QKeySequence(Qt::Key_F12));

  control_menu_->addSeparator();

  clock_group_ = new QActionGroup(this);
  clock_group_->setExclusive(true);
  clock_4mhz_ = control_menu_->addAction(tr("4MHz"));
  clock_4mhz_->setCheckable(true);
  clock_group_->addAction(clock_4mhz_);
  clock_8mhz_ = control_menu_->addAction(tr("8MHz"));
  clock_8mhz_->setCheckable(true);
  clock_group_->addAction(clock_8mhz_);

  control_menu_->addSeparator();

  mode_group_ = new QActionGroup(this);
  mode_group_->setExclusive(true);
  static const struct {
    const char* label;
    PC8801::Config::BASICMode mode;
  } kModes[] = {
      {"N88-V&1(S) mode", PC8801::Config::N88V1},
      {"N88-V1(&H) mode", PC8801::Config::N88V1H},
      {"N88-V&2 mode", PC8801::Config::N88V2},
      {"N88-V2(C&D) mode", PC8801::Config::N88V2CD},
      {"&N mode", PC8801::Config::N80},
      {"N8&0 mode", PC8801::Config::N802},
      {"N&80SR mode", PC8801::Config::N80V2},
  };
  mode_actions_.clear();
  for (const auto& item : kModes) {
    QAction* action = control_menu_->addAction(tr(item.label));
    action->setCheckable(true);
    action->setData(static_cast<int>(item.mode));
    mode_group_->addAction(action);
    mode_actions_.push_back(action);
    const int mode = static_cast<int>(item.mode);
    connect(action, &QAction::triggered, this, [this, mode]() {
      if (controller_) {
        QMetaObject::invokeMethod(controller_, "setBasicMode", Qt::QueuedConnection,
                                  Q_ARG(int, mode));
      }
    });
  }

  connect(clock_4mhz_, &QAction::triggered, this, [this]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setClock", Qt::QueuedConnection,
                                Q_ARG(int, 40));
    }
  });
  connect(clock_8mhz_, &QAction::triggered, this, [this]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setClock", Qt::QueuedConnection,
                                Q_ARG(int, 80));
    }
  });

  control_menu_->addSeparator();
  fullscreen_action_ = control_menu_->addAction(tr("Toggle &fullscreen"));
  fullscreen_action_->setCheckable(true);
  fullscreen_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
  connect(fullscreen_action_, &QAction::triggered, this, &MainWindow::toggleFullscreen);

  arrow_tenkey_action_ =
      control_menu_->addAction(tr("Map arrow keys to ten-key (&N)"));
  arrow_tenkey_action_->setCheckable(true);
  arrow_tenkey_action_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_N));
  connect(arrow_tenkey_action_, &QAction::triggered, this, [this](bool checked) {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setArrowTenkey", Qt::QueuedConnection,
                                Q_ARG(bool, checked));
    }
  });

  burst_action_ = control_menu_->addAction(tr("&Burst mode"));
  burst_action_->setCheckable(true);
  connect(burst_action_, &QAction::triggered, this, [this](bool checked) {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setBurstMode", Qt::QueuedConnection,
                                Q_ARG(bool, checked));
    }
  });

  control_menu_->addSeparator();
  exit_action_ = control_menu_->addAction(tr("E&xit"));
  exit_action_->setShortcut(HostShortcut(Qt::Key_Q));

  disk_menu_ = menuBar()->addMenu(tr("&Disk"));
  drive_actions_[0] = disk_menu_->addAction(tr("Drive &1..."));
  drive_actions_[1] = disk_menu_->addAction(tr("Drive &2..."));
  disk_menu_->addSeparator();
  change_both_action_ = disk_menu_->addAction(tr("&Change disk image..."));
  change_both_action_->setShortcut(HostShortcut(Qt::Key_O));
  multi_disk_editor_action_ =
      disk_menu_->addAction(tr("&Edit multi-disk image..."));

  connect(drive_actions_[0], &QAction::triggered, this,
          [this]() { openDiskImageDialog(0); });
  connect(drive_actions_[1], &QAction::triggered, this, [this]() {
    if (!drive_actions_[1]->menu()) {
      openDiskImageDialog(1);
    }
  });
  connect(change_both_action_, &QAction::triggered, this,
          &MainWindow::openBothDrivesDialog);
  connect(multi_disk_editor_action_, &QAction::triggered, this,
          &MainWindow::openMultiDiskEditorDialog);

  auto* tape_menu = menuBar()->addMenu(tr("Ta&pe"));
  AddPlaceholder(tape_menu, tr("&Open..."));
  tape_menu->menuAction()->setEnabled(false);

  tools_menu_ = menuBar()->addMenu(tr("&Tools"));
  auto* tools_menu = tools_menu_;
  QAction* configure_action = tools_menu->addAction(tr("&Configure..."));
  connect(configure_action, &QAction::triggered, this, &MainWindow::openConfigureDialog);
  tools_menu->addSeparator();
  show_status_action_ = tools_menu->addAction(tr("S&how Status"));
  show_status_action_->setCheckable(true);
  connect(show_status_action_, &QAction::triggered, this, [this](bool checked) {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setShowStatusBar", Qt::QueuedConnection,
                                Q_ARG(bool, checked));
    }
  });
  QAction* capture_action = tools_menu->addAction(tr("&Capture..."));
  capture_action->setShortcut(HostShortcut(Qt::Key_C));
  connect(capture_action, &QAction::triggered, this, &MainWindow::captureScreen);
  record_sound_action_ = tools_menu->addAction(tr("&Record Sound"));
  record_sound_action_->setCheckable(true);
  connect(record_sound_action_, &QAction::triggered, this, &MainWindow::toggleRecordSound);
  tools_menu->addSeparator();
  save_snapshot_action_ = tools_menu->addAction(tr("&Save Snapshot"));
  save_snapshot_action_->setShortcut(HostShortcut(Qt::Key_S));
  save_snapshot_submenu_ = new QMenu(this);
  save_snapshot_action_->setMenu(save_snapshot_submenu_);
  connect(save_snapshot_action_, &QAction::triggered, this, [this]() {
    invokeSaveSnapshot(-1);
  });

  load_snapshot_action_ = tools_menu->addAction(tr("&Load Snapshot"));
  load_snapshot_action_->setShortcut(HostShortcut(Qt::Key_L));
  load_snapshot_submenu_ = new QMenu(this);
  connect(load_snapshot_action_, &QAction::triggered, this, [this]() {
    invokeLoadSnapshot(-1);
  });

  connect(tools_menu_, &QMenu::aboutToShow, this, [this]() {
    if (record_sound_action_ && controller_) {
      bool recording = false;
      QMetaObject::invokeMethod(controller_, "isRecordingSound",
                                Qt::BlockingQueuedConnection,
                                Q_RETURN_ARG(bool, recording));
      record_sound_action_->setChecked(recording);
    }
    rebuildSnapshotMenu();
  });
  rebuildSnapshotMenu();

  auto* debug_menu = menuBar()->addMenu(tr("D&ebug"));
  fdc_status_action_ = debug_menu->addAction(tr("Show &FDC Status"));
  fdc_status_action_->setCheckable(true);
  fdc_status_action_->setEnabled(false);
  connect(fdc_status_action_, &QAction::triggered, this, [this](bool checked) {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setShowFdcStatus", Qt::QueuedConnection,
                                Q_ARG(bool, checked));
    }
  });
  AddPlaceholder(debug_menu, tr("Show &Register"), true, false);
  AddPlaceholder(debug_menu, tr("Show &OPN Register"), true, false);
  AddPlaceholder(debug_menu, tr("Show &Memory"), true, false);
  AddPlaceholder(debug_menu, tr("Show &Code"), true, false);
  AddPlaceholder(debug_menu, tr("Show &Basic code"), true, false);
  AddPlaceholder(debug_menu, tr("Show Out&port"), true, false);
  AddPlaceholder(debug_menu, tr("&Load Monitor"), true, false);
  debug_menu->addSeparator();
  AddPlaceholder(debug_menu, tr("Dump CPU&1 Log"), true, false);
  AddPlaceholder(debug_menu, tr("Dump CPU&2 Log"), true, false);

  help_menu_ = menuBar()->addMenu(tr("&Help"));
  about_action_ = help_menu_->addAction(tr("&About"));

  connect(exit_action_, &QAction::triggered, this, &QWidget::close);
  connect(reset_action_, &QAction::triggered, this, [this]() {
    if (!confirmReset()) {
      return;
    }
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "resetMachine", Qt::QueuedConnection);
    }
  });
  connect(about_action_, &QAction::triggered, this, [this]() {
    AboutDialog dlg(this);
    dlg.exec();
  });

  if (disk_menu_) {
    connect(disk_menu_, &QMenu::aboutToShow, this, [this]() {
      if (controller_) {
        QMetaObject::invokeMethod(controller_, "emitDiskConfiguration",
                                  Qt::QueuedConnection);
      }
    });
  }
}

void MainWindow::applyRomMissingUiState() {
  rom_missing_ = true;
  if (title_timer_) {
    title_timer_->stop();
  }
  if (change_both_action_) {
    change_both_action_->setEnabled(false);
  }

  for (QAction* top : menuBar()->actions()) {
    QMenu* menu = top->menu();
    if (!menu) {
      continue;
    }

    if (menu == control_menu_) {
      top->setEnabled(true);
      for (QAction* action : menu->actions()) {
        action->setEnabled(action == exit_action_);
      }
      continue;
    }

    if (menu == help_menu_) {
      top->setEnabled(true);
      for (QAction* action : menu->actions()) {
        action->setEnabled(action == about_action_);
      }
      continue;
    }

    top->setEnabled(false);
  }
}

void MainWindow::onRequiredRomMissing() {
  if (!window_shown_) {
    window_shown_ = true;
    show();
    applySavedWindowPosition();
  }
  statusBar()->show();
  statusBar()->showMessage(
      tr("Required ROM not found (pc88.rom or n88.rom). Place ROM files in the "
         "roms directory and restart."),
      0);
  applyRomMissingUiState();
}

void MainWindow::toggleRecordSound() {
  if (!controller_) {
    return;
  }
  QMetaObject::invokeMethod(controller_, "toggleRecordSound", Qt::QueuedConnection);
}

void MainWindow::captureScreen() {
  if (!controller_) {
    return;
  }

  PC8801::Config config {};
  QMetaObject::invokeMethod(controller_, "exportConfig", Qt::BlockingQueuedConnection,
                            Q_RETURN_ARG(PC8801::Config, config));

  QString path;
  if ((config.flag2 & PC8801::Config::genscrnshotname) == 0) {
    path = QFileDialog::getSaveFileName(
        this, tr("Save captured image"),
        QString::fromUtf8(M88GetCaptureDir()),
        tr("Bitmap image [4bpp] (*.bmp);;All files (*)"));
    if (path.isEmpty()) {
      return;
    }
    if (!path.endsWith(QStringLiteral(".bmp"), Qt::CaseInsensitive)) {
      path += QStringLiteral(".bmp");
    }
  }

  QMetaObject::invokeMethod(controller_, "captureScreen", Qt::QueuedConnection,
                            Q_ARG(QString, path));
}

void MainWindow::syncRememberPrefsFromConfig(const PC8801::Config& config) {
  save_position_ = (config.flag2 & PC8801::Config::saveposition) != 0;
  winpos_x_ = config.winposx;
  winpos_y_ = config.winposy;
}

void MainWindow::applySavedWindowPosition() {
  if (!save_position_ || fullscreen_) {
    return;
  }
  move(winpos_x_, winpos_y_);
}

void MainWindow::saveWindowPositionOnExit() {
  if (fullscreen_ || !controller_ || !save_position_ || emu_stopped_) {
    return;
  }
  const QPoint pos = frameGeometry().topLeft();
  if (QThread* ct = controller_->thread(); ct && ct != QThread::currentThread()) {
    QMetaObject::invokeMethod(controller_, "setWindowPosition", Qt::BlockingQueuedConnection,
                              Q_ARG(int, pos.x()), Q_ARG(int, pos.y()));
  } else if (controller_) {
    controller_->setWindowPosition(pos.x(), pos.y());
  }
}

MainWindow::MainWindow(const EmulatorController::Options& options, int scale,
                       const MainWindowStartup& startup, QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("M88"));
  if (!M88QtAppIcon().isNull()) {
    setWindowIcon(M88QtAppIcon());
  }
  view_scale_ = std::max(1, scale);
  save_position_ = startup.save_position;
  winpos_x_ = startup.winpos_x;
  winpos_y_ = startup.winpos_y;

  // Draw::Init is called from PC88::Init on the emulator thread (do not Init here).
  draw_ = new SharedFramebufferDraw();

  auto* central = new QWidget(this);
  central->setAutoFillBackground(true);
  central->setFocusPolicy(Qt::NoFocus);
  QPalette central_pal = central->palette();
  central_pal.setColor(QPalette::Window, Qt::black);
  central->setPalette(central_pal);
  auto* layout = new QVBoxLayout(central);
  layout->setContentsMargins(0, 0, 0, 0);

  view_ = new EmuView(central);
  view_->setMouseTracking(true);
  central->setMouseTracking(true);
  view_->attachFramebuffer(draw_);
  view_->setScale(view_scale_);
  view_->setHostInput(&host_input_);
  view_->installEventFilter(this);
  layout->addWidget(view_, 1);
  setCentralWidget(central);

  setupMenuBar();
  ensureMenuBarDocked();
  connectFullscreenMenuHooks();
  applyViewScale(view_scale_);
  applySavedWindowPosition();

  // Menu bar / status bar follow the desktop theme; only the emu view stays black.
  const QPalette app_pal = QApplication::palette();
  if (QMenuBar* mb = menuBar()) {
    mb->setAutoFillBackground(false);
    mb->setPalette(app_pal);
  }
  if (QStatusBar* sb = statusBar()) {
    sb->setAutoFillBackground(false);
    sb->setPalette(app_pal);
    sb->setFocusPolicy(Qt::NoFocus);
    sb->hide();

    fdc_text_label_ = new QLabel(sb);
    fdc_text_label_->setFocusPolicy(Qt::NoFocus);
    fdc_text_label_->setContentsMargins(0, 0, 8, 0);
    fdc_text_label_->hide();
    sb->addPermanentWidget(fdc_text_label_, 0);

    fdc_lamp_panel_ = new QWidget(sb);
    auto* lamp_layout = new QHBoxLayout(fdc_lamp_panel_);
    lamp_layout->setContentsMargins(0, 0, 8, 0);
    lamp_layout->setSpacing(4);
    static const char* kLampCaptions[] = {"1:", "2:", "S:"};
    for (int i = 0; i < 3; ++i) {
      auto* caption = new QLabel(tr(kLampCaptions[i]), fdc_lamp_panel_);
      fdc_lamp_labels_[i] = new QLabel(LampGlyph(0), fdc_lamp_panel_);
      lamp_layout->addWidget(caption);
      lamp_layout->addWidget(fdc_lamp_labels_[i]);
    }
    fdc_lamp_panel_->setLayout(lamp_layout);
    fdc_lamp_panel_->hide();
    sb->addPermanentWidget(fdc_lamp_panel_, 0);
  }

  EmulatorController::Options emu_options = options;
  emu_options.host_input = &host_input_;
  controller_ = new EmulatorController(draw_, emu_options);
  controller_->moveToThread(&emu_thread_);

  connect(&emu_thread_, &QThread::started, controller_, &EmulatorController::run);
  connect(controller_, &EmulatorController::finished, &emu_thread_, &QThread::quit);
  connect(&emu_thread_, &QThread::finished, controller_, &QObject::deleteLater);
  connect(controller_, &EmulatorController::frameReady, view_, &EmuView::refreshFrame,
          Qt::QueuedConnection);
  connect(controller_, &EmulatorController::frameReady, this, [this]() {
    if (rom_missing_) {
      return;
    }
    if (!window_shown_) {
      window_shown_ = true;
      show();
      applySavedWindowPosition();
      focusEmuView();
    }
  }, Qt::QueuedConnection);
  connect(view_, &EmuView::clearHostModifiers, controller_,
          &EmulatorController::clearHostModifiers, Qt::QueuedConnection);
  connect(view_, &EmuView::flushGuestKeys, controller_, &EmulatorController::flushGuestKeys,
          Qt::QueuedConnection);
  connect(view_, &EmuView::imeCommit, controller_, &EmulatorController::commitImeText,
          Qt::QueuedConnection);
  connect(controller_, &EmulatorController::requiredRomMissing, this,
          &MainWindow::onRequiredRomMissing);
  connect(controller_, &EmulatorController::failed, this, [this](const QString& msg) {
    if (rom_missing_) {
      return;
    }
    if (!window_shown_) {
      window_shown_ = true;
      show();
      applySavedWindowPosition();
    }
    if (!fullscreen_ && statusBar()) {
      statusBar()->showMessage(msg, 0);
    }
  });
  connect(controller_, &EmulatorController::started, this, [this]() {
    syncImeKanaInput();
    if (!fullscreen_ && statusBar()) {
      statusBar()->showMessage(tr("Emulator running"), 3000);
    }
  });
  connect(controller_, &EmulatorController::mouseCaptureChanged, view_,
          &EmuView::setMouseCapture);
  connect(controller_, &EmulatorController::machineConfigChanged, this,
          &MainWindow::updateControlMenu);
  connect(controller_, &EmulatorController::displayConfigChanged, this,
          &MainWindow::updateDisplayConfig);
  connect(controller_, &EmulatorController::statusUiChanged, this,
          &MainWindow::updateStatusUi);
  connect(controller_, &EmulatorController::titleStatsUpdated, this,
          &MainWindow::updateTitleStats);
  fullscreen_chrome_hide_timer_ = new QTimer(this);
  fullscreen_chrome_hide_timer_->setSingleShot(true);
  connect(fullscreen_chrome_hide_timer_, &QTimer::timeout, this,
          &MainWindow::hideFullscreenChrome);

  title_timer_ = new QTimer(this);
  title_timer_->setInterval(1000);
  connect(title_timer_, &QTimer::timeout, this, [this]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "sampleTitleStats",
                                Qt::QueuedConnection);
    }
  });
  title_timer_->start();
  connect(controller_, &EmulatorController::diskConfigurationChanged, this,
          &MainWindow::updateDiskMenu);
  connect(controller_, &EmulatorController::snapshotStateChanged, this,
          &MainWindow::updateSnapshotState);
  connect(controller_, &EmulatorController::recordSoundChanged, this,
          [this](bool recording) {
            if (record_sound_action_) {
              record_sound_action_->setChecked(recording);
            }
          });
  if (control_menu_) {
    connect(control_menu_, &QMenu::aboutToShow, this, [this]() {
      if (controller_) {
        QMetaObject::invokeMethod(controller_, "emitMachineConfig",
                                  Qt::QueuedConnection);
      }
    });
  }
  connect(controller_, &EmulatorController::statusMessage, this,
          [this](const QString& msg, int timeoutMs) {
            if (fullscreen_ || !statusBar()) {
              return;
            }
            statusBar()->showMessage(msg, timeoutMs);
          });

  // Keys go to WinKeyIF pending queue (thread-safe); applied on matrix In(), not frame order.
  connect(view_, &EmuView::keyDown, controller_, &EmulatorController::keyDown,
          Qt::QueuedConnection);
  connect(view_, &EmuView::keyUp, controller_, &EmulatorController::keyUp,
          Qt::QueuedConnection);

  if (QCoreApplication* app = QCoreApplication::instance()) {
    connect(app, &QCoreApplication::aboutToQuit, this, [this]() { stopEmulator(); });
  }

  if (QApplication* app = qApp) {
    app->installEventFilter(this);
  }

  emu_thread_.start();
}

MainWindow::~MainWindow() {
  M88WaylandIdleInhibitShutdown();
  showFullscreenCursor();
  if (QApplication* app = qApp) {
    app->removeEventFilter(this);
  }
  stopEmulator();
}

void MainWindow::syncWaylandIdleInhibit() {
  const bool want = M88WaylandIdleInhibitEnabled() && isVisible() && !isMinimized();
  M88WaylandIdleInhibitApply(want ? windowHandle() : nullptr, want);
}

void MainWindow::syncImeKanaInput() {
  LinuxIme::SetUserEnabled(M88ImeHalfKanaEnabled());
  LinuxIme::InitHost();
  if (view_) {
    view_->setImeInputEnabled(LinuxIme::Enabled());
  }
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (closing_) {
    event->accept();
    return;
  }
  if (!confirmExit()) {
    event->ignore();
    return;
  }
  closing_ = true;
  event->accept();
  saveWindowPositionOnExit();
  stopEmulator();
  QMainWindow::closeEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (!fullscreen_) {
    ensureMenuBarDocked();
  }
  focusEmuView();
  syncWaylandIdleInhibit();
}

void MainWindow::hideEvent(QHideEvent* event) {
  QMainWindow::hideEvent(event);
  syncWaylandIdleInhibit();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (fullscreen_ && event->type() == QEvent::MouseMove) {
    const QPoint global =
        static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
    if (isMouseInsideWindow(global) && isSignificantFullscreenMouseMove(global)) {
      noteFullscreenMouseActivity(global);
    }
  }

  if (watched == view_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (!key->isAutoRepeat() && key->modifiers().testFlag(Qt::AltModifier) &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
      toggleFullscreen();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange && !toggling_display_) {
    const bool fs = isFullScreen();
    if (fullscreen_action_) {
      fullscreen_action_->setChecked(fs);
    }
    if (fs != fullscreen_) {
      if (fs) {
        windowed_geometry_ = geometry();
        windowed_view_scale_ = view_scale_;
        fullscreen_ = true;
        applyFullscreenLayout();
      } else {
        fullscreen_ = false;
        applyWindowedLayout();
      }
    }
    syncWaylandIdleInhibit();
  }
  if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
    focusEmuView();
  }
}

void MainWindow::focusEmuView() {
  if (view_) {
    view_->setFocus(Qt::ActiveWindowFocusReason);
    // Do not grabKeyboard(): it breaks host IME (fcitx5/ibus) on Wayland/X11.
  }
}


