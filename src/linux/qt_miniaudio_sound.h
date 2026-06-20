#pragma once

#include "headers.h"
#include "linux/m88_spsc_pcm.h"
#include "pc88/config.h"
#include "pc88/sound.h"

#include <memory>

class PC88;

struct ma_device;
using ma_uint32 = unsigned int;

void QtMiniaudioSoundDataCallback(ma_device* device, void* output, const void* input,
                                  ma_uint32 frame_count);

namespace PC8801 {

// Linux Qt miniaudio driver (emu-thread SPSC produce, miniaudio callback consume).
class QtMiniaudioSound : public Sound {
  friend void ::QtMiniaudioSoundDataCallback(ma_device*, void*, const void*, ma_uint32);
 public:
  QtMiniaudioSound();
  ~QtMiniaudioSound();

  bool Init(PC88* pc, uint rate, uint buflen_ms);
  void Cleanup();
  void ApplyConfig(const Config* config);

  // Lockstep PCM contract: deliver floor(T*fs/100000) frames to SPSC, no ahead-read.
  void MixSlice(int emu_ticks);
  void CatchUpContract();
  void PrepareSleep(int emu_sleep_ticks);
  void ResetPcmContract();

  int GetSpscAvail() const;
  int GetSpscCapacity() const;
  int MinPlaybackHeadroom() const;
  int SpscSleepNeed(int emu_sleep_ticks) const;

 private:
  struct Device;

  static size_t SpscCapacityFrames(uint sample_rate_hz, uint buflen_ms);
  int PrimeSpscSilence(int frames);

  bool ChangeRate(uint rate, uint buflen_ms);
  void CloseDevice();
  void FillAudio(int16_t* stream, int frame_count);
  void ResetSpsc(uint sample_rate_hz, uint buflen_ms);
  int DrainFrames(int target_frames);
  int ContractFramesDue() const;
  int SamplesForEmuTicks(int emu_ticks) const;
  void SyncContractTicks();
  void ResyncPcmContractAfterRateChange();
  void MaintainPlaybackHeadroom(int min_frames);
  void RecomputeLatencyTargets(int mix_ring_samples);
  void TryRaiseAudioThreadPriority();
  bool WarmPulsePlaybackStream();
  void TeardownPlaybackDevice();
  bool OpenExplicitPlayback(const char* backend_name, ma_uint32 open_rate,
                            ma_uint32 frames_per_buffer);
  bool IsPulseBackend() const;
  int MaxSpscFrames() const { return max_spsc_frames_; }

  std::unique_ptr<Device> device_;
  M88SpscPcmRing spsc_;
  uint sample_rate_;
  uint current_rate_;
  uint current_buflen_ms_;
  int period_frames_ = 512;
  int target_spsc_frames_ = 1536;
  int max_spsc_frames_ = 3072;
  char playback_device_name_[256] = {};
  char current_device_name_[256] = {};
  char playback_backend_name_[32] = {};
  char current_backend_name_[32] = {};
  uint64_t contract_ticks_ = 0;
  int delivered_frames_ = 0;
};

}  // namespace PC8801
