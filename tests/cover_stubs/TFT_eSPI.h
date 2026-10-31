#pragma once
#include <stdint.h>
#include <assert.h>
#include <vector>
#define TFT_CYAN 0x07ff
#define TFT_LIGHTGREY 0xc618
class TFT_eSPI {
  bool swap=false;
public:
  std::vector<uint16_t> pixels=std::vector<uint16_t>(320*240,0);
  unsigned pushes=0;
  bool getSwapBytes(){return swap;} void setSwapBytes(bool value){swap=value;}
  void pushImage(int x,int y,int w,int h,uint16_t *p){
    assert(swap);assert(x>=0&&y>=0&&x+w<=320&&y+h<=240);++pushes;
    for(int row=0;row<h;++row)for(int col=0;col<w;++col)pixels[(y+row)*320+x+col]=p[row*w+col];
  }
  void fillRoundRect(int,int,int,int,int,uint16_t){} void drawRoundRect(int,int,int,int,int,uint16_t){}
  void setTextSize(int){} void setTextColor(uint16_t,uint16_t){} void setCursor(int,int){} void print(const char*){}
};
