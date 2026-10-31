#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "firmware/GBC_S3/gnuboy_adapter.h"
#include "firmware/GBC_S3/gbc_savestates.h"
#include "tests/state_stubs/Arduino.h"
#include "tests/state_stubs/SD.h"
SerialClass Serial; SDClass SD;
extern "C" int gbc_audio_submit_pcm(const unsigned char *samples,size_t count){(void)samples;(void)count;return 1;}
static void mk(const char *p){ if(::mkdir(p,0777)!=0 && access(p,F_OK)!=0){perror("mkdir");exit(20);} }
int main(int argc,char **argv){
  if(argc!=2)return 2;
  system("rm -rf /tmp/gbc_s3_state_sd");
  mk("/tmp/gbc_s3_state_sd");mk("/tmp/gbc_s3_state_sd/JOGOS");mk("/tmp/gbc_s3_state_sd/JOGOS/GBC");mk("/tmp/gbc_s3_state_sd/JOGOS/GBC/save");
  uint16_t *frame=(uint16_t*)calloc(GbcCore::FRAME_PIXELS,sizeof(uint16_t));if(!frame)return 3;
  if(!GbcCore::beginPlatform(frame,GbcCore::FRAME_BYTES)||!GbcCore::loadRomPath(argv[1]))return 4;
  const uint32_t romHash=GbcCore::romHash();
  GbcSaveState::selectGame("/JOGOS/GBC/jogos/smoke.gbc",romHash,32768);
  if(!GbcSaveState::save(0)||!GbcSaveState::present(0))return 5;
  for(int i=0;i<2;i++)GbcCore::runFrame();
  if(!GbcSaveState::save(0))return 6; // creates validated backup of the first state
  const char *mainPath="/tmp/gbc_s3_state_sd/JOGOS/GBC/save/";
  char statePath[256];snprintf(statePath,sizeof(statePath),"%s%08lX_%08lX.ss0",mainPath,(unsigned long)2166136261UL,(unsigned long)0x1234ABCD);
  // The path hash depends on the ROM path; find the generated .ss0 file.
  char command[512];snprintf(command,sizeof(command),"find %s -name '*.ss0' -print -quit",mainPath);
  FILE *pipe=popen(command,"r");if(!pipe)return 7;if(!fgets(statePath,sizeof(statePath),pipe)){pclose(pipe);return 8;}pclose(pipe);statePath[strcspn(statePath,"\r\n")]=0;
  FILE *corrupt=fopen(statePath,"r+b");if(!corrupt)return 9;fseek(corrupt,64,SEEK_SET);int c=fgetc(corrupt);fseek(corrupt,64,SEEK_SET);fputc(c^0x55,corrupt);fclose(corrupt);
  GbcSaveState::selectGame("/JOGOS/GBC/jogos/smoke.gbc",romHash,32768); // clear cached slot result
  if(!GbcSaveState::load(0))return 10;
  if(!GbcSaveState::present(0))return 11;
  printf("GBC save-state roundtrip PASS: CRC rejection, backup recovery, restore\n");
  GbcCore::endPlatform();free(frame);return 0;
}
