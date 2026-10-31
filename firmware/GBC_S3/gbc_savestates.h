#pragma once
#include <stdint.h>
namespace GbcSaveState {
  static constexpr uint32_t AUTO_INTERVAL_MS = 120000UL;
  void selectGame(const char *romPath, uint32_t romHash, uint32_t romSize);
  bool enabled();
  void setEnabled(bool value);
  bool present(int slot);
  bool save(int slot);
  bool load(int slot);
  void resetAutoTimer();
  bool autoDue();
  int saveAuto(); // 1=saved, 0=not due, -1=failed
  const char *gameKey();
}
