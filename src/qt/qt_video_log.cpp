#include "qt_video_log.h"

#include <QImage>

#include <cstdio>

bool M88QtIndexed8Available() {
  QImage probe(4, 4, QImage::Format_Indexed8);
  if (probe.isNull() || probe.format() != QImage::Format_Indexed8) {
    return false;
  }
  probe.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
  return probe.colorTable().size() >= 2;
}

void M88LogQtVideoBackend() {
  if (M88QtIndexed8Available()) {
    std::fprintf(stderr, "M88: video Qt Indexed8 (color table)\n");
  } else {
    std::fprintf(stderr, "M88: video Qt RGB32 (Indexed8 unavailable)\n");
  }
}
