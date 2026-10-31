


#include <stdio.h>
#include <time.h>
#include <stdint.h>

#include "defs.h"
#include "mem.h"
#include "rtc.h"
#include "rc.h"

struct rtc rtc;

static int syncrtc = 1;

rcvar_t rtc_exports[] =
{
	RCV_BOOL("syncrtc", &syncrtc, "resync RTC for elapsed time on load"),
	RCV_END
};


void rtc_latch(byte b)
{
	if ((rtc.latch ^ b) & b & 1)
	{
		rtc.regs[0] = rtc.s;
		rtc.regs[1] = rtc.m;
		rtc.regs[2] = rtc.h;
		rtc.regs[3] = rtc.d;
		rtc.regs[4] = ((rtc.d>>8)&1) | (rtc.stop<<6) | (rtc.carry<<7);
		rtc.regs[5] = 0xff;
		rtc.regs[6] = 0xff;
		rtc.regs[7] = 0xff;
	}
	rtc.latch = b;
}

void rtc_write(byte b)
{
	/* printf("write %02X: %02X (%d)\n", rtc.sel, b, b); */
	if (!(rtc.sel & 8)) return;
	switch (rtc.sel & 7)
	{
	case 0:
		rtc.s = rtc.regs[0] = b;
		while (rtc.s >= 60) rtc.s -= 60;
		break;
	case 1:
		rtc.m = rtc.regs[1] = b;
		while (rtc.m >= 60) rtc.m -= 60;
		break;
	case 2:
		rtc.h = rtc.regs[2] = b;
		while (rtc.h >= 24) rtc.h -= 24;
		break;
	case 3:
		rtc.regs[3] = b;
		rtc.d = (rtc.d & 0x100) | b;
		break;
	case 4:
		rtc.regs[4] = b;
		rtc.d = (rtc.d & 0xff) | ((b&1)<<8);
		rtc.stop = (b>>6)&1;
		rtc.carry = (b>>7)&1;
		break;
	}
}

void rtc_tick()
{
	if (rtc.stop) return;
	if (++rtc.t == 60)
	{
		if (++rtc.s == 60)
		{
			if (++rtc.m == 60)
			{
				if (++rtc.h == 24)
				{
					if (++rtc.d == 512)
					{
						rtc.d = 0;
						rtc.carry = 1;
					}
					rtc.h = 0;
				}
				rtc.m = 0;
			}
			rtc.s = 0;
		}
		rtc.t = 0;
	}
}

void rtc_save_internal(FILE *f)
{
	fprintf(f, "%d %d %d %02d %02d %02d %02d\n%lld\n",
		rtc.carry, rtc.stop, rtc.d, rtc.h, rtc.m, rtc.s, rtc.t,
		(long long) time(0));
}

void rtc_load_internal(FILE *f)
{
    int carry, stop, d, h, m, s, t;
    long long saved;
    char line[160], stamp[64], extra;
    // Bounded input and temporary values: a truncated file cannot alter RTC.
    if (!f || !fgets(line,sizeof(line),f) || !fgets(stamp,sizeof(stamp),f)) return;
    // Restrict integer token lengths before sscanf to avoid conversion overflow.
    if (sscanf(line,"%1d %1d %3d %2d %2d %2d %2d %c",&carry,&stop,&d,&h,&m,&s,&t,&extra)!=7 ||
        sscanf(stamp,"%12lld %c",&saved,&extra)!=1) return;
    if (carry<0 || carry>1 || stop<0 || stop>1 || d<0 || d>511 ||
        h<0 || h>23 || m<0 || m>59 || s<0 || s>59 || t<0 || t>59 || saved<0) return;
    rtc.carry=carry;rtc.stop=stop;rtc.d=d;rtc.h=h;rtc.m=m;rtc.s=s;rtc.t=t;
    // A device without a synchronized wall clock must not invent elapsed time.
    const int64_t now=(int64_t)time(0);
    if (syncrtc && !rtc.stop && saved>=946684800LL && now>saved) {
        const uint64_t elapsed=(uint64_t)(now-saved);
        const uint64_t seconds=(uint64_t)rtc.h*3600U+(uint64_t)rtc.m*60U+rtc.s+elapsed;
        const uint64_t days=(uint64_t)rtc.d+seconds/86400U;
        if (days>=512U)rtc.carry=1;
        rtc.d=(int)(days%512U);rtc.h=(int)(seconds/3600U%24U);
        rtc.m=(int)(seconds/60U%60U);rtc.s=(int)(seconds%60U);
    }
}












