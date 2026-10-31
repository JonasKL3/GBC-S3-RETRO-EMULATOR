#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../firmware/GBC_S3/gbc_video_scale.h"
static_assert(gbc_video_due(1,0,33486), "first frame");
static_assert(!gbc_video_due(33485+100,100,33486), "too early");
static_assert(gbc_video_due(33486+100,100,33486), "deadline");
static_assert(gbc_video_due(1000000,100,33486), "display must survive core lag");
static_assert(gbc_video_due(16743+100,100,16743), "native cadence");
static_assert(gbc_should_render(2,false,9000,16743), "half-frame phase delay must not halve enlarged FPS");
static_assert(gbc_should_render(3,true,16000,16743), "native keeps rendering with less than one frame phase lag");
static_assert(!gbc_should_render(2,false,34000,16743), "genuine backlog permits catch-up");
static_assert(gbc_should_render(4,false,34000,16743), "catch-up retains periodic picture");
int main(){
  uint16_t src[160*144], full[240*216], striped[240*216+2];
  for(unsigned i=0;i<160*144;++i)src[i]=(uint16_t)(i*37U);
  gbc_scale_rgb565_240x216(src,full);
  striped[0]=0x1234;striped[240*216+1]=0x5678;
  for(unsigned y=0;y<216;y+=8){
    unsigned rows=216-y<8?216-y:8;
    gbc_scale_rgb565_stripe(src,striped+1+y*240,y,rows);
  }
  assert(memcmp(full,striped+1,sizeof(full))==0);
  assert(striped[0]==0x1234 && striped[240*216+1]==0x5678);
}
