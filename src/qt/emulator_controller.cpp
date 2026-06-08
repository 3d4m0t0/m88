#include "emulator_controller.h"

#include "../linux/linux_config.h"
#include "../linux/pc88_key_fixup.h"
#include "../linux/half_kana_ime.h"
#include "../linux/linux_ime.h"
#include "../linux/linux_emulation.h"
#include "../linux/linux_frame_pace.h"
#include "../linux/qt_miniaudio_sound.h"
#include "../linux/linux_startup_log.h"
#include "../linux_compat/loadmon.h"
#include "qt_video_log.h"
#include "../linux/shared_framebuffer_draw.h"
#include "../win32/WinKeyIF.h"
#include "path.h"

#include "status.h"

#include "error.h"
#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "pc88/beep.h"
#include "pc88/opnif.h"
#include "pc88/pc88.h"
#include "pc88/tapemgr.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QThread>

#include <atomic>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>

namespace {

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
      *error_out = QObject::tr("ディスクが見つかりません: %1").arg(path);
    }
    return false;
  }
  if (!diskmgr.Mount(static_cast<uint>(drive), utf8.constData(), false, index,
                     false)) {
    diskmgr.Unmount(drive);
    if (error_out) {
      *error_out = QObject::tr("ディスクのマウントに失敗しました: %1").arg(path);
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
  PC8801::Config config{};
  std::unique_ptr<DiskManager> diskmgr;
  std::unique_ptr<TapeManager> tapemgr;
  std::unique_ptr<PC88> pc88;
  std::unique_ptr<PC8801::QtMiniaudioSound> sound;
  std::unique_ptr<PC8801::WinKeyIF> keyif;
  M88DrawSkip draw_skip;
  bool hw_reset_done = false;
  int post_reset_redraw_frames_ = 0;

  QString drive_path[2];
  QString pending_ime_utf8;
  EmulatorController::DiskOpType pending_disk_op = EmulatorController::DiskOpType::None;
  int pending_disk_drive = 0;
  int pending_disk_index = 0;
  QString pending_disk_path;

  std::mutex pending_mutex;
  bool ime_commit_requested = false;

  int burst_effclock = 100;
  uint64_t burst_window_begin_ns = 0;
  int burst_window_exec_ms = 0;

  QString ini_path;
  std::atomic<int> title_frame_count{0};
  std::atomic<long> title_exec_count{0};

};

EmulatorController::EmulatorController(SharedFramebufferDraw* draw, Options options,
                                       QObject* parent)
    : QObject(parent), draw_(draw), options_(std::move(options)) {
  impl_ = new Impl();
}

EmulatorController::~EmulatorController() {
  requestStop();
  shutdown();
  delete impl_;
  impl_ = nullptr;
}

void EmulatorController::requestStop() { running_ = false; }

bool EmulatorController::initialize() {
  if (!draw_ || !impl_) {
    return false;
  }

  M88InitRomPath(options_.rom_dir.isEmpty() ? nullptr
                                             : options_.rom_dir.toUtf8().constData());

  if (!M88HasRequiredRoms()) {
    emit failed(QStringLiteral("ROM not found (pc88.rom or n88.rom)"));
    return false;
  }

  char ini_path[512];
  bool ini_created = false;
  M88LoadStartupConfig(&impl_->config, options_.config_file.isEmpty()
                                              ? nullptr
                                              : options_.config_file.toUtf8().constData(),
                       ini_path, sizeof(ini_path), &ini_created);
  impl_->ini_path = QString::fromUtf8(ini_path);
  M88ApplyEnvOverrides(&impl_->config);
  M88LogConfigPath(ini_path, ini_created);

  if (options_.keyboard_type >= 0) {
    impl_->config.keytype =
        static_cast<PC8801::Config::KeyType>(options_.keyboard_type);
    M88NoteKeyboardCliOverride();
  } else {
    M88ApplyDetectedKeyboard(&impl_->config);
  }
  M88LoadKeyFixup(ini_path, &impl_->config);
  if (options_.arrow_tenkey) {
    impl_->config.flags |= PC8801::Config::usearrowfor10;
  }

  impl_->diskmgr = std::make_unique<DiskManager>();
  if (!impl_->diskmgr->Init()) {
    emit failed(QStringLiteral("Failed to initialize disk manager"));
    return false;
  }

  impl_->tapemgr = std::make_unique<TapeManager>();
  impl_->pc88 = std::make_unique<PC88>();
  if (!impl_->pc88->Init(draw_, impl_->diskmgr.get(), impl_->tapemgr.get())) {
    const char* detail = Error::GetErrorText();
    emit failed(detail && *detail
                    ? QString::fromUtf8(detail)
                    : QStringLiteral("Failed to initialize PC-8801 core"));
    return false;
  }

  impl_->keyif = std::make_unique<PC8801::WinKeyIF>();
  if (!impl_->keyif->Init() || !impl_->pc88->ConnectKeyboard(impl_->keyif.get())) {
    emit failed(QStringLiteral("Failed to initialize keyboard"));
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

  M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  impl_->sound->ApplyConfig(&impl_->config);
  impl_->keyif->ApplyConfig(&impl_->config);
  impl_->keyif->Activate(true);
  syncStatusBarFromConfig();
  LinuxIme::InitHost();
  M88LogMachine(&impl_->config);
  M88LogQtVideoBackend();
  M88LogSound(&impl_->config);
  M88LogKeyboard(&impl_->config);
  M88LogKeyFix();
  if (LinuxIme::Enabled() && impl_->config.keytype != PC8801::Config::PC98) {
    M88LogImeHalfKana();
  }
  M88LogFdd(&impl_->config);

  if (!options_.disk0.isEmpty()) {
    OpenDiskImageStartup(*impl_->diskmgr, options_.disk0);
    impl_->drive_path[0] = options_.disk0;
    if (impl_->diskmgr->GetNumDisks(0) > 1) {
      impl_->drive_path[1] = options_.disk0;
    }
  }

  M88PostStartupCpuReset(*impl_->pc88, &impl_->draw_skip, impl_->config.refreshtiming);
  impl_->hw_reset_done = true;
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->InvalidateUiStaging();
    draw_->StageUiFrame();
  }
  impl_->post_reset_redraw_frames_ = 60;
  emit frameReady();

  emitMachineConfig();
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
  if (!impl_) {
    return;
  }
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
  if (!LinuxIme::CommitUtf8(bytes.constData(), impl_->keyif.get(), &impl_->config)) {
    return;
  }
  const uint host_clock = static_cast<uint>(std::max(1, impl_->config.clock));
  const uint effclock = static_cast<uint>(std::max<int64_t>(
      1, impl_->config.clock * (impl_->config.speed / 10) / 100));
  impl_->pc88->TimeSync();
  int guard = 0;
  while (HalfKanaIme::InjectBusy() && guard++ < 8192) {
    HalfKanaIme::InjectPump(impl_->keyif.get());
    const int period = std::max(1, impl_->pc88->GetFramePeriod());
    proceedFrame(period, host_clock, effclock);
  }
  HalfKanaIme::InjectEndSession(impl_->keyif.get(), &impl_->config);
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
    emit statusMessage(tr("ドライブ %1 を取り出しました").arg(drive + 1), 2000);
    emitDiskConfigurationLocked();
    return;
  }

  QString error;
  if (!MountDriveImage(*impl_->diskmgr, drive, path, 0, &error)) {
    impl_->drive_path[drive].clear();
    emit statusMessage(error.isEmpty() ? tr("ディスクのマウントに失敗しました")
                                       : error,
                       5000);
    emitDiskConfigurationLocked();
    return;
  }

  impl_->drive_path[drive] = path;
  if (drive == 0) {
    options_.disk0 = path;
    syncDrive1AfterDrive0Change();
  }
  refreshDisplayAfterDiskChange();
  emit statusMessage(tr("ドライブ %1 にマウント: %2")
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
    emit statusMessage(tr("ディスクの選択に失敗しました"), 5000);
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
      emit statusMessage(tr("エミュレータの準備ができていません"));
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

  if (reset_requested_.exchange(false, std::memory_order_acq_rel)) {
    applyUserResetAndRefresh();
    emit statusMessage(tr("リセットしました"), 2000);
  }
}

bool EmulatorController::burstActive() const {
  return impl_ && (impl_->config.flags & PC8801::Config::cpuburst) != 0;
}

void EmulatorController::resetBurstPacing() {
  if (!impl_) {
    return;
  }
  impl_->burst_effclock = 100;
  impl_->burst_window_begin_ns = 0;
  impl_->burst_window_exec_ms = 0;
}

void EmulatorController::emulateBurstFrame() {
  if (!impl_ || !impl_->pc88) {
    return;
  }

  if (impl_->burst_window_begin_ns == 0) {
    impl_->burst_window_begin_ns = M88MonotonicNowNs();
    impl_->pc88->TimeSync();
  }

  const uint eff = static_cast<uint>(std::max(1, impl_->burst_effclock));
  const int chunk = std::max(1, 500 * impl_->config.speed / 1000);
  const uint64_t slice_end_ns = M88MonotonicNowNs() + 20'000'000ULL;

  while (running_ && M88MonotonicNowNs() < slice_end_ns) {
    proceedFrame(chunk, eff, eff);
    impl_->burst_window_exec_ms += 5;
    const uint64_t window_ms =
        (M88MonotonicNowNs() - impl_->burst_window_begin_ns) / 1'000'000ULL;
    if (window_ms >= 1000) {
      break;
    }
  }

  const uint64_t window_ms =
      (M88MonotonicNowNs() - impl_->burst_window_begin_ns) / 1'000'000ULL;
  if (window_ms < 1000) {
    LinuxIme::Pump(impl_->keyif.get());
    return;
  }

  const int ms = static_cast<int>(std::min<uint64_t>(window_ms, 1000));
  const int adapt =
      std::min(1000, impl_->burst_window_exec_ms) * impl_->burst_effclock * 100 /
          std::max(1, ms) +
      1;
  impl_->burst_effclock = std::min(10000, adapt);
  impl_->burst_window_begin_ns = 0;
  impl_->burst_window_exec_ms = 0;

  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->StageUiFrame();
  }
  LinuxIme::Pump(impl_->keyif.get());
  impl_->title_frame_count.fetch_add(1, std::memory_order_relaxed);
  emit frameReady();
  pollStatusUi();
}

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
  impl_->config = config;
  if (impl_->keyif) {
    impl_->keyif->ApplyConfig(&impl_->config);
  }
  M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  if (impl_->sound) {
    impl_->sound->ApplyConfig(&impl_->config);
  }
  resetBurstPacing();
  impl_->draw_skip.ForceUpdateAfterReset(impl_->config.refreshtiming);
  if (draw_) {
    draw_->InvalidateUiStaging();
  }
  syncStatusBarFromConfig();
  saveConfig();
  emitMachineConfig();
  pollStatusUi();
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
                       QString::fromUtf8(snap.message), snap.message_duration_ms);
}

void EmulatorController::emulateFrame() {
  if (!running_ || !impl_ || !impl_->pc88) {
    return;
  }

  processDeferredActions();
  if (!running_) {
    return;
  }

  M88LoadmonFrameBegin();
  if (burstActive()) {
    emulateBurstFrame();
    M88LoadmonFrameEnd();
    return;
  }

  const uint64_t frame_begin_ns = M88MonotonicNowNs();
  const int texec = impl_->pc88->GetFramePeriod();
  const int effclock =
      std::max(1, impl_->config.clock * (impl_->config.speed / 10) / 100);
  const uint clk = static_cast<uint>(impl_->config.clock);
  const uint eff = static_cast<uint>(effclock);
  const bool fullspeed =
      (impl_->config.flags & PC8801::Config::fullspeed) != 0;
  const int refresh_timing = std::max(1, impl_->config.refreshtiming);

  bool should_draw = impl_->post_reset_redraw_frames_ > 0 || refresh_timing <= 1;
  if (!should_draw) {
    should_draw = impl_->draw_skip.AfterProceed(frame_begin_ns, texec,
                                                impl_->config.speed,
                                                refresh_timing);
  }

  impl_->pc88->TimeSync();
  proceedFrame(texec, clk, eff);

  if (should_draw) {
    impl_->pc88->UpdateScreen(true);
    if (impl_->post_reset_redraw_frames_ > 0) {
      --impl_->post_reset_redraw_frames_;
    }
    if (draw_) {
      draw_->StageUiFrame();
    }
  }
  if (refresh_timing > 1) {
    impl_->draw_skip.EndFrame(frame_begin_ns);
  }

  LinuxIme::Pump(impl_->keyif.get());
  impl_->title_frame_count.fetch_add(1, std::memory_order_relaxed);
  emit frameReady();
  pollStatusUi();

  if (running_ && !fullspeed) {
    M88PaceFrame(frame_begin_ns, M88FramePeriodNs(texec, impl_->config.speed),
                 &running_);
  }
  M88LoadmonFrameEnd();
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

  while (running_) {
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    emulateFrame();
  }

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
  impl_->draw_skip.ForceUpdateAfterReset(impl_->config.refreshtiming);
  impl_->post_reset_redraw_frames_ = 30;
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
      (impl_->config.flags & PC8801::Config::showstatusbar) != 0,
      (impl_->config.flags & PC8801::Config::showfdcstatus) != 0,
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

void EmulatorController::setBurstMode(bool enabled) {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  const bool was = (impl_->config.flags & PC8801::Config::cpuburst) != 0;
  if (was == enabled) {
    return;
  }
  if (enabled) {
    impl_->config.flags |= PC8801::Config::cpuburst;
    impl_->config.flags &= ~PC8801::Config::fullspeed;
  } else {
    impl_->config.flags &= ~PC8801::Config::cpuburst;
  }
  resetBurstPacing();
  emitMachineConfig();
  emit statusMessage(enabled ? tr("Burst mode on") : tr("Burst mode off"), 2000);
}

void EmulatorController::applyConfigAndReset(const char* diag_tag, int prev_basicmode) {
  (void)prev_basicmode;
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    return;
  }
  // WinUI::Reset: keyif/core ApplyConfig then PC88::Reset.
  HalfKanaIme::InjectEndSession(impl_->keyif.get(), &impl_->config);
  impl_->keyif->FlushGuestKeys();
  impl_->keyif->ApplyConfig(&impl_->config);
  M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  if (impl_->sound) {
    impl_->sound->ApplyConfig(&impl_->config);
  }
  if (diag_tag && std::strcmp(diag_tag, "mode_switch") == 0) {
    M88LogMachine(&impl_->config);
  }
  M88UserCpuReset(*impl_->pc88, &impl_->draw_skip, impl_->config.refreshtiming);
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->InvalidateUiStaging();
    draw_->StageUiFrame();
  }
  impl_->post_reset_redraw_frames_ = 60;
  emit frameReady();
}

void EmulatorController::applyUserResetAndRefresh() {
  applyConfigAndReset("user_reset");
  emit statusMessage(tr("リセットしました"), 2000);
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
    emit statusMessage(tr("エミュレータの準備ができていません"));
    return;
  }
  reset_requested_.store(true, std::memory_order_release);
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
  impl_->config.clock = clock;
  // WinUI::IDM_4MHZ/8MHZ call Reset(), but real hardware only changes clock.
  impl_->keyif->ApplyConfig(&impl_->config);
  M88ApplyConfig(impl_->pc88.get(), &impl_->config);
  if (impl_->sound) {
    impl_->sound->ApplyConfig(&impl_->config);
  }
  emitMachineConfig();
  saveConfig();
  std::fprintf(stdout, "M88: CPUClock=%d (%.1f MHz)\n", clock, clock / 10.0);
  std::fflush(stdout);
  emit statusMessage(tr("Clock: %1 MHz").arg(clock / 10), 2000);
}

void EmulatorController::setBasicMode(int mode) {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  const auto bm = static_cast<PC8801::Config::BASICMode>(mode);
  if (bm == PC8801::Config::N802 && !impl_->pc88->IsN80Supported()) {
    return;
  }
  if (bm == PC8801::Config::N80V2 && !impl_->pc88->IsN80V2Supported()) {
    return;
  }
  if (bm == PC8801::Config::N88V2CD && !impl_->pc88->IsCDSupported()) {
    return;
  }
  if (impl_->config.basicmode == bm) {
    return;
  }
  const int prev_basicmode = static_cast<int>(impl_->config.basicmode);
  impl_->config.basicmode = bm;
  if (M88BasicModeFixesClock4MHz(mode)) {
    impl_->config.clock = 40;
  }
  applyConfigAndReset("mode_switch", prev_basicmode);
  emitMachineConfig();
  saveConfig();
  emit statusMessage(tr("Basic mode changed"), 2000);
}

