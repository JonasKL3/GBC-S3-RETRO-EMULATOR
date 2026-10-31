
#include <string.h>
#include <stdio.h>

#include "defs.h"
#include "cpu.h"
#include "cpuregs.h"
#include "hw.h"
#include "regs.h"
#include "lcd.h"
#include "rtc.h"
#include "mem.h"
#include "sound.h"
#include "port_memory.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif


#define I1(s, p) { 1, s, p }
#define I2(s, p) { 2, s, p }
#define I4(s, p) { 4, s, p }
#define R(r) I1(#r, &R_##r)
#define NOSAVE { -1, "\0\0\0\0", 0 }
#define END { 0, "\0\0\0\0", 0 }

struct svar
{
	int len;
	char key[4];
	void *ptr;
};

static int ver;
static int sramblock, iramblock, vramblock;
static int hramofs, hiofs, palofs, oamofs, wavofs;

struct svar svars[] = 
{
	I4("GbSs", &ver),
	
	I2("PC  ", &PC),
	I2("SP  ", &SP),
	I2("BC  ", &BC),
	I2("DE  ", &DE),
	I2("HL  ", &HL),
	I2("AF  ", &AF),
	
	I4("IME ", &cpu.ime),
	I4("ima ", &cpu.ima),
	I4("spd ", &cpu.speed),
	I4("halt", &cpu.halt),
	I4("div ", &cpu.div),
	I4("tim ", &cpu.tim),
	I4("lcdc", &cpu.lcdc),
	I4("snd ", &cpu.snd),
	
	I1("ints", &hw.ilines),
	I1("pad ", &hw.pad),
	I4("cgb ", &hw.cgb),
	I4("gba ", &hw.gba),
	
	I4("mbcm", &mbc.model),
	I4("romb", &mbc.rombank),
	I4("ramb", &mbc.rambank),
	I4("enab", &mbc.enableram),
	I4("batt", &mbc.batt),
	
	I4("rtcR", &rtc.sel),
	I4("rtcL", &rtc.latch),
	I4("rtcC", &rtc.carry),
	I4("rtcS", &rtc.stop),
	I4("rtcd", &rtc.d),
	I4("rtch", &rtc.h),
	I4("rtcm", &rtc.m),
	I4("rtcs", &rtc.s),
	I4("rtct", &rtc.t),
	I1("rtR8", &rtc.regs[0]),
	I1("rtR9", &rtc.regs[1]),
	I1("rtRA", &rtc.regs[2]),
	I1("rtRB", &rtc.regs[3]),
	I1("rtRC", &rtc.regs[4]),

	I4("S1on", &snd.ch[0].on),
	I4("S1p ", &snd.ch[0].pos),
	I4("S1c ", &snd.ch[0].cnt),
	I4("S1ec", &snd.ch[0].encnt),
	I4("S1sc", &snd.ch[0].swcnt),
	I4("S1sf", &snd.ch[0].swfreq),

	I4("S2on", &snd.ch[1].on),
	I4("S2p ", &snd.ch[1].pos),
	I4("S2c ", &snd.ch[1].cnt),
	I4("S2ec", &snd.ch[1].encnt),
	
	I4("S3on", &snd.ch[2].on),
	I4("S3p ", &snd.ch[2].pos),
	I4("S3c ", &snd.ch[2].cnt),
	
	I4("S4on", &snd.ch[3].on),
	I4("S4p ", &snd.ch[3].pos),
	I4("S4c ", &snd.ch[3].cnt),
	I4("S4ec", &snd.ch[3].encnt),
	
	I4("hdma", &hw.hdma),
	
	I4("sram", &sramblock),
	I4("iram", &iramblock),
	I4("vram", &vramblock),
	I4("hi  ", &hiofs),
	I4("pal ", &palofs),
	I4("oam ", &oamofs),
	I4("wav ", &wavofs),
	
	/* NOSAVE is a special code to prevent the rest of the table
	 * from being saved, used to support old stuff for backwards
	 * compatibility... */
	NOSAVE,

	/* the following are obsolete as of 0x104 */
	
	I4("hram", &hramofs),
	
	R(P1), R(SB), R(SC),
	R(DIV), R(TIMA), R(TMA), R(TAC),
	R(IE), R(IF),
	R(LCDC), R(STAT), R(LY), R(LYC),
	R(SCX), R(SCY), R(WX), R(WY),
	R(BGP), R(OBP0), R(OBP1),
	R(DMA),

	R(VBK), R(SVBK), R(KEY1),
	R(BCPS), R(BCPD), R(OCPS), R(OCPD),

	R(NR10), R(NR11), R(NR12), R(NR13), R(NR14),
	R(NR21), R(NR22), R(NR23), R(NR24),
	R(NR30), R(NR31), R(NR32), R(NR33), R(NR34),
	R(NR41), R(NR42), R(NR43), R(NR44),
	R(NR50), R(NR51), R(NR52),

	I1("DMA1", &R_HDMA1),
	I1("DMA2", &R_HDMA2),
	I1("DMA3", &R_HDMA3),
	I1("DMA4", &R_HDMA4),
	I1("DMA5", &R_HDMA5),
	
	END
};



static void state_yield(void) {
#ifdef ARDUINO_ARCH_ESP32
    vTaskDelay(1);
#endif
}

static un32 header_value(const byte *p) {
    return (un32)p[0] | ((un32)p[1]<<8) | ((un32)p[2]<<16) | ((un32)p[3]<<24);
}

int gnuboy_state_header_valid(const unsigned char *buf) {
    int i;
    const un32 irl=hw.cgb?8U:2U, vrl=hw.cgb?4U:2U;
    if (!buf || mbc.ramsize<1 || mbc.ramsize>16) return 0;
    // Our 0x105 writer has a canonical field order. Requiring it also rejects
    // duplicate fields: the old loader could apply an unchecked second offset.
    for(i=0;svars[i].len>0;++i) {
        const byte *entry=buf+(size_t)i*8U;
        if ((size_t)i*8U+8U>3312U || memcmp(entry,svars[i].key,4)) return 0;
        const un32 d=header_value(entry+4);
        if ((!memcmp(entry,"GbSs",4) && d!=0x105U) ||
            (!memcmp(entry,"iram",4) && d!=1U) ||
            (!memcmp(entry,"vram",4) && d!=1U+irl) ||
            (!memcmp(entry,"sram",4) && d!=1U+irl+vrl) ||
            (!memcmp(entry,"hi  ",4) && d!=3328U) ||
            (!memcmp(entry,"wav ",4) && d!=3312U) ||
            (!memcmp(entry,"pal ",4) && d!=3584U) ||
            (!memcmp(entry,"oam ",4) && d!=3840U) ||
            (!memcmp(entry,"cgb ",4) && d!=(un32)hw.cgb) ||
            (!memcmp(entry,"gba ",4) && d>1U) ||
            (!memcmp(entry,"spd ",4) && d>1U) ||
            (!memcmp(entry,"rtcd",4) && d>511U) ||
            (!memcmp(entry,"rtch",4) && d>23U) ||
            (!memcmp(entry,"rtcm",4) && d>59U) ||
            (!memcmp(entry,"rtcs",4) && d>59U) ||
            (!memcmp(entry,"rtct",4) && d>59U)) return 0;
    }
    return header_value(buf+(size_t)i*8U)==0 && header_value(buf+(size_t)i*8U+4U)==0;
}

static int state_read(FILE *f, byte *dst, size_t bytes) {
    while(bytes) {
        const size_t n=bytes>4096U?4096U:bytes;
        if(fread(dst,1,n,f)!=n)return 0;
        dst+=n;bytes-=n;state_yield();
    }
    return 1;
}
static int state_write(FILE *f,const void *src,size_t bytes) {
    const byte *p=src;
    while(bytes) {
        const size_t n=bytes>4096U?4096U:bytes;
        if(fwrite(p,1,n,f)!=n)return 0;
        p+=n;bytes-=n;state_yield();
    }
    return 1;
}

int loadstate_checked(FILE *f) {
    const size_t irl=hw.cgb?8U:2U, vrl=hw.cgb?4U:2U;
    if (!f || !ram.sbank || mbc.ramsize<1 || mbc.ramsize>16)return 0;
    const size_t bytes=4096U*(1U+irl+vrl+(size_t)mbc.ramsize*2U);
    if(fseek(f,0,SEEK_END)!=0 || ftell(f)!=(long)bytes || fseek(f,0,SEEK_SET)!=0)return 0;
    // Read everything before touching emulated state. A late SD error leaves
    // CPU, RAM, VRAM and mapper unchanged. No 4 KiB buffer on loopTask stack.
    byte *buf=gnuboy_psram_malloc(bytes);
    if(!buf)return 0;
    if(!state_read(f,buf,bytes) || !gnuboy_state_header_valid(buf)) {
        gnuboy_psram_free(buf);return 0;
    }
    for(int i=0;svars[i].len>0;++i) {
        const un32 d=header_value(buf+(size_t)i*8U+4U);
        switch(svars[i].len) {
          case 1: *(byte*)svars[i].ptr=(byte)d;break;
          case 2: *(un16*)svars[i].ptr=(un16)d;break;
          case 4: memcpy(svars[i].ptr,&d,4);break;
        }
    }
    memcpy(ram.hi,buf+3328,sizeof ram.hi);
    memcpy(lcd.pal,buf+3584,sizeof lcd.pal);
    memcpy(lcd.oam.mem,buf+3840,sizeof lcd.oam);
    memcpy(snd.wave,buf+3312,sizeof snd.wave);
    memcpy(ram.ibank,buf+4096U,4096U*irl);
    memcpy(lcd.vbank,buf+4096U*(1U+irl),4096U*vrl);
    memcpy(ram.sbank,buf+4096U*(1U+irl+vrl),8192U*(size_t)mbc.ramsize);
    gnuboy_psram_free(buf);return 1;
}

int savestate_checked(FILE *f) {
    const int irl=hw.cgb?8:2, vrl=hw.cgb?4:2;
    if(!f || !ram.sbank || mbc.ramsize<1 || mbc.ramsize>16)return 0;
    byte *buf=gnuboy_psram_malloc(4096U);
    if(!buf)return 0;
    un32 (*header)[2]=(un32(*)[2])buf;
    ver=0x105;iramblock=1;vramblock=1+irl;sramblock=1+irl+vrl;
    wavofs=3312;hiofs=3328;palofs=3584;oamofs=3840;
    memset(buf,0,4096U);
    for(int i=0;svars[i].len>0;++i) {
        un32 d=0;
        memcpy(header[i],svars[i].key,4);
        switch(svars[i].len) {
          case 1:d=*(byte*)svars[i].ptr;break;
          case 2:d=*(un16*)svars[i].ptr;break;
          case 4:memcpy(&d,svars[i].ptr,4);break;
        }
        header[i][1]=LIL(d);
    }
    memcpy(buf+hiofs,ram.hi,sizeof ram.hi);
    memcpy(buf+palofs,lcd.pal,sizeof lcd.pal);
    memcpy(buf+oamofs,lcd.oam.mem,sizeof lcd.oam);
    memcpy(buf+wavofs,snd.wave,sizeof snd.wave);
    const int ok=fseek(f,0,SEEK_SET)==0 && state_write(f,buf,4096U) &&
        state_write(f,ram.ibank,4096U*(size_t)irl) &&
        state_write(f,lcd.vbank,4096U*(size_t)vrl) &&
        state_write(f,ram.sbank,8192U*(size_t)mbc.ramsize);
    gnuboy_psram_free(buf);return ok;
}

// Preserve the upstream ABI for callers outside the embedded frontend.
void loadstate(FILE *f){(void)loadstate_checked(f);}
void savestate(FILE *f){(void)savestate_checked(f);}
