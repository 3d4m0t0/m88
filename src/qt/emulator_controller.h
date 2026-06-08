#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <mutex>

#include "pc88/config.h"

class SharedFramebufferDraw;

class EmulatorController : public QObject {
  Q_OBJECT

public:
  struct Options {
    QString rom_dir;
    QString config_file;
    QString disk0;
    int keyboard_type = -1;
    bool arrow_tenkey = false;
  };

  explicit EmulatorController(SharedFramebufferDraw* draw, Options options,
                            QObject* parent = nullptr);
  ~EmulatorController() override;

public slots:
  void requestStop();
  void run();
  void keyDown(quint32 vk, quint32 keydata);
  void keyUp(quint32 vk, quint32 keydata);
  void clearHostModifiers();
  void flushGuestKeys();
  void commitImeText(const QString& utf8);
  void mountDisk0(const QString& path);
  void ejectDisk0();
  void resetMachine();
  void setClock(int clock);
  void setBasicMode(int mode);
  void emitMachineConfig();

signals:
  void frameReady();
  void machineConfigChanged(int clock, int basicmode, bool n80_supported,
                            bool n80v2_supported, bool cd_supported);
  void failed(const QString& message);
  void statusMessage(const QString& message, int timeoutMs = 3000);
  void started();
  void finished();

private:
  bool initialize();
  void shutdown();
  void emulateFrame();
  void processDeferredActions();
  void applyConfigAndReset(const char* diag_tag = "reset", int prev_basicmode = -1);
  void applyUserResetAndRefresh();
  void refreshDisplayAfterDiskChange();
  void processImeCommit(const QString& utf8);
  void proceedFrame(int texec, uint clk, uint effclock);

  SharedFramebufferDraw* draw_ = nullptr;
  Options options_;
  std::atomic<bool> running_{true};
  std::atomic<bool> reset_requested_{false};

  struct Impl;
  Impl* impl_ = nullptr;
};
