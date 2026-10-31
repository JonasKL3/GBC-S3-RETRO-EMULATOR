#pragma once
#include <atomic>
#include <stdint.h>

// Exactly one producer and one consumer. A pending/reading frame is immutable.
class GbcFrameMailbox {
 public:
  bool beginWrite(){
    uint8_t expected=IDLE;
    return state.compare_exchange_strong(expected,WRITING,std::memory_order_acq_rel);
  }
  void publish(){state.store(READY,std::memory_order_release);}
  bool beginRead(){
    uint8_t expected=READY;
    return state.compare_exchange_strong(expected,READING,std::memory_order_acq_rel);
  }
  void finishRead(){state.store(IDLE,std::memory_order_release);}
  bool idle() const{return state.load(std::memory_order_acquire)==IDLE;}
 private:
  enum : uint8_t {IDLE,WRITING,READY,READING};
  std::atomic<uint8_t> state{IDLE};
};
