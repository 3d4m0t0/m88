#pragma once

class QApplication;
class QString;

// Installs Japanese UI translations when the system locale (or M88_LANG) is Japanese.
void M88InstallTranslations(QApplication& app);

// Translates emulation-core status bar text (English source) for display.
QString M88TranslateStatusMessage(const QString& message);
