#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CORE="$ROOT/firmware/GBC_S3/src/gnuboy"
BUILD="${TMPDIR:-/tmp}/gbc_s3_state_roundtrip.$$"
trap 'rm -rf "$BUILD" /tmp/gbc_s3_state_sd' EXIT
mkdir -p "$BUILD"
python3 - "$BUILD/smoke.gbc" <<'PY'
import sys
from pathlib import Path
p=bytearray(32768)
p[0x100:0x102]=bytes((0x18,0xFE))
p[0x134:0x13c]=b'GBCSMOKE'
p[0x143]=0x80;p[0x147]=0x00;p[0x148]=0x00;p[0x149]=0x00
Path(sys.argv[1]).write_bytes(p)
PY
CFLAGS="-std=gnu11 -O2 -w -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -I$ROOT/tests/host_stubs -I$CORE"
for f in cpu.c mem.c lcd.c lcdc.c hw.c rtc.c sound.c palette.c fastmem.c emu.c loader.c save.c inflate.c miniz_tinfl.c xz/xz_crc32.c xz/xz_crc64.c xz/xz_dec_lzma2.c xz/xz_dec_stream.c xz/xz_dec_bcj.c gnuboy_platform.c; do
  obj=$(printf '%s' "$f" | tr '/' '_')
  gcc $CFLAGS -c "$CORE/$f" -o "$BUILD/$obj.o"
done
CXXFLAGS="-std=gnu++11 -O2 -w -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -DGBC_VFS_MOUNT=\"/tmp/gbc_s3_state_sd\" -DGBC_HOST_SD_ROOT=\"/tmp/gbc_s3_state_sd\" -I$ROOT/tests/state_stubs -I$ROOT/tests/host_stubs -I$ROOT -I$ROOT/firmware/GBC_S3 -I$CORE"
g++ $CXXFLAGS -c "$ROOT/firmware/GBC_S3/gnuboy_adapter.cpp" -o "$BUILD/adapter.o"
g++ $CXXFLAGS -c "$ROOT/firmware/GBC_S3/gbc_savestates.cpp" -o "$BUILD/savestates.o"
g++ $CXXFLAGS -c "$ROOT/tests/host_gbc_state_roundtrip.cpp" -o "$BUILD/test.o"
g++ $BUILD/*.o -o "$BUILD/state_test"
"$BUILD/state_test" "$BUILD/smoke.gbc"
