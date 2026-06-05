#include "main_window.h"

#include "emu_view.h"
#include "../linux/shared_framebuffer_draw.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QShowEvent>
#include <QStatusBar>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdio>

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
    controller_->requestStop();
  }
  if (view_) {
    disconnect(view_, nullptr, controller_, nullptr);
  }

  if (emu_thread_.isRunning()) {
    emu_thread_.quit();
    // Poll with processEvents so pending cross-thread slots can finish; signals are
    // already disconnected so wait() will not deadlock on BlockingQueuedConnection.
    constexpr int kMaxMs = 5000;
    int elapsed = 0;
    while (emu_thread_.isRunning() && elapsed < kMaxMs) {
      if (QCoreApplication* app = QCoreApplication::instance()) {
        app->processEvents(QEventLoop::AllEvents, 25);
      }
      constexpr int kSliceMs = 25;
      emu_thread_.wait(kSliceMs);
      elapsed += kSliceMs;
    }
    if (emu_thread_.isRunning()) {
      std::fprintf(stderr,
                   "M88: emulator thread did not stop in 5s, terminating\n");
      emu_thread_.terminate();
      emu_thread_.wait(1000);
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

MainWindow::MainWindow(const EmulatorController::Options& options, int scale,
                       QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("M88"));
  const int s = std::max(1, scale);
  resize(640 * s + 16, 400 * s + 48);

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
  view_->setScale(s);
  layout->addWidget(view_, 1);
  setCentralWidget(central);

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
          &EmulatorController::clearHostModifiers, Qt::BlockingQueuedConnection);
  connect(view_, &EmuView::flushGuestKeys, controller_, &EmulatorController::flushGuestKeys,
          Qt::BlockingQueuedConnection);
  connect(view_, &EmuView::imeCommit, controller_, &EmulatorController::commitImeText,
          Qt::BlockingQueuedConnection);
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

  // Keys go to WinKeyIF pending queue (thread-safe); applied on matrix In(), not frame order.
  connect(view_, &EmuView::keyDown, controller_, &EmulatorController::keyDown,
          Qt::DirectConnection);
  connect(view_, &EmuView::keyUp, controller_, &EmulatorController::keyUp,
          Qt::DirectConnection);

  if (QCoreApplication* app = QCoreApplication::instance()) {
    connect(app, &QCoreApplication::aboutToQuit, this, [this]() { stopEmulator(); });
  }

  emu_thread_.start();
}

MainWindow::~MainWindow() { stopEmulator(); }

void MainWindow::closeEvent(QCloseEvent* event) {
  stopEmulator();
  QMainWindow::closeEvent(event);
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


