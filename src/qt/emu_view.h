#pragma once

#include <QHash>
#include <QImage>
#include <QWidget>

#include "draw.h"
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
  void setHostInput(QtHostInput::Host* host_input);
  void setMouseCapture(bool enabled);

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

signals:
  void keyDown(quint32 vk, quint32 keydata);
  void keyUp(quint32 vk, quint32 keydata);
  void clearHostModifiers();
  void flushGuestKeys();
  void imeCommit(const QString& utf8);

private:
  QVector<QRgb> colorTableFromPalette() const;
  bool imeComposing() const;

  SharedFramebufferDraw* draw_ = nullptr;
  QImage indices_;
  Draw::Palette palette_[256]{};
  int scale_ = 2;
  bool force480_layout_ = false;
  QString ime_preedit_;
  QHash<int, int> letter_shift_refs_;
  QHash<int, QtInput::LetterShiftAdjust> letter_shift_adj_;

  bool ime_block_keys_ = false;
  uint64_t last_frame_serial_ = 0;
  uint64_t last_palette_serial_ = 0;
  QtHostInput::Host* host_input_ = nullptr;
  bool mouse_capture_ = false;

  void updateMouseButtons(Qt::MouseButtons buttons);
};
