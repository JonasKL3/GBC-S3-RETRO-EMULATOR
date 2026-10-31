#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static constexpr bool gbc_video_due(int64_t now, int64_t last, int64_t interval) {
  return last == 0 || now - last >= interval;
}
static constexpr bool gbc_should_render(uint32_t frame,bool native,int64_t behind,uint32_t frameUs){
  return (native || (frame&1U)==0) && (behind<(int64_t)frameUs*2 || (frame&3U)==0);
}

// Scale a 160x144 RGB565 frame to 240x216 using nearest-neighbour 3:2 scaling.
// The source is the framebuffer written directly by GNUBoy's LCD renderer.
static inline void gbc_scale_rgb565_240x216(const uint16_t *src, uint16_t *dst) {
  unsigned dy = 0;
  for (unsigned y = 0; y < 144; ++y) {
    const uint16_t *row = src + (size_t)y * 160U;
    const bool duplicateY = ((y & 1U) == 0U);
    for (unsigned repeat = 0; repeat < (duplicateY ? 2U : 1U); ++repeat) {
      if (dy >= 216U) break;
      uint16_t *out = dst + (size_t)dy * 240U;
      for (unsigned x = 0; x < 80; ++x) {
        const uint16_t a = row[x * 2U];
        const uint16_t b = row[x * 2U + 1U];
        out[x * 3U] = a; out[x * 3U + 1U] = a; out[x * 3U + 2U] = b;
      }
      ++dy;
    }
  }
}

// Rows are bit-identical to the full-frame nearest-neighbour 3:2 scaler.
static inline void gbc_scale_rgb565_stripe(const uint16_t *src, uint16_t *dst,
                                           unsigned firstY, unsigned rows) {
  for(unsigned r=0;r<rows;++r){
    const unsigned dy=firstY+r;
    const unsigned sy=(dy/3U)*2U+((dy%3U)==2U?1U:0U);
    const uint16_t *in=src+(size_t)sy*160U;
    uint16_t *out=dst+(size_t)r*240U;
    for(unsigned x=0;x<80;++x){
      out[x*3U]=in[x*2U];out[x*3U+1U]=in[x*2U];out[x*3U+2U]=in[x*2U+1U];
    }
  }
}
