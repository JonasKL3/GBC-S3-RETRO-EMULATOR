
#ifndef __FASTMEM_H__
#define __FASTMEM_H__


#include "defs.h"
#include "mem.h"
#include <string.h>


// Map entries are real page bases, never pointers biased before an object.
static byte readb(int a) {
    a&=0xffff;
    byte *p=mbc.rmap[a>>12];
    return p?p[a&0xfff]:mem_read(a);
}
static void writeb(int a,byte b) {
    a&=0xffff;
    byte *p=mbc.wmap[a>>12];
    if(p)p[a&0xfff]=b;else mem_write(a,b);
}
static int readw(int a) {
    a&=0xffff;
    const int offset=a&0xfff;
    byte *p=mbc.rmap[a>>12];
    if(p && offset!=0xfff){
#ifdef IS_LITTLE_ENDIAN
        if(!(a&1)){gb_word value;memcpy(&value,p+offset,sizeof value);return value;}
#endif
        return p[offset]|((int)p[offset+1]<<8);
    }
    return readb(a)|((int)readb((a+1)&0xffff)<<8);
}
static void writew(int a,int w) {
    a&=0xffff;
    const int offset=a&0xfff;
    byte *p=mbc.wmap[a>>12];
    if(p && offset!=0xfff){
#ifdef IS_LITTLE_ENDIAN
        if(!(a&1)){const gb_word value=(gb_word)w;memcpy(p+offset,&value,sizeof value);return;}
#endif
        p[offset]=(byte)w;p[offset+1]=(byte)(w>>8);return;
    }
    writeb(a,(byte)w);writeb((a+1)&0xffff,(byte)(w>>8));
}

static byte readhi(int a)
{
	return readb(a | 0xff00);
}

static void writehi(int a, byte b)
{
	writeb(a | 0xff00, b);
}

#if 0
static byte readhi(int a)
{
	byte (*rd)() = hi_read[a];
	return rd ? rd(a) : (ram.hi[a] | himask[a]);
}

static void writehi(int a, byte b)
{
	byte (*wr)() = hi_write[a];
	if (wr) wr(a, b);
	else ram.hi[a] = b & ~himask[a];
}
#endif


#endif
