#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "firmware/GBC_S3/gnuboy_adapter.h"
#include "firmware/GBC_S3/gbc_savestates.h"
#include "firmware/GBC_S3/gbc_paths.h"
#include "tests/state_stubs/Arduino.h"
#include "tests/state_stubs/SD.h"
SerialClass Serial;SDClass SD;
extern "C" int gbc_audio_submit_pcm(const unsigned char*,size_t){return 1;}
int main(int argc,char **argv){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  assert(argc==2);
  uint16_t *frame=(uint16_t*)calloc(GbcCore::FRAME_PIXELS,2);assert(frame);
  assert(GbcCore::beginPlatform(frame,GbcCore::FRAME_BYTES));assert(GbcCore::loadRomPath(argv[1]));
  const char *romPath="/JOGOS/GBC/jogos/smoke.gbc";
  const uint32_t hash=GbcCore::romHash();uint32_t pathHash=2166136261u;
  for(const char *p=romPath;*p;++p)pathHash=(pathHash^(uint8_t)*p)*16777619u;
  GbcSaveState::selectGame(romPath,hash,(uint32_t)GbcCore::romSize());
  assert(!GbcSaveState::enabled());assert(!GbcSaveState::autoDue());
  assert(GbcSaveState::saveAuto()==0);
  assert(GbcSaveState::save(0));assert(GbcSaveState::save(0));
  char path[128],full[512];assert(GbcS3Paths::makeStatePath(path,sizeof path,pathHash,hash,0));
  snprintf(full,sizeof full,"%s%s",GBC_HOST_SD_ROOT,path);
  FILE *f=fopen(full,"r+b");assert(f);assert(fseek(f,64,SEEK_SET)==0);int b=fgetc(f);assert(b!=EOF);
  assert(fseek(f,64,SEEK_SET)==0);fputc(b^0x55,f);assert(fclose(f)==0);
  GbcSaveState::selectGame(romPath,hash,(uint32_t)GbcCore::romSize());
  assert(GbcSaveState::load(0));assert(GbcSaveState::present(0));
  GbcCore::endPlatform();free(frame);
  puts("Frontend save/load + CRC rejection + backup recovery PASS");return 0;
}
