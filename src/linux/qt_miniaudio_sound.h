#pragma once

#include "headers.h"
#include "pc88/config.h"
#include "pc88/sound.h"

#include <memory>

class PC88;

struct ma_device;
using ma_uint32 = unsigned int;

void QtMiniaudioSoundDataCallback(ma_device* device, void* output, const void* input,
                                  ma_uint32 frame_count);

namespace PC8801 {

class QtMiniaudioSound : public Sound {
  friend void ::QtMiniaudioSoundDataCallback(ma_device*, void*, const void*, ma_uint32);
 public:
  QtMiniaudioSound();
  ~QtMiniaudioSound();

  bool Init(PC88* pc, uint rate, uint buflen_ms);
  void Cleanup();
  void ApplyConfig(const Config* config);

 private:
  struct Device;

  bool ChangeRate(uint rate, uint buflen_ms);
  void CloseDevice();
  void FillAudio(int16_t* stream, int frame_count);

  std::unique_ptr<Device> device_;
  uint sample_rate_;
  uint current_rate_;
  uint current_buflen_ms_;
};

}  // namespace PC8801
