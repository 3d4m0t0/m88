#include "qt_miniaudio_sound.h"

#include "linux_audio_period.h"
#include "linux/m88_miniaudio_devices.h"
#include "headers.h"
#include "misc.h"
#include "pc88/pc88.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#endif

using namespace PC8801;

namespace {

constexpr uint kMutedRate = 100;
constexpr uint kMixRate = 55467;

constexpr int kDrainChunkFrames = 512;
constexpr int kDrainGuard = 512;
constexpr int kSpscDrainChunk = 512;
constexpr int kPrimePeriods = 3;
constexpr int kTargetPeriods = 5;
constexpr int kMaxPeriods = 8;
constexpr int kSrcRingMultiplier = 4;
constexpr int kSpscCapacityMultiplier = 6;
constexpr int kCallbackSpinIters = 8000;

const char* BackendDisplayName(ma_backend backend) {
  switch (backend) {
    case ma_backend_pulseaudio:
      return "PulseAudio";
    case ma_backend_alsa:
      return "ALSA";
    case ma_backend_jack:
      return "JACK";
    default:
      return "Auto";
  }
}

void LogAudioOutput(const ma_device& dev, uint sample_rate_hz) {
  const ma_backend backend =
      dev.pContext ? dev.pContext->backend : ma_backend_null;
  const char* backend_name = BackendDisplayName(backend);
  const char* device_name =
      dev.playback.name[0] != '\0' ? dev.playback.name : "default";
  std::printf("M88: %s  %s  %u Hz\n", backend_name, device_name, sample_rate_hz);
  std::fflush(stdout);
}

}  // namespace

void QtMiniaudioSoundDataCallback(ma_device* device, void* output, const void* /*input*/,
                                  ma_uint32 frame_count) {
  if (!device || !device->pUserData || !output || frame_count == 0) {
    if (output && frame_count > 0) {
      std::memset(output, 0, static_cast<size_t>(frame_count) * 2 * sizeof(int16_t));
    }
    return;
  }
  auto* self = static_cast<PC8801::QtMiniaudioSound*>(device->pUserData);
  self->FillAudio(static_cast<int16_t*>(output), static_cast<int>(frame_count));
}

struct QtMiniaudioSound::Device {
  ma_context context {};
  bool context_active = false;
  ma_device dev {};
  bool active = false;
};

QtMiniaudioSound::QtMiniaudioSound()
    : device_(std::make_unique<Device>()),
      sample_rate_(0),
      current_rate_(0),
      current_buflen_ms_(0) {}

QtMiniaudioSound::~QtMiniaudioSound() { Cleanup(); }

size_t QtMiniaudioSound::SpscCapacityFrames(uint sample_rate_hz, uint buflen_ms) {
  if (sample_rate_hz < 8000 || buflen_ms == 0) {
    return 0;
  }
  const size_t need =
      (static_cast<size_t>(sample_rate_hz) * buflen_ms + 999) / 1000;
  size_t cap = 64;
  const size_t want = need * kSpscCapacityMultiplier;
  while (cap < want) {
    cap <<= 1;
  }
  return cap;
}

bool QtMiniaudioSound::Init(PC88* pc, uint /*rate*/, uint /*buflen_ms*/) {
  current_rate_ = 0;
  current_buflen_ms_ = 0;
  sample_rate_ = 0;
  CloseDevice();
  return Sound::Init(pc, kMutedRate, 0);
}

void QtMiniaudioSound::Cleanup() {
  CloseDevice();
  spsc_.Reset();
  Sound::Cleanup();
}

void QtMiniaudioSound::ResetSpsc(uint sample_rate_hz, uint buflen_ms) {
  const size_t cap = SpscCapacityFrames(sample_rate_hz, buflen_ms);
  if (cap > 0) {
    spsc_.Init(cap);
  } else {
    spsc_.Reset();
  }
}

int QtMiniaudioSound::PrimeSpscSilence(int frames) {
  if (frames <= 0 || spsc_.Capacity() == 0) {
    return 0;
  }
  Sample silence[kDrainChunkFrames * 2] = {};
  int left = frames;
  int primed = 0;
  while (left > 0 && spsc_.Free() > 0) {
    const int chunk = std::min(left, kDrainChunkFrames);
    const size_t pushed = spsc_.Push(silence, static_cast<size_t>(chunk));
    if (pushed == 0) {
      break;
    }
    left -= static_cast<int>(pushed);
    primed += static_cast<int>(pushed);
  }
  return primed;
}

void QtMiniaudioSound::ApplyConfig(const Config* config) {
  if (!config) {
    return;
  }
  std::strncpy(playback_backend_name_, config->audiobackend,
               sizeof(playback_backend_name_) - 1);
  playback_backend_name_[sizeof(playback_backend_name_) - 1] = '\0';
  if (M88MiniaudioDevices::IsAutoBackend(playback_backend_name_)) {
    playback_device_name_[0] = '\0';
  } else {
    std::strncpy(playback_device_name_, config->audiodevice,
                 sizeof(playback_device_name_) - 1);
    playback_device_name_[sizeof(playback_device_name_) - 1] = '\0';
  }
  ChangeRate(config->sound, config->soundbuffer);
  Sound::ApplyConfig(config);
}

bool QtMiniaudioSound::ChangeRate(uint rate, uint buflen_ms) {
  if (current_rate_ == rate && current_buflen_ms_ == buflen_ms && device_->active &&
      std::strcmp(current_device_name_, playback_device_name_) == 0 &&
      std::strcmp(current_backend_name_, playback_backend_name_) == 0) {
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
    bufsize = static_cast<int>(
        (kMixRate * buflen_ms * kSrcRingMultiplier / 1000) & ~15);
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

  ResetSpsc(sample_rate_, buflen_ms);

  const ma_uint32 frames_per_buffer =
      static_cast<ma_uint32>(M88AudioPeriodFrames(rate, bufsize));

  bool opened_native = false;
  auto try_open = [&](ma_uint32 open_rate) -> bool {
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = 2;
    config.sampleRate = open_rate;
    config.periodSizeInFrames = frames_per_buffer;
    config.dataCallback = QtMiniaudioSoundDataCallback;
    config.pUserData = this;
    config.playback.pDeviceID = nullptr;

    const bool native_init = M88MiniaudioDevices::UsesNativeDeviceInit(
        playback_backend_name_, playback_device_name_);
    opened_native = native_init;

    ma_result result = MA_ERROR;
    if (native_init) {
      device_->context_active = false;
      result = ma_device_init(nullptr, &config, &device_->dev);
    } else {
      if (!M88MiniaudioDevices::InitContext(playback_backend_name_, &device_->context)) {
        std::fprintf(stderr, "M88: miniaudio context init failed (backend=%s)\n",
                     playback_backend_name_[0] ? playback_backend_name_ : "auto");
        return false;
      }
      device_->context_active = true;

      ma_device_id chosen_id {};
      const ma_device_id* device_id = nullptr;
      if (playback_device_name_[0] != '\0') {
        if (M88MiniaudioDevices::ResolvePlaybackIdInContext(
                &device_->context, playback_device_name_, &chosen_id)) {
          device_id = &chosen_id;
        } else {
          std::fprintf(stderr,
                       "M88: audio device not found: %s (backend=%s, using default)\n",
                       playback_device_name_,
                       playback_backend_name_[0] ? playback_backend_name_ : "auto");
        }
      }
      config.playback.pDeviceID = device_id;
      result = ma_device_init(&device_->context, &config, &device_->dev);
    }

    if (result != MA_SUCCESS) {
      std::fprintf(stderr, "miniaudio open failed (%u Hz, backend=%s): %d\n",
                   open_rate,
                   playback_backend_name_[0] ? playback_backend_name_ : "auto",
                   static_cast<int>(result));
      if (device_->context_active) {
        ma_context_uninit(&device_->context);
        device_->context = {};
        device_->context_active = false;
      }
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
      bufsize = static_cast<int>(
          (kMixRate * buflen_ms * kSrcRingMultiplier / 1000) & ~15);
      if (!SetRate(device_rate, bufsize)) {
        ma_device_uninit(&device_->dev);
        device_->dev = {};
        if (device_->context_active) {
          ma_context_uninit(&device_->context);
          device_->context = {};
          device_->context_active = false;
        }
        return false;
      }
      ResetSpsc(device_rate, buflen_ms);
    }

    FillWhenEmpty(true);
    RecomputeLatencyTargets(bufsize);
    const int prime_frames = period_frames_ * kPrimePeriods;
    PrimeSpscSilence(prime_frames > 0 ? prime_frames : target_spsc_frames_);
    ResyncPcmContractAfterRateChange();

    if (ma_device_start(&device_->dev) != MA_SUCCESS) {
      std::fprintf(stderr, "miniaudio start failed\n");
      ma_device_uninit(&device_->dev);
      device_->dev = {};
      if (device_->context_active) {
        ma_context_uninit(&device_->context);
        device_->context = {};
        device_->context_active = false;
      }
      return false;
    }

    device_->active = true;
    std::strncpy(current_device_name_, playback_device_name_,
                 sizeof(current_device_name_) - 1);
    current_device_name_[sizeof(current_device_name_) - 1] = '\0';
    std::strncpy(current_backend_name_, playback_backend_name_,
                 sizeof(current_backend_name_) - 1);
    current_backend_name_[sizeof(current_backend_name_) - 1] = '\0';
    return true;
  };

  if (!try_open(rate)) {
    SetRate(rate, 0);
    return false;
  }

  // Pulse via InitContext can start silent (miniaudio/Pulse quirk). Reopen after
  // native init — same fix for auto→Pulse and backend=pulse (+ optional device).
  const bool explicit_pulse =
      M88MiniaudioDevices::IsExplicitPulseBackend(playback_backend_name_);
  const bool needs_pulse_reopen =
      explicit_pulse || (opened_native && IsPulseBackend());

  if (needs_pulse_reopen) {
    TeardownPlaybackDevice();
    const ma_uint32 reopen_period =
        static_cast<ma_uint32>(M88AudioPeriodFrames(sample_rate_, bufsize));
    if (!OpenExplicitPlayback("pulse", sample_rate_, reopen_period)) {
      std::fprintf(stderr, "M88: explicit Pulse reopen failed\n");
      SetRate(rate, 0);
      return false;
    }
    std::strncpy(current_device_name_, playback_device_name_,
                 sizeof(current_device_name_) - 1);
    current_device_name_[sizeof(current_device_name_) - 1] = '\0';
    std::strncpy(current_backend_name_, playback_backend_name_,
                 sizeof(current_backend_name_) - 1);
    current_backend_name_[sizeof(current_backend_name_) - 1] = '\0';
    ResyncPcmContractAfterRateChange();
  }

  if (IsPulseBackend()) {
    WarmPulsePlaybackStream();
  }

  LogAudioOutput(device_->dev, sample_rate_);
  return true;
}

void QtMiniaudioSound::RecomputeLatencyTargets(int mix_ring_samples) {
  period_frames_ = static_cast<int>(
      M88AudioPeriodFrames(sample_rate_, mix_ring_samples));
  period_frames_ = std::max(period_frames_, 128);
  target_spsc_frames_ = period_frames_ * kTargetPeriods;
  max_spsc_frames_ = period_frames_ * kMaxPeriods;
  const int cfg_cap =
      static_cast<int>((static_cast<uint64_t>(sample_rate_) * current_buflen_ms_) / 1000);
  if (cfg_cap > 0) {
    max_spsc_frames_ = std::max(max_spsc_frames_, cfg_cap);
    target_spsc_frames_ = std::min(target_spsc_frames_, max_spsc_frames_);
  }
  const int ring_cap = static_cast<int>(spsc_.Capacity());
  if (ring_cap > 0) {
    max_spsc_frames_ = std::min(max_spsc_frames_, ring_cap);
    target_spsc_frames_ = std::min(target_spsc_frames_, max_spsc_frames_);
  }
}

void QtMiniaudioSound::TryRaiseAudioThreadPriority() {
#if defined(__linux__)
  static std::once_flag once;
  std::call_once(once, []() {
    sched_param sp {};
    sp.sched_priority = 2;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0) {
      setpriority(PRIO_PROCESS, 0, -10);
    }
  });
#endif
}

void QtMiniaudioSound::TeardownPlaybackDevice() {
  if (!device_) {
    return;
  }
  if (device_->active) {
    device_->dev.pUserData = nullptr;
    ma_device_stop(&device_->dev);
    ma_device_uninit(&device_->dev);
    device_->dev = {};
    device_->active = false;
  }
  if (device_->context_active) {
    ma_context_uninit(&device_->context);
    device_->context = {};
    device_->context_active = false;
  }
}

bool QtMiniaudioSound::IsPulseBackend() const {
  if (!device_ || !device_->active || !device_->dev.pContext) {
    return false;
  }
  return device_->dev.pContext->backend == ma_backend_pulseaudio;
}

bool QtMiniaudioSound::OpenExplicitPlayback(const char* backend_name, ma_uint32 open_rate,
                                            ma_uint32 frames_per_buffer) {
  if (!backend_name || backend_name[0] == '\0') {
    return false;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = ma_format_s16;
  config.playback.channels = 2;
  config.sampleRate = open_rate;
  config.periodSizeInFrames = frames_per_buffer;
  config.dataCallback = QtMiniaudioSoundDataCallback;
  config.pUserData = this;
  config.playback.pDeviceID = nullptr;

  if (!M88MiniaudioDevices::InitContext(backend_name, &device_->context)) {
    std::fprintf(stderr, "M88: explicit context init failed (backend=%s)\n", backend_name);
    return false;
  }
  device_->context_active = true;

  ma_device_id chosen_id {};
  const ma_device_id* device_id = nullptr;
  if (playback_device_name_[0] != '\0') {
    if (M88MiniaudioDevices::ResolvePlaybackIdInContext(
            &device_->context, playback_device_name_, &chosen_id)) {
      device_id = &chosen_id;
    } else {
      std::fprintf(stderr,
                   "M88: audio device not found: %s (backend=%s, using default)\n",
                   playback_device_name_, backend_name);
    }
  }
  config.playback.pDeviceID = device_id;

  if (ma_device_init(&device_->context, &config, &device_->dev) != MA_SUCCESS) {
    ma_context_uninit(&device_->context);
    device_->context = {};
    device_->context_active = false;
    return false;
  }

  if (ma_device_start(&device_->dev) != MA_SUCCESS) {
    ma_device_uninit(&device_->dev);
    device_->dev = {};
    ma_context_uninit(&device_->context);
    device_->context = {};
    device_->context_active = false;
    return false;
  }

  device_->active = true;
  return true;
}

bool QtMiniaudioSound::WarmPulsePlaybackStream() {
  if (!device_->active || !IsPulseBackend()) {
    return false;
  }
  if (ma_device_stop(&device_->dev) != MA_SUCCESS) {
    std::fprintf(stderr, "M88: pulse warm-up stop failed\n");
    return false;
  }
  if (ma_device_start(&device_->dev) != MA_SUCCESS) {
    std::fprintf(stderr, "M88: pulse warm-up start failed\n");
    return false;
  }
  return true;
}

void QtMiniaudioSound::CloseDevice() {
  if (!device_) {
    return;
  }
  TeardownPlaybackDevice();
  current_device_name_[0] = '\0';
  current_backend_name_[0] = '\0';
}

int QtMiniaudioSound::SamplesForEmuTicks(int emu_ticks) const {
  if (emu_ticks <= 0 || sample_rate_ < 8000) {
    return 0;
  }
  return static_cast<int>(
      (static_cast<uint64_t>(emu_ticks) * sample_rate_ + 99'999ULL) / 100'000ULL);
}

void QtMiniaudioSound::ResetPcmContract() {
  // Scheduler time does not rewind on CPU reset; zeroing contract_ticks/delivered
  // leaves a huge due-delivered backlog (same symptom as mid-run rate change).
  ResyncPcmContractAfterRateChange();
}

void QtMiniaudioSound::ResyncPcmContractAfterRateChange() {
  SyncContractTicks();
  delivered_frames_ = ContractFramesDue();
}

int QtMiniaudioSound::ContractFramesDue() const {
  if (sample_rate_ < 8000) {
    return 0;
  }
  return static_cast<int>(
      (contract_ticks_ * static_cast<uint64_t>(sample_rate_)) / 100'000ULL);
}

void QtMiniaudioSound::SyncContractTicks() {
  contract_ticks_ = static_cast<uint64_t>(GetEmuClockTicks());
}

int QtMiniaudioSound::MinPlaybackHeadroom() const {
  return std::max(period_frames_ * 2, 256);
}

int QtMiniaudioSound::SpscSleepNeed(int emu_sleep_ticks) const {
  if (emu_sleep_ticks <= 0) {
    return MinPlaybackHeadroom();
  }
  return SamplesForEmuTicks(emu_sleep_ticks) + period_frames_;
}

void QtMiniaudioSound::MaintainPlaybackHeadroom(int min_frames) {
  if (spsc_.Capacity() == 0 || min_frames <= 0) {
    return;
  }
  const int in_spsc = static_cast<int>(spsc_.Avail());
  if (in_spsc >= min_frames) {
    return;
  }
  SyncContractTicks();
  const int need = min_frames - in_spsc;
  int may_push = ContractFramesDue() - delivered_frames_;
  bool from_contract = may_push > 0;
  if (may_push <= 0) {
    if (GetRingAvail() < 61) {
      return;
    }
    may_push = std::min(need, period_frames_);
    from_contract = false;
  } else {
    may_push = std::min(need, may_push);
  }
  const int pushed = DrainFrames(may_push);
  if (pushed <= 0) {
    return;
  }
  if (from_contract) {
    delivered_frames_ += pushed;
  }
}

int QtMiniaudioSound::DrainFrames(int target_frames) {
  if (target_frames <= 0 || spsc_.Capacity() == 0) {
    return 0;
  }

  const int headroom = MaxSpscFrames() - static_cast<int>(spsc_.Avail());
  if (headroom <= 0) {
    return 0;
  }
  target_frames = std::min(target_frames, headroom);

  Sample scratch[kDrainChunkFrames * 2];
  int remaining = target_frames;
  int pushed = 0;
  int guard = 0;
  while (remaining > 0 && spsc_.Free() > 0 && guard++ < kDrainGuard) {
    const int want =
        std::min({remaining, kSpscDrainChunk, static_cast<int>(spsc_.Free())});
    const int got = Sound::GetOutput(scratch, want);
    if (got <= 0) {
      break;
    }
    spsc_.Push(scratch, static_cast<size_t>(got));
    remaining -= got;
    pushed += got;
  }
  return pushed;
}

void QtMiniaudioSound::MixSlice(int emu_ticks) {
  if (spsc_.Capacity() == 0 || emu_ticks <= 0) {
    return;
  }
  Update(nullptr);
  SyncContractTicks();
  const int due = ContractFramesDue();
  int backlog = due - delivered_frames_;
  if (backlog > 0) {
    const int headroom = MaxSpscFrames() - static_cast<int>(spsc_.Avail());
    const int push_goal = headroom > 0 ? std::min(backlog, headroom) : 0;
    const int pushed = push_goal > 0 ? DrainFrames(backlog) : 0;
    delivered_frames_ += pushed;
  }
  MaintainPlaybackHeadroom(MinPlaybackHeadroom());
}

void QtMiniaudioSound::CatchUpContract() {
  if (spsc_.Capacity() == 0) {
    return;
  }
  SyncContractTicks();
  const int backlog = ContractFramesDue() - delivered_frames_;
  if (backlog > 0) {
    const int headroom = MaxSpscFrames() - static_cast<int>(spsc_.Avail());
    const int push_goal = headroom > 0 ? std::min(backlog, headroom) : 0;
    const int pushed = push_goal > 0 ? DrainFrames(backlog) : 0;
    delivered_frames_ += pushed;
  }
  MaintainPlaybackHeadroom(MinPlaybackHeadroom());
}

void QtMiniaudioSound::PrepareSleep(int emu_sleep_ticks) {
  if (spsc_.Capacity() == 0 || emu_sleep_ticks <= 0) {
    return;
  }
  CatchUpContract();
  const int sleep_need = SpscSleepNeed(emu_sleep_ticks);
  MaintainPlaybackHeadroom(sleep_need);
}

int QtMiniaudioSound::GetSpscAvail() const {
  if (spsc_.Capacity() == 0) {
    return 0;
  }
  return static_cast<int>(spsc_.Avail());
}

int QtMiniaudioSound::GetSpscCapacity() const {
  if (spsc_.Capacity() == 0) {
    return 0;
  }
  return static_cast<int>(spsc_.Capacity());
}

void QtMiniaudioSound::FillAudio(int16_t* stream, int frame_count) {
  if (!stream || frame_count <= 0) {
    return;
  }
  TryRaiseAudioThreadPriority();
  auto* out = reinterpret_cast<Sample*>(stream);
  size_t written = spsc_.Pop(out, static_cast<size_t>(frame_count));
  int spin = 0;
  while (written < static_cast<size_t>(frame_count) && spin < kCallbackSpinIters) {
    if ((spin++ & 63) == 0) {
      std::this_thread::yield();
    }
    const size_t more =
        spsc_.Pop(out + written * 2, static_cast<size_t>(frame_count) - written);
    if (more == 0) {
      continue;
    }
    written += more;
    spin = 0;
  }
  if (written < static_cast<size_t>(frame_count)) {
    const size_t missing = static_cast<size_t>(frame_count) - written;
    std::memset(out + written * 2, 0, missing * 2 * sizeof(Sample));
  }
}
