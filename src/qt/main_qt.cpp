#include "emulator_controller.h"
#include "main_window.h"
#include "qt_platform.h"

#include "../linux/display_scale.h"
#include "../linux/linux_config.h"
#include "../linux/linux_paths.h"
#include "../linux/linux_startup_log.h"
#include "../linux_compat/path.h"
#include "../pc88/config.h"

#include <QApplication>
#include <QDir>
#include <QGuiApplication>
#include <QMetaType>
#include <QScreen>

Q_DECLARE_METATYPE(PC8801::Config)

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

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
  qRegisterMetaType<PC8801::Config>("PC8801::Config");

  PC8801::Config config;
  char ini_path[512];
  bool ini_created = false;
  M88LoadStartupConfig(
      &config,
      options.config_file.isEmpty() ? nullptr : options.config_file.toUtf8().constData(),
      ini_path, sizeof(ini_path), &ini_created);

  char canonical_ini[512];
  M88CanonicalConfigPath(ini_path, canonical_ini, sizeof(canonical_ini));
  options.resolved_ini_path = QString::fromUtf8(canonical_ini);

  M88LogConfigPath(canonical_ini, ini_created);
  M88LogDataPaths();

  M88ApplyStartupDirectory(&config, canonical_ini, !options.disk0.isEmpty());
  {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd))) {
      QDir::setCurrent(QString::fromLocal8Bit(cwd));
    }
  }
  M88LogWorkingDirectory();

  const bool wayland = M88QtIsWaylandSession();
  const bool save_pos_ini = (config.flag2 & PC8801::Config::saveposition) != 0;
  if (save_pos_ini && wayland) {
    std::fprintf(stderr,
                 "M88: WinPos: Wayland session — window position restore/save "
                 "disabled (WinPosX=%d WinPosY=%d in ini ignored)\n",
                 config.winposx, config.winposy);
  } else if (save_pos_ini) {
    std::fprintf(stderr, "M88: WinPos: applying WinPosX=%d WinPosY=%d from ini\n",
                 config.winposx, config.winposy);
  }

  M88InitRomPath(options.rom_dir.isEmpty() ? nullptr
                                            : options.rom_dir.toUtf8().constData());

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

  MainWindowStartup startup;
  startup.save_position = save_pos_ini && !wayland;
  startup.winpos_x = config.winposx;
  startup.winpos_y = config.winposy;

  MainWindow window(options, scale, startup);
  // MainWindow shows itself on the first frameReady (black background until then).
  return app.exec();
}
