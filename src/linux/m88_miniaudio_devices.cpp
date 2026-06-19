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

bool IsAutoBackend(const char* backend_name) {
  return !backend_name || backend_name[0] == '\0' ||
         strcasecmp(backend_name, "auto") == 0;
}

bool ParseBackend(const char* backend_name, ma_backend* out) {
  if (!out || IsAutoBackend(backend_name)) {
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

  out.reserve(count);
  for (ma_uint32 i = 0; i < count; ++i) {
    if (infos[i].name[0] == '\0') {
      continue;
    }
    out.push_back({infos[i].name});
  }

  ma_context_uninit(&context);
  return out;
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

  ma_device_info* infos = nullptr;
  ma_uint32 count = 0;
  const ma_result result =
      ma_context_get_devices(&context, &infos, &count, nullptr, nullptr);
  if (result != MA_SUCCESS) {
    ma_context_uninit(&context);
    return false;
  }

  bool found = false;
  for (ma_uint32 i = 0; i < count; ++i) {
    if (NamesEqual(infos[i].name, saved_name)) {
      *out_id = infos[i].id;
      found = true;
      break;
    }
  }

  ma_context_uninit(&context);
  return found;
}

}  // namespace M88MiniaudioDevices
