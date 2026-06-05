#pragma once

#include "headers.h"
#include "pc88/config.h"
#include "pc88/sound.h"

#include <SDL.h>

class PC88;

namespace PC8801 {

class LinuxSound : public Sound {
 public:
  LinuxSound();
  ~LinuxSound();

  bool Init(PC88* pc, uint rate, uint buflen_ms);
  void Cleanup();
  void ApplyConfig(const Config* config);

 private:
  bool ChangeRate(uint rate, uint buflen_ms);
  void CloseDevice();
  void FillAudio(Uint8* stream, int len_bytes);

  static void SdlCallback(void* userdata, Uint8* stream, int len);

  SDL_AudioDeviceID device_;
  uint sample_rate_;
  uint current_rate_;
  uint current_buflen_ms_;
};

}  // namespace PC8801
