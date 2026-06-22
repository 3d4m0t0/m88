#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>
#include <mutex>

#include "pc88/config.h"

class SharedFramebufferDraw;

namespace QtHostInput {
class Host;
}

class EmulatorController : public QObject {
  Q_OBJECT

public:
  struct Options {
    QString rom_dir;
    QString config_file;
    QString resolved_ini_path;
    QString disk0;
    int keyboard_type = -1;
    bool arrow_tenkey = false;
    QtHostInput::Host* host_input = nullptr;
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
  void setArrowTenkey(bool enabled);
  void setShowStatusBar(bool enabled);
  void setShowFdcStatus(bool enabled);
  void setWatchRegister(bool enabled);
  void sampleTitleStats();
  void pollExecStallWatchdog();
  void pollEmulationIdle();
  void emitFrameReady();
  PC8801::Config exportConfig();
  void importConfig(PC8801::Config config);
  void emitMachineConfig();
  void emitDiskConfiguration();
  void setFullscreenState(bool fullscreen, double refresh_hz);
  void setWindowPosition(int x, int y);
  void saveConfigNow();
  void captureScreen(const QString& save_path);
  void toggleRecordSound();
  bool isRecordingSound() const;
  void saveSnapshot(int slot = 0);
  void loadSnapshot(int slot = 0);

signals:
  void frameReady();
  void machineConfigChanged(int clock, int basicmode, bool n80_supported,
                            bool n80v2_supported, bool cd_supported,
                            bool burst_mode, bool arrow_tenkey, bool show_statusbar,
                            bool show_fdc_status, bool watch_register,
                            bool ask_before_reset, bool f12_as_reset, bool suppress_menu);
  void statusUiChanged(bool bar_enabled, bool show_fdc_lamps, int lamp0, int lamp1,
                       int lamp2, QString message, int message_ms, bool watch_register,
                       QString register_text);
  void diskConfigurationChanged(QString drive0Path, int drive0NumDisks,
                                int drive0Current, QStringList drive0Titles,
                                QString drive1Path, int drive1NumDisks,
                                int drive1Current, QStringList drive1Titles);
  void failed(const QString& message);
  void requiredRomMissing();
  void statusMessage(const QString& message, int timeoutMs = 3000);
  void titleStatsUpdated(int fps, int mhz_whole, int mhz_frac);
  void snapshotStateChanged(int current_slot);
  void recordSoundChanged(bool recording);
  void mouseCaptureChanged(bool enabled);
  void displayConfigChanged(bool force480, bool sync_to_vsync);
  void started();
  void finished();

private:
  enum class DiskOpType { None, ChangeImage, BothDrives, SelectDisk };

  bool initialize();
  void shutdown();
  void resetSequencerPacing();
  void updateSequencerAudio();
  bool burstActive() const;
  void syncStatusBarFromConfig();
  void pollStatusUi();
  void saveConfig();
  void processDeferredActions();
  void withVmPaused(const std::function<void()>& fn);
  void applyWinReset();
  void applyChangeDiskImage(int drive, const QString& path);
  void applyBothDrives(const QString& path);
  void applySelectDisk(int drive, int index);
  void syncDrive1AfterDrive0Change();
  void refreshDisplayAfterDiskChange();
  void processImeCommit(const QString& utf8);
  void proceedFrame(int texec, uint clk, uint effclock);
  void emitDiskConfigurationLocked();
  void syncHostInputFromConfig();
  void emitDisplayConfig();
  void queueDiskOp(DiskOpType op, int drive, int index, const QString& path);

  SharedFramebufferDraw* draw_ = nullptr;
  Options options_;
  std::atomic<bool> running_{true};

  struct Impl;
  Impl* impl_ = nullptr;
};
