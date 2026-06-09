#include "qt_platform.h"

#include <QGuiApplication>

bool M88QtIsWaylandSession() {
  return QGuiApplication::platformName() == QLatin1String("wayland");
}
