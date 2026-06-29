#include "qt_gl_config.h"

#include "qt_platform.h"

#include <QByteArray>
#include <QCoreApplication>

namespace {

bool PreferOpenGlEsFromEnvironment() {
  const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
  return platform == "wayland" || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
}

QSurfaceFormat MakeBaseFormat() {
  QSurfaceFormat fmt;
  fmt.setDepthBufferSize(0);
  fmt.setStencilBufferSize(0);
  fmt.setAlphaBufferSize(0);
  fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
  fmt.setSwapInterval(1);
  return fmt;
}

}  // namespace

QSurfaceFormat M88QtBaseGlFormat() { return MakeBaseFormat(); }

QSurfaceFormat M88QtDesktopGlFormat() {
  QSurfaceFormat fmt = MakeBaseFormat();
  fmt.setRenderableType(QSurfaceFormat::OpenGL);
  fmt.setVersion(2, 1);
  fmt.setProfile(QSurfaceFormat::CompatibilityProfile);
  return fmt;
}

QSurfaceFormat M88QtOpenGlEsFormat() {
  QSurfaceFormat fmt = MakeBaseFormat();
  fmt.setRenderableType(QSurfaceFormat::OpenGLES);
  fmt.setVersion(2, 0);
  return fmt;
}

void M88QtConfigureOpenGL() {
  const QSurfaceFormat fmt =
      PreferOpenGlEsFromEnvironment() ? M88QtOpenGlEsFormat() : M88QtDesktopGlFormat();
  QSurfaceFormat::setDefaultFormat(fmt);
}

QVector<QSurfaceFormat> M88QtGlFallbackFormats() {
  QVector<QSurfaceFormat> formats;
  formats.push_back(QSurfaceFormat::defaultFormat());
  if (PreferOpenGlEsFromEnvironment()) {
    formats.push_back(M88QtDesktopGlFormat());
  } else {
    formats.push_back(M88QtOpenGlEsFormat());
  }
  return formats;
}

const char* M88QtGlFormatLabel(const QSurfaceFormat& fmt) {
  if (fmt.renderableType() == QSurfaceFormat::OpenGLES) {
    return "OpenGL ES 2.0";
  }
  return "OpenGL 2.1";
}
