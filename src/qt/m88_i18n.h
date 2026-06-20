#pragma once

class QApplication;
class QString;

// Loads UI translations from JSON files.
// English and Japanese are embedded in the binary; other locales are read from
// share/m88-qt/translations/ (or M88_TRANSLATIONS_DIR).
// Locale selection: M88_LANG, then the system locale, then English.
void M88InstallTranslations(QApplication& app);

// Translates emulation-core status bar text (English source) for display.
QString M88TranslateStatusMessage(const QString& message);
