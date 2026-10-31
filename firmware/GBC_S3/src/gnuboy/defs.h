


#ifndef __DEFS_H__
#define __DEFS_H__

/* GNUBoy's original build system defines this for little-endian hosts.
 * ESP32-S3 and common host-test targets are little-endian; detect it here so
 * state headers and register serialization do not accidentally byte-swap. */
#if !defined(IS_LITTLE_ENDIAN)
#  if defined(ARDUINO_ARCH_ESP32)
#    define IS_LITTLE_ENDIAN 1
#  elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#    define IS_LITTLE_ENDIAN 1
#  endif
#endif



#ifdef IS_LITTLE_ENDIAN
#define LO 0
#define HI 1
#define LIL(x) (x)
#else
#define LO 1
#define HI 0
#define LIL(x) ((x<<24)|((x&0xff00)<<8)|((x>>8)&0xff00)|(x>>24))
#endif

typedef unsigned char byte;

typedef unsigned char un8;
typedef unsigned short un16;
typedef unsigned int un32;

typedef signed char n8;
typedef signed short n16;
typedef signed int n32;

typedef un16 gb_word;
typedef gb_word addr;

/* stuff from main.c ... */
void die(char *fmt, ...);
void doevents();
int load_rom_and_rc(char *rom);


#endif

