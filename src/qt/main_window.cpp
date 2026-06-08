#include "main_window.h"

#include "config_dialog.h"
#include "emu_view.h"
#include "../linux/shared_framebuffer_draw.h"

#include "../linux/display_scale.h"
#include "../linux/linux_config.h"
#include "../pc88/config.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QShowEvent>
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

namespace {

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

}  // namespace

void MainWindow::stopEmulator() {
  if (emu_stopped_) {
    return;
  }
  emu_stopped_ = true;

  if (view_) {
    view_->releaseKeyboard();
    view_->attachFramebuffer(nullptr);
  }

  if (controller_) {
    disconnect(controller_, nullptr, view_, nullptr);
    disconnect(controller_, nullptr, this, nullptr);
    disconnect(&emu_thread_, nullptr, controller_, nullptr);
    disconnect(controller_, nullptr, &emu_thread_, nullptr);
    QMetaObject::invokeMethod(controller_, "requestStop", Qt::QueuedConnection);
  }
  if (view_) {
    disconnect(view_, nullptr, controller_, nullptr);
  }

  if (emu_thread_.isRunning()) {
    emu_thread_.quit();
    QEventLoop wait_loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&emu_thread_, &QThread::finished, &wait_loop, &QEventLoop::quit);
    connect(&timeout, &QTimer::timeout, &wait_loop, &QEventLoop::quit);
    timeout.start(5000);
    wait_loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (emu_thread_.isRunning()) {
      std::fprintf(stderr,
                   "M88: emulator thread did not stop in 5s, terminating\n");
      emu_thread_.terminate();
      emu_thread_.wait(1000);
    }
    if (emu_thread_.isRunning()) {
      std::fprintf(stderr, "M88: emulator thread still hung, forcing exit\n");
      std::quick_exit(0);
    }
  }

  if (QCoreApplication* app = QCoreApplication::instance()) {
    app->processEvents(QEventLoop::AllEvents);
  }

  // controller_ may already be null (deleteLater on emu_thread_.finished).
  controller_.clear();

  if (draw_) {
    draw_->Cleanup();
    delete draw_;
    draw_ = nullptr;
  }
}

void MainWindow::applyViewScale(int scale) {
  view_scale_ = std::max(1, scale);
  if (view_) {
    view_->setScale(view_scale_);
  }
  int chrome_h = kM88QtChromeH;
  if (menuBar()) {
    chrome_h += menuBar()->sizeHint().height();
  }
  resize(kM88EmuWidth * view_scale_ + kM88QtChromeW,
         kM88EmuHeight * view_scale_ + chrome_h);
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
  rebuildDriveMenu(0, drive0Path, drive0NumDisks, drive0Current, drive0Titles);
  rebuildDriveMenu(1, drive1Path, drive1NumDisks, drive1Current, drive1Titles);
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
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "importConfig", Qt::QueuedConnection,
                                Q_ARG(PC8801::Config, config));
    }
  });
  if (dlg.exec() != QDialog::Accepted) {
    return;
  }
  QMetaObject::invokeMethod(controller_, "importConfig", Qt::QueuedConnection,
                            Q_ARG(PC8801::Config, dlg.config()));
}

void MainWindow::openDiskImageDialog(int drive) {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open disk image"), QString(), tr(kDiskImageFilter));
  if (controller_) {
    QMetaObject::invokeMethod(controller_, "changeDiskImage", Qt::QueuedConnection,
                              Q_ARG(int, drive), Q_ARG(QString, path));
  }
}

void MainWindow::openBothDrivesDialog() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open disk image"), QString(), tr(kDiskImageFilter));
  if (controller_) {
    QMetaObject::invokeMethod(controller_, "changeBothDrives", Qt::QueuedConnection,
                              Q_ARG(QString, path));
  }
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
  return QMessageBox::question(
             this, tr("Reset"),
             tr("Reset the emulated machine?"),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

bool MainWindow::confirmExit() {
  if (!ask_before_reset_) {
    return true;
  }
  return QMessageBox::question(
             this, tr("Exit"),
             tr("Exit M88?"),
             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::updateControlMenu(int clock, int basicmode, bool n80_supported,
                                    bool n80v2_supported, bool cd_supported,
                                    bool burst_mode, bool show_statusbar,
                                    bool show_fdc_status, bool ask_before_reset,
                                    bool f12_as_reset, bool suppress_menu) {
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
  burst_action_ = control_menu_->addAction(tr("&Burst mode"));
  burst_action_->setCheckable(true);
  connect(burst_action_, &QAction::triggered, this, [this](bool checked) {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "setBurstMode", Qt::QueuedConnection,
                                Q_ARG(bool, checked));
    }
  });

  reset_action_ = control_menu_->addAction(tr("&Reset"));
  reset_action_->setShortcut(QKeySequence(Qt::Key_F12));

  control_menu_->addSeparator();
  auto* exit_action = control_menu_->addAction(tr("E&xit"));
  exit_action->setShortcut(QKeySequence::Quit);

  disk_menu_ = menuBar()->addMenu(tr("&Disk"));
  drive_actions_[0] = disk_menu_->addAction(tr("Drive &1..."));
  drive_actions_[1] = disk_menu_->addAction(tr("Drive &2..."));
  disk_menu_->addSeparator();
  change_both_action_ = disk_menu_->addAction(tr("&Change disk image..."));

  auto* open_disk_shortcut = new QShortcut(QKeySequence::Open, this);
  open_disk_shortcut->setContext(Qt::WindowShortcut);
  connect(open_disk_shortcut, &QShortcut::activated, this,
          [this]() { openDiskImageDialog(0); });

  connect(drive_actions_[0], &QAction::triggered, this,
          [this]() { openDiskImageDialog(0); });
  connect(drive_actions_[1], &QAction::triggered, this, [this]() {
    if (!drive_actions_[1]->menu()) {
      openDiskImageDialog(1);
    }
  });
  connect(change_both_action_, &QAction::triggered, this,
          &MainWindow::openBothDrivesDialog);

  auto* tape_menu = menuBar()->addMenu(tr("Ta&pe"));
  AddPlaceholder(tape_menu, tr("&Open..."));

  auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
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
  QAction* capture_action = AddPlaceholder(tools_menu, tr("&Capture..."));
  capture_action->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F2));
  AddPlaceholder(tools_menu, tr("&Record Sound"), true, false);
  tools_menu->addSeparator();
  QAction* save_snapshot = AddPlaceholder(tools_menu, tr("&Save Snapshot"));
  save_snapshot->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F10));
  QAction* load_snapshot = AddPlaceholder(tools_menu, tr("&Load Snapshot"));
  load_snapshot->setShortcut(QKeySequence(Qt::ALT | Qt::Key_F1));

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

  auto* help_menu = menuBar()->addMenu(tr("&Help"));
  auto* about_action = help_menu->addAction(tr("&About"));

  connect(exit_action, &QAction::triggered, this, &QWidget::close);
  connect(reset_action_, &QAction::triggered, this, [this]() {
    if (!confirmReset()) {
      return;
    }
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "resetMachine", Qt::QueuedConnection);
    }
  });
  connect(about_action, &QAction::triggered, this, [this]() {
    QMessageBox::about(
        this, tr("About M88"),
        tr("<h3>M88</h3>"
           "<p>PC-8801 emulator (Linux Qt frontend)</p>"
           "<p>Click the display to focus keyboard input. "
           "Mount a disk from Disk → Drive 1.</p>"));
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

MainWindow::MainWindow(const EmulatorController::Options& options, int scale,
                       QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("M88"));
  view_scale_ = std::max(1, scale);

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
  view_->attachFramebuffer(draw_);
  view_->setScale(view_scale_);
  layout->addWidget(view_, 1);
  setCentralWidget(central);

  setupMenuBar();
  applyViewScale(view_scale_);

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

  controller_ = new EmulatorController(draw_, options);
  controller_->moveToThread(&emu_thread_);

  connect(&emu_thread_, &QThread::started, controller_, &EmulatorController::run);
  connect(controller_, &EmulatorController::finished, &emu_thread_, &QThread::quit);
  connect(&emu_thread_, &QThread::finished, controller_, &QObject::deleteLater);
  connect(controller_, &EmulatorController::frameReady, view_, &EmuView::refreshFrame,
          Qt::QueuedConnection);
  connect(controller_, &EmulatorController::frameReady, this, [this]() {
    if (!window_shown_) {
      window_shown_ = true;
      show();
      focusEmuView();
    }
  }, Qt::QueuedConnection);
  connect(view_, &EmuView::clearHostModifiers, controller_,
          &EmulatorController::clearHostModifiers, Qt::QueuedConnection);
  connect(view_, &EmuView::flushGuestKeys, controller_, &EmulatorController::flushGuestKeys,
          Qt::QueuedConnection);
  connect(view_, &EmuView::imeCommit, controller_, &EmulatorController::commitImeText,
          Qt::QueuedConnection);
  connect(controller_, &EmulatorController::failed, this, [this](const QString& msg) {
    if (!window_shown_) {
      window_shown_ = true;
      show();
    }
    statusBar()->showMessage(msg, 0);
  });
  connect(controller_, &EmulatorController::started, this, [this]() {
    statusBar()->showMessage(tr("Emulator running"), 3000);
  });
  connect(controller_, &EmulatorController::machineConfigChanged, this,
          &MainWindow::updateControlMenu);
  connect(controller_, &EmulatorController::statusUiChanged, this,
          &MainWindow::updateStatusUi);
  connect(controller_, &EmulatorController::titleStatsUpdated, this,
          &MainWindow::updateTitleStats);
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

  emu_thread_.start();
}

MainWindow::~MainWindow() { stopEmulator(); }

void MainWindow::closeEvent(QCloseEvent* event) {
  if (!confirmExit()) {
    event->ignore();
    return;
  }
  stopEmulator();
  QMainWindow::closeEvent(event);
  if (QApplication* app = qApp) {
    app->quit();
  }
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  focusEmuView();
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
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


