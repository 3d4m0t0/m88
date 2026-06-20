#pragma once

#include <QDialog>

class ConfirmDialog : public QDialog {
  Q_OBJECT

public:
  enum class Kind { Reset, Exit };

  explicit ConfirmDialog(Kind kind, QWidget* parent = nullptr);

  static bool Ask(Kind kind, QWidget* parent);

private:
  QString titleFor(Kind kind) const;
  QString messageFor(Kind kind) const;
  void applyFixedLayout();
};
