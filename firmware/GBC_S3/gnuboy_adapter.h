#pragma once
#include <stddef.h>
#include <stdint.h>

namespace GbcCore {
static constexpr uint16_t WIDTH = 160;
static constexpr uint16_t HEIGHT = 144;
static constexpr size_t FRAME_PIXELS = (size_t)WIDTH * HEIGHT;
static constexpr size_t FRAME_BYTES = FRAME_PIXELS * sizeof(uint16_t);

// Initialize the GNUBoy platform layer and allocate its large tile cache in PSRAM.
bool beginPlatform(uint16_t *framebuffer, size_t framebufferBytes);
void endPlatform();

// path is a VFS path, e.g. /sd/JOGOS/GBC/jogos/Game.gbc.
bool supportsCartridgeType(uint8_t type);
bool loadRomPath(const char *path);
int lastLoadIoError();
void unloadRom();
bool loaded();
void reset();
int runFrame();
void setFrameRendering(bool enabled); // Skip pixel generation, never CPU/PPU timing.
void setButtons(uint16_t buttons); // BTN_A..BTN_RIGHT mask from the shared frontend
uint16_t *framebuffer();
size_t romSize();
uint32_t romHash();
uint8_t cartType();
uint8_t ramSizeCode();
bool colorMode();
bool hasBatteryRam();
bool hasRtc();
uint8_t *cartRamData();
size_t cartRamSize();       // cartridge-declared RAM; zero if header says no RAM
size_t stateRamSize();      // RAM bytes serialized by GNUBoy's own state format
bool soundMasterEnabled();
void loadRtcFile(const char *vfsPath);
void saveRtcFile(const char *vfsPath);
}
