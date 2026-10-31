/* Minimal GNUBoy system backend for the GBC-S3 Arduino frontend. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include "defs.h"
#include "fb.h"
#include "pcm.h"
#include "sys.h"

struct fb fb;
struct pcm pcm;
int debug_trace = 0;

static byte pcm_storage[512];
extern int gbc_audio_submit_pcm(const unsigned char *samples, size_t count);

void gnuboy_pcm_configure(int hz, int length)
{
    if (length > (int)sizeof(pcm_storage)) length = (int)sizeof(pcm_storage);
    if (length < 1) length = 1;
    pcm.hz = hz;
    pcm.len = length;
    pcm.stereo = 0;
    pcm.buf = pcm_storage;
    pcm.pos = 0;
}

int pcm_submit(void)
{
    int count = pcm.pos;
    int ok = 1;
    if (count > 0 && pcm.buf)
        ok = gbc_audio_submit_pcm(pcm.buf, (size_t)count);
    pcm.pos = 0;
    return ok;
}

void vid_begin(void) {}
void vid_end(void) {}
void vid_init(void) {}
void vid_preinit(void) {}
void vid_close(void) {}
void vid_setpal(int i, int r, int g, int b) { (void)i; (void)r; (void)g; (void)b; }
void vid_settitle(char *title) { (void)title; }
void pcm_init(void) {}
void pcm_close(void) {}
void pcm_pause(int paused) { (void)paused; }
void ev_poll(int wait) { (void)wait; }
void sys_checkdir(char *path, int wr) { (void)path; (void)wr; }
void sys_sleep(int us) { (void)us; }
void sys_sanitize(char *s) { (void)s; }
void joy_init(void) {}
void joy_poll(void) {}
void joy_close(void) {}
void kb_init(void) {}
void kb_poll(void) {}
void kb_close(void) {}
void sys_initpath(char *exe) { (void)exe; }
void doevents(void) {}

void *sys_timer(void)
{
    static struct timeval tv;
    gettimeofday(&tv, NULL);
    return &tv;
}

int sys_elapsed(struct timeval *prev)
{
    struct timeval now;
    long long elapsed;
    if (!prev) return 0;
    gettimeofday(&now, NULL);
    elapsed = ((long long)now.tv_sec - (long long)prev->tv_sec) * 1000000LL;
    elapsed += (long long)now.tv_usec - (long long)prev->tv_usec;
    *prev = now;
    return (int)elapsed;
}

void die(char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt ? fmt : "GNUBoy fatal error\n", ap);
    va_end(ap);
    abort();
}

void debug_disassemble(addr a, int c) { (void)a; (void)c; }
