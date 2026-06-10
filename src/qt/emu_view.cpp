#include "emu_view.h"

#include "../linux/shared_framebuffer_draw.h"
#include "draw.h"
#include "qt_host_input.h"
#include "qt_input.h"

#include "../linux_compat/winkeys.h"

#include <QFocusEvent>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QCursor>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cstring>

namespace {

bool HostShortcutModifiers(const QKeyEvent& event) {
  switch (event.key()) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
      return false;
  }
  return event.modifiers().testFlag(Qt::AltModifier) ||
         event.modifiers().testFlag(Qt::MetaModifier);
}

}  // namespace

bool EmuView::imeComposing() const {
  return ime_block_keys_ || !ime_preedit_.isEmpty();
}

EmuView::EmuView(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_InputMethodEnabled, true);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, Qt::black);
  setPalette(pal);
}

void EmuView::attachFramebuffer(SharedFramebufferDraw* draw) { draw_ = draw; }

void EmuView::setScale(int scale) {
  scale_ = std::max(1, scale);
  updateGeometry();
  update();
}

void EmuView::setForce480Layout(bool enabled) {
  if (force480_layout_ == enabled) {
    return;
  }
  force480_layout_ = enabled;
  update();
}

void EmuView::setSuppressMenu(bool enabled) {
  QtInput::SetSuppressMenu(enabled);
}

void EmuView::setHostInput(QtHostInput::Host* host_input) {
  host_input_ = host_input;
}

void EmuView::setMouseCapture(bool enabled) {
  mouse_capture_ = enabled;
  if (host_input_) {
    host_input_->applyMouseCapture(enabled, this);
  }
}

void EmuView::updateMouseButtons(Qt::MouseButtons buttons) {
  if (!host_input_) {
    return;
  }
  uint mask = 0;
  if (buttons.testFlag(Qt::LeftButton)) {
    mask |= 1;
  }
  if (buttons.testFlag(Qt::RightButton)) {
    mask |= 2;
  }
  host_input_->mouse()->PostButtons(mask);
}

QSize EmuView::sizeHint() const {
  return QSize(640 * scale_, 400 * scale_);
}

QVector<QRgb> EmuView::colorTableFromPalette() const {
  QVector<QRgb> table(256);
  for (int i = 0; i < 256; ++i) {
    const Draw::Palette& p = palette_[i];
    table[i] = qRgb(p.red, p.green, p.blue);
  }
  return table;
}

void EmuView::refreshFrame() {
  if (!draw_) {
    return;
  }

  if (ime_preedit_.isEmpty()) {
    ime_preedit_ = QString::fromUtf8(draw_->GetImePreedit());
  }

  const uint8* data = nullptr;
  int bpl = 0;
  uint w = 0;
  uint h = 0;
  bool pal_changed = false;
  Draw::Region region {};
  const uint64_t serial = draw_->UiFrameSerial();
  const uint64_t pal_serial = draw_->UiPaletteSerial();
  const bool ime_only = draw_->ConsumeImeRepaint();
  if (serial == last_frame_serial_ && pal_serial == last_palette_serial_ && !ime_only) {
    return;
  }

  const bool got_frame =
      draw_->AcquireUiFrame(&data, &bpl, &w, &h, &pal_changed, palette_, 256, &region);
  if (!got_frame) {
    if (ime_only) {
      update(QRect(0, height() - 28, width(), 28));
    }
    return;
  }
  if (!data || bpl <= 0 || w == 0 || h == 0) {
    return;
  }

  last_frame_serial_ = serial;
  last_palette_serial_ = pal_serial;

  const int iw = static_cast<int>(w);
  const int ih = static_cast<int>(h);
  if (indices_.width() != iw || indices_.height() != ih ||
      indices_.format() != QImage::Format_Indexed8) {
    indices_ = QImage(iw, ih, QImage::Format_Indexed8);
  }
  const int dst_bpl = indices_.bytesPerLine();
  if (dst_bpl == bpl) {
    std::memcpy(indices_.bits(), data, static_cast<size_t>(bpl) * ih);
  } else {
    for (int y = 0; y < ih; ++y) {
      std::memcpy(indices_.scanLine(y), data + static_cast<size_t>(y) * bpl,
                  static_cast<size_t>(iw));
    }
  }
  if (pal_changed || indices_.colorTable().size() != 256) {
    indices_.setColorTable(colorTableFromPalette());
  }
  update();
}

void EmuView::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);

  if (!indices_.isNull()) {
    const int dst_w = indices_.width() * scale_;
    const int dst_h = indices_.height() * scale_;
    const int box_h = force480_layout_ ? 480 * scale_ : dst_h;
    const int x = (width() - dst_w) / 2;
    const int y = (height() - box_h) / 2 + (box_h - dst_h) / 2;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(QRect(x, y, dst_w, dst_h), indices_);
  }

  if (!ime_preedit_.isEmpty()) {
    const int bar_h = 28;
    const QRect bar(0, height() - bar_h, width(), bar_h);
    painter.fillRect(bar, QColor(20, 24, 40, 220));
    painter.setPen(QColor(120, 180, 255));
    painter.drawRect(bar);
    painter.setPen(Qt::white);
    painter.drawText(bar.adjusted(8, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     ime_preedit_);
  }
}

void EmuView::mousePressEvent(QMouseEvent* event) {
  setFocus(Qt::MouseFocusReason);
  if (mouse_capture_) {
    updateMouseButtons(event->buttons());
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void EmuView::mouseReleaseEvent(QMouseEvent* event) {
  if (mouse_capture_) {
    updateMouseButtons(event->buttons());
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void EmuView::mouseMoveEvent(QMouseEvent* event) {
  if (!mouse_capture_ || !host_input_) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  const QPoint center = rect().center();
  const QPoint pos = event->pos();
  const int dx = (center.x() - pos.x()) / 2;
  const int dy = (center.y() - pos.y()) / 2;
  if (dx != 0 || dy != 0) {
    host_input_->mouse()->PostMovement(dx, dy);
    QCursor::setPos(mapToGlobal(center));
  }
  event->accept();
}

bool EmuView::event(QEvent* event) {
  if (event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (key->modifiers().testFlag(Qt::AltModifier) &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
      key->accept();
      return true;
    }
    if (HostShortcutModifiers(*key)) {
      key->ignore();
      return false;
    }
  }
  return QWidget::event(event);
}

void EmuView::focusOutEvent(QFocusEvent* event) {
  emit flushGuestKeys();
  emit clearHostModifiers();
  QWidget::focusOutEvent(event);
}

void EmuView::keyPressEvent(QKeyEvent* event) {
  if (event->modifiers().testFlag(Qt::AltModifier) &&
      (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
    event->accept();
    return;
  }
  if (HostShortcutModifiers(*event)) {
    event->ignore();
    return;
  }
  if (!ime_preedit_.isEmpty()) {
    event->ignore();
    return;
  }
  if (!event->isAutoRepeat()) {
    const uint vk = QtInput::VkFromKeyEvent(*event);
    if (vk) {
      emit keyDown(vk, QtInput::KeyDataFromEvent(*event));
    }
  }
  event->accept();
}

void EmuView::keyReleaseEvent(QKeyEvent* event) {
  if (HostShortcutModifiers(*event)) {
    event->ignore();
    return;
  }
  if (imeComposing()) {
    if (!event->isAutoRepeat()) {
      const uint vk = QtInput::VkFromKeyEvent(*event);
      if (vk && QtInput::IsHostModifierVk(vk)) {
        emit keyUp(vk, QtInput::KeyExtended(*event));
      }
      emit clearHostModifiers();
    }
    event->ignore();
    return;
  }
  if (QtInput::IsHostImeModifierKey(event->key())) {
    event->ignore();
    return;
  }
  if (!event->isAutoRepeat()) {
    const uint vk = QtInput::VkFromKeyEvent(*event);
    if (vk) {
      emit keyUp(vk, QtInput::KeyDataFromEvent(*event));
    }
  }
  event->accept();
}

void EmuView::inputMethodEvent(QInputMethodEvent* event) {
  const QString prev_preedit = ime_preedit_;
  const QString new_preedit = event->preeditString();

  QString commit = event->commitString();
  if (commit.isEmpty() && event->replacementLength() > 0 && !prev_preedit.isEmpty()) {
    commit = prev_preedit;
  }

  if (!commit.isEmpty()) {
    ime_preedit_.clear();
    ime_block_keys_ = false;

    if (draw_) {
      draw_->SetImePreedit("");
    }
    emit imeCommit(commit);
  } else if (!new_preedit.isEmpty()) {
    if (prev_preedit.isEmpty()) {
      emit flushGuestKeys();
    }
    ime_preedit_ = new_preedit;
    ime_block_keys_ = true;
    if (draw_) {
      draw_->SetImePreedit(ime_preedit_.toUtf8().constData());
    }
  } else {
    ime_preedit_.clear();
    ime_block_keys_ = false;
    if (!prev_preedit.isEmpty()) {
      emit clearHostModifiers();
    }
    if (draw_) {
      draw_->SetImePreedit("");
    }
  }
  event->accept();
  update();
}

QVariant EmuView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    const int bar_h = 28;
    return QRect(0, height() - bar_h, width(), bar_h);
  }
  return QWidget::inputMethodQuery(query);
}
