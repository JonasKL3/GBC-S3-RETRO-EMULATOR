#pragma once
#include <stddef.h>
#include <stdint.h>

// The transport holds its portMUX while calling any method of this FIFO.
class DabbleRxRing {
 public:
  static constexpr size_t CAPACITY=1024;
  bool append(const uint8_t *src,size_t n){
    if(n>CAPACITY-count_ || (!src && n))return false;
    for(size_t i=0;i<n;++i){data_[tail_]=src[i];tail_=(tail_+1)%CAPACITY;}
    count_+=n;return true;
  }
  size_t size() const{return count_;}
  uint8_t pop(){
    if(!count_)return 0;
    const uint8_t v=data_[head_];head_=(head_+1)%CAPACITY;--count_;return v;
  }
  void clear(){head_=tail_=count_=0;}
 private:
  uint8_t data_[CAPACITY]{};
  size_t head_=0,tail_=0,count_=0;
};
