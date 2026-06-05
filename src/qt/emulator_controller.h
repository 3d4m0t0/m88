#pragma once

#include <QObject>
#include <QString>
#include <atomic>

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

  void requestStop();

public slots:
  void run();
  void keyDown(quint32 vk, quint32 keydata);
  void keyUp(quint32 vk, quint32 keydata);
  void clearHostModifiers();
  void flushGuestKeys();
  void commitImeText(const QString& utf8);

signals:
  void frameReady();
  void failed(const QString& message);
  void started();
  void finished();

private:
  bool initialize();
  void shutdown();
  void emulateFrame();

  SharedFramebufferDraw* draw_ = nullptr;
  Options options_;
  std::atomic<bool> running_{true};

  struct Impl;
  Impl* impl_ = nullptr;
};
