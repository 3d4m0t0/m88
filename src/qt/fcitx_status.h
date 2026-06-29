#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include <Qt>

class QKeyEvent;

// fcitx5/fcitx4 global IM state (session D-Bus only).
// TODO(ibus): Mirror host IME on/off via ibus D-Bus the same way; without sync the
// Qt IM module steals direct keys when enabled and half-width alnum breaks.
class FcitxStatus : public QObject {
  Q_OBJECT

public:
  explicit FcitxStatus(QObject* parent = nullptr);

  bool available() const { return available_; }
  QString currentInputMethod() const { return current_im_; }
  int state() const { return state_; }
  // Japanese IM active (any non-keyboard-* engine; mozc/skk/anthy preserved by fcitx).
  bool hostKanaInputActive() const;
  QString statusLine() const;
  QString toolTipText() const;

  bool matchesTriggerKey(const QKeyEvent& event) const;
  bool toggleInputMethod();

public slots:
  void refresh();

signals:
  void changed();

private:
  struct TriggerKey {
    Qt::KeyboardModifiers mods = Qt::NoModifier;
    int key = 0;
  };

  bool queryFcitx5();
  bool queryFcitx4();
  void loadTriggerKeys();
  static bool parseFcitxKeySpec(const QString& spec, TriggerKey* out);

  bool available_ = false;
  QString current_im_;
  int state_ = -1;
  QVector<TriggerKey> trigger_keys_;
};
