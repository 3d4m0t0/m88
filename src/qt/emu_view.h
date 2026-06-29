#pragma once

#include <QImage>
#include <QWidget>

#include "draw.h"
#include "fcitx_status.h"
#include "qt_input.h"

class SharedFramebufferDraw;

namespace QtHostInput {
class Host;
}

class EmuView : public QWidget {
  Q_OBJECT

public:
  explicit EmuView(QWidget* parent = nullptr);

  void attachFramebuffer(SharedFramebufferDraw* draw);
  void setScale(int scale);
  void setForce480Layout(bool enabled);
  void setSuppressMenu(bool enabled);
  // Host half-kana IME available (Configure); does not enable Qt IM on the playfield.
  void setImeKanaAvailable(bool available);
  void setImeSessionActive(bool active);
  bool imeSessionActive() const { return ime_session_active_; }
  void syncImeSessionFromHost(bool active);
  void setHostInput(QtHostInput::Host* host_input);
  void setFcitxStatus(FcitxStatus* fcitx_status);
  void setMouseCapture(bool enabled);

  // Application event filter: handle before Qt IM (KDE/fcitx may swallow KeyPress).
  bool handleGuestKeyPress(QKeyEvent* event);
  bool handleGuestKeyRelease(QKeyEvent* event);
  // Intercept fcitx trigger keys before the Qt IM module (fcitx5-qt) on focus return.
  bool consumeHostImeHotkey(QKeyEvent* event);
  void reconnectHostInputMethod();

public slots:
  void refreshFrame();

protected:
  bool event(QEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void keyReleaseEvent(QKeyEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  QSize sizeHint() const override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;

signals:
  void keyDown(quint32 vk, quint32 keydata);
  void keyUp(quint32 vk, quint32 keydata);
  void clearHostModifiers();
  void flushGuestKeys();
  void imeCommit(const QString& utf8);
  void imeSessionChanged(bool active);
  void emuFocusReceived();
  void hostImeHotkeyPressed();

private:
  QVector<QRgb> colorTableFromPalette() const;
  bool imeComposing() const;
  bool passKeyToIme(const QKeyEvent& event) const;
  bool sendSpaceToGuest(const QKeyEvent& event) const;
  void injectAsciiCommit(const QString& text);
  static bool isAsciiOnly(const QString& text);
  void applyImeSessionActive(bool active);
  void attachHostInputMethod(bool available);
  void refreshHostInputMethod();
  bool tryHostImeHotkey(QKeyEvent* event);
  bool imeInlineEnabled() const;
  QRect imeInlineBarRect() const;
  void invalidateImeInlineBar();

  SharedFramebufferDraw* draw_ = nullptr;
  QImage indices_;
  Draw::Palette palette_[256]{};
  int scale_ = 2;
  bool force480_layout_ = false;
  QString ime_preedit_;

  bool ime_block_keys_ = false;
  bool ime_kana_available_ = false;
  bool ime_session_active_ = false;
  bool ime_internal_focus_shift_ = false;
  bool host_ime_hotkey_armed_ = false;
  bool reconnecting_im_ = false;
  bool space_pending_guest_ = false;
  uint64_t last_frame_serial_ = 0;
  uint64_t last_palette_serial_ = 0;
  QtHostInput::Host* host_input_ = nullptr;
  FcitxStatus* fcitx_status_ = nullptr;
  bool mouse_capture_ = false;

  void updateMouseButtons(Qt::MouseButtons buttons);
};
