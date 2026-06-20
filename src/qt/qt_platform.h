#pragma once

class QIcon;

bool M88QtIsWaylandSession();

QIcon M88QtAppIcon();

// Probe host IME once after QApplication construction (cached for Configure dialog).
void M88QtProbeImeAtStartup();
