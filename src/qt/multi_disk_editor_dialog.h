#pragma once

#include <QDialog>

#include <array>

#include "pc88/diskmgr.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class MultiDiskEditorDialog : public QDialog {
  Q_OBJECT

 public:
  explicit MultiDiskEditorDialog(QWidget* parent = nullptr);

 private slots:
  void onNew();
  void onLoad();
  void browseOutputDirectory();
  void browseSlot(int index);
  void blankSlot(int index);
  void clearSlot(int index);
  void onOk();
  void onApply();

 private:
  static constexpr int kMaxSlots = DiskImageHolder::max_disks;
  static constexpr int kMinVisibleRows = 8;

  struct SlotData {
    MultiDiskSlot::Kind kind = MultiDiskSlot::Kind::Empty;
    QString file_path;
    int file_disk_index = 0;
    QString title;
    uint blank_type = 1;
  };

  struct SlotRowWidgets {
    QWidget* row = nullptr;
    QLabel* number_label = nullptr;
    QLineEdit* title_edit = nullptr;
    QLabel* source_label = nullptr;
    QPushButton* browse_button = nullptr;
    QPushButton* blank_button = nullptr;
    QPushButton* clear_button = nullptr;
  };

  void clearAllSlots();
  void loadFromFile(const QString& path);
  void refreshSlotRows();
  void refreshRowVisibility();
  void syncSlotsFromUi();
  int lastUsedSlotIndex() const;
  int visibleRowCount() const;
  QString slotSourceText(int index) const;
  bool applyChanges(bool close_on_success);
  bool ensureOutputPath();
  QString normalizeOutputDirectory(const QString& raw) const;
  QString normalizeOutputFileName(const QString& raw) const;
  QString combinedOutputPath() const;
  void syncOutputPath();
  void setOutputFromFullPath(const QString& path);
  void clearOutputFields();
  void markDirty();
  void clearDirty();
  void updateApplyEnabled();
  MultiDiskSlot toMultiDiskSlot(int index) const;
  QString readDiskTitle(const QString& path, int disk_index) const;

  QString output_dir_;
  QString output_name_;
  QString output_path_;
  bool dirty_ = false;
  std::array<SlotData, kMaxSlots> slots_ {};
  std::array<SlotRowWidgets, kMaxSlots> rows_ {};

  QLineEdit* output_dir_edit_ = nullptr;
  QLineEdit* output_name_edit_ = nullptr;
  QScrollArea* scroll_area_ = nullptr;
  QVBoxLayout* rows_layout_ = nullptr;
  QPushButton* ok_button_ = nullptr;
  QPushButton* apply_button_ = nullptr;

  static constexpr const char* kDiskImageFilter =
      "8801 disk image (*.d88);;All files (*)";
};
