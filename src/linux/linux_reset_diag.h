#pragma once

class PC88;

// Log CPU / VRTC state immediately after reset and after ~1 frame of Proceed.
// tag: "mode_switch", "user_reset", "startup", etc.
// prev_basicmode < 0 when not applicable.
void M88DiagAfterReset(PC88& pc88, const char* tag, int basicmode, int prev_basicmode,
                       int host_clock, int effclock);
