#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#undef _GNU_SOURCE
#define _GNU_SOURCE
#include <string.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "defs.h"
#include "loader.h"
#include "port_memory.h"
#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif
#include "regs.h"
#include "mem.h"
#include "hw.h"
#include "rtc.h"
#include "rc.h"
#include "lcd.h"
#include "inflate.h"
#include "miniz.h"
#define XZ_USE_CRC64
#include "xz/xz.h"
#include "save.h"
#include "sound.h"
#include "sys.h"

static int mbc_table[256] =
{
	0, 1, 1, 1, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 3,
	3, 3, 3, 3, 0, 0, 0, 0, 0, 5, 5, 5, MBC_RUMBLE, MBC_RUMBLE, MBC_RUMBLE, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,

	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, MBC_HUC3, MBC_HUC1
};

static int rtc_table[256] =
{
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0
};

static int batt_table[256] =
{
	0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0,
	1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0,
	0
};

static int romsize_table[256] =
{
	2, 4, 8, 16, 32, 64, 128, 256, 512,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 128, 128, 128
	/* 0, 0, 72, 80, 96  -- actual values but bad to use these! */
};

static int ramsize_table[256] =
{
	1, 1, 1, 4, 16,
	8 /* RAM size code 05 = 64 KiB = eight 8 KiB banks. */
};


static char *bootroms[2];
static char *romfile;
static char *sramfile;
static char *rtcfile;
static char *saveprefix;

static char *savename;
static char *savedir;

static int saveslot;

static int forcebatt, nobatt;
static int forcedmg, gbamode;

static int memfill = -1, memrand = -1;


static void initmem(void *mem, int size)
{
	char *p = mem;
	if (memrand >= 0)
	{
		srand(memrand ? memrand : time(0));
		while(size--) *(p++) = rand();
	}
	else if (memfill >= 0)
		memset(p, memfill, size);
}

static int loader_io_errno;
int loader_get_io_errno(void){return loader_io_errno;}

static byte *loadfile(FILE *f, int *len)
{
#if !defined(ARDUINO_ARCH_ESP32)
	long original = ftell(f);
#endif
	long file_size = -1;
	byte *d = 0;
	int total = 0;
#if !defined(ARDUINO_ARCH_ESP32)
	byte buf[4096];
#endif

	/* ROMs are regular SD files on the embedded target. Allocate once in
	 * PSRAM to avoid hundreds of reallocations and internal-heap pressure. */
	if (fseek(f, 0, SEEK_END) == 0) {
		file_size = ftell(f);
		if (file_size > 0 && file_size <= 16L * 1024L * 1024L && fseek(f, 0, SEEK_SET) == 0) {
			d = (byte *)gnuboy_psram_malloc((size_t)file_size);
			if (!d) {loader_set_error("ROM allocation failed: PSRAM bytes=%ld\n",file_size);return 0;}
			while (total < file_size) {
				int want = (file_size - total) > 4096L ? 4096 : (int)(file_size - total);
				errno=0;
				int got = (int)fread(d + total, 1, (size_t)want, f);
				if (got <= 0) {
                    loader_io_errno=errno?errno:EIO;
                    loader_set_error("ROM read failed: offset=%d/%ld errno=%d ferror=%d\n",total,file_size,errno,ferror(f));
                    gnuboy_psram_free(d); return 0;
                }
				total += got;
#ifdef ARDUINO_ARCH_ESP32
                // Release the CPU for idle/BLE tasks during SD loading.
                vTaskDelay(1);
#endif
			}
			*len = total;
			return d;
		}
	}
#ifdef ARDUINO_ARCH_ESP32
    // The frontend accepts regular SD files only. Never reserve 4 KiB of
    // loopTask stack for a stream fallback that this target does not use.
    loader_io_errno=errno;
    loader_set_error("ROM seek/size failed: bytes=%ld errno=%d\n",file_size,errno);
    return 0;
#else
	/* Fallback for a non-seekable host stream. */
	if (original >= 0) fseek(f, original, SEEK_SET);
	total = 0;
	for (;;) {
		int got = (int)fread(buf, 1, sizeof(buf), f);
		if (got <= 0) break;
		byte *resized = (byte *)gnuboy_psram_realloc(d, (size_t)total + (size_t)got);
		if (!resized) { gnuboy_psram_free(d); return 0; }
		d = resized;
		memcpy(d + total, buf, (size_t)got);
		total += got;
	}
	*len = total;
	return d;
#endif
}

static byte *inf_buf;
static int inf_pos, inf_len;
static char* loader_error;
static char loader_error_buf[256];

char* loader_get_error(void)
{
	return loader_error;
}

void loader_set_error(char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(loader_error_buf, sizeof loader_error_buf, fmt, ap);
	va_end(ap);
	loader_error = loader_error_buf;
}

static int inflate_callback(byte b)
{
	if (inf_pos >= inf_len)
	{
		inf_len += 512;
		inf_buf = realloc(inf_buf, inf_len);
		if (!inf_buf) {
			loader_set_error("out of memory inflating file @ %d bytes\n", inf_pos);
			return -1;
		}
	}
	inf_buf[inf_pos++] = b;
	return 0;
}

typedef int (*unzip_or_inflate_func) (const unsigned char *data, long *p, int (* callback) (unsigned char d));

static byte *gunzip_or_inflate(byte *data, int *len, unsigned offset,
	unzip_or_inflate_func func)
{
	long pos = 0;
	inf_buf = 0;
	inf_pos = inf_len = 0;
	if (func(data+offset, &pos, inflate_callback) < 0)
		return data;
	gnuboy_psram_free(data);
	*len = inf_pos;
	return inf_buf;
}

static byte *gunzip(byte *data, int *len) {
	return gunzip_or_inflate(data, len, 0, unzip);
}

/* primitive pkzip decompressor. it can only decompress the first
   file in a zip archive. */
static byte *pkunzip(byte *data, int *len) {
	unsigned short fnl, el, comp;
	unsigned int st;
	void *new;
	size_t newlen;
	int oldlen = *len;
	if (*len < 128) return data;
	memcpy(&comp, data+8, 2);
	comp = LIL(comp);
	if(comp != 0 && comp != 8) return data;
	memcpy(&fnl, data+26, 2);
	memcpy(&el, data+28, 2);
	fnl = LIL(fnl);
	el = LIL(el);
	st = 30 + fnl + el;
	if(*len < st) return data;
	if(comp == 0) {
		inf_buf = realloc(NULL, *len - st);
		memcpy(inf_buf, data+st, *len - st);
		gnuboy_psram_free(data);
		inf_len = *len = *len - st;
		return inf_buf;
	}
	*len -= st;
	newlen = 0;
	new = tinfl_decompress_mem_to_heap(data+st, *len, &newlen, 0);
	if(new) {
		*len = newlen;
		gnuboy_psram_free(data);
		return new;
	}
	*len = oldlen;
	return data;
}

static int write_dec(byte *data, int len) {
	int i;
	for(i=0; i < len; i++)
		if(inflate_callback(data[i])) return -1;
	return 0;
}

static int unxz(byte *data, int len) {
	struct xz_buf b;
	struct xz_dec *s;
	enum xz_ret ret;
	unsigned char out[4096];

	/*
	 * Support up to 64 MiB dictionary. The actually needed memory
	 * is allocated once the headers have been parsed.
	*/
	s = xz_dec_init(XZ_DYNALLOC, 1 << 26);
	if(!s) goto err;

	b.in = data;
	b.in_pos = 0;
	b.in_size = len;
	b.out = out;
	b.out_pos = 0;
	b.out_size = sizeof(out);

	while (1) {
		ret = xz_dec_run(s, &b);
		if(b.out_pos == sizeof(out)) {
			if(write_dec(out, sizeof(out))) goto err;
			b.out_pos = 0;
		}

		if(ret == XZ_OK) continue;

		if(write_dec(out, b.out_pos)) goto err;

		if(ret == XZ_STREAM_END) {
			xz_dec_end(s);
			return 0;
		}
		goto err;
	}

	err:
	xz_dec_end(s);
	return -1;
}

static byte *do_unxz(byte *data, int *len) {
	xz_crc32_init();
	xz_crc64_init();
	inf_buf = 0;
	inf_pos = inf_len = 0;
	if (unxz(data, *len) < 0)
		return data;
	gnuboy_psram_free(data);
	*len = inf_pos;
	return inf_buf;
}

static byte *decompress(byte *data, int *len)
{
	if (data[0] == 0x1f && data[1] == 0x8b)
		return gunzip(data, len);
	if (data[0] == 0xFD && !memcmp(data+1, "7zXZ", 4))
		return do_unxz(data, len);
	if (data[0] == 'P' && !memcmp(data+1, "K\03\04", 3))
		return pkunzip(data, len);
	return data;
}

static FILE* rom_loadfile(char *fn, byte** data, int *len) {
	FILE *f;
    loader_set_error("");loader_io_errno=0;errno=0;
	if (strcmp(fn, "-")) f = fopen(fn, "rb");
	else f = stdin;
	if (!f) {
		loader_io_errno=errno?errno:EIO;
        loader_set_error("ROM fopen failed: errno=%d path=%s\n",errno,fn);
		return f;
	}
	*data = loadfile(f, len);
	if (!*data) {
		fclose(f);
        if(!loader_get_error() || !loader_get_error()[0])loader_set_error("ROM read failed: %s\n",fn);
        return 0;
	}
#if defined(ARDUINO_ARCH_ESP32)
	/* The GBC-S3 frontend accepts raw .gb/.gbc files only. Reject compressed
	 * containers here so PSRAM-owned ROM buffers never cross allocator domains. */
	if (*len >= 5 && (((*data)[0] == 0x1f && (*data)[1] == 0x8b) ||
	    ((*data)[0] == 0xfd && !memcmp(*data + 1, "7zXZ", 4)) ||
	    ((*data)[0] == 'P' && !memcmp(*data + 1, "K\03\04", 3)))) {
		loader_set_error("compressed ROM containers are not supported on GBC-S3; use raw .gb/.gbc\n");
		gnuboy_psram_free(*data); *data = 0;
		if (strcmp(fn, "-")) fclose(f);
		return 0;
	}
#else
	*data = decompress(*data, len);
#endif
	return f;
}

int bootrom_load() {
	byte *data;
	int len;
	FILE *f;
	REG(RI_BOOT) = 0xff;
	if (!bootroms[hw.cgb] || !bootroms[hw.cgb][0]) return 0;
	f = rom_loadfile(bootroms[hw.cgb], &data, &len);
	if(!f) return -1;
    if(len<0 || len>16384){gnuboy_psram_free(data);fclose(f);return -1;}
    byte *resized=gnuboy_psram_realloc(data,16384);
    if(!resized){gnuboy_psram_free(data);fclose(f);return -1;}
	bootrom.bank = (byte (*)[16384])resized;
	memset(bootrom.bank[0]+len, 0xff, 16384-len);
	memcpy(bootrom.bank[0]+0x100, rom.bank[0]+0x100, 0x100);
	fclose(f);
	REG(RI_BOOT) = 0xfe;
	return 0;
}

/* memory allocation breakdown:
   loadfile returns local buffer retrieved via realloc()
   it's called only by rom_loadfile.
     rom_loadfile is called by bootrom_load and rom_load.
      bootrom_load is called once per romfile load via loader_init from
      load_rom_and_rc, and mem ends up in bootrom.bank, loader_unload() frees it.
      rom_load is called by rom_load_simple and loader_init().
       rom_load_simple is only called by rominfo in main.c, which we can ignore.
       the mem allocated by loadfile thru loader_init/rom_load ends up in
       rom.bank, which is freed in loader_unload(), just like the malloc'd
       rom.sbank.
   where it gets complicated is when rom_loadfile uncompresses data.
   the allocation returned by loadfile is passed to decompress().
   if it fails, it returns the original loadfile allocation, on success
   it returns a pointer to inf_buf which contains the uncompressed data.
*/

int rom_load()
{
	FILE *f;
	byte c, *data, *header;
	int len = 0, rlen;
	f = rom_loadfile(romfile, &data, &len);
	if(!f) return -1;
	if (!data || len < 0x150) {
		loader_set_error("ROM file is too short (%d bytes)\n", len);
		if (data) gnuboy_psram_free(data);
		if (strcmp(romfile, "-")) fclose(f);
		return -1;
	}
	header = data;

	memcpy(rom.name, header+0x0134, 16);
	if (rom.name[14] & 0x80) rom.name[14] = 0;
	if (rom.name[15] & 0x80) rom.name[15] = 0;
	rom.name[16] = 0;

	c = header[0x0147];
	mbc.type = mbc_table[c];
	mbc.batt = (batt_table[c] && !nobatt) || forcebatt;
	rtc.batt = rtc_table[c];
	mbc.romsize = romsize_table[header[0x0148]];
	mbc.ramsize = ramsize_table[header[0x0149]];

	if (!mbc.romsize) {
		loader_set_error("unknown ROM size %02X\n", header[0x0148]);
		gnuboy_psram_free(data); if (strcmp(romfile, "-")) fclose(f); return -1;
	}
	if (!mbc.ramsize) {
		loader_set_error("unknown SRAM size %02X\n", header[0x0149]);
		gnuboy_psram_free(data); if (strcmp(romfile, "-")) fclose(f); return -1;
	}

	rlen = 16384 * mbc.romsize;

	c = header[0x0143];

	/* Embedded-port safety: reject inconsistent files and check allocations. */
	if (len > rlen) {
		loader_set_error("ROM file larger than header-declared size (%d > %d)\n", len, rlen);
		gnuboy_psram_free(data);
		if (strcmp(romfile, "-")) fclose(f);
		return -1;
	}
	/* from this point on, we may no longer access data and header */
	{
		byte *resized = (byte *)gnuboy_psram_realloc(data, (size_t)rlen);
		if (!resized) {
			loader_set_error("out of memory allocating ROM (%d bytes)\n", rlen);
			gnuboy_psram_free(data);
			if (strcmp(romfile, "-")) fclose(f);
			return -1;
		}
		rom.bank = (byte (*)[16384])resized;
	}
	if (rlen > len) memset(rom.bank[0]+len, 0xff, rlen - len);

	ram.sbank = (byte (*)[8192])gnuboy_psram_malloc(8192U * (size_t)mbc.ramsize);
	if (!ram.sbank) {
		loader_set_error("out of memory allocating cartridge RAM (%d bytes)\n", 8192 * mbc.ramsize);
		gnuboy_psram_free(rom.bank); rom.bank = 0;
		if (strcmp(romfile, "-")) fclose(f);
		return -1;
	}

	initmem(ram.sbank, 8192 * mbc.ramsize);
	initmem(ram.ibank, 4096 * 8);

	mbc.rombank = 1;
	mbc.rambank = 0;

	hw.cgb = ((c == 0x80) || (c == 0xc0)) && !forcedmg;
	hw.gba = (hw.cgb && gbamode);

	if (strcmp(romfile, "-")) fclose(f);

	return 0;
}

int rom_load_simple(char *fn) {
	romfile = fn;
	return rom_load();
}

int sram_load()
{
	FILE *f;

	if (!mbc.batt || !sramfile || !*sramfile) return -1;

	/* Consider sram loaded at this point, even if file doesn't exist */
	ram.loaded = 1;

	f = fopen(sramfile, "rb");
	if (!f) return -1;
	fread(ram.sbank, 8192, mbc.ramsize, f);
	fclose(f);
	
	return 0;
}


int sram_save()
{
	FILE *f;

	/* If we crash before we ever loaded sram, DO NOT SAVE! */
	if (!mbc.batt || !sramfile || !ram.loaded || !mbc.ramsize)
		return -1;
	
	f = fopen(sramfile, "wb");
	if (!f) return -1;
	fwrite(ram.sbank, 8192, mbc.ramsize, f);
	fclose(f);
	
	return 0;
}


void state_save(int n)
{
	FILE *f;
	char *name;

	if (n < 0) n = saveslot;
	if (n < 0) n = 0;
    if(!saveprefix || n>999)return;
	name = malloc(strlen(saveprefix) + 5);
    if(!name)return;
	sprintf(name, "%s.%03d", saveprefix, n);

	if ((f = fopen(name, "wb")))
	{
		savestate(f);
		fclose(f);
	}
	free(name);
}


void state_load(int n)
{
	FILE *f;
	char *name;

	if (n < 0) n = saveslot;
	if (n < 0) n = 0;
    if(!saveprefix || n>999)return;
	name = malloc(strlen(saveprefix) + 5);
    if(!name)return;
	sprintf(name, "%s.%03d", saveprefix, n);

	if ((f = fopen(name, "rb")))
	{
		loadstate(f);
		fclose(f);
		vram_dirty();
		pal_dirty();
		sound_dirty();
		mem_updatemap();
	}
	free(name);
}

void rtc_save()
{
	FILE *f;
	if (!rtc.batt) return;
	if (!(f = fopen(rtcfile, "wb"))) return;
	rtc_save_internal(f);
	fclose(f);
}

void rtc_load()
{
	FILE *f;
	if (!rtc.batt) return;
	if (!(f = fopen(rtcfile, "r"))) return;
	rtc_load_internal(f);
	fclose(f);
}

#define FREENULL(X) do { free(X); X = 0; } while(0)
#define PSRAM_FREENULL(X) do { gnuboy_psram_free(X); X = 0; } while(0)
void loader_unload()
{
	sram_save();
	if (romfile) FREENULL(romfile);
	if (sramfile) FREENULL(sramfile);
	if (rtcfile) FREENULL(rtcfile);
	if (saveprefix) FREENULL(saveprefix);
	if (rom.bank) PSRAM_FREENULL(rom.bank);
	if (ram.sbank) PSRAM_FREENULL(ram.sbank);
	if (bootrom.bank) PSRAM_FREENULL(bootrom.bank);
	mbc.type = mbc.romsize = mbc.ramsize = mbc.batt = 0;
}

static char *base(char *s)
{
	char *p;
	p = strrchr(s, '/');
	if (p) return p+1;
	return s;
}

static char *ldup(char *s)
{
	int i;
	char *n, *p;
	p = n = malloc(strlen(s)+1);
    if(!n)return NULL;
	for (i = 0; s[i]; i++) if (isalnum((unsigned char)s[i])) *(p++) = tolower((unsigned char)s[i]);
	*p = 0;
	return n;
}

static void cleanup()
{
	sram_save();
	rtc_save();
	/* IDEA - if error, write emergency savestate..? */
}

int loader_init(char *s)
{
#ifdef ARDUINO_ARCH_ESP32
    (void)s;
    loader_set_error("desktop loader_init is disabled on embedded; use rom_load_simple\n");
    return -1;
#else
	char *name, *p;

	sys_checkdir(savedir, 1); /* needs to be writable */

	romfile = s;
	if(rom_load()) return -1;
	bootrom_load();
	vid_settitle(rom.name);
	if (savename && *savename)
	{
		if (savename[0] == '-' && savename[1] == 0)
			name = ldup(rom.name);
		else name = strdup(savename);
	}
	else if (romfile && *base(romfile) && strcmp(romfile, "-"))
	{
		name = strdup(base(romfile));
		p = strrchr(name, '.');
		if (p) *p = 0;
	}
	else name = ldup(rom.name);

	saveprefix = malloc(strlen(savedir) + strlen(name) + 2);
	sprintf(saveprefix, "%s/%s", savedir, name);

	sramfile = malloc(strlen(saveprefix) + 5);
	strcpy(sramfile, saveprefix);
	strcat(sramfile, ".sav");

	rtcfile = malloc(strlen(saveprefix) + 5);
	strcpy(rtcfile, saveprefix);
	strcat(rtcfile, ".rtc");

	sram_load();
	rtc_load();

	atexit(cleanup);
	return 0;
#endif
}

rcvar_t loader_exports[] =
{
	RCV_STRING("bootrom_dmg", &bootroms[0], "bootrom for DMG games"),
	RCV_STRING("bootrom_cgb", &bootroms[1], "bootrom for CGB games"),
	RCV_STRING("savedir", &savedir, "save directory"),
	RCV_STRING("savename", &savename, "base filename for saves"),
	RCV_INT("saveslot", &saveslot, "which savestate slot to use"),
	RCV_BOOL("forcebatt", &forcebatt, "save SRAM even on carts w/o battery"),
	RCV_BOOL("nobatt", &nobatt, "never save SRAM"),
	RCV_BOOL("forcedmg", &forcedmg, "force DMG mode for CGB carts"),
	RCV_BOOL("gbamode", &gbamode, "simulate cart being used on a GBA"),
	RCV_INT("memfill", &memfill, ""),
	RCV_INT("memrand", &memrand, ""),
	RCV_END
};









