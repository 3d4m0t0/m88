#include "emulator_controller.h"
#include "main_window.h"

#include "../linux/display_scale.h"
#include "../linux/linux_config.h"
#include "../pc88/config.h"

#include <QApplication>
#include <QGuiApplication>
#include <QScreen>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int ParseKeyboardType(const char* name) {
  return M88ParseKeyboardType(name);
}

int main(int argc, char** argv) {
  EmulatorController::Options options;
  int scale = 0;
  bool scale_explicit = false;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
      scale = std::atoi(argv[++i]);
      scale_explicit = true;
      continue;
    }
    if (std::strcmp(argv[i], "--rom-dir") == 0 && i + 1 < argc) {
      options.rom_dir = QString::fromLocal8Bit(argv[++i]);
      continue;
    }
    if (std::strcmp(argv[i], "-d0") == 0 && i + 1 < argc) {
      options.disk0 = QString::fromLocal8Bit(argv[++i]);
      continue;
    }
    if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      options.config_file = QString::fromLocal8Bit(argv[++i]);
      continue;
    }
    if (std::strcmp(argv[i], "--keyboard") == 0 && i + 1 < argc) {
      options.keyboard_type = ParseKeyboardType(argv[++i]);
      continue;
    }
    if (std::strcmp(argv[i], "--arrow-tenkey") == 0) {
      options.arrow_tenkey = true;
      continue;
    }
  }

  QApplication app(argc, argv);

  PC8801::Config config;
  char ini_path[512];
  bool ini_created = false;
  M88LoadStartupConfig(
      &config,
      options.config_file.isEmpty() ? nullptr : options.config_file.toUtf8().constData(),
      ini_path, sizeof(ini_path), &ini_created);

  int desktop_w = 0;
  int desktop_h = 0;
  if (QScreen* screen = QGuiApplication::primaryScreen()) {
    const QRect avail = screen->availableGeometry();
    desktop_w = avail.width();
    desktop_h = avail.height();
  }
  scale = M88ResolveScreenScale(desktop_w, desktop_h, kM88QtChromeW, kM88QtChromeH,
                                scale, scale_explicit);
  M88PrintScreenScale(scale, scale_explicit);

  MainWindow window(options, scale);
  // MainWindow shows itself on the first frameReady (black background until then).
  return app.exec();
}
