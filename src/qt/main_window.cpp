#include "main_window.h"

#include "emu_view.h"
#include "../linux/shared_framebuffer_draw.h"

#include "../linux/display_scale.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShowEvent>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QEventLoop>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

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

void MainWindow::setupMenuBar() {
  auto* file_menu = menuBar()->addMenu(tr("ファイル(&F)"));
  auto* open_disk = file_menu->addAction(tr("ディスクを開く(&O)..."));
  open_disk->setShortcut(QKeySequence::Open);
  auto* eject_disk = file_menu->addAction(tr("ディスクを取り出す(&J)"));
  file_menu->addSeparator();
  auto* exit_action = file_menu->addAction(tr("終了(&X)"));
  exit_action->setShortcut(QKeySequence::Quit);

  auto* view_menu = menuBar()->addMenu(tr("表示(&V)"));
  auto* scale_group = new QActionGroup(this);
  scale_group->setExclusive(true);
  for (int scale : {1, 2, 3, 4}) {
    auto* action = view_menu->addAction(tr("倍率 %1×").arg(scale));
    action->setCheckable(true);
    action->setData(scale);
    scale_group->addAction(action);
    if (scale == view_scale_) {
      action->setChecked(true);
    }
  }

  auto* machine_menu = menuBar()->addMenu(tr("マシン(&M)"));
  auto* reset_action = machine_menu->addAction(tr("リセット(&R)"));
  reset_action->setShortcut(Qt::Key_F5);

  auto* help_menu = menuBar()->addMenu(tr("ヘルプ(&H)"));
  auto* about_action = help_menu->addAction(tr("M88 について(&A)"));

  connect(open_disk, &QAction::triggered, this, [this]() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("ディスクイメージを開く"), QString(),
        tr("ディスクイメージ (*.d88 *.hdm *.xdf *.dup *.2hd);;すべてのファイル (*)"));
    if (!path.isEmpty() && controller_) {
      QMetaObject::invokeMethod(controller_, "mountDisk0", Qt::QueuedConnection,
                                Q_ARG(QString, path));
    }
  });
  connect(eject_disk, &QAction::triggered, this, [this]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "ejectDisk0", Qt::QueuedConnection);
    }
  });
  connect(exit_action, &QAction::triggered, this, &QWidget::close);
  connect(scale_group, &QActionGroup::triggered, this, [this](QAction* action) {
    if (action) {
      applyViewScale(action->data().toInt());
    }
  });
  connect(reset_action, &QAction::triggered, this, [this]() {
    if (controller_) {
      QMetaObject::invokeMethod(controller_, "resetMachine", Qt::QueuedConnection);
    }
  });
  connect(about_action, &QAction::triggered, this, [this]() {
    QMessageBox::about(
        this, tr("M88 について"),
        tr("<h3>M88</h3>"
           "<p>PC-8801 エミュレータ（Linux Qt フロントエンド）</p>"
           "<p>キー入力は画面をクリックしてフォーカスを合わせてください。"
           "ディスクは「ファイル」メニューからマウントできます。</p>"));
  });
}

MainWindow::MainWindow(const EmulatorController::Options& options, int scale,
                       QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("M88"));
  view_scale_ = std::max(1, scale);

  // Avoid KDE/Qt theme window color flashing before the first emulator frame.
  setAutoFillBackground(true);
  QPalette win_pal = palette();
  win_pal.setColor(QPalette::Window, Qt::black);
  setPalette(win_pal);

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

  statusBar()->setFocusPolicy(Qt::NoFocus);

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


