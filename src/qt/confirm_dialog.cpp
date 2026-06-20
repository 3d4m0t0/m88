#include "confirm_dialog.h"

#include "qt_platform.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

bool ConfirmDialog::Ask(Kind kind, QWidget* parent) {
  ConfirmDialog dlg(kind, parent);
  return dlg.exec() == QDialog::Accepted;
}

QString ConfirmDialog::titleFor(Kind kind) const {
  switch (kind) {
    case Kind::Reset:
      return tr("Reset");
    case Kind::Exit:
      return tr("Exit");
  }
  return QString();
}

QString ConfirmDialog::messageFor(Kind kind) const {
  switch (kind) {
    case Kind::Reset:
      return tr("Reset the emulated machine?");
    case Kind::Exit:
      return tr("Exit M88?");
  }
  return QString();
}

ConfirmDialog::ConfirmDialog(Kind kind, QWidget* parent) : QDialog(parent) {
  setWindowTitle(titleFor(kind));
  if (!M88QtAppIcon().isNull()) {
    setWindowIcon(M88QtAppIcon());
  }

  constexpr int kOuterMargin = 10;
  constexpr int kIconSize = 32;

  const QString message = messageFor(kind);

  auto* icon_label = new QLabel(this);
  const QIcon question_icon =
      style()->standardIcon(QStyle::SP_MessageBoxQuestion, nullptr, this);
  icon_label->setPixmap(question_icon.pixmap(kIconSize, kIconSize));
  icon_label->setFixedSize(kIconSize, kIconSize);

  auto* message_label = new QLabel(message, this);
  message_label->setWordWrap(false);

  auto* yes_button = new QPushButton(tr("&Yes"), this);
  auto* no_button = new QPushButton(tr("&No"), this);
  no_button->setDefault(true);
  no_button->setAutoDefault(true);
  connect(yes_button, &QPushButton::clicked, this, &QDialog::accept);
  connect(no_button, &QPushButton::clicked, this, &QDialog::reject);

  auto* message_row = new QHBoxLayout();
  message_row->setContentsMargins(0, 0, 0, 0);
  message_row->setSpacing(12);
  message_row->addWidget(icon_label, 0, Qt::AlignTop);
  message_row->addWidget(message_label, 1, Qt::AlignVCenter);

  auto* button_row = new QHBoxLayout();
  button_row->setContentsMargins(0, 0, 0, 0);
  button_row->setSpacing(6);
  button_row->addStretch();
  button_row->addWidget(yes_button);
  button_row->addWidget(no_button);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kOuterMargin, kOuterMargin, kOuterMargin, kOuterMargin);
  layout->setSpacing(12);
  layout->addLayout(message_row);
  layout->addLayout(button_row);

  applyFixedLayout();
}

void ConfirmDialog::applyFixedLayout() {
  if (QLayout* l = layout()) {
    l->activate();
    setFixedSize(l->sizeHint());
  }
}
