#pragma once
#include <stdint.h>
#include <stddef.h>
class File { public: operator bool()const{return true;} bool isDirectory()const{return true;} const char* name()const{return "test.gbc";} File openNextFile(){return File();} size_t size()const{return 0;} int read(uint8_t*,size_t){return 0;} size_t write(const uint8_t*,size_t n){return n;} void close(){} void flush(){} };
#define FILE_READ 0
#define FILE_WRITE 1
