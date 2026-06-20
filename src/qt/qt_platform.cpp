#include "qt_platform.h"

#include "../linux/linux_ime.h"

#include <QGuiApplication>
#include <QIcon>

bool M88QtIsWaylandSession() {
  return QGuiApplication::platformName() == QLatin1String("wayland");
}

QIcon M88QtAppIcon() {
  static const QIcon icon(QStringLiteral(":/icons/M88.ico"));
  return icon;
}

void M88QtProbeImeAtStartup() {
  const bool qt_im = QGuiApplication::inputMethod() != nullptr;
  LinuxIme::ProbeHostAvailability(qt_im);
}
