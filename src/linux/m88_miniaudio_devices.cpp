#include "m88_miniaudio_devices.h"

#include <cstring>

namespace M88MiniaudioDevices {
namespace {

bool NamesEqual(const char* a, const char* b) {
  if (!a) {
    a = "";
  }
  if (!b) {
    b = "";
  }
  return std::strcmp(a, b) == 0;
}

const char* DeviceIdCString(ma_backend backend, const ma_device_id& id) {
  switch (backend) {
    case ma_backend_pulseaudio:
      return id.pulse;
    case ma_backend_alsa:
      return id.alsa;
    case ma_backend_jack:
      return "";
    default:
      return "";
  }
}

bool DeviceIdMatches(ma_backend backend, const ma_device_info& info,
                     const char* saved) {
  const char* id_str = DeviceIdCString(backend, info.id);
  return id_str[0] != '\0' && NamesEqual(id_str, saved);
}

bool BackendIsAuto(const char* backend_name) {
  return !backend_name || backend_name[0] == '\0' ||
         strcasecmp(backend_name, "auto") == 0;
}

bool ParseBackend(const char* backend_name, ma_backend* out) {
  if (!out || BackendIsAuto(backend_name)) {
    return false;
  }
  if (strcasecmp(backend_name, "pulse") == 0 ||
      strcasecmp(backend_name, "pulseaudio") == 0) {
    *out = ma_backend_pulseaudio;
    return true;
  }
  if (strcasecmp(backend_name, "alsa") == 0) {
    *out = ma_backend_alsa;
    return true;
  }
  if (strcasecmp(backend_name, "jack") == 0) {
    *out = ma_backend_jack;
    return true;
  }
  return false;
}

bool InitContextImpl(const char* backend_name, ma_context* context) {
  if (!context) {
    return false;
  }
  ma_backend single {};
  if (ParseBackend(backend_name, &single)) {
    const ma_backend backends[] = {single};
    return ma_context_init(backends, 1, nullptr, context) == MA_SUCCESS;
  }
  return ma_context_init(nullptr, 0, nullptr, context) == MA_SUCCESS;
}

}  // namespace

bool IsExplicitPulseBackend(const char* backend_name) {
  return backend_name &&
         (strcasecmp(backend_name, "pulse") == 0 ||
          strcasecmp(backend_name, "pulseaudio") == 0);
}

bool IsAutoBackend(const char* backend_name) {
  return BackendIsAuto(backend_name);
}

bool UsesNativeDeviceInit(const char* backend_name, const char* /*device_name*/) {
  // Auto only: probe backend via ma_device_init(NULL, ...), then reopen if Pulse.
  // Explicit pulse/alsa/jack use InitContext directly (one stream).
  return BackendIsAuto(backend_name);
}

bool InitContext(const char* backend_name, ma_context* context) {
  return InitContextImpl(backend_name, context);
}

std::vector<Entry> ListPlayback(const char* backend_name) {
  std::vector<Entry> out;
  ma_context context {};
  if (!InitContextImpl(backend_name, &context)) {
    return out;
  }

  ma_device_info* infos = nullptr;
  ma_uint32 count = 0;
  if (ma_context_get_devices(&context, &infos, &count, nullptr, nullptr) != MA_SUCCESS) {
    ma_context_uninit(&context);
    return out;
  }

  const ma_backend backend = context.backend;
  out.reserve(count);
  for (ma_uint32 i = 0; i < count; ++i) {
    if (infos[i].name[0] == '\0') {
      continue;
    }
    Entry entry;
    entry.name = infos[i].name;
    const char* id_str = DeviceIdCString(backend, infos[i].id);
    if (id_str[0] != '\0') {
      entry.id = id_str;
    }
    out.push_back(std::move(entry));
  }

  ma_context_uninit(&context);
  return out;
}

bool ResolvePlaybackIdInContext(ma_context* context, const char* saved_name,
                                ma_device_id* out_id) {
  if (!context || !out_id || !saved_name || saved_name[0] == '\0') {
    return false;
  }

  ma_device_info* infos = nullptr;
  ma_uint32 count = 0;
  const ma_result result =
      ma_context_get_devices(context, &infos, &count, nullptr, nullptr);
  if (result != MA_SUCCESS) {
    return false;
  }

  const ma_backend backend = context->backend;

  // Prefer Pulse/ALSA sink id (stored in config after device-id fix).
  for (ma_uint32 i = 0; i < count; ++i) {
    if (DeviceIdMatches(backend, infos[i], saved_name)) {
      *out_id = infos[i].id;
      return true;
    }
  }

  // Legacy configs stored the UI description in AudioDevice=.
  ma_uint32 name_matches = 0;
  ma_uint32 name_index = 0;
  for (ma_uint32 i = 0; i < count; ++i) {
    if (NamesEqual(infos[i].name, saved_name)) {
      name_index = i;
      ++name_matches;
    }
  }
  if (name_matches == 1) {
    *out_id = infos[name_index].id;
    return true;
  }
  return false;
}

bool ResolvePlaybackId(const char* backend_name, const char* saved_name,
                       ma_device_id* out_id) {
  if (!out_id || !saved_name || saved_name[0] == '\0') {
    return false;
  }

  ma_context context {};
  if (!InitContextImpl(backend_name, &context)) {
    return false;
  }

  const bool found = ResolvePlaybackIdInContext(&context, saved_name, out_id);
  ma_context_uninit(&context);
  return found;
}

}  // namespace M88MiniaudioDevices
