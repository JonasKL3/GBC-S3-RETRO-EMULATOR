#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CORE="$ROOT/firmware/GBC_S3/src/gnuboy"
BUILD="${TMPDIR:-/tmp}/gbc_s3_gnuboy_smoke.$$"
trap 'rm -rf "$BUILD"' EXIT
mkdir -p "$BUILD"
python3 - "$BUILD/smoke.gbc" "$BUILD/smoke.gb" "$BUILD/rtc.gbc" "$BUILD/mbc2.gbc" <<'PY'
import sys
from pathlib import Path
for out, cgb, cart_type, ram_code in ((sys.argv[1],0x80,0x00,0),(sys.argv[2],0x00,0x00,0),(sys.argv[3],0x80,0x10,2),(sys.argv[4],0x80,0x06,0)):
    p=bytearray(32768)
    p[0x100:0x102]=bytes((0x18,0xFE)) # JR -2: safe, deterministic test ROM loop
    p[0x134:0x13c]=b'GBCSMOKE'
    p[0x143]=cgb
    p[0x147]=cart_type
    p[0x148]=0x00
    p[0x149]=ram_code
    Path(out).write_bytes(p)
PY
CFLAGS="-std=gnu11 -O2 -w -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -I$ROOT/tests/host_stubs -I$CORE"
for f in cpu.c mem.c lcd.c lcdc.c hw.c rtc.c sound.c palette.c fastmem.c emu.c loader.c save.c inflate.c miniz_tinfl.c xz/xz_crc32.c xz/xz_crc64.c xz/xz_dec_lzma2.c xz/xz_dec_stream.c xz/xz_dec_bcj.c gnuboy_platform.c; do
  obj=$(printf '%s' "$f" | tr '/' '_')
  gcc $CFLAGS -c "$CORE/$f" -o "$BUILD/$obj.o"
done
g++ -std=gnu++11 -O2 -w -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -I"$ROOT/tests/host_stubs" -I"$ROOT" -I"$CORE" -c "$ROOT/firmware/GBC_S3/gnuboy_adapter.cpp" -o "$BUILD/gnuboy_adapter.o"
g++ -std=gnu++11 -O2 -w -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -I"$ROOT/tests/host_stubs" -I"$ROOT" -I"$CORE" -c "$ROOT/tests/host_gnuboy_smoke.cpp" -o "$BUILD/host_gnuboy_smoke.o"
g++ "$BUILD"/*.o -o "$BUILD/smoke"
"$BUILD/smoke" "$BUILD/smoke.gbc" "$BUILD/state_cgb.raw" 1 0 0
"$BUILD/smoke" "$BUILD/smoke.gb" "$BUILD/state_dmg.raw" 0 0 0
"$BUILD/smoke" "$BUILD/rtc.gbc" "$BUILD/state_rtc.raw" 1 1 1
"$BUILD/smoke" "$BUILD/mbc2.gbc" "$BUILD/state_mbc2.raw" 1 1 0
python3 - "$BUILD/state_cgb.raw" "$BUILD/state_dmg.raw" "$BUILD/state_rtc.raw" "$BUILD/state_mbc2.raw" <<'PY'
import sys
from pathlib import Path
def check(path, blocks, vram, sram, label):
    p=Path(path).read_bytes()
    assert len(p) == blocks * 4096, f"unexpected {label} state size: {len(p)}"
    def field(k):
        for off in range(0,4096,8):
            if p[off:off+4] == k.encode(): return int.from_bytes(p[off+4:off+8],"little")
            if p[off:off+4] == b"\0\0\0\0": break
        raise AssertionError(f"missing state field {k!r}")
    assert field("GbSs") == 0x105
    assert field("iram") == 1
    assert field("vram") == vram
    assert field("sram") == sram
    assert field("hi  ") == 3328
    assert field("wav ") == 3312
    assert field("pal ") == 3584
    assert field("oam ") == 3840
check(sys.argv[1],15,9,13,"CGB")
check(sys.argv[2],7,3,5,"DMG")
check(sys.argv[3],15,9,13,"CGB MBC3 RTC")
check(sys.argv[4],15,9,13,"CGB MBC2")
print("GNUBoy state layout PASS: DMG/CGB/MBC2/MBC3 RTC; internal offsets validated")
PY
