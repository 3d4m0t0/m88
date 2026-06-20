#include "about_dialog.h"

#include "qt_platform.h"

#include "../linux/m88_port_version.h"

#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QLabel* MakePlainLabel(const QString& text, QWidget* parent, bool wrap = false) {
  auto* label = new QLabel(text, parent);
  label->setWordWrap(wrap);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return label;
}

QLabel* MakeLinkLabel(const QString& url, QWidget* parent) {
  auto* label = new QLabel(
      QStringLiteral("<a href=\"%1\">%2</a>").arg(url, url), parent);
  label->setOpenExternalLinks(true);
  label->setWordWrap(false);
  label->setTextFormat(Qt::RichText);
  label->setTextInteractionFlags(Qt::TextBrowserInteraction);
  return label;
}

int MaxLineWidth(const QFontMetrics& fm, const QString& text) {
  int w = 0;
  for (const QString& line : text.split(QLatin1Char('\n'))) {
    w = std::max(w, fm.horizontalAdvance(line));
  }
  return w;
}

}  // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("About M88"));
  if (!M88QtAppIcon().isNull()) {
    setWindowIcon(M88QtAppIcon());
  }

  constexpr int kOuterMargin = 6;
  constexpr int kBodyHeight = 118;

  auto* ok_button = new QPushButton(tr("OK"), this);
  ok_button->setDefault(true);
  ok_button->setAutoDefault(true);
  connect(ok_button, &QPushButton::clicked, this, &QDialog::accept);

  auto* icon_label = new QLabel(this);
  icon_label->setPixmap(M88QtAppIcon().pixmap(32, 32));
  icon_label->setFixedSize(32, 32);

  const QString title_text =
      tr("M88 for Linux (Qt) (rel %1)\n"
         "PC-8801 series emulator.\n"
         "Copyright (C) 1998, 2003 cisc")
          .arg(QString::fromLatin1(M88_LINUX_QT_VER_STRING));

  const int ok_w = ok_button->sizeHint().width();
  const int side = kOuterMargin * 2 + icon_label->width() + 8 + ok_w + 16;
  const QFontMetrics fm(font());
  const int title_col_w = MaxLineWidth(fm, title_text);
  const int dialog_w = side + title_col_w;

  auto* title_label = new QLabel(title_text, this);
  title_label->setWordWrap(false);

  auto* header_text = new QVBoxLayout();
  header_text->setContentsMargins(0, 0, 0, 0);
  header_text->addWidget(title_label);
  header_text->addStretch();

  auto* header_row = new QHBoxLayout();
  header_row->setContentsMargins(0, 0, 0, 0);
  header_row->setSpacing(8);
  header_row->addWidget(icon_label, 0, Qt::AlignTop);
  header_row->addLayout(header_text, 1);
  header_row->addWidget(ok_button, 0, Qt::AlignTop);

  const QString kLinuxRepo = QStringLiteral("https://github.com/3d4m0t0/m88");
  const QString kForkRepo = QStringLiteral("https://github.com/rururutan/m88");
  const QString kOriginal = QStringLiteral("http://www.retropc.net/cisc/m88/");
  const QString kCredits =
      tr("Credits: FM sound (Tatsuya Sato fm.c), N80/SR (arearea)");

  const int body_inner_w = dialog_w - kOuterMargin * 2 - 4;

  auto* body_content = new QWidget(this);
  body_content->setFixedWidth(body_inner_w);
  auto* body_layout = new QVBoxLayout(body_content);
  body_layout->setContentsMargins(2, 2, 2, 2);
  body_layout->setSpacing(2);
  body_layout->addWidget(
      MakePlainLabel(tr("build date: %1").arg(QString::fromLatin1(__DATE__)),
                     body_content));
  body_layout->addWidget(MakePlainLabel(tr("GitHub:"), body_content));
  body_layout->addWidget(MakeLinkLabel(kLinuxRepo, body_content));
  body_layout->addWidget(MakePlainLabel(tr("Forked from:"), body_content));
  body_layout->addWidget(MakeLinkLabel(kForkRepo, body_content));
  body_layout->addWidget(MakePlainLabel(tr("Original:"), body_content));
  body_layout->addWidget(MakeLinkLabel(kOriginal, body_content));
  body_layout->addWidget(MakePlainLabel(kCredits, body_content, true));
  body_layout->addStretch();

  auto* scroll = new QScrollArea(this);
  scroll->setWidget(body_content);
  scroll->setWidgetResizable(false);
  scroll->setFrameShape(QFrame::StyledPanel);
  scroll->setFixedHeight(kBodyHeight);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(kOuterMargin, kOuterMargin, kOuterMargin, kOuterMargin);
  layout->setSpacing(6);
  layout->addLayout(header_row);
  layout->addWidget(scroll);

  const int header_h = std::max(
      icon_label->height(),
      title_label->sizeHint().height() + ok_button->sizeHint().height() / 4);
  setFixedSize(dialog_w, header_h + kBodyHeight + kOuterMargin * 2 + layout->spacing());
}
