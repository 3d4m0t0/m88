#include "linux_sound.h"

#include "headers.h"
#include "misc.h"
#include "pc88/pc88.h"

#include <algorithm>
#include <cstdio>

using namespace PC8801;

namespace {

constexpr uint kMutedRate = 100;
// Sound::mixrate — internal ring holds pre-resampler samples at this rate.
constexpr uint kMixRate = 55467;

}  // namespace

LinuxSound::LinuxSound()
    : device_(0),
      sample_rate_(0),
      current_rate_(0),
      current_buflen_ms_(0) {}

LinuxSound::~LinuxSound() { Cleanup(); }

bool LinuxSound::Init(PC88* pc, uint /*rate*/, uint /*buflen_ms*/) {
  current_rate_ = 0;
  current_buflen_ms_ = 0;
  device_ = 0;
  sample_rate_ = 0;

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
    return false;
  }

  return Sound::Init(pc, kMutedRate, 0);
}

void LinuxSound::Cleanup() {
  CloseDevice();
  Sound::Cleanup();
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void LinuxSound::ApplyConfig(const Config* config) {
  if (!config) {
    return;
  }
  ChangeRate(config->sound, config->soundbuffer);
  Sound::ApplyConfig(config);
  // Refill on underrun; OPNIF::TimeEvent keeps Count/Mix in sync when effclock matches Windows.
  FillWhenEmpty(true);
}

bool LinuxSound::ChangeRate(uint rate, uint buflen_ms) {
  if (current_rate_ == rate && current_buflen_ms_ == buflen_ms && device_ != 0) {
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

  // Ring stores 55467 Hz samples; SoundBuffer ms is latency at mix rate.
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

  SDL_AudioSpec want {};
  SDL_AudioSpec have {};
  want.freq = static_cast<int>(sample_rate_);
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = static_cast<Uint16>(
      std::max(512, std::min(2048, bufsize > 0 ? bufsize / 16 : 1024)));
  want.callback = SdlCallback;
  want.userdata = this;

  device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (!device_) {
    std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    SetRate(rate, 0);
    return false;
  }

  const uint device_rate = static_cast<uint>(have.freq);
  sample_rate_ = device_rate;
  if (device_rate != rate && device_rate >= 8000) {
    if (want.freq != static_cast<int>(device_rate)) {
      std::fprintf(stderr,
                   "SDL audio: requested %u Hz, device %u Hz (reconfiguring resampler)\n",
                   rate, device_rate);
    }
    bufsize = static_cast<int>((kMixRate * buflen_ms / 1000) & ~15);
    if (!SetRate(device_rate, bufsize)) {
      CloseDevice();
      return false;
    }
  }
  SDL_PauseAudioDevice(device_, 0);
  return true;
}

void LinuxSound::CloseDevice() {
  if (device_) {
    SDL_PauseAudioDevice(device_, 1);
    SDL_CloseAudioDevice(device_);
    device_ = 0;
  }
}

void LinuxSound::SdlCallback(void* userdata, Uint8* stream, int len) {
  auto* self = static_cast<LinuxSound*>(userdata);
  self->FillAudio(stream, len);
}

void LinuxSound::FillAudio(Uint8* stream, int len_bytes) {
  if (!stream || len_bytes <= 0) {
    return;
  }

  auto* out = reinterpret_cast<Sample*>(stream);
  const int samples = len_bytes / static_cast<int>(sizeof(Sample) * 2);
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
