#include "gbc_s3_covers.h"
#include "gbc_paths.h"
#include <Arduino.h>
#include <SD.h>
#include <FS.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

namespace GbcS3Covers {
static constexpr unsigned WIDTH=104,HEIGHT=136,PIX_BYTES=WIDTH*HEIGHT*2;
static constexpr unsigned CACHE_COUNT=8,CHUNK=1024;
static constexpr uint32_t SETTLE_MS=160;
static constexpr uint16_t BACKGROUND=0x000B,FRAME=0x5AEB;
struct Entry {
  char rom[240];
  bool ready;
  uint32_t age,retryAt;
};
static Entry entries[CACHE_COUNT]={};
static uint16_t *pool=nullptr;
static uint32_t clockAge=0,selectedAt=0;
static char selectedRom[240]={};
static File input;
static int writing=-1;
static unsigned completed=0,nextSource=0;
static bool ioFailure=false,invalidFile=false;
static uint16_t le16(const uint8_t *p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint16_t *pixels(unsigned slot){return pool+slot*WIDTH*HEIGHT;}
static bool due(uint32_t now,uint32_t deadline){return (int32_t)(now-deadline)>=0;}

void cancel(){
  if(input)input.close();
  if(writing>=0 && !entries[writing].ready)entries[writing].rom[0]=0;
  writing=-1;completed=nextSource=0;selectedRom[0]=0;
  ioFailure=invalidFile=false;
}
void invalidate(){cancel();memset(entries,0,sizeof entries);}
bool begin(){
  if(!pool)pool=(uint16_t*)heap_caps_malloc(CACHE_COUNT*PIX_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!pool){Serial.println("[GBC][COVER][WARN] PSRAM indisponivel");return false;}
  invalidate();return true;
}
static int find(const char *rom){
  for(unsigned i=0;i<CACHE_COUNT;++i)if(!strcmp(entries[i].rom,rom))return (int)i;
  return -1;
}
static bool select(const char *rom){
  if(!rom||!rom[0]||strlen(rom)>=sizeof selectedRom)return false;
  if(strcmp(selectedRom,rom)){
    cancel();snprintf(selectedRom,sizeof selectedRom,"%s",rom);selectedAt=millis();
  }
  return true;
}
static int reserve(){
  int slot=find(selectedRom);
  if(slot<0){
    slot=0;
    for(unsigned i=0;i<CACHE_COUNT;++i){
      if(!entries[i].rom[0]){slot=(int)i;break;}
      if(entries[i].age<entries[slot].age)slot=(int)i;
    }
  }
  Entry &entry=entries[slot];memset(&entry,0,sizeof entry);
  snprintf(entry.rom,sizeof entry.rom,"%s",selectedRom);entry.age=++clockAge;
  return slot;
}
static void paint(TFT_eSPI &tft,unsigned slot,int x,int y){
  const bool swap=tft.getSwapBytes();tft.setSwapBytes(true);
  tft.pushImage(x,y,WIDTH,HEIGHT,pixels(slot));tft.setSwapBytes(swap);
  entries[slot].age=++clockAge;
}
static void failed(){
  if(input)input.close();
  Entry &entry=entries[writing];
  entry.ready=false;entry.retryAt=millis()+((ioFailure||invalidFile)?2000:5000);
  Serial.printf("[GBC][COVER][%s] %s retry=%ums\n",ioFailure?"IO_FAIL":invalidFile?"INVALID":"MISS",selectedRom,(ioFailure||invalidFile)?2000:5000);
  writing=-1;completed=nextSource=0;ioFailure=invalidFile=false;
}
void drawMissing(TFT_eSPI &tft, int x, int y) {
  tft.fillRoundRect(x, y, WIDTH, HEIGHT, 5, BACKGROUND);
  tft.drawRoundRect(x + 5, y + 5, WIDTH - 10, HEIGHT - 10, 4, FRAME);
  tft.setTextSize(2);
  tft.setTextColor(TFT_CYAN, BACKGROUND);
  tft.setCursor(x + 9, y + 42);
  tft.print("GBC-S3");
  tft.setTextSize(1);
  tft.setTextColor(TFT_LIGHTGREY, BACKGROUND);
  tft.setCursor(x + 17, y + 80);
  tft.print("SEM CAPA");
  tft.setCursor(x + 11, y + 96);
  tft.print("ARQUIVO .COV");
}

void draw(TFT_eSPI &tft,const char *rom,int x,int y){
  if(!pool && !begin()){drawMissing(tft,x,y);return;}
  if(!select(rom)){cancel();drawMissing(tft,x,y);return;}
  const int cached=find(rom);
  if(cached>=0 && entries[cached].ready)paint(tft,(unsigned)cached,x,y);
  else drawMissing(tft,x,y);
}

// Called only from the menu's producer task, after processing input. Each call
// performs at most one open/header operation or one 1024-byte data read. No new
// task touches shared SPI, and a partial file is never published to the cache.
void service(TFT_eSPI &tft,const char *rom,int x,int y){
  if(!pool||!select(rom))return;
  const uint32_t now=millis();
  if((uint32_t)(now-selectedAt)<SETTLE_MS)return;
  if(writing<0){
    const int cached=find(rom);
    if(cached>=0 && (entries[cached].ready||!due(now,entries[cached].retryAt)))return;
    writing=reserve();completed=nextSource=0;ioFailure=invalidFile=false;
  }
  if(!input){
    if(nextSource>=2){failed();return;}
    char stem[230],path[320];
    const char *name=strrchr(selectedRom,'/');name=name?name+1:selectedRom;
    if(!GbcS3Paths::isRomPath(name)||!GbcS3Paths::makeRomStem(stem,sizeof stem,name)||
       !GbcS3Paths::makeCoverPath(path,sizeof path,nextSource++==0?GbcS3Paths::COVER:GbcS3Paths::PNG,stem)){
      invalidFile=true;failed();return;
    }
    errno=0;input=SD.open(path,FILE_READ);
    if(!input){if(errno && errno!=ENOENT)ioFailure=true;return;}
    if(input.size()!=12+PIX_BYTES){invalidFile=true;input.close();return;}
    uint8_t header[12];errno=0;
    if(input.read(header,sizeof header)!=sizeof header){ioFailure=true;input.close();return;}
    if(memcmp(header,"NS3C",4)||le16(header+4)!=WIDTH||le16(header+6)!=HEIGHT||le16(header+8)!=1||le16(header+10)!=0){
      invalidFile=true;input.close();return;
    }
    return;
  }
  const unsigned count=PIX_BYTES-completed>CHUNK?CHUNK:PIX_BYTES-completed;
  errno=0;
  const size_t read=input.read((uint8_t*)pixels((unsigned)writing)+completed,count);
  if(read!=count){
    Serial.printf("[GBC][COVER][READ_FAIL] offset=%u expected=%u got=%u errno=%d\n",completed,count,(unsigned)read,errno);
    ioFailure=true;input.close();completed=0;return;
  }
  completed+=(unsigned)read;
  if(completed==PIX_BYTES){
    input.close(); // Release SD before the TFT transaction.
    const int slot=writing;entries[slot].ready=true;entries[slot].retryAt=0;
    writing=-1;completed=nextSource=0;
    paint(tft,(unsigned)slot,x,y);
    Serial.printf("[GBC][COVER][OK] %s bytes=%u cache=8\n",selectedRom,PIX_BYTES);
  }
}
} // namespace GbcS3Covers
