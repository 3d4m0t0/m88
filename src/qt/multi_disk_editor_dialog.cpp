#include "multi_disk_editor_dialog.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>
#include <unistd.h>

namespace {

#define MD_TR(source) QCoreApplication::translate("MultiDiskEditorDialog", source)

void SetWorkingDirectory(const QString& dir) {
  if (dir.isEmpty()) {
    return;
  }
  const QByteArray utf8 = QFileInfo(dir).absoluteFilePath().toUtf8();
  if (chdir(utf8.constData()) == 0) {
    QDir::setCurrent(QString::fromLocal8Bit(utf8.constData()));
  }
}

QString DiskTypeLabel(uint type) {
  switch (type) {
    case 0:
      return MD_TR("2D");
    case 2:
      return MD_TR("2HD");
    default:
      return MD_TR("2DD");
  }
}

bool PromptBlankDisk(QWidget* parent, QString* title_out, uint* type_out) {
  QDialog dlg(parent);
  dlg.setWindowTitle(MD_TR("New blank disk"));

  auto* title_edit = new QLineEdit(&dlg);
  title_edit->setMaxLength(16);

  auto* type_combo = new QComboBox(&dlg);
  type_combo->addItem(MD_TR("2D"), 0);
  type_combo->addItem(MD_TR("2DD"), 1);
  type_combo->addItem(MD_TR("2HD"), 2);
  type_combo->setCurrentIndex(1);

  auto* form = new QFormLayout(&dlg);
  form->addRow(MD_TR("Title:"), title_edit);
  form->addRow(MD_TR("Type:"), type_combo);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                       &dlg);
  form->addRow(buttons);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) {
    return false;
  }

  *title_out = title_edit->text().trimmed();
  if (title_out->isEmpty()) {
    *title_out = MD_TR("Untitled");
  }
  *type_out = static_cast<uint>(type_combo->currentData().toInt());
  return true;
}

}  // namespace

MultiDiskEditorDialog::MultiDiskEditorDialog(QWidget* parent)
    : QDialog(parent) {
  setWindowTitle(tr("Multi-disk image editor"));
  resize(760, 440);

  auto* root = new QVBoxLayout(this);

  auto* top_row = new QHBoxLayout();
  auto* new_button = new QPushButton(tr("New"), this);
  auto* load_button = new QPushButton(tr("Load"), this);
  top_row->addWidget(new_button);
  top_row->addWidget(load_button);
  top_row->addStretch(1);
  root->addLayout(top_row);

  auto* output_dir_row = new QHBoxLayout();
  output_dir_row->addWidget(new QLabel(tr("Output directory:"), this));
  output_dir_edit_ = new QLineEdit(this);
  output_dir_edit_->setPlaceholderText(tr("directory path"));
  output_dir_row->addWidget(output_dir_edit_, 1);
  auto* output_dir_browse = new QPushButton(tr("Browse..."), this);
  output_dir_row->addWidget(output_dir_browse);
  root->addLayout(output_dir_row);

  auto* output_name_row = new QHBoxLayout();
  output_name_row->addWidget(new QLabel(tr("Output file:"), this));
  output_name_edit_ = new QLineEdit(this);
  output_name_edit_->setPlaceholderText(tr("filename.d88"));
  output_name_row->addWidget(output_name_edit_, 1);
  root->addLayout(output_name_row);

  scroll_area_ = new QScrollArea(this);
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto* scroll_content = new QWidget(scroll_area_);
  rows_layout_ = new QVBoxLayout(scroll_content);
  rows_layout_->setContentsMargins(0, 0, 0, 0);
  rows_layout_->setSpacing(2);

  auto* header_row = new QWidget(scroll_content);
  auto* header_layout = new QHBoxLayout(header_row);
  header_layout->setContentsMargins(0, 0, 0, 0);
  auto* header_num = new QLabel(QStringLiteral("#"), header_row);
  header_num->setMinimumWidth(28);
  auto* header_name = new QLabel(tr("Name"), header_row);
  header_name->setMinimumWidth(140);
  auto* header_source = new QLabel(tr("Source"), header_row);
  header_source->setMinimumWidth(220);
  header_layout->addWidget(header_num);
  header_layout->addWidget(header_name);
  header_layout->addWidget(header_source, 1);
  header_layout->addSpacing(3 * 80);
  rows_layout_->addWidget(header_row);

  for (int i = 0; i < kMaxSlots; ++i) {
    SlotRowWidgets& row = rows_[i];
    row.row = new QWidget(scroll_content);
    auto* layout = new QHBoxLayout(row.row);
    layout->setContentsMargins(0, 0, 0, 0);

    row.number_label = new QLabel(QStringLiteral("%1.").arg(i + 1), row.row);
    row.number_label->setMinimumWidth(28);
    row.title_edit = new QLineEdit(row.row);
    row.title_edit->setMaxLength(16);
    row.title_edit->setMinimumWidth(140);
    row.title_edit->setEnabled(false);
    row.source_label = new QLabel(row.row);
    row.source_label->setMinimumWidth(220);
    row.browse_button = new QPushButton(tr("Browse..."), row.row);
    row.blank_button = new QPushButton(tr("Blank"), row.row);
    row.clear_button = new QPushButton(tr("Clear"), row.row);

    layout->addWidget(row.number_label);
    layout->addWidget(row.title_edit);
    layout->addWidget(row.source_label, 1);
    layout->addWidget(row.browse_button);
    layout->addWidget(row.blank_button);
    layout->addWidget(row.clear_button);

    rows_layout_->addWidget(row.row);

    connect(row.browse_button, &QPushButton::clicked, this, [this, i]() {
      browseSlot(i);
    });
    connect(row.blank_button, &QPushButton::clicked, this, [this, i]() {
      blankSlot(i);
    });
    connect(row.clear_button, &QPushButton::clicked, this, [this, i]() {
      clearSlot(i);
    });
    connect(row.title_edit, &QLineEdit::textEdited, this, [this]() {
      markDirty();
    });
  }
  rows_layout_->addStretch(1);
  scroll_area_->setWidget(scroll_content);
  root->addWidget(scroll_area_, 1);

  auto* buttons = new QDialogButtonBox(this);
  ok_button_ = buttons->addButton(QDialogButtonBox::Ok);
  apply_button_ = buttons->addButton(QDialogButtonBox::Apply);
  buttons->addButton(QDialogButtonBox::Cancel);
  root->addWidget(buttons);

  connect(new_button, &QPushButton::clicked, this, &MultiDiskEditorDialog::onNew);
  connect(load_button, &QPushButton::clicked, this, &MultiDiskEditorDialog::onLoad);
  connect(output_dir_browse, &QPushButton::clicked, this,
          &MultiDiskEditorDialog::browseOutputDirectory);
  connect(output_dir_edit_, &QLineEdit::textEdited, this, [this]() { markDirty(); });
  connect(output_name_edit_, &QLineEdit::textEdited, this, [this]() { markDirty(); });
  connect(output_dir_edit_, &QLineEdit::editingFinished, this, [this]() {
    const QString raw = output_dir_edit_->text().trimmed();
    if (raw.isEmpty()) {
      output_dir_.clear();
      output_dir_edit_->clear();
    } else {
      output_dir_ = normalizeOutputDirectory(raw);
      output_dir_edit_->setText(output_dir_);
    }
    syncOutputPath();
  });
  connect(output_name_edit_, &QLineEdit::editingFinished, this, [this]() {
    const QString raw = output_name_edit_->text().trimmed();
    if (raw.isEmpty()) {
      output_name_.clear();
      output_name_edit_->clear();
    } else {
      output_name_ = normalizeOutputFileName(raw);
      output_name_edit_->setText(output_name_);
    }
    syncOutputPath();
  });
  connect(buttons, &QDialogButtonBox::accepted, this, &MultiDiskEditorDialog::onOk);
  connect(apply_button_, &QPushButton::clicked, this, &MultiDiskEditorDialog::onApply);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  refreshSlotRows();
  refreshRowVisibility();
  updateApplyEnabled();
}

void MultiDiskEditorDialog::markDirty() {
  dirty_ = true;
  updateApplyEnabled();
}

void MultiDiskEditorDialog::clearDirty() {
  dirty_ = false;
  updateApplyEnabled();
}

void MultiDiskEditorDialog::updateApplyEnabled() {
  if (apply_button_) {
    apply_button_->setEnabled(dirty_);
  }
}

void MultiDiskEditorDialog::clearOutputFields() {
  output_dir_.clear();
  output_name_.clear();
  output_path_.clear();
  if (output_dir_edit_) {
    output_dir_edit_->clear();
  }
  if (output_name_edit_) {
    output_name_edit_->clear();
  }
}

void MultiDiskEditorDialog::clearAllSlots() {
  for (SlotData& slot : slots_) {
    slot = SlotData {};
  }
  clearOutputFields();
  refreshSlotRows();
  refreshRowVisibility();
}

QString MultiDiskEditorDialog::normalizeOutputDirectory(const QString& raw) const {
  QString path = raw.trimmed();
  if (path.isEmpty()) {
    return path;
  }

  QFileInfo info(path);
  if (!info.isAbsolute()) {
    path = QFileInfo(QDir::currentPath(), path).absoluteFilePath();
  } else {
    path = info.absoluteFilePath();
  }
  if (info.isFile()) {
    path = QFileInfo(path).absolutePath();
  }
  return QDir(path).absolutePath();
}

QString MultiDiskEditorDialog::normalizeOutputFileName(const QString& raw) const {
  QString name = raw.trimmed();
  if (name.isEmpty()) {
    return name;
  }

  name = QFileInfo(name).fileName();
  if (QFileInfo(name).suffix().isEmpty()) {
    name += QStringLiteral(".d88");
  }
  return name;
}

QString MultiDiskEditorDialog::combinedOutputPath() const {
  if (output_dir_.isEmpty() || output_name_.isEmpty()) {
    return QString();
  }
  return QFileInfo(output_dir_, output_name_).absoluteFilePath();
}

void MultiDiskEditorDialog::syncOutputPath() {
  output_path_ = combinedOutputPath();
}

void MultiDiskEditorDialog::setOutputFromFullPath(const QString& path) {
  const QFileInfo info(path);
  output_dir_ = info.absolutePath();
  output_name_ = normalizeOutputFileName(info.fileName());
  if (output_dir_edit_) {
    output_dir_edit_->setText(output_dir_);
  }
  if (output_name_edit_) {
    output_name_edit_->setText(output_name_);
  }
  syncOutputPath();
}

QString MultiDiskEditorDialog::readDiskTitle(const QString& path,
                                             int disk_index) const {
  D88::DiskCatalogInfo catalog[DiskImageHolder::max_disks];
  int ndisks = 0;
  const QByteArray utf8 = QFileInfo(path).absoluteFilePath().toUtf8();
  if (!ReadDiskImageCatalog(utf8.constData(), catalog, DiskImageHolder::max_disks,
                            &ndisks) ||
      disk_index < 0 || disk_index >= ndisks) {
    return QString();
  }
  return QString::fromLocal8Bit(catalog[disk_index].title).trimmed();
}

void MultiDiskEditorDialog::loadFromFile(const QString& path) {
  if (path.isEmpty()) {
    return;
  }

  D88::DiskCatalogInfo catalog[DiskImageHolder::max_disks];
  int ndisks = 0;
  const QByteArray utf8 = QFileInfo(path).absoluteFilePath().toUtf8();
  if (!ReadDiskImageCatalog(utf8.constData(), catalog, DiskImageHolder::max_disks,
                            &ndisks)) {
    QMessageBox::warning(this, tr("Load disk image"),
                         tr("Could not read disk image:\n%1").arg(path));
    return;
  }

  clearAllSlots();
  const QString source_path = QFileInfo(path).absoluteFilePath();
  setOutputFromFullPath(source_path);
  SetWorkingDirectory(output_dir_);

  for (int i = 0; i < ndisks && i < kMaxSlots; ++i) {
    SlotData& slot = slots_[i];
    slot.kind = MultiDiskSlot::Kind::FromFile;
    slot.file_path = source_path;
    slot.file_disk_index = i;
    slot.title = QString::fromLocal8Bit(catalog[i].title).trimmed();
    slot.blank_type = 1;
  }

  refreshSlotRows();
  refreshRowVisibility();
  clearDirty();
}

void MultiDiskEditorDialog::syncSlotsFromUi() {
  for (int i = 0; i < kMaxSlots; ++i) {
    if (slots_[i].kind == MultiDiskSlot::Kind::Empty || !rows_[i].title_edit) {
      continue;
    }
    slots_[i].title = rows_[i].title_edit->text().trimmed();
  }
}

void MultiDiskEditorDialog::refreshSlotRows() {
  for (int i = 0; i < kMaxSlots; ++i) {
    SlotRowWidgets& row = rows_[i];
    const SlotData& slot = slots_[i];
    const bool active = slot.kind != MultiDiskSlot::Kind::Empty;

    if (row.title_edit) {
      row.title_edit->setEnabled(active);
      row.title_edit->setText(active ? slot.title : QString());
    }
    if (row.source_label) {
      row.source_label->setText(active ? slotSourceText(i) : tr("(empty)"));
    }
    if (row.browse_button) {
      row.browse_button->setEnabled(true);
    }
    if (row.blank_button) {
      row.blank_button->setEnabled(true);
    }
    if (row.clear_button) {
      row.clear_button->setEnabled(active);
    }
  }
}

int MultiDiskEditorDialog::lastUsedSlotIndex() const {
  for (int i = kMaxSlots - 1; i >= 0; --i) {
    if (slots_[i].kind != MultiDiskSlot::Kind::Empty) {
      return i;
    }
  }
  return -1;
}

int MultiDiskEditorDialog::visibleRowCount() const {
  const int last = lastUsedSlotIndex();
  return std::min(kMaxSlots, std::max(kMinVisibleRows, last + 2));
}

void MultiDiskEditorDialog::refreshRowVisibility() {
  const int visible = visibleRowCount();
  for (int i = 0; i < kMaxSlots; ++i) {
    if (rows_[i].row) {
      rows_[i].row->setVisible(i < visible);
    }
  }
}

QString MultiDiskEditorDialog::slotSourceText(int index) const {
  const SlotData& slot = slots_[index];
  switch (slot.kind) {
    case MultiDiskSlot::Kind::FromFile: {
      const QFileInfo info(slot.file_path);
      QString text = info.fileName();
      if (slot.file_disk_index > 0) {
        text += tr(" (disk %1)").arg(slot.file_disk_index + 1);
      }
      return text;
    }
    case MultiDiskSlot::Kind::Blank:
      return tr("Blank (%1)").arg(DiskTypeLabel(slot.blank_type));
    default:
      return tr("(empty)");
  }
}

MultiDiskSlot MultiDiskEditorDialog::toMultiDiskSlot(int index) const {
  MultiDiskSlot out {};
  const SlotData& slot = slots_[index];
  out.kind = slot.kind;
  if (slot.kind == MultiDiskSlot::Kind::FromFile) {
    const QByteArray path = QFileInfo(slot.file_path).absoluteFilePath().toUtf8();
    strncpy(out.file_path, path.constData(), sizeof(out.file_path) - 1);
    out.file_disk_index = slot.file_disk_index;
  } else if (slot.kind == MultiDiskSlot::Kind::Blank) {
    out.blank_type = slot.blank_type;
  }

  const QByteArray title = slot.title.toUtf8();
  strncpy(out.title, title.constData(), sizeof(out.title) - 1);
  return out;
}

void MultiDiskEditorDialog::onNew() {
  clearAllSlots();
  markDirty();
}

void MultiDiskEditorDialog::onLoad() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Load disk image"), QDir::currentPath(), tr(kDiskImageFilter));
  if (path.isEmpty()) {
    return;
  }
  loadFromFile(path);
}

void MultiDiskEditorDialog::browseOutputDirectory() {
  QString start = output_dir_edit_->text().trimmed();
  if (start.isEmpty()) {
    start = QDir::currentPath();
  } else {
    start = normalizeOutputDirectory(start);
  }

  const QString dir = QFileDialog::getExistingDirectory(
      this, tr("Select output directory"), start);
  if (dir.isEmpty()) {
    return;
  }

  output_dir_ = QDir(dir).absolutePath();
  output_dir_edit_->setText(output_dir_);
  syncOutputPath();
  SetWorkingDirectory(output_dir_);
  markDirty();
}

void MultiDiskEditorDialog::browseSlot(int index) {
  if (index < 0 || index >= kMaxSlots) {
    return;
  }

  const QString path = QFileDialog::getOpenFileName(
      this, tr("Select disk image"), QDir::currentPath(), tr(kDiskImageFilter));
  if (path.isEmpty()) {
    return;
  }

  SetWorkingDirectory(QFileInfo(path).absolutePath());

  SlotData& slot = slots_[index];
  slot.kind = MultiDiskSlot::Kind::FromFile;
  slot.file_path = QFileInfo(path).absoluteFilePath();
  slot.file_disk_index = 0;
  slot.title = readDiskTitle(slot.file_path, 0);
  slot.blank_type = 1;

  refreshSlotRows();
  refreshRowVisibility();
  markDirty();
}

void MultiDiskEditorDialog::blankSlot(int index) {
  if (index < 0 || index >= kMaxSlots) {
    return;
  }

  QString title;
  uint type = 1;
  if (!PromptBlankDisk(this, &title, &type)) {
    return;
  }

  SlotData& slot = slots_[index];
  slot.kind = MultiDiskSlot::Kind::Blank;
  slot.file_path.clear();
  slot.file_disk_index = 0;
  slot.title = title;
  slot.blank_type = type;

  refreshSlotRows();
  refreshRowVisibility();
  markDirty();
}

void MultiDiskEditorDialog::clearSlot(int index) {
  if (index < 0 || index >= kMaxSlots) {
    return;
  }
  slots_[index] = SlotData {};
  refreshSlotRows();
  refreshRowVisibility();
  markDirty();
}

bool MultiDiskEditorDialog::ensureOutputPath() {
  const QString dir_raw = output_dir_edit_->text().trimmed();
  const QString name_raw = output_name_edit_->text().trimmed();

  if (dir_raw.isEmpty()) {
    browseOutputDirectory();
    if (output_dir_.isEmpty()) {
      return false;
    }
  } else {
    output_dir_ = normalizeOutputDirectory(dir_raw);
    output_dir_edit_->setText(output_dir_);
  }

  if (name_raw.isEmpty()) {
    QMessageBox::warning(this, tr("Write multi-disk image"),
                         tr("Enter an output file name."));
    if (output_name_edit_) {
      output_name_edit_->setFocus();
    }
    return false;
  }

  output_name_ = normalizeOutputFileName(name_raw);
  output_name_edit_->setText(output_name_);
  syncOutputPath();
  return !output_path_.isEmpty();
}

bool MultiDiskEditorDialog::applyChanges(bool close_on_success) {
  syncSlotsFromUi();

  int disk_count = 0;
  for (const SlotData& slot : slots_) {
    if (slot.kind != MultiDiskSlot::Kind::Empty) {
      ++disk_count;
    }
  }
  if (disk_count <= 0) {
    QMessageBox::warning(this, tr("Write multi-disk image"),
                         tr("Add at least one disk image."));
    return false;
  }

  if (!ensureOutputPath()) {
    return false;
  }

  std::array<MultiDiskSlot, kMaxSlots> out_slots {};
  for (int i = 0; i < kMaxSlots; ++i) {
    out_slots[i] = toMultiDiskSlot(i);
  }

  const QByteArray out_utf8 = QFileInfo(output_path_).absoluteFilePath().toUtf8();
  if (!WriteMultiDiskImage(out_utf8.constData(), out_slots.data(), kMaxSlots,
                           nullptr)) {
    QMessageBox::critical(this, tr("Write multi-disk image"),
                          tr("Failed to write disk image:\n%1").arg(output_path_));
    return false;
  }

  SetWorkingDirectory(QFileInfo(output_path_).absolutePath());
  clearDirty();

  if (close_on_success) {
    accept();
  }
  return true;
}

void MultiDiskEditorDialog::onOk() {
  if (!dirty_) {
    accept();
    return;
  }
  applyChanges(true);
}

void MultiDiskEditorDialog::onApply() {
  applyChanges(false);
}
