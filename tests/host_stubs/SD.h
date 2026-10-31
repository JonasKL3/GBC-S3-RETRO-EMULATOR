#pragma once
#include "FS.h"
#include "SPI.h"
class SDClass {public: bool begin(int,SPIClass&,uint32_t,const char*,uint8_t,bool){return true;} void end(){} File open(const char*,int){return File();} bool exists(const char*){return true;} bool mkdir(const char*){return true;} bool remove(const char*){return true;} bool rename(const char*,const char*){return true;} uint64_t cardSize(){return 0;}};
extern SDClass SD;
