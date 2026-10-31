#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <Arduino.h>
#include <SD.h>
#include <TFT_eSPI.h>
#include "firmware/GBC_S3/gbc_s3_covers.h"
SerialClass Serial;SDClass SD;CoverIoStats coverIo;uint32_t coverTestNow=0;
static void makeCov(const char *name,bool invalid=false){
  char path[512];snprintf(path,sizeof path,"%s/JOGOS/GBC/cover/%s.cov",GBC_HOST_SD_ROOT,name);
  FILE *f=fopen(path,"wb");assert(f);
  const unsigned char header[12]={'N','S','3','C',104,0,136,0,1,0,(unsigned char)(invalid?1:0),0};
  assert(fwrite(header,1,12,f)==12);
  const unsigned short colors[]={0xffff,0xf800,0x07e0,0x001f,0x8410,0};
  for(unsigned i=0;i<104*136;++i){unsigned short c=colors[i%6];fputc(c&255,f);fputc(c>>8,f);}
  assert(fclose(f)==0);
}
static void settle(TFT_eSPI &tft,const char *rom){
  coverTestNow+=160;
  for(unsigned i=0;i<36;++i)GbcS3Covers::service(tft,rom,5,6);
}
int main(){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  assert(SD.mkdir("/JOGOS/GBC/cover"));assert(SD.mkdir("/JOGOS/GBC/png"));
  makeCov("test");makeCov("other");makeCov("partial");makeCov("bad",true);
  TFT_eSPI tft;assert(GbcS3Covers::begin());
  const char *test="/JOGOS/GBC/jogos/test.gbc";
  unsigned opens=coverIo.openCalls;
  GbcS3Covers::draw(tft,test,5,6);assert(tft.pushes==0&&coverIo.openCalls==opens);
  coverTestNow=159;GbcS3Covers::service(tft,test,5,6);assert(coverIo.openCalls==opens);
  coverTestNow=160;GbcS3Covers::service(tft,test,5,6);assert(coverIo.live==1);
  for(unsigned i=0;i<28;++i)GbcS3Covers::service(tft,test,5,6);
  assert(tft.pushes==1&&coverIo.live==0&&coverIo.maxRead<=1024&&!tft.getSwapBytes());
  const uint16_t colors[]={0xffff,0xf800,0x07e0,0x001f,0x8410,0};
  for(unsigned i=0;i<104*136;++i)assert(tft.pixels[(6+i/104)*320+5+i%104]==colors[i%6]);
  assert(SD.remove("/JOGOS/GBC/cover/test.cov"));
  GbcS3Covers::draw(tft,"/JOGOS/GBC/jogos/other.gbc",5,6);settle(tft,"/JOGOS/GBC/jogos/other.gbc");
  opens=coverIo.openCalls;unsigned pushes=tft.pushes;
  GbcS3Covers::draw(tft,test,5,6);assert(tft.pushes==pushes+1&&coverIo.openCalls==opens);
  // A partially read source never becomes a cached image; cancel closes it.
  const char *partial="/JOGOS/GBC/jogos/partial.gbc";
  GbcS3Covers::draw(tft,partial,5,6);coverTestNow+=160;
  GbcS3Covers::service(tft,partial,5,6);GbcS3Covers::service(tft,partial,5,6);assert(coverIo.live==1);
  GbcS3Covers::cancel();assert(coverIo.live==0);
  pushes=tft.pushes;GbcS3Covers::draw(tft,partial,5,6);assert(tft.pushes==pushes);
  settle(tft,partial);assert(tft.pushes==pushes+1);
  // Missing/invalid sources have a bounded retry delay rather than permanent misses.
  const char *later="/JOGOS/GBC/jogos/later.gbc";
  GbcS3Covers::draw(tft,later,5,6);settle(tft,later);opens=coverIo.openCalls;
  for(unsigned i=0;i<20;++i)GbcS3Covers::service(tft,later,5,6);
  assert(coverIo.openCalls==opens);makeCov("later");coverTestNow+=5000;
  pushes=tft.pushes;for(unsigned i=0;i<36;++i)GbcS3Covers::service(tft,later,5,6);
  assert(tft.pushes==pushes+1);
  const char *bad="/JOGOS/GBC/jogos/bad.gbc";
  GbcS3Covers::draw(tft,bad,5,6);pushes=tft.pushes;settle(tft,bad);assert(tft.pushes==pushes);
  makeCov("bad");coverTestNow+=2000;for(unsigned i=0;i<36;++i)GbcS3Covers::service(tft,bad,5,6);
  assert(tft.pushes==pushes+1);
  GbcS3Covers::invalidate();pushes=tft.pushes;GbcS3Covers::draw(tft,partial,5,6);assert(tft.pushes==pushes);
  assert(coverIo.live==0);
  puts("Covers PASS: no IO in draw, 160ms settle, 1024-byte maximum read, exact RGB565, cache hit without source, partial cancellation, missing/invalid retry, byte-swap restored");
}
