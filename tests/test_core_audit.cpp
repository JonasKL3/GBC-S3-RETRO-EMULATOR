#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "firmware/GBC_S3/gnuboy_adapter.h"
#include "firmware/GBC_S3/src/dabble/DabbleRxRing.h"
extern "C" {
#include "firmware/GBC_S3/src/gnuboy/defs.h"
#include "firmware/GBC_S3/src/gnuboy/save.h"
#include "firmware/GBC_S3/src/gnuboy/rtc.h"
#include "firmware/GBC_S3/src/gnuboy/hw.h"
#include "firmware/GBC_S3/src/gnuboy/cpu.h"
#include "firmware/GBC_S3/src/gnuboy/regs.h"
#include "firmware/GBC_S3/src/gnuboy/sound.h"
#include "firmware/GBC_S3/src/gnuboy/mem.h"
#include "firmware/GBC_S3/src/gnuboy/fastmem.h"
}
extern "C" int gbc_audio_submit_pcm(const unsigned char*,size_t){return 1;}

static FILE *bytes_file(const uint8_t *p,size_t n){
  FILE *f=tmpfile();assert(f);assert(fwrite(p,1,n,f)==n);rewind(f);return f;
}
static void ring_test(){
  DabbleRxRing ring;uint8_t packet[600];for(unsigned i=0;i<600;++i)packet[i]=(uint8_t)i;
  assert(ring.append(packet,600));assert(ring.size()==600);
  assert(!ring.append(packet,600));assert(ring.size()==600);
  for(unsigned i=0;i<500;++i)assert(ring.pop()==(uint8_t)i);
  assert(ring.append(packet,600));
  for(unsigned i=500;i<600;++i)assert(ring.pop()==(uint8_t)i);
  for(unsigned i=0;i<600;++i)assert(ring.pop()==(uint8_t)i);
  assert(ring.size()==0);assert(ring.pop()==0);assert(!ring.append(nullptr,1));
  assert(ring.append(packet,600));ring.clear();assert(ring.size()==0);
}
static void rtc_test(){
  memset(&rtc,0,sizeof rtc);rtc.sel=12;rtc_write(1);assert(rtc.d==256);
  rtc_latch(0);rtc_latch(1);assert((rtc.regs[4]&1)==1);
  rtc.d=511;rtc.h=23;rtc.m=59;rtc.s=59;rtc.t=59;rtc_tick();
  assert(rtc.d==0 && rtc.carry==1);
  const char invalid[]="0 0 999999999999999999999 0 0 0 0\n0\n";
  const struct rtc before=rtc;FILE *f=bytes_file((const uint8_t*)invalid,strlen(invalid));
  rtc_load_internal(f);fclose(f);assert(memcmp(&before,&rtc,sizeof rtc)==0);
  const char truncated[]="0 0 1";f=bytes_file((const uint8_t*)truncated,strlen(truncated));
  rtc_load_internal(f);fclose(f);assert(memcmp(&before,&rtc,sizeof rtc)==0);
  // Year 2000: the old implementation looped once per tick for decades.
  const char old[]="0 0 0 00 00 00 00\n946684800\n";
  f=bytes_file((const uint8_t*)old,strlen(old));rtc_load_internal(f);fclose(f);
  assert(rtc.d>=0 && rtc.d<512 && rtc.h<24 && rtc.m<60 && rtc.s<60);
}
int main(int argc,char **argv){
  setvbuf(stdout,nullptr,_IONBF,0);
#ifdef _WIN32
  _set_error_mode(1); // Route CRT assertions to stderr, never a desktop dialog.
  _set_abort_behavior(0,3);
#endif
  assert(argc==2);ring_test();rtc_test();puts("ring + RTC PASS");fflush(stdout);
  uint16_t *frame=(uint16_t*)calloc(GbcCore::FRAME_PIXELS,2);assert(frame);
  puts("platform BEGIN");assert(GbcCore::beginPlatform(frame,GbcCore::FRAME_BYTES));
  assert(!GbcCore::loadRomPath("/gbc_s3_test_missing_file/no_rom.gbc"));assert(GbcCore::lastLoadIoError()!=0);
  puts("ROM BEGIN");assert(GbcCore::loadRomPath(argv[1]));puts("ROM OK");
  for(int a=0;a<0x8000;a+=97)assert(readb(a)==mem_read(a));
  for(int a=0xc000;a<0xe000;a+=127){writeb(a,(byte)a);assert(readb(a)==mem_read(a));}
  writew(0xcfff,0xabcd);assert(readw(0xcfff)==0xabcd);
  writew(0xc010,0x1234);assert(readw(0xc010)==0x1234);
  writew(0xc011,0x5678);assert(readw(0xc011)==0x5678);
  assert(readw(0xffff)==(mem_read(0xffff)|((int)mem_read(0)<<8)));
  if(mbc.type==MBC_MBC2){
    mbc_write(0x2100,0);assert(mbc.rombank==1);mbc_write(0,0x0a);
    writeb(0xa000,0xab);assert(readb(0xa000)==0xfb);assert(readb(0xa200)==0xfb);
  }
  if(mbc.type==MBC_MBC3 && mbc.romsize>128){
    assert(mbc.ramsize==8);mbc_write(0x2000,200);assert(mbc.rombank==200);
    assert(readb(0x4000)==200);mbc_write(0,0x0a);mbc_write(0x4000,7);
    writeb(0xa000,0x6a);assert(ram.sbank[7][0]==0x6a);
  }
  if(mbc.type==MBC_MBC5){
    assert(mbc.ramsize==8);mbc_write(0x2000,0);assert(mbc.rombank==0);
    mbc_write(0,0x0a);mbc_write(0x4000,7);writeb(0xa000,0x5a);
    assert(ram.sbank[7][0]==0x5a);assert(readb(0xa000)==0x5a);
  }
  for(int i=0;i<30;++i)assert(GbcCore::runFrame()==0);
  // Exercise a negative sample in wave channel 3 with UB trapping enabled.
  memset(snd.wave,0,sizeof snd.wave);R_NR32=0x20;R_NR51=0x44;s3_init();
  cpu.snd=10000;sound_mix();puts("core frames + wave audio PASS");fflush(stdout);
  FILE *f=tmpfile();assert(f);assert(savestate_checked(f));assert(fflush(f)==0);
  const long length=ftell(f);assert(length>4096);
  uint8_t *state=(uint8_t*)malloc((size_t)length);assert(state);rewind(f);
  assert(fread(state,1,(size_t)length,f)==(size_t)length);fclose(f);
  assert(gnuboy_state_header_valid(state));
  f=bytes_file(state,(size_t)length);assert(loadstate_checked(f));fclose(f);
  struct cpu cpuBefore=cpu;struct hw hwBefore=hw;
  f=bytes_file(state,(size_t)length-1);assert(!loadstate_checked(f));fclose(f);
  assert(!memcmp(&cpu,&cpuBefore,sizeof cpu) && !memcmp(&hw,&hwBefore,sizeof hw));
  // Insert a duplicate offset where the required terminator belongs.
  unsigned off=0;while(off<3312 && memcmp(state+off,"\0\0\0\0",4))off+=8;
  assert(off<3312);memcpy(state+off,"hi  ",4);memset(state+off+4,0xff,4);
  assert(!gnuboy_state_header_valid(state));
  f=bytes_file(state,(size_t)length);assert(!loadstate_checked(f));fclose(f);
  assert(!memcmp(&cpu,&cpuBefore,sizeof cpu) && !memcmp(&hw,&hwBefore,sizeof hw));
  free(state);GbcCore::endPlatform();free(frame);
  puts("checked states + corruption rejection PASS");return 0;
}
