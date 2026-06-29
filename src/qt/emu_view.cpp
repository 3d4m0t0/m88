#include "emu_view.h"

#include "../linux/shared_framebuffer_draw.h"
#include "draw.h"
#include "emu_gl_view.h"
#include "qt_gl_config.h"
#include "qt_host_input.h"
#include "qt_input.h"
#include "shared_rgba_framebuffer.h"

#include "../linux_compat/winkeys.h"

#include "../linux/half_kana_ime.h"

#include <QCoreApplication>
#include <QEventLoop>
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
#include <QScreen>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

constexpr int kImeInlineBarHeight = 28;

bool EmuView::imeInlineEnabled() const {
  return ime_session_active_ && ime_kana_available_;
}

QRect EmuView::imeInlineBarRect() const {
  return QRect(0, height() - kImeInlineBarHeight, width(), kImeInlineBarHeight);
}

void EmuView::invalidateImeInlineBar() {
  if (height() > 0 && width() > 0) {
    update(imeInlineBarRect());
  }
}

bool EmuView::imeComposing() const {
  return imeInlineEnabled() && (ime_block_keys_ || !ime_preedit_.isEmpty());
}

bool EmuView::passKeyToIme(const QKeyEvent& event) const {
  (void)event;
  return imeInlineEnabled() && !ime_preedit_.isEmpty();
}

bool EmuView::sendSpaceToGuest(const QKeyEvent& event) const {
  if (event.key() != Qt::Key_Space || event.isAutoRepeat()) {
    return false;
  }
  if (HostShortcutModifiers(event)) {
    return false;
  }
  // While preedit is active, Space belongs to the host IME (e.g. fullwidth space).
  return !imeInlineEnabled() || ime_preedit_.isEmpty();
}

EmuView::EmuView(QWidget* parent) : QWidget(parent) {
  setFocusPolicy(Qt::StrongFocus);
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAutoFillBackground(false);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  vsync_timer_ = new QTimer(this);
  vsync_timer_->setTimerType(Qt::PreciseTimer);
  connect(vsync_timer_, &QTimer::timeout, this, &EmuView::onSoftwareVsyncTick);
}

void EmuView::attachFramebuffer(SharedFramebufferDraw* draw) { draw_ = draw; }

void EmuView::attachRgbaFramebuffer(SharedRgbaFramebuffer* rgba_framebuffer) {
  rgba_framebuffer_ = rgba_framebuffer;
  if (gl_view_) {
    gl_view_->attachFramebuffer(rgba_framebuffer);
  }
}

void EmuView::setScale(int scale) {
  scale_ = std::max(1, scale);
  if (gl_view_) {
    gl_view_->setScale(scale_);
  }
  scaled_pixmap_ = QPixmap();
  scaled_pixmap_serial_ = 0;
  updateGeometry();
  update();
}

void EmuView::setForce480Layout(bool enabled) {
  if (force480_layout_ == enabled) {
    return;
  }
  force480_layout_ = enabled;
  if (gl_view_) {
    gl_view_->setForce480Layout(force480_layout_);
  }
  update();
}

void EmuView::setSuppressMenu(bool enabled) {
  QtInput::SetSuppressMenu(enabled);
}

void EmuView::setImeKanaAvailable(bool available) {
  if (ime_kana_available_ == available) {
    return;
  }
  ime_kana_available_ = available;
  if (!available) {
    applyImeSessionActive(false);
    setAttribute(Qt::WA_InputMethodEnabled, false);
    return;
  }
  attachHostInputMethod(true);
}

void EmuView::syncImeSessionFromHost(bool active) {
  if (!ime_kana_available_) {
    active = false;
  }
  applyImeSessionActive(active);
}

void EmuView::setImeSessionActive(bool active) {
  syncImeSessionFromHost(active);
}

void EmuView::attachHostInputMethod(bool available) {
  setAttribute(Qt::WA_InputMethodEnabled, available);
  if (!available) {
    return;
  }
  reconnectHostInputMethod();
}

void EmuView::reconnectHostInputMethod() {
  if (!ime_kana_available_ || reconnecting_im_) {
    return;
  }
  if (hasFocus() && testAttribute(Qt::WA_InputMethodEnabled)) {
    reconnecting_im_ = true;
    ime_internal_focus_shift_ = true;
    clearFocus();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    setFocus(Qt::OtherFocusReason);
    ime_internal_focus_shift_ = false;
    reconnecting_im_ = false;
  }
  refreshHostInputMethod();
}

void EmuView::applyImeSessionActive(bool active) {
  if (ime_session_active_ == active) {
    return;
  }
  ime_session_active_ = active;
  if (!active) {
    ime_preedit_.clear();
    ime_block_keys_ = false;
    if (draw_) {
      draw_->SetImePreedit("");
    }
    if (QInputMethod* im = QGuiApplication::inputMethod()) {
      im->hide();
      im->reset();
    }
  }
  refreshHostInputMethod();
  invalidateImeInlineBar();
  emit imeSessionChanged(active);
}

void EmuView::refreshHostInputMethod() {
  if (QInputMethod* im = QGuiApplication::inputMethod()) {
    im->update(Qt::ImQueryAll);
  }
}

bool EmuView::consumeHostImeHotkey(QKeyEvent* event) {
  return tryHostImeHotkey(event);
}

bool EmuView::tryHostImeHotkey(QKeyEvent* event) {
  if (!event || !fcitx_status_ || !ime_kana_available_) {
    return false;
  }
  if (event->type() == QEvent::KeyPress) {
    if (!fcitx_status_->matchesTriggerKey(*event)) {
      return false;
    }
    fcitx_status_->toggleInputMethod();
    host_ime_hotkey_armed_ = true;
    event->accept();
    emit hostImeHotkeyPressed();
    return true;
  }
  if (event->type() == QEvent::KeyRelease && host_ime_hotkey_armed_) {
    host_ime_hotkey_armed_ = false;
    event->accept();
    return true;
  }
  return false;
}

void EmuView::setHostInput(QtHostInput::Host* host_input) {
  host_input_ = host_input;
}

void EmuView::setFcitxStatus(FcitxStatus* fcitx_status) {
  fcitx_status_ = fcitx_status;
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
  return QSize(emu_wid_ * scale_, emu_hei_ * scale_);
}

void EmuView::recreateGlView(const QSurfaceFormat& fmt) {
  QSurfaceFormat::setDefaultFormat(fmt);
  if (gl_view_) {
    layout()->removeWidget(gl_view_);
    delete gl_view_;
    gl_view_ = nullptr;
  }
  gl_view_ = new EmuGlView(this);
  gl_view_->attachFramebuffer(rgba_framebuffer_);
  gl_view_->setScale(scale_);
  gl_view_->setForce480Layout(force480_layout_);
  static_cast<QVBoxLayout*>(layout())->addWidget(gl_view_);
}

void EmuView::decideRenderBackend() {
  if (gl_mode_decided_) {
    return;
  }
  QWidget* top = window();
  if (!top || !top->isVisible()) {
    return;
  }
  gl_mode_decided_ = true;

  for (const QSurfaceFormat& fmt : M88QtGlFallbackFormats()) {
    recreateGlView(fmt);
    gl_view_->show();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    if (gl_view_->isValid() && gl_view_->glReady()) {
      use_gl_ = true;
      vsync_timer_->stop();
      if (fmt.swapInterval() > 0) {
        std::fprintf(stderr, "M88: display VSync enabled (swap interval %d).\n",
                    fmt.swapInterval());
      }
      std::fprintf(stderr, "M88: OpenGL display backend enabled (%s).\n",
                   M88QtGlFormatLabel(fmt));
      return;
    }
  }

  use_gl_ = false;
  if (gl_view_) {
    layout()->removeWidget(gl_view_);
    delete gl_view_;
    gl_view_ = nullptr;
  }
  startSoftwareVsync();
  std::fprintf(stderr, "M88: OpenGL unavailable, using software rendering.\n");
}

void EmuView::startSoftwareVsync() {
  double hz = 60.0;
  if (window() && window()->screen()) {
    hz = window()->screen()->refreshRate();
  }
  if (hz <= 0.0) {
    hz = 60.0;
  }
  const int interval_ms = std::max(1, static_cast<int>(std::lround(1000.0 / hz)));
  vsync_timer_->setInterval(interval_ms);
  vsync_timer_->start();
  std::fprintf(stderr, "M88: software display VSync ~%.1f Hz (%d ms).\n", hz, interval_ms);
}

void EmuView::onSoftwareVsyncTick() {
  if (!software_present_pending_) {
    return;
  }
  software_present_pending_ = false;
  update();
}

void EmuView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  decideRenderBackend();
}

void EmuView::syncImeOverlay() {
  if (draw_ && imeInlineEnabled() && ime_preedit_.isEmpty()) {
    ime_preedit_ = QString::fromUtf8(draw_->GetImePreedit());
  }
  if (use_gl_ && gl_view_) {
    gl_view_->setImePreedit(imeInlineEnabled() ? ime_preedit_ : QString());
  } else {
    invalidateImeInlineBar();
  }
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
  if (!gl_mode_decided_) {
    decideRenderBackend();
  }

  if (use_gl_) {
    syncImeOverlay();
    if (gl_view_) {
      gl_view_->refreshFrame();
    }
    return;
  }

  refreshFrameSoftware();
}

void EmuView::refreshFrameSoftware() {
  if (!draw_) {
    return;
  }

  syncImeOverlay();

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
      invalidateImeInlineBar();
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
  emu_wid_ = iw;
  emu_hei_ = ih;
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

  if (scale_ > 1) {
    if (serial != scaled_pixmap_serial_) {
      scaled_pixmap_ = QPixmap::fromImage(indices_.scaled(iw * scale_, ih * scale_,
                                                           Qt::IgnoreAspectRatio,
                                                           Qt::FastTransformation));
      scaled_pixmap_serial_ = serial;
    }
  } else {
    scaled_pixmap_ = QPixmap();
    scaled_pixmap_serial_ = 0;
  }

  software_present_pending_ = true;
  if (!vsync_timer_->isActive()) {
    update();
  }
}

void EmuView::paintEvent(QPaintEvent* /*event*/) {
  if (use_gl_) {
    return;
  }

  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);

  if (!indices_.isNull()) {
    const int dst_w = indices_.width() * scale_;
    const int dst_h = indices_.height() * scale_;
    const int box_h = force480_layout_ ? 480 * scale_ : dst_h;
    const int x = (width() - dst_w) / 2;
    const int y = (height() - box_h) / 2 + (box_h - dst_h) / 2;
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (!scaled_pixmap_.isNull()) {
      painter.drawPixmap(QRect(x, y, dst_w, dst_h), scaled_pixmap_);
    } else {
      painter.drawImage(QRect(x, y, dst_w, dst_h), indices_);
    }
  }

  if (imeInlineEnabled() && !ime_preedit_.isEmpty()) {
    const QRect bar = imeInlineBarRect();
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

bool EmuView::isAsciiOnly(const QString& text) {
  if (text.isEmpty()) {
    return false;
  }
  for (const QChar ch : text) {
    const ushort u = ch.unicode();
    if (u < 0x20 || u > 0x7e) {
      return false;
    }
  }
  return true;
}

void EmuView::injectAsciiCommit(const QString& text) {
  for (const QChar ch : text) {
    const uint vk = QtInput::VkFromAsciiChar(ch);
    if (!vk) {
      continue;
    }
    const uint32 kd = QtInput::KeyDataForAsciiChar(ch);
    emit keyDown(vk, kd);
    emit keyUp(vk, kd);
  }
}

bool EmuView::handleGuestKeyPress(QKeyEvent* event) {
  if (!event) {
    return false;
  }
  if (tryHostImeHotkey(event)) {
    return true;
  }
  if (ime_session_active_) {
    return false;
  }
  if (event->modifiers().testFlag(Qt::AltModifier) &&
      (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
    event->accept();
    return true;
  }
  if (HostShortcutModifiers(*event)) {
    event->ignore();
    return false;
  }
  if (passKeyToIme(*event)) {
    return false;
  }
  if (sendSpaceToGuest(*event)) {
    if (!event->isAutoRepeat()) {
      emit keyDown(VK_SPACE, QtInput::KeyDataFromEvent(*event));
      space_pending_guest_ = true;
    }
    event->accept();
    return true;
  }
  if (!event->isAutoRepeat()) {
    uint vk = QtInput::VkFromKeyEvent(*event);
    if (event->key() == Qt::Key_Space) {
      vk = VK_SPACE;
    }
    if (vk == 0 && !event->text().isEmpty()) {
      vk = QtInput::VkFromAsciiChar(event->text()[0]);
    }
    if (vk != 0) {
      emit keyDown(vk, QtInput::KeyDataFromEvent(*event));
      event->accept();
      return true;
    }
  }
  return false;
}

bool EmuView::handleGuestKeyRelease(QKeyEvent* event) {
  if (!event) {
    return false;
  }
  if (tryHostImeHotkey(event)) {
    return true;
  }
  if (ime_session_active_) {
    return false;
  }
  if (HostShortcutModifiers(*event)) {
    event->ignore();
    return false;
  }
  if (passKeyToIme(*event)) {
    return false;
  }
  if (event->key() == Qt::Key_Space && space_pending_guest_) {
    if (!event->isAutoRepeat()) {
      emit keyUp(VK_SPACE, QtInput::KeyExtended(*event));
      space_pending_guest_ = false;
    }
    event->accept();
    return true;
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
    return false;
  }
  if (QtInput::IsHostImeModifierKey(event->key())) {
    event->ignore();
    return false;
  }
  if (!event->isAutoRepeat()) {
    uint vk = QtInput::VkFromKeyEvent(*event);
    if (vk == 0 && !event->text().isEmpty()) {
      vk = QtInput::VkFromAsciiChar(event->text()[0]);
    }
    if (vk) {
      emit keyUp(vk, QtInput::KeyDataFromEvent(*event));
      event->accept();
      return true;
    }
  }
  return false;
}

bool EmuView::event(QEvent* event) {
  if (event->type() == QEvent::KeyPress) {
    if (handleGuestKeyPress(static_cast<QKeyEvent*>(event))) {
      return true;
    }
  } else if (event->type() == QEvent::KeyRelease) {
    if (handleGuestKeyRelease(static_cast<QKeyEvent*>(event))) {
      return true;
    }
  }
  if (event->type() == QEvent::ShortcutOverride) {
    auto* key = static_cast<QKeyEvent*>(event);
    if (fcitx_status_ && fcitx_status_->matchesTriggerKey(*key)) {
      key->accept();
      return true;
    }
    if (key->modifiers().testFlag(Qt::AltModifier) &&
        (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
      key->accept();
      return true;
    }
    if (HostShortcutModifiers(*key)) {
      key->ignore();
      return false;
    }
    if (ime_session_active_) {
      key->ignore();
      return false;
    }
    if (imeComposing()) {
      return QWidget::event(event);
    }
    if (QtInput::VkFromKeyEvent(*key) ||
        (!key->text().isEmpty() && QtInput::VkFromAsciiChar(key->text()[0]))) {
      key->accept();
      return true;
    }
  }
  return QWidget::event(event);
}

void EmuView::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  if (ime_kana_available_) {
    refreshHostInputMethod();
  }
  emit emuFocusReceived();
}

void EmuView::focusOutEvent(QFocusEvent* event) {
  space_pending_guest_ = false;
  if (!ime_internal_focus_shift_) {
    emit flushGuestKeys();
    emit clearHostModifiers();
  }
  QWidget::focusOutEvent(event);
}

void EmuView::keyPressEvent(QKeyEvent* event) {
  if (handleGuestKeyPress(event)) {
    return;
  }
  QWidget::keyPressEvent(event);
}

void EmuView::keyReleaseEvent(QKeyEvent* event) {
  if (handleGuestKeyRelease(event)) {
    return;
  }
  QWidget::keyReleaseEvent(event);
}

void EmuView::inputMethodEvent(QInputMethodEvent* event) {
  if (!imeInlineEnabled()) {
    event->accept();
    return;
  }

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
    if (isAsciiOnly(commit)) {
      std::vector<uint16_t> hw;
      if (!HalfKanaIme::CommitUtf8ToHalfKana(commit.toUtf8().constData(), &hw) || hw.empty()) {
        injectAsciiCommit(commit);
        event->accept();
        invalidateImeInlineBar();
        return;
      }
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
  invalidateImeInlineBar();
}

QVariant EmuView::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImEnabled) {
    return ime_session_active_ && ime_kana_available_;
  }
  if (!imeInlineEnabled()) {
    if (query == Qt::ImCursorRectangle) {
      return QRect();
    }
  } else if (query == Qt::ImCursorRectangle) {
    return imeInlineBarRect();
  }
  return QWidget::inputMethodQuery(query);
}
