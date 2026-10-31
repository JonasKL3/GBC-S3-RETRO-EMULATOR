#pragma once
#include <assert.h>
#include <atomic>
#include <chrono>
#include <thread>
#include <stdint.h>
#include <vector>
class TFT_eSPI {
 public:
  struct Rect {int x,y,w,h;};
  std::atomic<bool> blocked{false};
  std::atomic<unsigned> pushes{0};
  std::atomic<uint16_t> lastColor{0};
  std::vector<uint16_t> pixels=std::vector<uint16_t>(320*240,0);
  std::vector<Rect> rectangles;
  void pushImage(int x,int y,int w,int h,uint16_t *data){
    while(blocked.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(x>=0&&y>=0&&w>0&&h>0&&x+w<=320&&y+h<=240);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)pixels[(y+row)*320+x+col]=data[row*w+col];
    rectangles.push_back({x,y,w,h});
    lastColor.store(data[0]);++pushes;
  }
};
