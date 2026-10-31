#!/usr/bin/env sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
STUB="$ROOT/tests/host_stubs"
FW="$ROOT/firmware/GBC_S3"
FLAGS="-std=gnu++11 -w -DGBC_HOST_TEST -D_GNU_SOURCE -DARDUINO_ARCH_ESP32 -DCONFIG_IDF_TARGET_ESP32S3 -DUSE_HSPI_PORT -DTFT_CS=10 -DTFT_DC=9 -DTFT_RST=4 -DTFT_MOSI=11 -DTFT_MISO=13 -DTFT_SCLK=12 -DTFT_RGB_ORDER=0 -I$STUB -I$FW -I$FW/src -I$FW/src/gnuboy"
g++ $FLAGS -x c++ -fsyntax-only "$FW/GBC_S3.ino"
g++ $FLAGS -fsyntax-only "$FW/gnuboy_adapter.cpp"
g++ $FLAGS -fsyntax-only "$FW/gbc_savestates.cpp"
g++ $FLAGS -fsyntax-only "$FW/gbc_audio_output.cpp"
g++ $FLAGS -fsyntax-only "$FW/gbc_s3_covers.cpp"
g++ $FLAGS -fsyntax-only "$FW/halo_library.cpp"
printf '%s\n' 'GBC-S3 host syntax checks: PASS (stub headers; not an Arduino toolchain build)'
