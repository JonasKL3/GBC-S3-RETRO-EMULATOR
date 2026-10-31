#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>
#include "../firmware/GBC_S3/gbc_frame_mailbox.h"
#include "../firmware/GBC_S3/gbc_video_scale.h"

static_assert(gbc_should_render(0,false,0,16743),"first enlarged frame");
static_assert(!gbc_should_render(1,false,0,16743),"skip alternate pixel generation");
static_assert(gbc_should_render(2,false,0,16743),"enlarged cadence");
static_assert(gbc_should_render(1,true,0,16743),"native cadence");
static_assert(!gbc_should_render(2,false,34000,16743),"catch up only with a genuine backlog");
static_assert(gbc_should_render(4,false,20000,16743),"bound display starvation");
int main(){
#ifdef _WIN32
  _set_error_mode(1);_set_abort_behavior(0,3);
#endif
  GbcFrameMailbox gate;
  assert(gate.idle());assert(gate.beginWrite());assert(!gate.idle());
  assert(!gate.beginRead());assert(!gate.beginWrite());
  gate.publish();assert(!gate.beginWrite());assert(gate.beginRead());
  assert(!gate.beginRead());assert(!gate.beginWrite());
  gate.finishRead();assert(gate.idle());

  unsigned snapshot[256]={};unsigned job=0,received=0;
  constexpr unsigned FRAMES=5000;
  std::thread consumer([&](){
    for(unsigned expected=1;expected<=FRAMES;++expected){
      while(!gate.beginRead())std::this_thread::yield();
      assert(job==expected);
      for(unsigned i=0;i<256;++i)assert(snapshot[i]==expected+i);
      if((expected&15)==0)std::this_thread::yield();
      gate.finishRead();++received;
    }
  });
  for(unsigned serial=1;serial<=FRAMES;++serial){
    while(!gate.beginWrite())std::this_thread::yield();
    for(unsigned i=0;i<256;++i)snapshot[i]=serial+i;
    job=serial;gate.publish();
  }
  consumer.join();assert(received==FRAMES);assert(gate.idle());
  puts("Frame mailbox PASS: busy rejection, exclusive ownership, 5000 threaded handoffs, render admission");
}
