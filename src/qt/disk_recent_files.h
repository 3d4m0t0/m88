#pragma once

#include <QString>
#include <QStringList>

// Recent disk image paths (QSettings), newest first, max 10 entries.
class DiskRecentFiles {
 public:
  static constexpr int kMaxEntries = 10;

  static QStringList paths();
  static void add(const QString& path);

 private:
  static QString normalizedPath(const QString& path);
};
