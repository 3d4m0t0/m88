#pragma once

#include "miniaudio.h"

#include <string>
#include <vector>

namespace M88MiniaudioDevices {

struct Entry {
  std::string name;  // UI label (PulseAudio description, etc.)
  std::string id;    // Backend device id (PulseAudio sink name, ALSA id, ...)
};

// backend_name: empty / "auto" = miniaudio default priority; "pulse", "alsa", "jack".
bool IsExplicitPulseBackend(const char* backend_name);
bool IsAutoBackend(const char* backend_name);

// True for auto backend: miniaudio picks backend (ma_device_init(NULL, ...)).
// If Pulse is selected, reopen with InitContext("pulse") + optional device ID.
// pulse/alsa/jack use InitContext directly (single playback stream).
bool UsesNativeDeviceInit(const char* backend_name, const char* device_name);

bool InitContext(const char* backend_name, ma_context* context);

std::vector<Entry> ListPlayback(const char* backend_name);

// Returns true when out_id is filled for ma_device_config.playback.pDeviceID.
// out_id is only valid for the given context (do not reuse across contexts).
bool ResolvePlaybackIdInContext(ma_context* context, const char* saved_name,
                                ma_device_id* out_id);

bool ResolvePlaybackId(const char* backend_name, const char* saved_name,
                       ma_device_id* out_id);

}  // namespace M88MiniaudioDevices
