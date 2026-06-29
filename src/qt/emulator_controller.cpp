#include "emulator_controller.h"
#include "m88_i18n.h"

#include "../linux/linux_config.h"
#include "../linux/keyboard_vk.h"
#include "../linux/pc88_key_fixup.h"
#include "../linux/half_kana_ime.h"
#include "../linux/linux_ime.h"
#include "../linux/linux_emulation.h"
#include "../linux/linux_emu_thread.h"
#include "../linux/linux_emu_time_pace.h"
#include "../linux/linux_sequencer.h"
#include "../linux/m88_stall_watchdog.h"
#include "../linux/qt_miniaudio_sound.h"
#include "../linux/linux_paths.h"
#include "../linux/linux_startup_log.h"
#include "qt_host_input.h"
#include "qt_input.h"
#include "qt_video_log.h"
#include "../linux/shared_framebuffer_draw.h"
#include "shared_rgba_framebuffer.h"
#include "../win32/WinKeyIF.h"
#include "path.h"
#include "rom_log.h"
#include "screen_capture.h"

#include "file.h"

#include "status.h"

#include "error.h"
#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "pc88/beep.h"
#include "pc88/opnif.h"
#include "pc88/pc88.h"
#include "pc88/tapemgr.h"
#include "Z80c.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include <QThread>

#include <atomic>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <vector>

namespace {

#define EMU_TR(source) QCoreApplication::translate("EmulatorController", source)

bool RomFileExists(const char* filename) {
  return M88RomExists(filename);
}

bool DiskFileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

QString ResolveDiskPath(const QString& path) {
  const QString resolved = QFileInfo(path).canonicalFilePath();
  return resolved.isEmpty() ? path : resolved;
}

bool MountDriveImage(DiskManager& diskmgr, int drive, const QString& path,
                     int index, QString* error_out = nullptr) {
  if (path.isEmpty()) {
    return true;
  }
  const QString resolved = ResolveDiskPath(path);
  const QByteArray utf8 = resolved.toUtf8();
  if (!DiskFileExists(utf8.constData())) {
    if (error_out) {
      *error_out = EMU_TR("Disk not found: %1").arg(path);
    }
    return false;
  }
  if (!diskmgr.Mount(static_cast<uint>(drive), utf8.constData(), false, index,
                     false)) {
    diskmgr.Unmount(drive);
    if (error_out) {
      *error_out = EMU_TR("Failed to mount disk: %1").arg(path);
    }
    return false;
  }
  return true;
}

void OpenDiskImageStartup(DiskManager& diskmgr, const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  MountDriveImage(diskmgr, 0, path, 0);
  if (diskmgr.GetNumDisks(0) > 1) {
    MountDriveImage(diskmgr, 1, path, 1);
  } else {
    diskmgr.Unmount(1);
  }
}

}  // namespace

struct EmulatorController::Impl {
  struct CpuDumpSchedule {
    enum class Phase : uint8_t { Idle, PreAction, PostAction };
    enum class Action : uint8_t { None, Reset, SetClock, SetBasicMode };

    Phase phase = Phase::Idle;
    int frames_remaining = 0;
    Action action = Action::None;
    int pending_clock = 0;
    PC8801::Config::BASICMode pending_basic_mode = PC8801::Config::N88V1;
    bool logging_cpu1 = false;
    bool logging_cpu2 = false;
  };

  PC8801::Config config{};
  std::unique_ptr<DiskManager> diskmgr;
  std::unique_ptr<TapeManager> tapemgr;
  std::unique_ptr<PC88> pc88;
  std::unique_ptr<PC8801::QtMiniaudioSound> sound;
  std::unique_ptr<PC8801::WinKeyIF> keyif;
  M88Sequencer seq;
  M88EmuTimePacer emu_time_pacer;
  M88EmuThread emu_thread;
  bool hw_reset_done = false;
  std::atomic<int> post_reset_redraw_frames_{0};

  bool cpu_dump_arm_cpu1 = false;
  bool cpu_dump_arm_cpu2 = false;
  CpuDumpSchedule cpu_dump_schedule;
  uint cpu_dump_last_tick_frame = 0;

  QString drive_path[2];
  QString pending_ime_utf8;
  EmulatorController::DiskOpType pending_disk_op = EmulatorController::DiskOpType::None;
  int pending_disk_drive = 0;
  int pending_disk_index = 0;
  QString pending_disk_path;

  std::mutex pending_mutex;
  bool ime_commit_requested = false;

  int current_snapshot_slot = 0;

  QString ini_path;
  std::atomic<int> title_frame_count{0};
  std::atomic<long> title_exec_count{0};

  bool shutdown_done = false;
};

EmulatorController::EmulatorController(SharedFramebufferDraw* draw,
                                       SharedRgbaFramebuffer* rgba_framebuffer,
                                       Options options, QObject* parent)
    : QObject(parent),
      draw_(draw),
      rgba_framebuffer_(rgba_framebuffer),
      options_(std::move(options)) {
  impl_ = new Impl();
}

EmulatorController::~EmulatorController() {
  requestStop();
  shutdown();
  delete impl_;
  impl_ = nullptr;
}

void EmulatorController::requestStop() {
  running_ = false;
  if (impl_) {
    impl_->emu_thread.Stop();
  }
}

bool EmulatorController::initialize() {
  if (!draw_ || !impl_) {
    return false;
  }

  M88InitRomPath(options_.rom_dir.isEmpty() ? nullptr
                                             : options_.rom_dir.toUtf8().constData());

  if (!M88HasRequiredRoms()) {
    M88LogRequiredRomMissing(m88dir);
    emit requiredRomMissing();
    return false;
  }

  char ini_path[512] = {};
  bool ini_created = false;
  if (!options_.resolved_ini_path.isEmpty()) {
    char canonical[512];
    M88CanonicalConfigPath(options_.resolved_ini_path.toUtf8().constData(), canonical,
                           sizeof(canonical));
    impl_->ini_path = QString::fromUtf8(canonical);
    if (!M88LoadConfigAtPath(&impl_->config,
                             impl_->ini_path.toUtf8().constData())) {
      M88LoadStartupConfig(&impl_->config,
                           options_.config_file.isEmpty()
                               ? nullptr
                               : options_.config_file.toUtf8().constData(),
                           ini_path, sizeof(ini_path), &ini_created);
      M88CanonicalConfigPath(ini_path, canonical, sizeof(canonical));
      impl_->ini_path = QString::fromUtf8(canonical);
    }
  } else {
    M88LoadStartupConfig(&impl_->config, options_.config_file.isEmpty()
                                                ? nullptr
                                                : options_.config_file.toUtf8().constData(),
                         ini_path, sizeof(ini_path), &ini_created);
    char canonical[512];
    M88CanonicalConfigPath(ini_path, canonical, sizeof(canonical));
    impl_->ini_path = QString::fromUtf8(canonical);
  }
  M88ApplyEnvOverrides(&impl_->config);

  if (options_.keyboard_type >= 0) {
    impl_->config.keytype =
        static_cast<PC8801::Config::KeyType>(options_.keyboard_type);
    if (impl_->config.keytype == PC8801::Config::PC98) {
      impl_->config.keytype = PC8801::Config::AT106;
    }
    M88NoteKeyboardCliOverride();
  } else if (!M88IniHasHostKeyboard()) {
    M88ApplyDetectedKeyboard(&impl_->config);
  }
  M88LoadKeyFixup(impl_->ini_path.toUtf8().constData(), &impl_->config);
  if (options_.arrow_tenkey) {
    impl_->config.flags |= PC8801::Config::usearrowfor10;
  }

  impl_->diskmgr = std::make_unique<DiskManager>();
  if (!impl_->diskmgr->Init()) {
    emit failed(tr("Failed to initialize disk manager"));
    return false;
  }

  impl_->tapemgr = std::make_unique<TapeManager>();
  impl_->pc88 = std::make_unique<PC88>();
  if (!impl_->pc88->Init(draw_, impl_->diskmgr.get(), impl_->tapemgr.get())) {
    const char* detail = Error::GetErrorText();
    emit failed(detail && *detail
                    ? QString::fromUtf8(detail)
                    : tr("Failed to initialize PC-8801 core"));
    return false;
  }

  impl_->keyif = std::make_unique<PC8801::WinKeyIF>();
  if (!impl_->keyif->Init() || !impl_->pc88->ConnectKeyboard(impl_->keyif.get())) {
    emit failed(tr("Failed to initialize keyboard"));
    return false;
  }

  impl_->sound = std::make_unique<PC8801::QtMiniaudioSound>();
  if (!impl_->sound->Init(impl_->pc88.get(), 8000, 0)) {
    std::fprintf(stderr, "Warning: sound init failed\n");
  } else if (!impl_->pc88->GetOPN1()->Connect(impl_->sound.get()) ||
             !impl_->pc88->GetOPN2()->Connect(impl_->sound.get()) ||
             !impl_->pc88->GetBEEP()->Connect(impl_->sound.get())) {
    std::fprintf(stderr, "Warning: failed to connect sound\n");
  }

  if (options_.host_input) {
    options_.host_input->InitPad();
    impl_->pc88->ConnectPadInput(options_.host_input->pad());
    impl_->pc88->ConnectMouseUI(options_.host_input->mouse());
  }

  M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  impl_->keyif->ApplyConfig(&impl_->config);
  impl_->keyif->Activate(true);
  syncHostInputFromConfig();
  syncStatusBarFromConfig();
  LinuxIme::SetUserEnabled(M88ImeHalfKanaEnabled());
  LinuxIme::InitHost();
  M88LogQtVideoBackend();
  M88LogSound(&impl_->config);
  M88LogKeyboard(&impl_->config);
  M88LogKeyFix();
  if (LinuxIme::Enabled()) {
    M88LogImeHalfKana();
  }
  M88LogFdd(&impl_->config);
  M88LogMachine(&impl_->config);
  M88LogFrameTiming(impl_->pc88->GetFramePeriod(), impl_->config.speed);

  if (!options_.disk0.isEmpty()) {
    OpenDiskImageStartup(*impl_->diskmgr, options_.disk0);
    impl_->drive_path[0] = options_.disk0;
    if (impl_->diskmgr->GetNumDisks(0) > 1) {
      impl_->drive_path[1] = options_.disk0;
    }
  }

  M88SeqApplyConfig(impl_->seq, impl_->config);
  impl_->seq.ResetPacing();
  impl_->emu_time_pacer.Reset();
  updateSequencerAudio();
  M88PostStartupCpuReset(*impl_->pc88, &impl_->seq, impl_->config.refreshtiming);
  if (impl_->sound) {
    impl_->sound->ApplyConfig(&impl_->config);
    impl_->sound->ResetPcmContract();
  }
  impl_->hw_reset_done = true;
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->InvalidateUiStaging();
    draw_->StageUiFrame();
    if (rgba_framebuffer_) {
      rgba_framebuffer_->StageFromDraw(draw_);
    }
  }
  impl_->post_reset_redraw_frames_.store(60, std::memory_order_relaxed);
  emit frameReady();

  emitMachineConfig();
  emitDisplayConfig();
  emitDiskConfiguration();
  emit started();
  return true;
}

void EmulatorController::saveConfig() {
  if (!impl_ || impl_->ini_path.isEmpty()) {
    return;
  }
  M88SaveConfigFile(&impl_->config, impl_->ini_path.toUtf8().constData());
}

void EmulatorController::shutdown() {
  if (!impl_ || impl_->shutdown_done) {
    return;
  }
  impl_->shutdown_done = true;
  impl_->emu_thread.Stop();
  impl_->emu_thread.Join();
  saveConfig();
  draw_ = nullptr;
  if (impl_->sound) {
    impl_->sound->Cleanup();
    impl_->sound.reset();
  }
  if (impl_->keyif) {
    impl_->keyif->Activate(false);
  }
  if (impl_->diskmgr) {
    impl_->diskmgr->Unmount(0);
    impl_->diskmgr->Unmount(1);
  }
  // Tear down tape while PC88 (scheduler owner) is still alive; Close() clears
  // scheduler so ~TapeManager does not touch a destroyed PC88.
  if (impl_->tapemgr) {
    impl_->tapemgr->Close();
    impl_->tapemgr.reset();
  }
  impl_->pc88.reset();
  impl_->diskmgr.reset();
  impl_->keyif.reset();
}

void EmulatorController::proceedFrame(int texec, uint clk, uint effclock) {
  if (!impl_ || !impl_->pc88 || texec <= 0) {
    return;
  }
  const int ret =
      impl_->pc88->Proceed(static_cast<uint>(texec), clk, effclock);
  impl_->title_exec_count.fetch_add(static_cast<long>(clk) * ret,
                                    std::memory_order_relaxed);
}

void EmulatorController::processImeCommit(const QString& utf8) {
  if (!impl_ || !impl_->keyif || !impl_->pc88 || utf8.isEmpty()) {
    return;
  }
  const QByteArray bytes = utf8.toUtf8();
  withVmPaused([&]() {
    if (!LinuxIme::CommitUtf8(bytes.constData(), impl_->keyif.get(), &impl_->config)) {
      return;
    }
    const uint host_clock =
        static_cast<uint>(std::max(1, M88EffectiveClock(&impl_->config)));
    const uint effclock = static_cast<uint>(std::max<int64_t>(
        1, host_clock * (impl_->config.speed / 10) / 100));
    impl_->pc88->TimeSync();
    int guard = 0;
    while (HalfKanaIme::InjectBusy() && guard++ < 8192) {
      HalfKanaIme::InjectPump(impl_->keyif.get());
      const int period = std::max(1, impl_->pc88->GetFramePeriod());
      proceedFrame(period, host_clock, effclock);
    }
    HalfKanaIme::InjectEndSession(impl_->keyif.get(), &impl_->config);
  });
}

void EmulatorController::emitDiskConfigurationLocked() {
  if (!impl_ || !impl_->diskmgr) {
    return;
  }
  QStringList titles0;
  QStringList titles1;
  const int nd0 = static_cast<int>(impl_->diskmgr->GetNumDisks(0));
  const int nd1 = static_cast<int>(impl_->diskmgr->GetNumDisks(1));
  for (int i = 0; i < nd0 && i < 60; ++i) {
    const char* title = impl_->diskmgr->GetImageTitle(0, static_cast<uint>(i));
    titles0.push_back(title && *title ? QString::fromUtf8(title)
                                      : QStringLiteral("(untitled)"));
  }
  for (int i = 0; i < nd1 && i < 60; ++i) {
    const char* title = impl_->diskmgr->GetImageTitle(1, static_cast<uint>(i));
    titles1.push_back(title && *title ? QString::fromUtf8(title)
                                      : QStringLiteral("(untitled)"));
  }
  emit diskConfigurationChanged(
      impl_->drive_path[0], nd0, impl_->diskmgr->GetCurrentDisk(0), titles0,
      impl_->drive_path[1], nd1, impl_->diskmgr->GetCurrentDisk(1), titles1);
}

void EmulatorController::emitDiskConfiguration() {
  if (!impl_) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  emitDiskConfigurationLocked();
}

void EmulatorController::syncDrive1AfterDrive0Change() {
  if (!impl_ || !impl_->diskmgr) {
    return;
  }
  if (impl_->diskmgr->GetNumDisks(0) > 1) {
    if (impl_->drive_path[1].isEmpty()) {
      if (MountDriveImage(*impl_->diskmgr, 1, impl_->drive_path[0], 1)) {
        impl_->drive_path[1] = impl_->drive_path[0];
      }
    }
  } else {
    impl_->diskmgr->Unmount(1);
    impl_->drive_path[1].clear();
  }
}

void EmulatorController::applyChangeDiskImage(int drive, const QString& path) {
  if (!impl_ || !impl_->diskmgr || drive < 0 || drive > 1) {
    return;
  }

  impl_->diskmgr->Unmount(drive);

  if (path.isEmpty()) {
    impl_->drive_path[drive].clear();
    if (drive == 0) {
      options_.disk0.clear();
      syncDrive1AfterDrive0Change();
    }
    refreshDisplayAfterDiskChange();
    emit statusMessage(tr("Drive %1 ejected").arg(drive + 1), 2000);
    emitDiskConfigurationLocked();
    return;
  }

  QString error;
  if (!MountDriveImage(*impl_->diskmgr, drive, path, 0, &error)) {
    impl_->drive_path[drive].clear();
    emit statusMessage(error.isEmpty() ? tr("Failed to mount disk") : error, 5000);
    emitDiskConfigurationLocked();
    return;
  }

  impl_->drive_path[drive] = path;
  if (drive == 0) {
    options_.disk0 = path;
    syncDrive1AfterDrive0Change();
  }
  refreshDisplayAfterDiskChange();
  emit statusMessage(tr("Mounted on drive %1: %2")
                         .arg(drive + 1)
                         .arg(QFileInfo(path).fileName()));
  emitDiskConfigurationLocked();
}

void EmulatorController::applyBothDrives(const QString& path) {
  if (!impl_ || !impl_->diskmgr) {
    return;
  }
  impl_->diskmgr->Unmount(1);
  impl_->drive_path[1].clear();
  applyChangeDiskImage(0, path);
}

void EmulatorController::applySelectDisk(int drive, int index) {
  if (!impl_ || !impl_->diskmgr || drive < 0 || drive > 1) {
    return;
  }
  if (impl_->drive_path[drive].isEmpty()) {
    return;
  }

  int mount_index = index;
  if (index == 63) {
    mount_index = -1;
  }

  const QString resolved = ResolveDiskPath(impl_->drive_path[drive]);
  const QByteArray utf8 = resolved.toUtf8();
  if (!impl_->diskmgr->Mount(static_cast<uint>(drive), utf8.constData(), false,
                              mount_index, false)) {
    emit statusMessage(tr("Failed to select disk"), 5000);
    emitDiskConfigurationLocked();
    return;
  }

  refreshDisplayAfterDiskChange();
  emitDiskConfigurationLocked();
}

void EmulatorController::processDeferredActions() {
  if (!impl_) {
    return;
  }

  DiskOpType disk_op = DiskOpType::None;
  int disk_drive = 0;
  int disk_index = 0;
  QString disk_path;
  {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    disk_op = impl_->pending_disk_op;
    disk_drive = impl_->pending_disk_drive;
    disk_index = impl_->pending_disk_index;
    disk_path = impl_->pending_disk_path;
    impl_->pending_disk_op = DiskOpType::None;
    impl_->pending_disk_path.clear();
  }
  if (disk_op != DiskOpType::None) {
    if (!impl_->diskmgr) {
      emit statusMessage(tr("Emulator is not ready"));
    } else {
      switch (disk_op) {
        case DiskOpType::ChangeImage:
          applyChangeDiskImage(disk_drive, disk_path);
          break;
        case DiskOpType::BothDrives:
          applyBothDrives(disk_path);
          break;
        case DiskOpType::SelectDisk:
          applySelectDisk(disk_drive, disk_index);
          break;
        default:
          break;
      }
    }
  }

  QString ime_utf8;
  {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    if (impl_->ime_commit_requested) {
      impl_->ime_commit_requested = false;
      ime_utf8 = impl_->pending_ime_utf8;
      impl_->pending_ime_utf8.clear();
    }
  }
  if (!ime_utf8.isEmpty()) {
    processImeCommit(ime_utf8);
  }
}

bool EmulatorController::burstActive() const {
  return impl_ && (impl_->config.flags & PC8801::Config::cpuburst) != 0;
}

void EmulatorController::resetSequencerPacing() {
  if (!impl_) {
    return;
  }
  M88SeqApplyConfig(impl_->seq, impl_->config);
  impl_->seq.ResetPacing();
  impl_->emu_time_pacer.Reset();
  updateSequencerAudio();
}

void EmulatorController::updateSequencerAudio() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  // Both paths slice Proceed so the audio callback can interleave during a frame.
  impl_->seq.SetProceedSliceTicks(10);
}

void EmulatorController::withVmPaused(const std::function<void()>& fn) {
  if (!impl_) {
    return;
  }
  impl_->emu_thread.Pause();
  struct ResumeGuard {
    M88EmuThread* thread = nullptr;
    ~ResumeGuard() {
      if (thread) {
        thread->Resume();
      }
    }
  } guard{&impl_->emu_thread};
  if (fn) {
    fn();
  }
}

void EmulatorController::pollEmulationIdle() {
  tickCpuDumpSchedule();
  processDeferredActions();
  pollStatusUi();
}

void EmulatorController::emitFrameReady() { emit frameReady(); }

PC8801::Config EmulatorController::exportConfig() {
  if (!impl_) {
    return {};
  }
  return impl_->config;
}

void EmulatorController::importConfig(PC8801::Config config) {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  withVmPaused([&]() {
    impl_->config = config;
    if (impl_->keyif) {
      impl_->keyif->ApplyConfig(&impl_->config);
    }
    M88ApplyConfig(impl_->pc88.get(), &impl_->config);
    if (impl_->sound) {
      impl_->sound->ApplyConfig(&impl_->config);
      impl_->sound->ResetPcmContract();
      updateSequencerAudio();
    }
    resetSequencerPacing();
    impl_->seq.ForceDrawAfterReset(impl_->config.refreshtiming);
    if (draw_) {
      draw_->InvalidateUiStaging();
    }
    syncStatusBarFromConfig();
    saveConfig();
    LinuxIme::SetUserEnabled(M88ImeHalfKanaEnabled());
    LinuxIme::InitHost();
    syncHostInputFromConfig();
  });
  emitMachineConfig();
  emitDisplayConfig();
  pollStatusUi();
}

void EmulatorController::syncHostInputFromConfig() {
  if (!impl_) {
    return;
  }
  QtInput::SetHostAt101(impl_->config.keytype == PC8801::Config::AT101);
  M88Input::SetHostAt101(impl_->config.keytype == PC8801::Config::AT101);
  emit mouseCaptureChanged((impl_->config.flags & PC8801::Config::enablemouse) != 0);
}

void EmulatorController::setFullscreenState(bool /*fullscreen*/, double /*refresh_hz*/) {}

void EmulatorController::setWindowPosition(int x, int y) {
  if (!impl_) {
    return;
  }
  impl_->config.winposx = x;
  impl_->config.winposy = y;
}

void EmulatorController::saveConfigNow() { saveConfig(); }

namespace {

QString SnapshotBaseTitle(const QString& drive0_path) {
  if (!drive0_path.isEmpty()) {
    const QString base = QFileInfo(drive0_path).completeBaseName();
    if (!base.isEmpty()) {
      return base;
    }
  }
  return QStringLiteral("snapshot");
}

QString SnapshotFileName(const QString& drive0_path, int slot) {
  const QString name =
      QStringLiteral("%1_%2.s88").arg(SnapshotBaseTitle(drive0_path)).arg(slot);
  return QDir(QString::fromUtf8(M88GetSnapshotDir())).filePath(name);
}

QString CaptureDiskBaseName(const QString& drive0_path, const QString& drive1_path) {
  for (const QString& path : {drive0_path, drive1_path}) {
    if (!path.isEmpty()) {
      const QString base = QFileInfo(path).completeBaseName();
      if (!base.isEmpty()) {
        return base;
      }
    }
  }
  return QString();
}

QString WithDiskPrefix(const QString& disk_base, const QString& body) {
  if (disk_base.isEmpty()) {
    return body;
  }
  return QStringLiteral("%1_%2").arg(disk_base, body);
}

QString AutoCaptureBmpName(const QString& drive0_path, const QString& drive1_path) {
  const QDateTime now = QDateTime::currentDateTime();
  const QString stamp = QStringLiteral("%1%2%3%4%5")
                            .arg(now.date().day(), 2, 10, QChar('0'))
                            .arg(now.time().hour(), 2, 10, QChar('0'))
                            .arg(now.time().minute(), 2, 10, QChar('0'))
                            .arg(now.time().second(), 2, 10, QChar('0'))
                            .arg(now.time().msec() / 10, 2, 10, QChar('0'));
  return WithDiskPrefix(CaptureDiskBaseName(drive0_path, drive1_path),
                        stamp + QStringLiteral(".bmp"));
}

QString AutoRecordWavName(const QString& drive0_path, const QString& drive1_path) {
  const QDateTime now = QDateTime::currentDateTime();
  const QString stamp = QStringLiteral("%1%2%3%4")
                            .arg(now.date().day(), 2, 10, QChar('0'))
                            .arg(now.time().hour(), 2, 10, QChar('0'))
                            .arg(now.time().minute(), 2, 10, QChar('0'))
                            .arg(now.time().second(), 2, 10, QChar('0'));
  return WithDiskPrefix(CaptureDiskBaseName(drive0_path, drive1_path),
                        stamp + QStringLiteral(".wav"));
}

}  // namespace

void EmulatorController::captureScreen(const QString& save_path) {
  if (!draw_) {
    emit statusMessage(tr("Could not capture screen"), 3000);
    return;
  }

  const uint8* data = nullptr;
  int bpl = 0;
  uint width = 0;
  uint height = 0;
  bool palette_changed = false;
  Draw::Palette palette[256] = {};
  if (!draw_->AcquireUiFrame(&data, &bpl, &width, &height, &palette_changed, palette, 256) ||
      !data || width < 640 || height < 400) {
    emit statusMessage(tr("Could not capture screen"), 3000);
    return;
  }

  std::vector<uint8> bmp;
  if (M88ScreenCapture::BuildBmp4(data, bpl, palette, &bmp) == 0) {
    emit statusMessage(tr("Failed to create screen image"), 3000);
    return;
  }

  QString path = save_path;
  if (path.isEmpty()) {
    if (!impl_ || (impl_->config.flag2 & PC8801::Config::genscrnshotname) == 0) {
      emit statusMessage(tr("No save path specified"), 3000);
      return;
    }
    path = QDir(QString::fromUtf8(M88GetCaptureDir()))
               .filePath(AutoCaptureBmpName(impl_->drive_path[0], impl_->drive_path[1]));
  }

  const QByteArray utf8 = path.toUtf8();
  FileIO file;
  if (!file.Open(utf8.constData(), FileIO::create) ||
      file.Write(bmp.data(), static_cast<int32>(bmp.size())) !=
          static_cast<int32>(bmp.size())) {
    emit statusMessage(tr("Could not save screen image to %1").arg(path), 3000);
    return;
  }

  emit statusMessage(tr("Screen image saved to %1").arg(path), 3000);
}

void EmulatorController::toggleRecordSound() {
  if (!impl_ || !impl_->sound) {
    emit statusMessage(tr("Sound output is disabled"), 3000);
    return;
  }

  if (!impl_->sound->IsDumping()) {
    const QString path = QDir(QString::fromUtf8(M88GetCaptureDir()))
                             .filePath(AutoRecordWavName(impl_->drive_path[0],
                                                         impl_->drive_path[1]));
    const QByteArray utf8 = path.toUtf8();
    bool began = false;
    withVmPaused([&]() {
      began = impl_->sound->DumpBegin(utf8.constData());
    });
    if (!began) {
      emit statusMessage(tr("Could not record sound to %1").arg(path), 3000);
      return;
    }
    emit recordSoundChanged(true);
    emit statusMessage(tr("Sound recording started: %1").arg(path), 3000);
    return;
  }

  withVmPaused([&]() { impl_->sound->DumpEnd(); });
  emit recordSoundChanged(false);
  emit statusMessage(tr("Sound recording stopped"), 3000);
}

bool EmulatorController::isRecordingSound() const {
  return impl_ && impl_->sound && impl_->sound->IsDumping();
}

void EmulatorController::saveSnapshot(int slot) {
  if (!impl_ || !impl_->pc88) {
    emit statusMessage(tr("Emulator is not ready"), 3000);
    return;
  }
  if (slot < 0) {
    slot = impl_->current_snapshot_slot;
  } else if (slot > 9) {
    slot = 0;
  }

  const QString path = SnapshotFileName(impl_->drive_path[0], slot);
  const QByteArray utf8 = path.toUtf8();
  bool saved = false;
  withVmPaused([&]() {
    saved = impl_->pc88->SaveShapshot(utf8.constData(), &impl_->config);
  });
  if (saved) {
    impl_->current_snapshot_slot = slot;
    emit snapshotStateChanged(slot);
    emit statusMessage(tr("Saved to %1").arg(path), 3000);
  } else {
    emit statusMessage(tr("Could not save to %1").arg(path), 3000);
  }
}

void EmulatorController::loadSnapshot(int slot) {
  if (!impl_ || !impl_->pc88 || !impl_->diskmgr || !impl_->keyif) {
    emit statusMessage(tr("Emulator is not ready"), 3000);
    return;
  }
  if (slot < 0 || slot > 9) {
    slot = impl_->current_snapshot_slot;
  }

  const QString path = SnapshotFileName(impl_->drive_path[0], slot);
  const QByteArray utf8 = path.toUtf8();

  const char* diskname = nullptr;
  QByteArray disk_utf8;
  if (!impl_->drive_path[0].isEmpty() && impl_->diskmgr->GetNumDisks(0) >= 2) {
    syncDrive1AfterDrive0Change();
    disk_utf8 = ResolveDiskPath(impl_->drive_path[0]).toUtf8();
    diskname = disk_utf8.constData();
  }

  bool loaded = false;
  withVmPaused([&]() {
    loaded = impl_->pc88->LoadShapshot(utf8.constData(), &impl_->config, diskname);
    if (!loaded) {
      return;
    }
    impl_->current_snapshot_slot = slot;
    impl_->keyif->ApplyConfig(&impl_->config);
    if (impl_->sound) {
      impl_->sound->ApplyConfig(&impl_->config);
      impl_->sound->ResetPcmContract();
      updateSequencerAudio();
    }
    syncHostInputFromConfig();
    resetSequencerPacing();
    impl_->pc88->UpdateScreen(true);
    if (draw_) {
      draw_->InvalidateUiStaging();
      draw_->StageUiFrame();
    }
    impl_->post_reset_redraw_frames_.store(60, std::memory_order_relaxed);
  });
  if (!loaded) {
    emit statusMessage(tr("Could not restore from %1").arg(path), 3000);
    return;
  }
  emit frameReady();
  emitMachineConfig();
  emitDiskConfiguration();
  emit snapshotStateChanged(slot);
  emit statusMessage(tr("Restored from %1").arg(path), 3000);
}

void EmulatorController::emitDisplayConfig() {
  if (!impl_) {
    return;
  }
  emit displayConfigChanged((impl_->config.flags & PC8801::Config::force480) != 0,
                            (impl_->config.flag2 & PC8801::Config::synctovsync) != 0);
}

void EmulatorController::sampleTitleStats() {
  if (!impl_) {
    return;
  }
  const int frames = impl_->title_frame_count.exchange(0, std::memory_order_relaxed);
  const long exec = impl_->title_exec_count.exchange(0, std::memory_order_relaxed);
  const long freq = exec / 10000;
  emit titleStatsUpdated(frames, static_cast<int>(freq / 100),
                         static_cast<int>(freq % 100));
}

void EmulatorController::pollExecStallWatchdog() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  M88StallWatchdogEmuState emu {};
  impl_->emu_thread.FillWatchdogState(&emu);
  M88StallWatchdogPoll(running_.load(std::memory_order_relaxed), emu,
                       impl_->pc88.get());
}

void EmulatorController::syncStatusBarFromConfig() {
  if (!impl_) {
    return;
  }
  if (impl_->config.flags & PC8801::Config::showstatusbar) {
    statusdisplay.Enable((impl_->config.flags & PC8801::Config::showfdcstatus) != 0);
  } else {
    statusdisplay.Disable();
  }
}

void EmulatorController::pollStatusUi() {
  StatusUiSnapshot snap {};
  if (!statusdisplay.PollUiSnapshot(&snap)) {
    return;
  }
  emit statusUiChanged(snap.bar_enabled, snap.show_fdc_lamps, snap.lamp_level[0],
                       snap.lamp_level[1], snap.lamp_level[2],
                       M88TranslateStatusMessage(QString::fromUtf8(snap.message)),
                       snap.message_duration_ms, snap.watch_register,
                       QString::fromUtf8(snap.register_line));
}

void EmulatorController::run() {
  if (!initialize()) {
    emit finished();
    if (QThread* t = thread()) {
      t->quit();
    }
    return;
  }

  impl_->hw_reset_done = false;

  M88StallWatchdogInit();

  M88EmuThread::Params params;
  params.vm = impl_->pc88.get();
  params.seq = &impl_->seq;
  params.config = &impl_->config;
  params.draw = draw_;
  params.rgba_fb = rgba_framebuffer_;
  params.keyif = impl_->keyif.get();
  params.post_reset_frames = &impl_->post_reset_redraw_frames_;
  params.title_frame_count = &impl_->title_frame_count;
  params.title_exec_count = &impl_->title_exec_count;
  params.running = &running_;
  params.emu_pacer = &impl_->emu_time_pacer;
  params.audio_hint = [this]() {
    M88EmuTimePacer::AudioHint hint;
    if (impl_->sound && impl_->sound->GetSpscCapacity() > 0) {
      hint.ring_avail = impl_->sound->GetSpscAvail();
      hint.ring_size = impl_->sound->GetSpscCapacity();
      hint.sample_rate_hz =
          static_cast<int>(impl_->sound->GetOutputSampleRate());
      hint.min_ring_frames = impl_->sound->MinPlaybackHeadroom();
    }
    return hint;
  };
  params.mix_audio_slice = [this](int emu_ticks) {
    if (impl_->sound) {
      impl_->sound->MixSlice(emu_ticks);
    }
  };
  params.drain_audio = [this]() {
    if (impl_->sound) {
      impl_->sound->CatchUpContract();
    }
  };
  params.prepare_audio_sleep = [this](int emu_sleep_ticks) {
    if (impl_->sound) {
      impl_->sound->PrepareSleep(emu_sleep_ticks);
    }
  };
  params.on_frame = [this](bool drew_screen) {
    if (drew_screen) {
      QMetaObject::invokeMethod(this, "emitFrameReady", Qt::QueuedConnection);
    }
    QMetaObject::invokeMethod(this, "pollEmulationIdle", Qt::QueuedConnection);
  };
  params.emu_realtime_priority = false;

  impl_->emu_thread.Start(std::move(params));

  // Run the controller thread event loop so QueuedConnection slots (reset, disk,
  // sampleTitleStats, etc.) are dispatched while the emu thread runs separately.
  QEventLoop loop;
  QTimer stop_poll;
  stop_poll.setInterval(50);
  QObject::connect(&stop_poll, &QTimer::timeout, this, [&]() {
    if (!running_.load(std::memory_order_relaxed)) {
      impl_->emu_thread.Stop();
      loop.quit();
    }
  });
  stop_poll.start();
  loop.exec();
  stop_poll.stop();

  M88StallWatchdogShutdown();
  impl_->emu_thread.Join();

  shutdown();
  emit finished();
  if (QThread* t = thread()) {
    t->quit();
  }
}

void EmulatorController::keyDown(quint32 vk, quint32 keydata) {
  if (impl_ && impl_->keyif && vk) {
    impl_->keyif->KeyDown(static_cast<uint>(vk), keydata);
  }
}

void EmulatorController::keyUp(quint32 vk, quint32 keydata) {
  if (impl_ && impl_->keyif && vk) {
    impl_->keyif->KeyUp(static_cast<uint>(vk), keydata);
  }
}

void EmulatorController::clearHostModifiers() {
  if (impl_ && impl_->keyif) {
    impl_->keyif->ClearHostModifiers();
  }
}

void EmulatorController::flushGuestKeys() {
  if (impl_ && impl_->keyif) {
    impl_->keyif->FlushGuestKeys();
  }
}

void EmulatorController::commitImeText(const QString& utf8) {
  if (!impl_ || utf8.isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->pending_ime_utf8 = utf8;
  impl_->ime_commit_requested = true;
}

void EmulatorController::refreshDisplayAfterDiskChange() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  impl_->seq.ForceDrawAfterReset(impl_->config.refreshtiming);
  impl_->post_reset_redraw_frames_.store(30, std::memory_order_relaxed);
  if (draw_) {
    draw_->InvalidateUiStaging();
  }
}

void EmulatorController::emitMachineConfig() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  emit machineConfigChanged(
      impl_->config.clock, static_cast<int>(impl_->config.basicmode),
      impl_->pc88->IsN80Supported(), impl_->pc88->IsN80V2Supported(),
      impl_->pc88->IsCDSupported(),
      (impl_->config.flags & PC8801::Config::cpuburst) != 0,
      (impl_->config.flags & PC8801::Config::usearrowfor10) != 0,
      (impl_->config.flags & PC8801::Config::showstatusbar) != 0,
      (impl_->config.flags & PC8801::Config::showfdcstatus) != 0,
      (impl_->config.flags & PC8801::Config::watchregister) != 0,
      (impl_->config.flags & PC8801::Config::askbeforereset) != 0,
      (impl_->config.flags & PC8801::Config::disablef12reset) == 0,
      (impl_->config.flags & PC8801::Config::suppressmenu) != 0);
  syncStatusBarFromConfig();
  pollStatusUi();
}

void EmulatorController::setShowStatusBar(bool enabled) {
  if (!impl_) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::showstatusbar) != 0;
  if (was == enabled) {
    return;
  }
  if (enabled) {
    impl_->config.flags |= PC8801::Config::showstatusbar;
  } else {
    impl_->config.flags &= ~PC8801::Config::showstatusbar;
  }
  syncStatusBarFromConfig();
  saveConfig();
  emitMachineConfig();
}

void EmulatorController::setShowFdcStatus(bool enabled) {
  if (!impl_) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::showfdcstatus) != 0;
  if (was == enabled) {
    return;
  }
  if (enabled) {
    impl_->config.flags |= PC8801::Config::showfdcstatus;
  } else {
    impl_->config.flags &= ~PC8801::Config::showfdcstatus;
  }
  if (impl_->pc88) {
    M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  }
  syncStatusBarFromConfig();
  saveConfig();
  emitMachineConfig();
}

namespace {

constexpr int kCpuDumpPreFrames = 1;
constexpr int kCpuDumpPostFrames = 16;

QString CpuDumpFilename(int cpu_index) {
  return QStringLiteral("CPU%1.dmp").arg(cpu_index + 1);
}

void SetCpuDumpEnabled(PC88* pc88, M88EmuThread& emu_thread, int cpu_index, bool enabled,
                       bool wait_for_host_reset = false) {
  if (!pc88 || (cpu_index != 0 && cpu_index != 1)) {
    return;
  }
  Z80C* cpu = cpu_index == 0 ? pc88->GetCPU1() : pc88->GetCPU2();
  emu_thread.Pause();
  cpu->EnableDump(enabled);
  if (enabled && wait_for_host_reset) {
    cpu->SetDumpWaitForHostReset(true);
  }
  emu_thread.Resume();
}

void NotifyCpuDumpHostReset(PC88* pc88, M88EmuThread& emu_thread, int cpu_index) {
  if (!pc88 || (cpu_index != 0 && cpu_index != 1)) {
    return;
  }
  Z80C* cpu = cpu_index == 0 ? pc88->GetCPU1() : pc88->GetCPU2();
  emu_thread.Pause();
  if (cpu->GetDumpState()) {
    cpu->NotifyHostReset();
  }
  emu_thread.Resume();
}

}  // namespace

bool EmulatorController::cpuDumpArmed() const {
  return impl_ && (impl_->cpu_dump_arm_cpu1 || impl_->cpu_dump_arm_cpu2);
}

void EmulatorController::stopCpuDumpCapture() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  auto& s = impl_->cpu_dump_schedule;
  if (s.logging_cpu1) {
    SetCpuDumpEnabled(impl_->pc88.get(), impl_->emu_thread, 0, false);
  }
  if (s.logging_cpu2) {
    SetCpuDumpEnabled(impl_->pc88.get(), impl_->emu_thread, 1, false);
  }
  s = {};
}

void EmulatorController::finishCpuDumpCapture() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  auto& s = impl_->cpu_dump_schedule;
  if (s.phase == Impl::CpuDumpSchedule::Phase::Idle) {
    return;
  }

  const bool had1 = s.logging_cpu1;
  const bool had2 = s.logging_cpu2;
  uint8 reason1 = 0;
  uint8 reason2 = 0;
  if (had1) {
    reason1 = static_cast<Z80C*>(impl_->pc88->GetCPU1())->GetDumpAutoStopReason();
  }
  if (had2) {
    reason2 = static_cast<Z80C*>(impl_->pc88->GetCPU2())->GetDumpAutoStopReason();
  }

  stopCpuDumpCapture();

  QStringList files;
  QStringList notes;
  if (had1) {
    files.push_back(CpuDumpFilename(0));
    if (reason1 == 1) {
      notes.push_back(tr("CPU1 loop"));
    } else if (reason1 == 2) {
      notes.push_back(tr("CPU1 reset window"));
    }
  }
  if (had2) {
    files.push_back(CpuDumpFilename(1));
    if (reason2 == 1) {
      notes.push_back(tr("CPU2 loop"));
    } else if (reason2 == 2) {
      notes.push_back(tr("CPU2 reset window"));
    }
  }

  QString msg = tr("CPU log capture finished (%1)").arg(files.join(QStringLiteral(", ")));
  if (!notes.isEmpty()) {
    msg += tr(" [%1]").arg(notes.join(QStringLiteral(", ")));
  }
  emit statusMessage(msg, 4000);
}

void EmulatorController::pollCpuDumpAutoStop() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  const auto& s = impl_->cpu_dump_schedule;
  if (s.phase != Impl::CpuDumpSchedule::Phase::PostAction) {
    return;
  }
  bool logging = false;
  if (s.logging_cpu1) {
    auto* cpu = static_cast<Z80C*>(impl_->pc88->GetCPU1());
    if (cpu->GetDumpState()) {
      logging = true;
    }
  }
  if (s.logging_cpu2) {
    auto* cpu = static_cast<Z80C*>(impl_->pc88->GetCPU2());
    if (cpu->GetDumpState()) {
      logging = true;
    }
  }
  if (!logging) {
    finishCpuDumpCapture();
  }
}

void EmulatorController::startCpuDumpCapture(CpuDumpTrigger action) {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  if (impl_->cpu_dump_schedule.phase != Impl::CpuDumpSchedule::Phase::Idle) {
    stopCpuDumpCapture();
  }
  auto& s = impl_->cpu_dump_schedule;
  switch (action) {
    case CpuDumpTrigger::Reset:
      s.action = Impl::CpuDumpSchedule::Action::Reset;
      break;
    case CpuDumpTrigger::SetClock:
      s.action = Impl::CpuDumpSchedule::Action::SetClock;
      break;
    case CpuDumpTrigger::SetBasicMode:
      s.action = Impl::CpuDumpSchedule::Action::SetBasicMode;
      break;
  }
  s.logging_cpu1 = impl_->cpu_dump_arm_cpu1;
  s.logging_cpu2 = impl_->cpu_dump_arm_cpu2;
  s.phase = Impl::CpuDumpSchedule::Phase::PreAction;
  s.frames_remaining = kCpuDumpPreFrames;
  impl_->cpu_dump_last_tick_frame =
      static_cast<uint>(impl_->title_frame_count.load(std::memory_order_relaxed));

  QStringList files;
  if (s.logging_cpu1) {
    files.push_back(CpuDumpFilename(0));
  }
  if (s.logging_cpu2) {
    files.push_back(CpuDumpFilename(1));
  }
  emit statusMessage(tr("CPU log capture started → %1").arg(files.join(QStringLiteral(", "))),
                     4000);
}

void EmulatorController::executeCpuDumpScheduledAction() {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    return;
  }
  auto& s = impl_->cpu_dump_schedule;
  withVmPaused([&]() {
    if (s.logging_cpu1) {
      auto* cpu = static_cast<Z80C*>(impl_->pc88->GetCPU1());
      cpu->EnableDump(true);
      cpu->SetDumpWaitForHostReset(true);
    }
    if (s.logging_cpu2) {
      auto* cpu = static_cast<Z80C*>(impl_->pc88->GetCPU2());
      cpu->EnableDump(true);
      cpu->SetDumpWaitForHostReset(true);
    }
    PC8801::Config hw = M88ConfigForHardware(impl_->config);
    switch (s.action) {
      case Impl::CpuDumpSchedule::Action::Reset:
        break;
      case Impl::CpuDumpSchedule::Action::SetClock:
        impl_->config.clock = s.pending_clock;
        hw = M88ConfigForHardware(impl_->config);
        break;
      case Impl::CpuDumpSchedule::Action::SetBasicMode:
        impl_->config.basicmode = s.pending_basic_mode;
        hw = M88ConfigForHardware(impl_->config);
        break;
      default:
        break;
    }
    M88ApplyWinReset(
        *impl_->keyif, *impl_->pc88, impl_->seq, &hw, draw_,
        [&](PC8801::Config* cfg) {
          if (impl_->sound) {
            impl_->sound->ApplyConfig(cfg);
          }
        });
    impl_->seq.ForceDrawAfterReset(impl_->config.refreshtiming);
    impl_->pc88->UpdateScreen(true);
    if (draw_) {
      draw_->InvalidateUiStaging();
      draw_->StageUiFrame();
    }
    impl_->post_reset_redraw_frames_.store(60, std::memory_order_relaxed);
    if (s.logging_cpu1) {
      static_cast<Z80C*>(impl_->pc88->GetCPU1())->NotifyHostReset();
    }
    if (s.logging_cpu2) {
      static_cast<Z80C*>(impl_->pc88->GetCPU2())->NotifyHostReset();
    }
  });
  if (s.action == Impl::CpuDumpSchedule::Action::SetClock ||
      s.action == Impl::CpuDumpSchedule::Action::SetBasicMode) {
    emitMachineConfig();
  }
  emit frameReady();
}

void EmulatorController::tickCpuDumpSchedule() {
  if (!impl_) {
    return;
  }
  auto& s = impl_->cpu_dump_schedule;
  if (s.phase == Impl::CpuDumpSchedule::Phase::Idle) {
    return;
  }
  pollCpuDumpAutoStop();
  if (s.phase == Impl::CpuDumpSchedule::Phase::Idle) {
    return;
  }
  const uint frame =
      static_cast<uint>(impl_->title_frame_count.load(std::memory_order_relaxed));
  if (frame == impl_->cpu_dump_last_tick_frame) {
    return;
  }
  impl_->cpu_dump_last_tick_frame = frame;
  if (--s.frames_remaining > 0) {
    pollCpuDumpAutoStop();
    return;
  }
  switch (s.phase) {
    case Impl::CpuDumpSchedule::Phase::PreAction:
      executeCpuDumpScheduledAction();
      s.phase = Impl::CpuDumpSchedule::Phase::PostAction;
      s.frames_remaining = kCpuDumpPostFrames;
      pollCpuDumpAutoStop();
      break;
    case Impl::CpuDumpSchedule::Phase::PostAction:
      finishCpuDumpCapture();
      break;
    default:
      stopCpuDumpCapture();
      break;
  }
}

void EmulatorController::setCpuDumpLog(int cpu_index, bool enabled) {
  if (!impl_ || (cpu_index != 0 && cpu_index != 1)) {
    return;
  }
  if (cpu_index == 0) {
    impl_->cpu_dump_arm_cpu1 = enabled;
  } else {
    impl_->cpu_dump_arm_cpu2 = enabled;
  }
  const QString filename = CpuDumpFilename(cpu_index);
  if (enabled) {
    emit statusMessage(
        tr("CPU%1 auto-dump armed for reset/mode change → %2")
            .arg(cpu_index + 1)
            .arg(filename),
        4000);
  } else {
    emit statusMessage(tr("CPU%1 auto-dump disarmed").arg(cpu_index + 1), 3000);
  }
}

void EmulatorController::setWatchRegister(bool enabled) {
  if (!impl_) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::watchregister) != 0;
  if (was == enabled) {
    return;
  }
  if (enabled) {
    impl_->config.flags |= PC8801::Config::watchregister;
  } else {
    impl_->config.flags &= ~PC8801::Config::watchregister;
  }
  withVmPaused([&]() {
    if (impl_->pc88) {
      M88ApplyConfig(impl_->pc88.get(), &impl_->config);
      if (enabled) {
        Z80C* cpu1 = impl_->pc88->GetCPU1();
        Z80C* cpu2 = impl_->pc88->GetCPU2();
        statusdisplay.UpdateRegisterWatch(true, "%.4X(%.2X)/%.4X", cpu1->GetPC(),
                                          cpu1->GetReg().ireg, cpu2->GetPC());
      } else {
        statusdisplay.UpdateRegisterWatch(false);
      }
    }
  });
  saveConfig();
  emitMachineConfig();
  pollStatusUi();
}

void EmulatorController::setBurstMode(bool enabled) {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::cpuburst) != 0;
  if (was == enabled) {
    return;
  }
  withVmPaused([&]() {
    if (enabled) {
      impl_->config.flags |= PC8801::Config::cpuburst;
      impl_->config.flags &= ~PC8801::Config::fullspeed;
    } else {
      impl_->config.flags &= ~PC8801::Config::cpuburst;
    }
    impl_->pc88->TimeSync();
    resetSequencerPacing();
    if (!enabled && impl_->sound) {
      impl_->sound->ResetPcmContract();
      updateSequencerAudio();
    }
  });
  emitMachineConfig();
  emit statusMessage(enabled ? tr("Burst mode on") : tr("Burst mode off"), 2000);
}

void EmulatorController::setArrowTenkey(bool enabled) {
  if (!impl_ || !impl_->keyif) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::usearrowfor10) != 0;
  if (was == enabled) {
    return;
  }
  if (enabled) {
    impl_->config.flags |= PC8801::Config::usearrowfor10;
  } else {
    impl_->config.flags &= ~PC8801::Config::usearrowfor10;
  }
  withVmPaused([&]() { impl_->keyif->ApplyConfig(&impl_->config); });
  saveConfig();
  emitMachineConfig();
  emit statusMessage(enabled ? tr("Arrow keys mapped to ten-key")
                             : tr("Arrow keys not mapped to ten-key"),
                     2000);
}

void EmulatorController::applyWinReset() {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    return;
  }
  withVmPaused([&]() {
    PC8801::Config hw = M88ConfigForHardware(impl_->config);
    M88ApplyWinReset(
        *impl_->keyif, *impl_->pc88, impl_->seq, &hw, draw_,
        [&](PC8801::Config* cfg) {
          if (impl_->sound) {
            impl_->sound->ApplyConfig(cfg);
          }
        });
    impl_->seq.ForceDrawAfterReset(impl_->config.refreshtiming);
    impl_->pc88->UpdateScreen(true);
    if (draw_) {
      draw_->InvalidateUiStaging();
      draw_->StageUiFrame();
    }
    impl_->post_reset_redraw_frames_.store(60, std::memory_order_relaxed);
  });
  emit frameReady();
}

void EmulatorController::queueDiskOp(DiskOpType op, int drive, int index,
                                   const QString& path) {
  if (!impl_) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->pending_disk_op = op;
  impl_->pending_disk_drive = drive;
  impl_->pending_disk_index = index;
  impl_->pending_disk_path = path;
}

void EmulatorController::changeDiskImage(int drive, const QString& path) {
  if (!impl_ || drive < 0 || drive > 1) {
    return;
  }
  queueDiskOp(DiskOpType::ChangeImage, drive, 0, path);
}

void EmulatorController::changeBothDrives(const QString& path) {
  if (!impl_) {
    return;
  }
  queueDiskOp(DiskOpType::BothDrives, 0, 0, path);
}

void EmulatorController::selectDisk(int drive, int index) {
  if (!impl_ || drive < 0 || drive > 1 || index < 0 || index >= 64) {
    return;
  }
  queueDiskOp(DiskOpType::SelectDisk, drive, index, QString());
}

void EmulatorController::mountDisk0(const QString& path) {
  changeDiskImage(0, path);
}

void EmulatorController::ejectDisk0() {
  changeDiskImage(0, QString());
}

void EmulatorController::resetMachine() {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    emit statusMessage(tr("Emulator is not ready"));
    return;
  }
  if (cpuDumpArmed()) {
    startCpuDumpCapture(CpuDumpTrigger::Reset);
    return;
  }
  M88LogSoftReset();
  applyWinReset();
}

void EmulatorController::setClock(int clock) {
  if (!impl_ || !impl_->pc88 || !impl_->keyif || (clock != 40 && clock != 80)) {
    return;
  }
  if (M88BasicModeFixesClock4MHz(static_cast<int>(impl_->config.basicmode))) {
    return;
  }
  if (impl_->config.clock == clock) {
    return;
  }
  if (cpuDumpArmed()) {
    impl_->cpu_dump_schedule.pending_clock = clock;
    startCpuDumpCapture(CpuDumpTrigger::SetClock);
    return;
  }
  impl_->config.clock = clock;
  applyWinReset();
}

void EmulatorController::setBasicMode(int mode) {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    return;
  }
  const auto bm = static_cast<PC8801::Config::BASICMode>(mode);
  if (impl_->config.basicmode == bm) {
    return;
  }
  if (cpuDumpArmed()) {
    impl_->cpu_dump_schedule.pending_basic_mode = bm;
    startCpuDumpCapture(CpuDumpTrigger::SetBasicMode);
    return;
  }
  impl_->config.basicmode = bm;
  applyWinReset();
}

