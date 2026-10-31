#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "firmware/GBC_S3/gnuboy_adapter.h"
extern "C" {
#include "firmware/GBC_S3/src/gnuboy/defs.h"
#include "firmware/GBC_S3/src/gnuboy/save.h"
#include "firmware/GBC_S3/src/gnuboy/lcd.h"
#include "firmware/GBC_S3/src/gnuboy/sound.h"
#include "firmware/GBC_S3/src/gnuboy/mem.h"
}
extern "C" int gbc_audio_submit_pcm(const unsigned char*,size_t){return 1;}
static std::vector<unsigned char> state(){
  FILE *f=tmpfile();assert(f);assert(savestate_checked(f));assert(fflush(f)==0);
  long size=ftell(f);assert(size>4096);std::vector<unsigned char> out((size_t)size);
  rewind(f);assert(fread(out.data(),1,out.size(),f)==out.size());fclose(f);return out;
}
int main(int argc,char **argv){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  assert(argc==2);std::vector<uint16_t> frame(GbcCore::FRAME_PIXELS);
  assert(GbcCore::beginPlatform(frame.data(),GbcCore::FRAME_BYTES));assert(GbcCore::loadRomPath(argv[1]));
  // Continuously mutate tile data while LCDC advances, rather than rendering a
  // static blank ROM. Pixel generation must not alter emulated machine timing.
  const unsigned char program[]={0x3e,0x00,0xea,0x00,0x80,0x3c,0x18,0xfa};
  memcpy(rom.bank[0]+0x100,program,sizeof program);
  const auto initial=state();
  for(unsigned i=0;i<24;++i){GbcCore::setFrameRendering(true);assert(GbcCore::runFrame()==0);}
  const auto referenceState=state();const auto referencePixels=frame;
  GbcCore::reset();
  FILE *f=tmpfile();assert(f);assert(fwrite(initial.data(),1,initial.size(),f)==initial.size());rewind(f);
  assert(loadstate_checked(f));fclose(f);vram_dirty();pal_dirty();sound_dirty();mem_updatemap();
  for(unsigned i=0;i<24;++i){GbcCore::setFrameRendering((i&1U)!=0);assert(GbcCore::runFrame()==0);}
  assert(state()==referenceState);assert(frame==referencePixels);
  GbcCore::endPlatform();puts("Frame-render skip PASS: CPU/RAM/PPU/audio/RTC state and final pixels identical");
}
