#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <thread>
#include <chrono>
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <freertos/task.h>
#include "firmware/GBC_S3/gbc_video_output.h"
SerialClass Serial;thread_local TestTask *currentTestTask=nullptr;

static void verify(const TFT_eSPI &panel,const std::vector<uint16_t> &frame,bool enlarged){
  const unsigned width=enlarged?240:160,height=enlarged?216:144;
  const unsigned x0=enlarged?40:80,y0=enlarged?12:48;
  // Independent nearest-neighbour mapping, covering every displayed pixel.
  for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
    unsigned sx=enlarged?(x*2U/3U):x,sy=enlarged?(y*2U/3U):y;
    assert(panel.pixels[(y0+y)*320+x0+x]==frame[sy*160+sx]);
  }
}
static void send(const std::vector<uint16_t> &frame,bool enlarged){
  assert(GbcVideoOutput::submit(frame.data(),enlarged));GbcVideoOutput::drain();
  
}
int main(){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  TFT_eSPI panel;std::vector<uint16_t> stripe(240*8),frame(160*144);
  for(unsigned i=0;i<frame.size();++i)frame[i]=(uint16_t)((i*997U)^(i>>3));
  const auto original=frame;
  assert(!GbcVideoOutput::begin(panel,nullptr));
  assert(GbcVideoOutput::begin(panel,stripe.data()));panel.blocked=true;
  assert(GbcVideoOutput::submit(frame.data(),false));
  for(auto &p:frame)p^=0x1111;
  assert(!GbcVideoOutput::submit(frame.data(),true)); // In-flight snapshot stays immutable.
  std::thread unblock([&](){std::this_thread::sleep_for(std::chrono::milliseconds(20));panel.blocked=false;});
  GbcVideoOutput::drain();unblock.join();
  assert(panel.pushes==1);verify(panel,original,false);
  
  unsigned before=panel.pushes.load();
  send(original,false);assert(panel.pushes==before);

  send(frame,true);verify(panel,frame,true);
  assert(panel.pushes==before+27);
  before=panel.pushes.load();send(frame,true);verify(panel,frame,true);
  assert(panel.pushes==before);

  // Source row 42 duplicates into output rows 63/64, straddling two stripes.
  for(unsigned x=0;x<160;++x)frame[42*160+x]^=0x7bef;
  send(frame,true);verify(panel,frame,true);
  assert(panel.pushes==before+2);
  before=panel.pushes.load();
  GbcVideoOutput::invalidate(); // Caller cleared/drew a menu over the panel.
  for(auto &p:panel.pixels)p=0xeeee;
  send(frame,true);verify(panel,frame,true);
  assert(panel.pushes==before+27);

  before=panel.pushes.load();send(frame,false);verify(panel,frame,false);
  assert(panel.pushes==before+1); // Mode change forces redraw.
  frame[15*160+9]^=0x1234;
  before=panel.pushes.load();send(frame,false);verify(panel,frame,false);
  assert(panel.pushes==before+1); // Native keeps full changed transfer.

  // invalidate() is also a fence: it must wait for an active pushImage.
  frame[0]^=1;panel.blocked=true;assert(GbcVideoOutput::submit(frame.data(),false));
  std::thread unblockAgain([&](){std::this_thread::sleep_for(std::chrono::milliseconds(20));panel.blocked=false;});
  GbcVideoOutput::invalidate();unblockAgain.join();verify(panel,frame,false);
  before=panel.pushes.load();send(frame,false);
  assert(panel.pushes==before+1);
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  
  puts("Async video runtime PASS: exact patterned pixels, snapshot isolation, busy drop, drain/invalidate fences, identical-frame suppression, dirty stripe boundary, mode redraw, native/scaled bytes");
  fflush(stdout);
  // Permanent worker, like the firmware: don't destruct its fixtures on exit.
  std::_Exit(0);
}
