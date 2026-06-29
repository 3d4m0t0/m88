#include "fcitx_status.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QFile>
#include <QKeyEvent>
#include <QTextStream>

#include <cstdlib>

namespace {

struct TriggerKeySpec {
  Qt::KeyboardModifiers mods = Qt::NoModifier;
  int key = 0;
};

constexpr auto kFcitx5Service = "org.fcitx.Fcitx5";
constexpr auto kFcitx4Service = "org.fcitx.Fcitx";
constexpr auto kControllerPath = "/controller";
constexpr auto kControllerIface = "org.fcitx.Fcitx.Controller1";

bool CallStringMethod(const char* service, const char* method, QString* out) {
  if (!out) {
    return false;
  }
  QDBusInterface iface(service, kControllerPath, kControllerIface,
                       QDBusConnection::sessionBus());
  if (!iface.isValid()) {
    return false;
  }
  QDBusReply<QString> reply = iface.call(method);
  if (!reply.isValid()) {
    return false;
  }
  *out = reply.value();
  return true;
}

bool CallIntMethod(const char* service, const char* method, int* out) {
  if (!out) {
    return false;
  }
  QDBusInterface iface(service, kControllerPath, kControllerIface,
                       QDBusConnection::sessionBus());
  if (!iface.isValid()) {
    return false;
  }
  QDBusReply<int> reply = iface.call(method);
  if (!reply.isValid()) {
    return false;
  }
  *out = reply.value();
  return true;
}

bool CallVoidMethod(const char* service, const char* method) {
  QDBusInterface iface(service, kControllerPath, kControllerIface,
                       QDBusConnection::sessionBus());
  if (!iface.isValid()) {
    return false;
  }
  QDBusReply<void> reply = iface.call(method);
  return reply.isValid();
}

QString StateLabel(int state) {
  switch (state) {
    case 0:
      return QObject::tr("closed");
    case 1:
      return QObject::tr("inactive");
    case 2:
      return QObject::tr("active");
    default:
      return QObject::tr("unknown");
  }
}

QString FcitxConfigPath() {
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  if (xdg && xdg[0] != '\0') {
    return QString::fromUtf8(xdg) + QStringLiteral("/fcitx5/config");
  }
  const char* home = std::getenv("HOME");
  if (!home || home[0] == '\0') {
    return {};
  }
  return QString::fromUtf8(home) + QStringLiteral("/.config/fcitx5/config");
}

int QtKeyFromFcitxName(const QString& name) {
  const QString key = name.trimmed().toLower();
  if (key == QStringLiteral("space")) {
    return Qt::Key_Space;
  }
  if (key == QStringLiteral("zenkaku_hankaku") || key == QStringLiteral("hankaku") ||
      key == QStringLiteral("hangul_hanja")) {
    return Qt::Key_Zenkaku_Hankaku;
  }
  if (key == QStringLiteral("henkan")) {
    return Qt::Key_Henkan;
  }
  if (key == QStringLiteral("muhenkan")) {
    return Qt::Key_Muhenkan;
  }
  if (key == QStringLiteral("hiragana_katakana") || key == QStringLiteral("katakana")) {
    return Qt::Key_Hiragana_Katakana;
  }
  if (key == QStringLiteral("kana_lock")) {
    return Qt::Key_Kana_Lock;
  }
  if (key == QStringLiteral("eisu_toggle")) {
    return Qt::Key_Eisu_toggle;
  }
  return 0;
}

void AppendDefaultTriggerKeys(QVector<TriggerKeySpec>* keys) {
  if (!keys) {
    return;
  }
  const TriggerKeySpec defaults[] = {
      {Qt::MetaModifier, Qt::Key_Space},
      {Qt::ControlModifier, Qt::Key_Space},
      {Qt::NoModifier, Qt::Key_Zenkaku_Hankaku},
      {Qt::NoModifier, Qt::Key_Henkan},
      {Qt::NoModifier, Qt::Key_Muhenkan},
  };
  for (const TriggerKeySpec& spec : defaults) {
    bool found = false;
    for (const TriggerKeySpec& existing : *keys) {
      if (existing.key == spec.key && existing.mods == spec.mods) {
        found = true;
        break;
      }
    }
    if (!found) {
      keys->push_back(spec);
    }
  }
}

}  // namespace

bool FcitxStatus::parseFcitxKeySpec(const QString& spec, TriggerKey* out) {
  if (!out || spec.trimmed().isEmpty()) {
    return false;
  }
  QStringList parts = spec.split(QLatin1Char('+'), Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    return false;
  }
  const QString key_name = parts.takeLast().trimmed();
  Qt::KeyboardModifiers mods = Qt::NoModifier;
  for (const QString& part : parts) {
    const QString mod = part.trimmed().toLower();
    if (mod == QStringLiteral("super") || mod == QStringLiteral("meta") ||
        mod == QStringLiteral("win")) {
      mods |= Qt::MetaModifier;
    } else if (mod == QStringLiteral("control") || mod == QStringLiteral("ctrl")) {
      mods |= Qt::ControlModifier;
    } else if (mod == QStringLiteral("alt")) {
      mods |= Qt::AltModifier;
    } else if (mod == QStringLiteral("shift")) {
      mods |= Qt::ShiftModifier;
    } else {
      return false;
    }
  }
  const int key = QtKeyFromFcitxName(key_name);
  if (key == 0) {
    return false;
  }
  out->mods = mods;
  out->key = key;
  return true;
}

FcitxStatus::FcitxStatus(QObject* parent) : QObject(parent) {
  loadTriggerKeys();
  refresh();
}

void FcitxStatus::loadTriggerKeys() {
  trigger_keys_.clear();

  QVector<TriggerKeySpec> specs;
  const QString path = FcitxConfigPath();
  if (!path.isEmpty()) {
    QFile file(path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
      QTextStream in(&file);
      QString section;
      while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) {
          continue;
        }
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
          section = line.mid(1, line.size() - 2);
          continue;
        }
        if (section != QStringLiteral("Hotkey/TriggerKeys") &&
            section != QStringLiteral("Hotkey/AltTriggerKeys")) {
          continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
          continue;
        }
        TriggerKey parsed;
        if (parseFcitxKeySpec(line.mid(eq + 1), &parsed)) {
          specs.push_back({parsed.mods, parsed.key});
        }
      }
    }
  }

  AppendDefaultTriggerKeys(&specs);
  trigger_keys_.reserve(specs.size());
  for (const TriggerKeySpec& spec : specs) {
    trigger_keys_.push_back({spec.mods, spec.key});
  }
}

bool FcitxStatus::matchesTriggerKey(const QKeyEvent& event) const {
  if (event.isAutoRepeat()) {
    return false;
  }
  const Qt::KeyboardModifiers mods =
      event.modifiers() &
      (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
  for (const TriggerKey& spec : trigger_keys_) {
    if (event.key() == spec.key && mods == spec.mods) {
      return true;
    }
  }
  return false;
}

bool FcitxStatus::toggleInputMethod() {
  if (CallVoidMethod(kFcitx5Service, "Toggle")) {
    return true;
  }
  if (CallVoidMethod(kFcitx4Service, "Toggle")) {
    return true;
  }
  return false;
}

void FcitxStatus::refresh() {
  const QString prev_im = current_im_;
  const int prev_state = state_;
  const bool prev_available = available_;

  available_ = false;
  current_im_.clear();
  state_ = -1;

  if (queryFcitx5() || queryFcitx4()) {
    available_ = true;
  }

  if (prev_im != current_im_ || prev_state != state_ || prev_available != available_) {
    emit changed();
  }
}

bool FcitxStatus::queryFcitx5() {
  QString im;
  int state = -1;
  if (!CallStringMethod(kFcitx5Service, "CurrentInputMethod", &im)) {
    return false;
  }
  CallIntMethod(kFcitx5Service, "State", &state);
  current_im_ = im;
  state_ = state;
  return true;
}

bool FcitxStatus::queryFcitx4() {
  QString im;
  int state = -1;
  if (!CallStringMethod(kFcitx4Service, "CurrentInputMethod", &im)) {
    return false;
  }
  CallIntMethod(kFcitx4Service, "State", &state);
  current_im_ = im;
  state_ = state;
  return true;
}

bool FcitxStatus::hostKanaInputActive() const {
  if (!available_ || current_im_.isEmpty()) {
    return false;
  }
  if (current_im_.startsWith(QStringLiteral("keyboard-"))) {
    return false;
  }
  // fcitx5 State: 0=closed, 1=inactive, 2=active (fcitx5-remote / dbus State)
  return state_ == 2;
}

QString FcitxStatus::statusLine() const {
  if (!available_ || current_im_.isEmpty()) {
    return {};
  }
  if (current_im_.startsWith(QStringLiteral("keyboard-"))) {
    return tr("Host IM: %1 (direct)").arg(current_im_);
  }
  return tr("Host IM: %1").arg(current_im_);
}

QString FcitxStatus::toolTipText() const {
  if (!available_) {
    return {};
  }
  return tr("fcitx CurrentInputMethod (global D-Bus)\nIM: %1\nState: %2")
      .arg(current_im_.isEmpty() ? tr("(none)") : current_im_, StateLabel(state_));
}
