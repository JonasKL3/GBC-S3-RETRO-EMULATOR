#include <stdlib.h>
#include <string.h>
#ifdef ARDUINO_ARCH_ESP32
#include <esp_heap_caps.h>
#endif

#include "defs.h"
#include "fb.h"
#include "sys.h"

/*
 * These lookup tables originally occupied about 165 KiB of zero-initialized
 * DRAM.  That is too expensive on ESP32-S3 and caused the firmware linker to
 * overflow dram0_0_seg even though the tables are only needed in indexed
 * framebuffer mode. Allocate them lazily in PSRAM instead. The GBC-S3
 * frontend uses RGB565 (fb.indexed == 0), so normal operation allocates none
 * of these tables.
 */
static byte *palmap = NULL;
static byte *pallock = NULL;
static int *palrev = NULL;

/* Course color mapping, for when palette is exhausted. */
static byte (*crsmap)[32768] = NULL;
static int (*crsrev)[256] = NULL;
static int palette_tables_failed = 0;

static void *palette_alloc_zero(size_t bytes)
{
#ifdef ARDUINO_ARCH_ESP32
    void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    void *p = calloc(1, bytes);
#endif
    if (p) memset(p, 0, bytes);
    return p;
}

static int palette_tables_init(void)
{
    if (palmap && pallock && palrev && crsmap && crsrev) return 1;
    if (palette_tables_failed) return 0;

    palmap = (byte *)palette_alloc_zero(32768U * sizeof(byte));
    pallock = (byte *)palette_alloc_zero(256U * sizeof(byte));
    palrev = (int *)palette_alloc_zero(256U * sizeof(int));
    crsmap = (byte (*)[32768])palette_alloc_zero(4U * 32768U * sizeof(byte));
    crsrev = (int (*)[256])palette_alloc_zero(4U * 256U * sizeof(int));

    if (!palmap || !pallock || !palrev || !crsmap || !crsrev)
    {
#ifdef ARDUINO_ARCH_ESP32
        if (palmap) heap_caps_free(palmap);
        if (pallock) heap_caps_free(pallock);
        if (palrev) heap_caps_free(palrev);
        if (crsmap) heap_caps_free(crsmap);
        if (crsrev) heap_caps_free(crsrev);
#else
        free(palmap); free(pallock); free(palrev); free(crsmap); free(crsrev);
#endif
        palmap = NULL; pallock = NULL; palrev = NULL;
        crsmap = NULL; crsrev = NULL;
        palette_tables_failed = 1;
        return 0;
    }
    return 1;
}
static const int crsmask[4] = { 0x7BDE, 0x739C, 0x6318, 0x4210 };

enum plstatus
{
	pl_unused = 0,
	pl_linger,
	pl_active,
	pl_locked
};


static byte bestmatch(int c)
{
	byte n, best;
	int r, g, b;
	int r2, g2, b2, c2;
	int err, besterr;

	r = (c & 0x001F) << 3;
	g = (c & 0x03E0) >> 2;
	b = (c & 0x7C00) >> 7;

	best = 0;
	besterr = 1024;
	for (n = 1; n; n++)
	{
		c2 = palrev[n];
		r2 = (c2 & 0x001F) << 3;
		g2 = (c2 & 0x03E0) >> 2;
		b2 = (c2 & 0x7C00) >> 7;
		err = abs(r-r2) + abs(b-b2) + abs(g-g2);
		if (err < besterr)
		{
			besterr = err;
			best = n;
		}
	}
	return best;
}

static void makecourse(int c, byte n)
{
	int i;
	for (i = 0; i < 4; i++)
	{
		c &= crsmask[i];
		crsmap[i][c] = n;
		crsrev[i][n] = c;
	}
}

static byte findcourse(int c)
{
	int i;
	byte n;
	for (i = 0; i < 4; i++)
	{
		c &= crsmask[i];
		n = crsmap[i][c];
		if (crsrev[i][n] == c)
			return n;
	}
	return 0;
}


void pal_lock(byte n)
{
	if (!n || !pallock) return;
	if (pallock[n] >= pl_locked)
		pallock[n]++;
	else pallock[n] = pl_locked;
}

byte pal_getcolor(int c, int r, int g, int b)
{
	byte n;
	static byte l;
	if (!palette_tables_init()) return 0;
	c &= 0x7fff;
	n = palmap[c];
	if (n && pallock[n] && palrev[n] == c)
	{
		pal_lock(n);
		return n;
	}
	for (n = l+1; n != l; n++)
	{
		if (!n || pallock[n] /* || n < 16 */) continue;
		pal_lock(n);
		palmap[c] = n;
		palrev[n] = c;
		makecourse(c, n);
		vid_setpal(n, r, g, b);
		return (l = n);
	}
	n = findcourse(c);
	pal_lock(n);
	return n;
}

void pal_release(byte n)
{
	if (!pallock) return;
	if (pallock[n] >= pl_locked)
		pallock[n]--;
}


void pal_expire()
{
	int i;
	if (!pallock) return;
	for (i = 0; i < 256; i++)
		if (pallock[i] && pallock[i] < pl_locked)
			pallock[i]--;
}


void pal_set332()
{
	int i, r, g, b;
	
	fb.indexed = 0;
	fb.cc[0].r = 5;
	fb.cc[1].r = 5;
	fb.cc[2].r = 6;
	fb.cc[0].l = 0;
	fb.cc[1].l = 3;
	fb.cc[2].l = 6;
	
	i = 0;
	for (b = 0; b < 4; b++) for (g = 0; g < 8; g++) for (r = 0; r < 8; r++)
		vid_setpal(i++, (r<<5)|(r<<2)|(r>>1),
			   (g<<5)|(g<<2)|(g>>1), (b<<6)|(b<<4)|(b<<2)|b);
}





