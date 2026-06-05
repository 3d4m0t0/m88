#include "emu_view.h"

#include "../linux/shared_framebuffer_draw.h"
#include "draw.h"
#include "qt_input.h"

#include "../linux_compat/winkeys.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>
#include <cstring>

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
  const uint8* data = nullptr;
  int bpl = 0;
  uint w = 0;
  uint h = 0;
  bool pal_changed = false;
  if (!draw_->AcquireUiFrame(&data, &bpl, &w, &h, &pal_changed, palette_, 256)) {
    return;
  }
  if (!data || bpl <= 0 || w == 0 || h == 0) {
    return;
  }

  indices_ = QImage(const_cast<uint8*>(data), static_cast<int>(w), static_cast<int>(h), bpl,
                    QImage::Format_Indexed8);
  if (pal_changed || indices_.colorTable().size() != 256) {
    indices_.setColorTable(colorTableFromPalette());
  }
  if (ime_preedit_.isEmpty()) {
    ime_preedit_ = QString::fromUtf8(draw_->GetImePreedit());
  }
  update();
}

void EmuView::paintEvent(QPaintEvent* /*event*/) {
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);

  if (!indices_.isNull()) {
    const int dst_w = indices_.width() * scale_;
    const int dst_h = indices_.height() * scale_;
    const int x = (width() - dst_w) / 2;
    const int y = (height() - dst_h) / 2;
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
  QWidget::mousePressEvent(event);
}

void EmuView::keyPressEvent(QKeyEvent* event) {
  if (!ime_preedit_.isEmpty()) {
    event->ignore();
    return;
  }
  if (!event->isAutoRepeat()) {
    const QtInput::LetterShiftAdjust adj = QtInput::LetterShiftAdjustFor(*event);
    if (adj != QtInput::LetterShiftAdjust::None) {
      if (!letter_shift_adj_.contains(event->key())) {
        letter_shift_adj_[event->key()] = adj;
      }
      if (++letter_shift_refs_[event->key()] == 1) {
        if (adj == QtInput::LetterShiftAdjust::AddShift) {
          emit keyDown(VK_LSHIFT, 0);
        } else {
          emit keyUp(VK_LSHIFT, 0);
        }
      }
    }
    const uint vk = QtInput::VkFromKeyEvent(*event);
    if (vk) {
      emit keyDown(vk, QtInput::KeyDataFromEvent(*event));
    }
  }
  event->accept();
}

void EmuView::keyReleaseEvent(QKeyEvent* event) {
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
    const auto ref_it = letter_shift_refs_.find(event->key());
    if (ref_it != letter_shift_refs_.end() && --ref_it.value() <= 0) {
      const QtInput::LetterShiftAdjust adj = letter_shift_adj_.value(event->key());
      letter_shift_refs_.erase(ref_it);
      letter_shift_adj_.remove(event->key());
      if (adj == QtInput::LetterShiftAdjust::AddShift) {
        emit keyUp(VK_LSHIFT, 0);
      } else if (adj == QtInput::LetterShiftAdjust::RemoveShift) {
        emit keyDown(VK_LSHIFT, 0);
      }
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
    letter_shift_refs_.clear();
    letter_shift_adj_.clear();

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
