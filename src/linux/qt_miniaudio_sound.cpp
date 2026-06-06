#include "qt_miniaudio_sound.h"

#include "headers.h"
#include "misc.h"
#include "pc88/pc88.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace PC8801;

namespace {

constexpr uint kMutedRate = 100;
constexpr uint kMixRate = 55467;

}  // namespace

void QtMiniaudioSoundDataCallback(ma_device* device, void* output, const void* /*input*/,
                                  ma_uint32 frame_count) {
  if (!device || !device->pUserData || !output) {
    return;
  }
  auto* self = static_cast<PC8801::QtMiniaudioSound*>(device->pUserData);
  self->FillAudio(static_cast<int16_t*>(output), static_cast<int>(frame_count));
}

struct QtMiniaudioSound::Device {
  ma_device dev {};
  bool active = false;
};

QtMiniaudioSound::QtMiniaudioSound()
    : device_(std::make_unique<Device>()),
      sample_rate_(0),
      current_rate_(0),
      current_buflen_ms_(0) {}

QtMiniaudioSound::~QtMiniaudioSound() { Cleanup(); }

bool QtMiniaudioSound::Init(PC88* pc, uint /*rate*/, uint /*buflen_ms*/) {
  current_rate_ = 0;
  current_buflen_ms_ = 0;
  sample_rate_ = 0;
  CloseDevice();

  return Sound::Init(pc, kMutedRate, 0);
}

void QtMiniaudioSound::Cleanup() {
  CloseDevice();
  Sound::Cleanup();
}

void QtMiniaudioSound::ApplyConfig(const Config* config) {
  if (!config) {
    return;
  }
  ChangeRate(config->sound, config->soundbuffer);
  Sound::ApplyConfig(config);
  FillWhenEmpty(true);
}

bool QtMiniaudioSound::ChangeRate(uint rate, uint buflen_ms) {
  if (current_rate_ == rate && current_buflen_ms_ == buflen_ms && device_->active) {
    return true;
  }

  current_rate_ = rate;
  current_buflen_ms_ = buflen_ms;

  CloseDevice();

  sample_rate_ = rate;
  if (rate < 8000) {
    sample_rate_ = 0;
    rate = kMutedRate;
  }

  int bufsize = 0;
  if (sample_rate_ > 0) {
    bufsize = static_cast<int>((kMixRate * buflen_ms / 1000) & ~15);
  }
  if (rate < 1000) {
    bufsize = 0;
  }

  if (!SetRate(rate, bufsize)) {
    return false;
  }

  if (bufsize <= 0 || sample_rate_ == 0) {
    return true;
  }

  const ma_uint32 frames_per_buffer = static_cast<ma_uint32>(std::max(
      512, std::min(2048, bufsize > 0 ? bufsize / 16 : 1024)));

  auto try_open = [&](ma_uint32 open_rate) -> bool {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = open_rate;
    config.periodSizeInFrames = frames_per_buffer;
    config.dataCallback = QtMiniaudioSoundDataCallback;
    config.pUserData = this;

    const ma_result result = ma_device_init(nullptr, &config, &device_->dev);
    if (result != MA_SUCCESS) {
      std::fprintf(stderr, "miniaudio open failed (%u Hz): %d\n", open_rate,
                   static_cast<int>(result));
      return false;
    }

    const uint device_rate = device_->dev.sampleRate;
    sample_rate_ = device_rate;
    if (device_rate != rate && device_rate >= 8000) {
      if (open_rate != device_rate) {
        std::fprintf(stderr,
                     "miniaudio: requested %u Hz, device %u Hz (reconfiguring resampler)\n",
                     rate, device_rate);
      }
      bufsize = static_cast<int>((kMixRate * buflen_ms / 1000) & ~15);
      if (!SetRate(device_rate, bufsize)) {
        ma_device_uninit(&device_->dev);
        return false;
      }
    }

    if (ma_device_start(&device_->dev) != MA_SUCCESS) {
      std::fprintf(stderr, "miniaudio start failed\n");
      ma_device_uninit(&device_->dev);
      return false;
    }

    device_->active = true;
    return true;
  };

  if (!try_open(rate)) {
    SetRate(rate, 0);
    return false;
  }

  return true;
}

void QtMiniaudioSound::CloseDevice() {
  if (!device_ || !device_->active) {
    return;
  }
  ma_device_stop(&device_->dev);
  ma_device_uninit(&device_->dev);
  device_->dev = {};
  device_->active = false;
}

void QtMiniaudioSound::FillAudio(int16_t* stream, int frame_count) {
  if (!stream || frame_count <= 0) {
    return;
  }

  auto* out = reinterpret_cast<Sample*>(stream);
  const int samples = frame_count;
  int written = 0;

  SoundSource* src = GetSoundSource();
  if (src) {
    written = src->Get(out, samples);
  }

  if (written < samples) {
    std::memset(out + written * 2, 0,
                static_cast<size_t>(samples - written) * sizeof(Sample) * 2);
  }
}
