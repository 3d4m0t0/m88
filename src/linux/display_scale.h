#pragma once

#include <algorithm>

constexpr int kM88EmuWidth = 640;
constexpr int kM88EmuHeight = 400;

// Qt MainWindow: resize(640*s + chrome_w, 400*s + chrome_h).
constexpr int kM88QtChromeW = 16;
constexpr int kM88QtChromeH = 48;

// Integer scale that fits (emu_w * s, emu_h * s) inside available area.
// chrome_w/h reserve space for window frame, status bar, etc.
inline int M88ComputeIntegerScale(int avail_w, int avail_h, int chrome_w = 0,
                                  int chrome_h = 0) {
  const int inner_w = std::max(1, avail_w - chrome_w);
  const int inner_h = std::max(1, avail_h - chrome_h);
  const int sx = inner_w / kM88EmuWidth;
  const int sy = inner_h / kM88EmuHeight;
  return std::max(1, std::min(sx, sy));
}

// Largest integer fit halved (minimum 1). Used when m88.ini ScreenScale=auto.
inline int M88AutoScreenScale(int avail_w, int avail_h, int chrome_w = 0,
                              int chrome_h = 0) {
  const int fit = M88ComputeIntegerScale(avail_w, avail_h, chrome_w, chrome_h);
  return std::max(1, fit / 2);
}
