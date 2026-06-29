#pragma once

#include <QSurfaceFormat>
#include <QVector>

void M88QtConfigureOpenGL();

QSurfaceFormat M88QtBaseGlFormat();
QSurfaceFormat M88QtDesktopGlFormat();
QSurfaceFormat M88QtOpenGlEsFormat();
QVector<QSurfaceFormat> M88QtGlFallbackFormats();
const char* M88QtGlFormatLabel(const QSurfaceFormat& fmt);
