#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "firmware/GBC_S3/gnuboy_adapter.h"
extern "C" {
#include "firmware/GBC_S3/src/gnuboy/save.h"
#include "firmware/GBC_S3/src/gnuboy/lcd.h"
#include "firmware/GBC_S3/src/gnuboy/sound.h"
#include "firmware/GBC_S3/src/gnuboy/mem.h"
}
extern "C" int gbc_audio_submit_pcm(const unsigned char *samples, size_t count) { (void)samples; (void)count; return 1; }
int main(int argc, char **argv) {
  if (argc != 6) return 2;
  uint16_t *frame = (uint16_t *)calloc(GbcCore::FRAME_PIXELS, sizeof(uint16_t));
  if (!frame) return 3;
  if (!GbcCore::beginPlatform(frame, GbcCore::FRAME_BYTES)) return 4;
  if (!GbcCore::loadRomPath(argv[1])) { fprintf(stderr,"ROM load failed\n"); return 5; }
  if ((GbcCore::colorMode()?1:0) != atoi(argv[3])) return 11;
  if ((GbcCore::hasBatteryRam()?1:0) != atoi(argv[4])) return 14;
  if ((GbcCore::hasRtc()?1:0) != atoi(argv[5])) return 15;
  if (!GbcCore::supportsCartridgeType(0x10) || GbcCore::supportsCartridgeType(0x20)) return 16;
  FILE *rf=fopen(argv[1],"rb"); if(!rf)return 12; uint32_t fileHash=2166136261u; int rb;
  while((rb=fgetc(rf))!=EOF){fileHash^=(uint8_t)rb;fileHash*=16777619u;} fclose(rf);
  if(fileHash!=GbcCore::romHash())return 13;
  for (int i=0;i<3;i++) if (GbcCore::runFrame()!=0) return 6;
  FILE *state=fopen(argv[2],"wb"); if(!state) return 7; savestate(state); fclose(state);
  for (int i=0;i<2;i++) if (GbcCore::runFrame()!=0) return 8;
  state=fopen(argv[2],"rb"); if(!state) return 9; loadstate(state); fclose(state);
  vram_dirty(); pal_dirty(); sound_dirty(); mem_updatemap();
  if (GbcCore::runFrame()!=0) return 10;
  uint32_t hash=2166136261u;
  const uint8_t *bytes=(const uint8_t *)GbcCore::framebuffer();
  for (size_t i=0;i<GbcCore::FRAME_BYTES;i++) { hash^=bytes[i];hash*=16777619u; }
  printf("GNUBoy adapter smoke PASS: CGB=%d ROM=%lu SRAM=%lu framebuffer=%08x\n",
         GbcCore::colorMode()?1:0,(unsigned long)GbcCore::romSize(),(unsigned long)GbcCore::cartRamSize(),hash);
  GbcCore::endPlatform(); free(frame); return 0;
}
