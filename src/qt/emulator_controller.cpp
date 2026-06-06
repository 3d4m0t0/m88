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

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <sys/stat.h>

namespace {

bool RomFileExists(const char* filename) {
  char path[MAX_PATH];
  M88RomPath(path, sizeof(path), filename);
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool DiskFileExists(const char* path) {
  if (!path || !*path) {
    return false;
  }
  struct stat st {};
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool UnmountDrives(DiskManager& diskmgr, int drive) {
  bool had_disk = false;
  if (drive != 1) {
    had_disk = diskmgr.GetNumDisks(0) > 0 || diskmgr.GetNumDisks(1) > 0;
    diskmgr.Unmount(0);
    diskmgr.Unmount(1);
  } else {
    had_disk = diskmgr.GetNumDisks(drive) > 0;
    diskmgr.Unmount(drive);
  }
  return had_disk;
}

bool MountDiskPath(DiskManager& diskmgr, int drive, const QString& path,
                   QString* error_out = nullptr, bool unmount_first = true) {
  if (unmount_first) {
    UnmountDrives(diskmgr, drive);
  }
  if (path.isEmpty()) {
    return true;
  }
  const QString resolved = QFileInfo(path).canonicalFilePath();
  const QByteArray utf8 =
      resolved.isEmpty() ? path.toUtf8() : resolved.toUtf8();
  if (!DiskFileExists(utf8.constData())) {
    if (error_out) {
      *error_out = QObject::tr("ディスクが見つかりません: %1").arg(path);
    }
    std::fprintf(stderr, "Disk not found: %s\n", utf8.constData());
    return false;
  }
  if (!diskmgr.Mount(static_cast<uint>(drive), utf8.constData(), false, 0, false)) {
    diskmgr.Unmount(drive);
    if (error_out) {
      *error_out = QObject::tr("ディスクのマウントに失敗しました: %1").arg(path);
    }
    std::fprintf(stderr, "Failed to mount disk: %s\n", utf8.constData());
    return false;
  }
  std::fprintf(stderr, "M88: mounted drive %d: %s\n", drive, utf8.constData());
  if (diskmgr.GetNumDisks(0) > 1) {
    if (!diskmgr.Mount(1, utf8.constData(), false, 1, false)) {
      diskmgr.Unmount(1);
      std::fprintf(stderr, "Warning: failed to mount second disk in image: %s\n",
                   utf8.constData());
    }
  }
  return true;
}

void MountDiskOptional(DiskManager& diskmgr, int drive, const QString& path) {
  MountDiskPath(diskmgr, drive, path, nullptr);
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

  std::mutex pending_mutex;
  QString pending_mount_path;
  QString pending_ime_utf8;
  bool mount_requested = false;
  bool eject_requested = false;
  bool ime_commit_requested = false;

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

  if (!RomFileExists("pc88.rom")) {
    emit failed(QStringLiteral("ROM not found (pc88.rom)"));
    return false;
  }

  char ini_path[512];
  bool ini_created = false;
  M88LoadStartupConfig(&impl_->config, options_.config_file.isEmpty()
                                              ? nullptr
                                              : options_.config_file.toUtf8().constData(),
                       ini_path, sizeof(ini_path), &ini_created);
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

  MountDiskOptional(*impl_->diskmgr, 0, options_.disk0);

  M88PostStartupCpuReset(*impl_->pc88, &impl_->draw_skip, impl_->config.refreshtiming);
  impl_->hw_reset_done = true;
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->InvalidateUiStaging();
    draw_->StageUiFrame();
  }
  impl_->post_reset_redraw_frames_ = 60;
  emit frameReady();

  emit started();
  return true;
}

void EmulatorController::shutdown() {
  if (!impl_) {
    return;
  }
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
  impl_->pc88->Proceed(static_cast<uint>(texec), clk, effclock);
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

void EmulatorController::processDeferredActions() {
  if (!impl_) {
    return;
  }

  bool do_eject = false;
  {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    do_eject = impl_->eject_requested;
    impl_->eject_requested = false;
  }
  if (do_eject) {
    QString msg;
    if (impl_->diskmgr) {
      if (!UnmountDrives(*impl_->diskmgr, 0)) {
        msg = tr("マウントされているディスクはありません");
      } else {
        options_.disk0.clear();
        msg = tr("ドライブ 0 を取り出しました");
      }
      if (!msg.isEmpty()) {
        emit statusMessage(msg, 2000);
      }
    }
  }

  QString mount_path;
  {
    std::lock_guard<std::mutex> lock(impl_->pending_mutex);
    if (impl_->mount_requested) {
      impl_->mount_requested = false;
      mount_path = impl_->pending_mount_path;
      impl_->pending_mount_path.clear();
    }
  }
  if (!mount_path.isEmpty()) {
    if (!impl_->diskmgr) {
      emit statusMessage(tr("エミュレータの準備ができていません"));
    } else {
      const QString resolved = QFileInfo(mount_path).canonicalFilePath();
      const QByteArray utf8 =
          resolved.isEmpty() ? mount_path.toUtf8() : resolved.toUtf8();
      if (!DiskFileExists(utf8.constData())) {
        emit statusMessage(tr("ディスクが見つかりません: %1").arg(mount_path), 5000);
      } else {
        if (UnmountDrives(*impl_->diskmgr, 0)) {
          options_.disk0.clear();
          std::fprintf(stderr,
                       "M88: unmounted previous disk before opening new image\n");
          emit statusMessage(tr("マウント済みのディスクを取り出しました"), 2000);
        }
        QString error;
        if (!MountDiskPath(*impl_->diskmgr, 0, mount_path, &error, false)) {
          emit statusMessage(
              error.isEmpty() ? tr("ディスクのマウントに失敗しました") : error, 5000);
        } else {
          options_.disk0 = mount_path;
          refreshDisplayAfterDiskChange();
          emit statusMessage(
              tr("ドライブ 0 にマウント: %1").arg(QFileInfo(mount_path).fileName()));
        }
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

void EmulatorController::emulateFrame() {
  if (!running_ || !impl_ || !impl_->pc88) {
    return;
  }

  processDeferredActions();
  if (!running_) {
    return;
  }

  M88LoadmonFrameBegin();
  const uint64_t frame_begin_ns = M88MonotonicNowNs();
  const int texec = impl_->pc88->GetFramePeriod();
  const int effclock =
      std::max(1, impl_->config.clock * (impl_->config.speed / 10) / 100);
  const uint clk = static_cast<uint>(impl_->config.clock);
  const uint eff = static_cast<uint>(effclock);
  impl_->pc88->TimeSync();
  proceedFrame(texec, clk, eff);

  // Qt: refresh every frame. Sequencer-style draw_skip can skip Unlock() when
  // refresh=false and !updated, leaving frame_ready_=false (frozen UI until F5).
  impl_->pc88->UpdateScreen(true);
  if (impl_->post_reset_redraw_frames_ > 0) {
    --impl_->post_reset_redraw_frames_;
  }

  if (draw_) {
    draw_->StageUiFrame();
  }
  LinuxIme::Pump(impl_->keyif.get());
  emit frameReady();

  if (running_) {
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

void EmulatorController::applyUserResetAndRefresh() {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    return;
  }
  HalfKanaIme::InjectEndSession(impl_->keyif.get(), &impl_->config);
  impl_->keyif->FlushGuestKeys();
  impl_->keyif->ApplyConfig(&impl_->config);
  M88UserCpuReset(*impl_->pc88, &impl_->draw_skip, impl_->config.refreshtiming);
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->InvalidateUiStaging();
    draw_->StageUiFrame();
  }
  impl_->post_reset_redraw_frames_ = 60;
  emit frameReady();
}

void EmulatorController::mountDisk0(const QString& path) {
  if (!impl_ || path.isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->pending_mount_path = path;
  impl_->mount_requested = true;
}

void EmulatorController::ejectDisk0() {
  if (!impl_) {
    return;
  }
  std::lock_guard<std::mutex> lock(impl_->pending_mutex);
  impl_->eject_requested = true;
}

void EmulatorController::resetMachine() {
  if (!impl_ || !impl_->pc88 || !impl_->keyif) {
    emit statusMessage(tr("エミュレータの準備ができていません"));
    return;
  }
  reset_requested_.store(true, std::memory_order_release);
}

