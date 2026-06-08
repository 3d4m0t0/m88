#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
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
  void changeDiskImage(int drive, const QString& path);
  void changeBothDrives(const QString& path);
  void selectDisk(int drive, int index);
  void mountDisk0(const QString& path);
  void ejectDisk0();
  void resetMachine();
  void setClock(int clock);
  void setBasicMode(int mode);
  void setBurstMode(bool enabled);
  void setShowStatusBar(bool enabled);
  void setShowFdcStatus(bool enabled);
  void sampleTitleStats();
  PC8801::Config exportConfig();
  void importConfig(PC8801::Config config);
  void emitMachineConfig();
  void emitDiskConfiguration();

signals:
  void frameReady();
  void machineConfigChanged(int clock, int basicmode, bool n80_supported,
                            bool n80v2_supported, bool cd_supported,
                            bool burst_mode, bool show_statusbar,
                            bool show_fdc_status, bool ask_before_reset,
                            bool f12_as_reset, bool suppress_menu);
  void statusUiChanged(bool bar_enabled, bool show_fdc_lamps, int lamp0, int lamp1,
                       int lamp2, QString message, int message_ms);
  void diskConfigurationChanged(QString drive0Path, int drive0NumDisks,
                                int drive0Current, QStringList drive0Titles,
                                QString drive1Path, int drive1NumDisks,
                                int drive1Current, QStringList drive1Titles);
  void failed(const QString& message);
  void statusMessage(const QString& message, int timeoutMs = 3000);
  void titleStatsUpdated(int fps, int mhz_whole, int mhz_frac);
  void started();
  void finished();

private:
  enum class DiskOpType { None, ChangeImage, BothDrives, SelectDisk };

  bool initialize();
  void shutdown();
  void emulateFrame();
  void emulateBurstFrame();
  void resetBurstPacing();
  bool burstActive() const;
  void syncStatusBarFromConfig();
  void pollStatusUi();
  void saveConfig();
  void processDeferredActions();
  void applyChangeDiskImage(int drive, const QString& path);
  void applyBothDrives(const QString& path);
  void applySelectDisk(int drive, int index);
  void syncDrive1AfterDrive0Change();
  void applyConfigAndReset(const char* diag_tag = "reset", int prev_basicmode = -1);
  void applyUserResetAndRefresh();
  void refreshDisplayAfterDiskChange();
  void processImeCommit(const QString& utf8);
  void proceedFrame(int texec, uint clk, uint effclock);
  void emitDiskConfigurationLocked();
  void queueDiskOp(DiskOpType op, int drive, int index, const QString& path);

  SharedFramebufferDraw* draw_ = nullptr;
  Options options_;
  std::atomic<bool> running_{true};
  std::atomic<bool> reset_requested_{false};

  struct Impl;
  Impl* impl_ = nullptr;
};
