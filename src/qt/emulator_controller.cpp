#include "emulator_controller.h"

#include "../linux/linux_config.h"
#include "../linux/pc88_key_fixup.h"
#include "../linux/half_kana_ime.h"
#include "../linux/linux_ime.h"
#include "../linux/linux_frame_pace.h"
#include "../linux/linux_sound.h"
#include "../linux/linux_startup_log.h"
#include "qt_video_log.h"
#include "../linux/shared_framebuffer_draw.h"
#include "../win32/WinKeyIF.h"
#include "path.h"

#include "pc88/config.h"
#include "pc88/diskmgr.h"
#include "pc88/beep.h"
#include "pc88/opnif.h"
#include "pc88/pc88.h"
#include "pc88/tapemgr.h"

#include <SDL.h>

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

void MountDiskOptional(DiskManager& diskmgr, int drive, const QString& path) {
  diskmgr.Unmount(drive);
  if (drive == 1) {
    return;
  }
  diskmgr.Unmount(1);
  if (path.isEmpty()) {
    return;
  }
  const QString resolved = QFileInfo(path).canonicalFilePath();
  const QByteArray utf8 =
      resolved.isEmpty() ? path.toUtf8() : resolved.toUtf8();
  if (!DiskFileExists(utf8.constData())) {
    std::fprintf(stderr, "Disk not found, starting without disk: %s\n", utf8.constData());
    return;
  }
  if (!diskmgr.Mount(static_cast<uint>(drive), utf8.constData(), false, 0, false)) {
    diskmgr.Unmount(drive);
    std::fprintf(stderr, "Failed to mount disk, starting without disk: %s\n", utf8.constData());
    return;
  }
  std::fprintf(stderr, "M88: mounted drive %d: %s\n", drive, utf8.constData());
  if (diskmgr.GetNumDisks(0) > 1) {
    if (!diskmgr.Mount(1, utf8.constData(), false, 1, false)) {
      diskmgr.Unmount(1);
      std::fprintf(stderr, "Warning: failed to mount second disk in image: %s\n",
                   utf8.constData());
    }
  }
}

}  // namespace

struct EmulatorController::Impl {
  PC8801::Config config{};
  std::unique_ptr<DiskManager> diskmgr;
  std::unique_ptr<TapeManager> tapemgr;
  std::unique_ptr<PC88> pc88;
  std::unique_ptr<PC8801::LinuxSound> sound;
  std::unique_ptr<PC8801::WinKeyIF> keyif;
  M88DrawSkip draw_skip;
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

  if (!RomFileExists("pc88.rom") && !RomFileExists("disk.rom")) {
    emit failed(QStringLiteral("ROM not found (pc88.rom or disk.rom)"));
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
    emit failed(QStringLiteral("Failed to initialize PC-8801 core"));
    return false;
  }

  impl_->keyif = std::make_unique<PC8801::WinKeyIF>();
  if (!impl_->keyif->Init() || !impl_->pc88->ConnectKeyboard(impl_->keyif.get())) {
    emit failed(QStringLiteral("Failed to initialize keyboard"));
    return false;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
  }

  impl_->sound = std::make_unique<PC8801::LinuxSound>();
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
  impl_->pc88->Reset();
  impl_->draw_skip.Reset();
  impl_->pc88->UpdateScreen(true);
  if (draw_) {
    draw_->StageUiFrame();
  }
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
  if (SDL_WasInit(SDL_INIT_AUDIO)) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
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

void EmulatorController::emulateFrame() {
  if (!impl_ || !impl_->pc88) {
    return;
  }
  const uint32 frame_begin_ms = SDL_GetTicks();
  const int texec = impl_->pc88->GetFramePeriod();
  const int effclock =
      std::max(1, impl_->config.clock * (impl_->config.speed / 10) / 100);
  impl_->pc88->TimeSync();
  impl_->pc88->Proceed(texec, impl_->config.clock, effclock);
  if (impl_->draw_skip.AfterProceed(frame_begin_ms, texec, impl_->config.speed,
                                    impl_->config.refreshtiming)) {
    impl_->pc88->UpdateScreen();
    if (draw_) {
      draw_->StageUiFrame();
    }
    LinuxIme::Pump(impl_->keyif.get());
    emit frameReady();
  } else {
    LinuxIme::Pump(impl_->keyif.get());
  }
  impl_->draw_skip.EndFrame(frame_begin_ms);

  M88PaceFrame(frame_begin_ms,
               M88FramePeriodMs(texec, impl_->config.speed));
}

void EmulatorController::run() {
  if (!initialize()) {
    emit finished();
    if (QThread* t = thread()) {
      t->quit();
    }
    return;
  }

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
  int guard = 0;
  while (HalfKanaIme::InjectBusy() && guard++ < 8192) {
    HalfKanaIme::InjectPump(impl_->keyif.get());
    const int period = std::max(1, impl_->pc88->GetFramePeriod());
    impl_->pc88->Proceed(static_cast<uint>(period), host_clock, effclock);
  }
  HalfKanaIme::InjectEndSession(impl_->keyif.get(), &impl_->config);
}

