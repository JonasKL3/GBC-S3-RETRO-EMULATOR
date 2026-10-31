#include "gnuboy_adapter.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

extern "C" {
#include "src/gnuboy/emu.h"
#include "src/gnuboy/fb.h"
#include "src/gnuboy/hw.h"
#include "src/gnuboy/lcd.h"
#include "src/gnuboy/loader.h"
#include "src/gnuboy/mem.h"
#include "src/gnuboy/pcm.h"
#include "src/gnuboy/regs.h"
#include "src/gnuboy/rtc.h"
#include "src/gnuboy/sound.h"
void gnuboy_pcm_configure(int hz, int length);
}

namespace GbcCore {
static uint16_t *activeFramebuffer = nullptr;
static uint8_t *patternCache = nullptr;
static bool platformReady = false;
static bool romLoaded = false;
static size_t activeRomBytes = 0;

bool beginPlatform(uint16_t *framebuffer, size_t framebufferBytes) {
  if (!framebuffer || framebufferBytes < FRAME_BYTES) return false;
  activeFramebuffer = framebuffer;

  if (!patternCache) {
    patternCache = static_cast<uint8_t *>(heap_caps_malloc(
        4096U * 8U * 8U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!patternCache) return false;
  }
  if (lcd_set_pattern_buffer(patternCache, 4096U * 8U * 8U) != 0) return false;

  memset(activeFramebuffer, 0, FRAME_BYTES);
  fb.ptr = reinterpret_cast<byte *>(activeFramebuffer);
  fb.w = WIDTH;
  fb.h = HEIGHT;
  fb.pelsize = 2;
  fb.pitch = WIDTH * sizeof(uint16_t);
  fb.indexed = 0;
  fb.cc[0].r = 3; fb.cc[0].l = 11; // RGB565 red
  fb.cc[1].r = 2; fb.cc[1].l = 5;  // RGB565 green
  fb.cc[2].r = 3; fb.cc[2].l = 0;  // RGB565 blue
  fb.cc[3].r = 0; fb.cc[3].l = 0;
  fb.yuv = 0;
  fb.enabled = 1;
  fb.dirty = 1;
  fb.delegate_scaling = 1;

  gnuboy_pcm_configure(22050, 512);
  emu_init();
  platformReady = true;
  return true;
}

void unloadRom() {
  if (romLoaded || rom.bank || ram.sbank || bootrom.bank) loader_unload();
  romLoaded = false;
  activeRomBytes = 0;
  if (activeFramebuffer) memset(activeFramebuffer, 0, FRAME_BYTES);
}

void endPlatform() {
  unloadRom();
  if (patternCache) {
    lcd_set_pattern_buffer(nullptr, 0);
    heap_caps_free(patternCache);
    patternCache = nullptr;
  }
  activeFramebuffer = nullptr;
  platformReady = false;
}

bool supportsCartridgeType(uint8_t type) {
  // Mapper IDs implemented by this GNUBoy revision. Reject unknown IDs rather
  // than silently booting them as ROM-only and producing misleading failures.
  switch (type) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x05: case 0x06: case 0x08: case 0x09:
    case 0x0F: case 0x10: case 0x11: case 0x12: case 0x13:
    case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E:
    case 0xFE: case 0xFF:
      return true;
    default:
      return false;
  }
}

bool loadRomPath(const char *path) {
  if (!platformReady || !path || !path[0]) return false;
  unloadRom();
  // GNUBoy keeps some machine registers in globals. Clear the previous
  // cartridge's state only after its owned allocations have been released.
  memset(&mbc, 0, sizeof(mbc));
  memset(&hw, 0, sizeof(hw));
  memset(&rtc, 0, sizeof(rtc));
  memset(&ram, 0, sizeof(ram));
  memset(&rom, 0, sizeof(rom));
  memset(&bootrom, 0, sizeof(bootrom));
  char *ownedPath = strdup(path);
  if (!ownedPath) return false;

  if (rom_load_simple(ownedPath) != 0) {
    fprintf(stderr,"[GBC][CORE][LOAD_FAIL] %s\n",
            loader_get_error()?loader_get_error():"ROM read/allocation failed");
    // rom_load_simple stores the allocated path even on failure; loader_unload
    // owns and releases it, along with any partially initialized core memory.
    loader_unload();
    romLoaded = false;
    return false;
  }
  if (!rom.bank || mbc.romsize <= 0 || !ram.sbank || mbc.ramsize <= 0) {
    loader_unload();
    romLoaded = false;
    return false;
  }

  activeRomBytes = (size_t)mbc.romsize * 16384U;
  // GNUBoy's upstream default leaves RAM uninitialized when memfill=-1.
  // Use deterministic zeroed cartridge/work RAM for a stable embedded boot.
  memset(ram.sbank, 0, 8192U * (size_t)mbc.ramsize);
  memset(ram.ibank, 0, sizeof(ram.ibank));
  fb.enabled=1;
  emu_reset();
  vram_dirty();
  pal_dirty();
  sound_dirty();
  romLoaded = true;
  return true;
}

int lastLoadIoError(){return loader_get_io_errno();}

bool loaded() { return romLoaded; }
void reset() { if (romLoaded) { fb.enabled=1; emu_reset(); } }
int runFrame() { return romLoaded ? emu_run_frame() : -1; }
void setFrameRendering(bool enabled) { fb.enabled=enabled?1:0; }

void setButtons(uint16_t buttons) {
  if (!romLoaded) return;
  pad_set(PAD_A,      (buttons & (1U << 0)) != 0);
  pad_set(PAD_B,      (buttons & (1U << 1)) != 0);
  pad_set(PAD_SELECT, (buttons & (1U << 2)) != 0);
  pad_set(PAD_START,  (buttons & (1U << 3)) != 0);
  pad_set(PAD_UP,     (buttons & (1U << 4)) != 0);
  pad_set(PAD_DOWN,   (buttons & (1U << 5)) != 0);
  pad_set(PAD_LEFT,   (buttons & (1U << 6)) != 0);
  pad_set(PAD_RIGHT,  (buttons & (1U << 7)) != 0);
}

uint16_t *framebuffer() { return activeFramebuffer; }
size_t romSize() { return activeRomBytes; }
uint32_t romHash() {
  if (!romLoaded || !rom.bank || !activeRomBytes) return 0;
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(rom.bank);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < activeRomBytes; ++i) {
    hash ^= bytes[i]; hash *= 16777619UL;
#ifdef ARDUINO_ARCH_ESP32
    if ((i & 65535U) == 65535U) vTaskDelay(1);
#endif
  }
  return hash;
}
uint8_t cartType() { return (rom.bank && rom.bank[0]) ? rom.bank[0][0x147] : 0xFF; }
uint8_t ramSizeCode() { return (rom.bank && rom.bank[0]) ? rom.bank[0][0x149] : 0; }
bool colorMode() { return hw.cgb != 0; }
bool hasBatteryRam() { return romLoaded && mbc.batt && (ramSizeCode() != 0 || mbc.type == MBC_MBC2); }
bool hasRtc() { return romLoaded && rtc.batt != 0; }
uint8_t *cartRamData() { return reinterpret_cast<uint8_t *>(ram.sbank); }
size_t stateRamSize() { return romLoaded && mbc.ramsize > 0 ? (size_t)mbc.ramsize * 8192U : 0; }
size_t cartRamSize() { return hasBatteryRam() ? stateRamSize() : 0; }
bool soundMasterEnabled() { return (R_NR52 & 0x80) != 0; }

void loadRtcFile(const char *vfsPath) {
  if (!hasRtc() || !vfsPath || !vfsPath[0]) return;
  FILE *f = fopen(vfsPath, "rb");
  if (!f) return;
  rtc_load_internal(f);
  fclose(f);
}
void saveRtcFile(const char *vfsPath) {
  if (!hasRtc() || !vfsPath || !vfsPath[0]) return;
  FILE *f = fopen(vfsPath, "wb");
  if (!f) return;
  rtc_save_internal(f);
  fflush(f);
  fclose(f);
}
} // namespace GbcCore
