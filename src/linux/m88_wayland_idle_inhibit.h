#pragma once

class QWindow;

bool M88WaylandIdleInhibitAvailable();
void M88WaylandIdleInhibitApply(QWindow* window, bool enable);
void M88WaylandIdleInhibitShutdown();
