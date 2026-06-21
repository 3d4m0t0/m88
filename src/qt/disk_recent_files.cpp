#include "disk_recent_files.h"

#include <QFileInfo>
#include <QSettings>

namespace {

constexpr const char* kSettingsGroup = "disk";
constexpr const char* kRecentKey = "recentFiles";

QStringList loadStored() {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  const QStringList stored = settings.value(QLatin1String(kRecentKey)).toStringList();
  settings.endGroup();
  return stored;
}

void saveStored(const QStringList& paths) {
  QSettings settings;
  settings.beginGroup(QLatin1String(kSettingsGroup));
  settings.setValue(QLatin1String(kRecentKey), paths);
  settings.endGroup();
}

}  // namespace

QString DiskRecentFiles::normalizedPath(const QString& path) {
  if (path.isEmpty()) {
    return {};
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    return {};
  }
  return info.canonicalFilePath();
}

QStringList DiskRecentFiles::paths() {
  QStringList out;
  out.reserve(kMaxEntries);
  for (const QString& entry : loadStored()) {
    const QString canonical = normalizedPath(entry);
    if (!canonical.isEmpty() && !out.contains(canonical)) {
      out.push_back(canonical);
    }
    if (out.size() >= kMaxEntries) {
      break;
    }
  }
  if (out != loadStored()) {
    saveStored(out);
  }
  return out;
}

void DiskRecentFiles::add(const QString& path) {
  const QString canonical = normalizedPath(path);
  if (canonical.isEmpty()) {
    return;
  }
  QStringList list = loadStored();
  list.removeAll(canonical);
  list.prepend(canonical);
  while (list.size() > kMaxEntries) {
    list.removeLast();
  }
  saveStored(list);
}
