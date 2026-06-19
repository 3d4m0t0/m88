#include "m88_i18n.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <cstdio>
#include <cstring>

namespace {

constexpr auto kAppTranslationResource = ":/i18n/m88-qt_ja.json";

bool UseJapaneseUi() {
  const QByteArray forced = qgetenv("M88_LANG");
  if (forced == "ja" || forced == "jp") {
    return true;
  }
  if (forced == "en" || forced == "C") {
    return false;
  }
  switch (QLocale::system().language()) {
    case QLocale::Japanese:
      return true;
    default:
      return false;
  }
}

class M88JsonTranslator final : public QTranslator {
 public:
  explicit M88JsonTranslator(QObject* parent = nullptr) : QTranslator(parent) {
    if (!loadFromResource(QString::fromLatin1(kAppTranslationResource))) {
      const QString file_path =
          QCoreApplication::applicationDirPath() +
          QStringLiteral("/translations/m88-qt_ja.json");
      loadFromFile(file_path);
    }
  }

  bool isLoaded() const { return !map_.isEmpty(); }
  int entryCount() const { return map_.size(); }

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
      map_.insert(context.toUtf8() + '\0' + source.toUtf8(), translation);
    }
    return !map_.isEmpty();
  }

  QHash<QByteArray, QString> map_;
};

bool LoadQtBaseTranslator(QApplication& app) {
  QTranslator* qt_translator = new QTranslator(&app);
  const QString translations_path =
      QLibraryInfo::path(QLibraryInfo::TranslationsPath);
  if (qt_translator->load(QStringLiteral("qtbase_ja"), translations_path)) {
    app.installTranslator(qt_translator);
    return true;
  }
  delete qt_translator;
  return false;
}

}  // namespace

void M88InstallTranslations(QApplication& app) {
  if (!UseJapaneseUi()) {
    return;
  }
  LoadQtBaseTranslator(app);
  auto* app_translator = new M88JsonTranslator(&app);
  if (app_translator->isLoaded()) {
    app.installTranslator(app_translator);
    std::fprintf(stderr, "M88: UI language: Japanese (%d strings)\n",
                 app_translator->entryCount());
  } else {
    std::fprintf(stderr,
                 "M88: warning: Japanese UI translations could not be loaded "
                 "(resource %s)\n",
                 kAppTranslationResource);
    delete app_translator;
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
