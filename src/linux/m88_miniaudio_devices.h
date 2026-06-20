#pragma once

#include "miniaudio.h"

#include <string>
#include <vector>

namespace M88MiniaudioDevices {

struct Entry {
  std::string name;
};

// backend_name: empty / "auto" = miniaudio default priority; "pulse", "alsa", "jack".
bool IsExplicitPulseBackend(const char* backend_name);
bool IsAutoBackend(const char* backend_name);

// True for auto and pulse: first open via ma_device_init(NULL, ...).
// Pulse then reopens with InitContext("pulse") + optional device ID.
// ALSA/JACK always use InitContext + device ID directly.
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
