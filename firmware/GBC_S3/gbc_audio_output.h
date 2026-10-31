#pragma once
#include <stddef.h>
#include <stdint.h>
namespace GbcAudioOutput {
  static constexpr int BCLK = 15, LRC = 16, DIN = 17;
  static constexpr int RATE = 22050, MAX_SAMPLES = 512;
  bool begin();
  void pause(bool paused);
  bool initialized();
  bool enabled();
  void setEnabled(bool on);
  void setVolume(int percent);
  int volume();


  bool submitPcm(const uint8_t *samples, size_t count);
}
