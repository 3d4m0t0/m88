#include "m88_i18n.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>

#include <cstdio>
#include <cstring>

namespace {

const char* const kEmbeddedLocales[] = {"en", "ja"};

bool IsEmbeddedLocale(const QString& locale) {
  for (const char* embedded : kEmbeddedLocales) {
    if (locale == QLatin1String(embedded)) {
      return true;
    }
  }
  return false;
}

QString NormalizeLocaleTag(QString tag) {
  tag = tag.trimmed().replace(QLatin1Char('-'), QLatin1Char('_'));
  if (tag.compare(QStringLiteral("jp"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("ja");
  }
  if (tag.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0 ||
      tag.startsWith(QStringLiteral("zh_hans"), Qt::CaseInsensitive) ||
      tag.startsWith(QStringLiteral("zh_cn"), Qt::CaseInsensitive)) {
    return QStringLiteral("zh_CN");
  }
  if (tag.startsWith(QStringLiteral("zh_tw"), Qt::CaseInsensitive) ||
      tag.startsWith(QStringLiteral("zh_hk"), Qt::CaseInsensitive) ||
      tag.startsWith(QStringLiteral("zh_mo"), Qt::CaseInsensitive) ||
      tag.startsWith(QStringLiteral("zh_hant"), Qt::CaseInsensitive)) {
    return QStringLiteral("zh_TW");
  }
  if (tag.compare(QStringLiteral("C"), Qt::CaseInsensitive) == 0 ||
      tag.compare(QStringLiteral("POSIX"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("en");
  }
  return tag;
}

QString ResolveUiLocaleTag() {
  const QByteArray forced = qgetenv("M88_LANG");
  if (!forced.isEmpty()) {
    return NormalizeLocaleTag(QString::fromUtf8(forced));
  }

  const QLocale locale = QLocale::system();
  switch (locale.language()) {
    case QLocale::Japanese:
      return QStringLiteral("ja");
    case QLocale::English:
      return QStringLiteral("en");
    case QLocale::Korean:
      return QStringLiteral("ko");
    case QLocale::German:
      return QStringLiteral("de");
    case QLocale::French:
      return QStringLiteral("fr");
    case QLocale::Spanish:
      return QStringLiteral("es");
    case QLocale::Chinese: {
      const QString name = locale.name();
      if (name.startsWith(QStringLiteral("zh_TW"), Qt::CaseInsensitive) ||
          name.startsWith(QStringLiteral("zh_HK"), Qt::CaseInsensitive) ||
          name.startsWith(QStringLiteral("zh_MO"), Qt::CaseInsensitive) ||
          name.startsWith(QStringLiteral("zh_Hant"), Qt::CaseInsensitive)) {
        return QStringLiteral("zh_TW");
      }
      if (name.startsWith(QStringLiteral("zh_CN"), Qt::CaseInsensitive) ||
          name.startsWith(QStringLiteral("zh_Hans"), Qt::CaseInsensitive)) {
        return QStringLiteral("zh_CN");
      }
      break;
    }
    default:
      break;
  }
  return NormalizeLocaleTag(locale.name());
}

QStringList LocaleCandidates(const QString& locale_tag) {
  QStringList candidates;
  const QString normalized = NormalizeLocaleTag(locale_tag);
  if (!normalized.isEmpty()) {
    candidates << normalized;
  }
  const int sep = normalized.indexOf(QLatin1Char('_'));
  if (sep > 0) {
    const QString language = normalized.left(sep);
    if (!candidates.contains(language)) {
      candidates << language;
    }
  }
  return candidates;
}

QStringList TranslationSearchPaths() {
  QStringList paths;

  const QByteArray env_dir = qgetenv("M88_TRANSLATIONS_DIR");
  if (!env_dir.isEmpty()) {
    paths << QDir::cleanPath(QString::fromLocal8Bit(env_dir));
  }

  const QStringList data_dirs =
      QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
  for (const QString& data_dir : data_dirs) {
    const QString path = data_dir + QStringLiteral("/m88-qt/translations");
    if (!paths.contains(path)) {
      paths << path;
    }
  }

  const QString app_dir = QCoreApplication::applicationDirPath();
  const QString rel_share = QDir(app_dir).filePath(QStringLiteral("../share/m88-qt/translations"));
  const QString local_dir = QDir(app_dir).filePath(QStringLiteral("translations"));
  for (const QString& path : {rel_share, local_dir}) {
    const QString cleaned = QDir::cleanPath(path);
    if (!paths.contains(cleaned)) {
      paths << cleaned;
    }
  }

  return paths;
}

class M88JsonTranslator final : public QTranslator {
 public:
  explicit M88JsonTranslator(QObject* parent = nullptr) : QTranslator(parent) {}

  bool isLoaded() const { return !map_.isEmpty(); }
  int entryCount() const { return map_.size(); }
  const QString& loadedLocale() const { return loaded_locale_; }
  const QString& loadedFrom() const { return loaded_from_; }

  bool loadLocale(const QString& locale_tag) {
    map_.clear();
    loaded_locale_.clear();
    loaded_from_.clear();

    for (const QString& candidate : LocaleCandidates(locale_tag)) {
      if (IsEmbeddedLocale(candidate)) {
        const QString resource_path =
            QStringLiteral(":/i18n/m88-qt_%1.json").arg(candidate);
        if (loadFromResource(resource_path)) {
          loaded_locale_ = candidate;
          loaded_from_ = resource_path;
          return true;
        }
        continue;
      }

      for (const QString& dir : TranslationSearchPaths()) {
        const QString file_path =
            dir + QStringLiteral("/m88-qt_") + candidate + QStringLiteral(".json");
        if (loadFromFile(file_path)) {
          loaded_locale_ = candidate;
          loaded_from_ = file_path;
          return true;
        }
      }
    }
    return false;
  }

  QString translate(const char* context, const char* sourceText, const char* disambiguation,
                    int n) const override {
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    if (!context || !sourceText) {
      return {};
    }
    const QByteArray key = QByteArray(context) + '\0' + sourceText;
    const auto it = map_.constFind(key);
    if (it != map_.constEnd()) {
      return it.value();
    }
    return {};
  }

 private:
  bool loadFromResource(const QString& resource_path) {
    QFile file(resource_path);
    if (!file.open(QIODevice::ReadOnly)) {
      return false;
    }
    return ingestJson(file.readAll());
  }

  bool loadFromFile(const QString& file_path) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
      return false;
    }
    return ingestJson(file.readAll());
  }

  bool ingestJson(const QByteArray& bytes) {
    const QJsonDocument doc = QJsonDocument::fromJson(bytes);
    const QJsonArray array =
        doc.isObject() ? doc.object().value(QStringLiteral("translations")).toArray()
                       : doc.array();
    if (array.isEmpty()) {
      return false;
    }

    QHash<QByteArray, QString> next;
    for (const QJsonValue& entry : array) {
      if (!entry.isObject()) {
        continue;
      }
      const QJsonObject obj = entry.toObject();
      const QString context = obj.value(QStringLiteral("context")).toString();
      const QString source = obj.value(QStringLiteral("source")).toString();
      const QString translation = obj.value(QStringLiteral("translation")).toString();
      if (context.isEmpty() || source.isEmpty() || translation.isEmpty()) {
        continue;
      }
      next.insert(context.toUtf8() + '\0' + source.toUtf8(), translation);
    }
    if (next.isEmpty()) {
      return false;
    }
    map_ = std::move(next);
    return true;
  }

  QHash<QByteArray, QString> map_;
  QString loaded_locale_;
  QString loaded_from_;
};

bool LoadQtBaseTranslator(QApplication& app, const QString& locale_tag) {
  if (locale_tag.startsWith(QStringLiteral("en"))) {
    return false;
  }

  QTranslator* qt_translator = new QTranslator(&app);
  const QString translations_path =
      QLibraryInfo::path(QLibraryInfo::TranslationsPath);
  for (const QString& candidate : LocaleCandidates(locale_tag)) {
    if (qt_translator->load(QStringLiteral("qtbase_") + candidate, translations_path)) {
      app.installTranslator(qt_translator);
      return true;
    }
  }

  delete qt_translator;
  return false;
}

bool InstallAppTranslator(QApplication& app, const QString& locale_tag,
                            const char* requested_label) {
  auto* app_translator = new M88JsonTranslator(&app);
  if (app_translator->loadLocale(locale_tag)) {
    app.installTranslator(app_translator);
    std::fprintf(stderr, "M88: UI language: %s (%d strings, %s)\n",
                 app_translator->loadedLocale().toUtf8().constData(),
                 app_translator->entryCount(),
                 app_translator->loadedFrom().toUtf8().constData());
    return true;
  }

  std::fprintf(stderr,
               "M88: warning: UI translations for %s could not be loaded; "
               "falling back to English\n",
               requested_label);
  delete app_translator;
  return false;
}

}  // namespace

void M88InstallTranslations(QApplication& app) {
  const QString requested = ResolveUiLocaleTag();
  const QByteArray requested_label = requested.toUtf8();

  if (requested.startsWith(QStringLiteral("en"))) {
    if (InstallAppTranslator(app, QStringLiteral("en"), requested_label.constData())) {
      return;
    }
    std::fprintf(stderr, "M88: UI language: English\n");
    return;
  }

  LoadQtBaseTranslator(app, requested);
  if (InstallAppTranslator(app, requested, requested_label.constData())) {
    return;
  }

  LoadQtBaseTranslator(app, QStringLiteral("en"));
  if (!InstallAppTranslator(app, QStringLiteral("en"), requested_label.constData())) {
    std::fprintf(stderr, "M88: UI language: English (source strings)\n");
  }
}

QString M88TranslateStatusMessage(const QString& message) {
  if (message.isEmpty()) {
    return message;
  }

  if (message.endsWith(QStringLiteral(" mode"))) {
    const QString mode = message.left(message.size() - 5);
    return QCoreApplication::translate("StatusBar", "%1 mode")
        .arg(QCoreApplication::translate("BasicMode", mode.toUtf8().constData()));
  }

  static const char* kFdcPrefixes[] = {
      "ReadDiagnostic ", "ReadID ", "WriteID ", "Scan ", "Write ", "Read ",
  };
  for (const char* prefix : kFdcPrefixes) {
    if (message.startsWith(QLatin1String(prefix))) {
      return QCoreApplication::translate("StatusBar", prefix) +
             message.mid(static_cast<int>(std::strlen(prefix)));
    }
  }

  const QString translated =
      QCoreApplication::translate("StatusBar", message.toUtf8().constData());
  if (translated != message) {
    return translated;
  }
  return message;
}
