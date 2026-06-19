#pragma once

#include "miniaudio.h"

#include <string>
#include <vector>

namespace M88MiniaudioDevices {

struct Entry {
  std::string name;
};

// backend_name: empty / "auto" = miniaudio default priority; "pulse", "alsa", "jack".
bool InitContext(const char* backend_name, ma_context* context);

std::vector<Entry> ListPlayback(const char* backend_name);

// Returns true when out_id is filled for ma_device_config.playback.pDeviceID.
bool ResolvePlaybackId(const char* backend_name, const char* saved_name,
                       ma_device_id* out_id);

}  // namespace M88MiniaudioDevices
