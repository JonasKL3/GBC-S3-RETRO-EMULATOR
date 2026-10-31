#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
class SerialClass { public: template<class... A> int printf(const char*,A...){return 0;} void println(const char* = ""){} };
extern SerialClass Serial;
inline uint32_t millis(){ static uint32_t t=1000; t+=17; return t; }
inline void delay(uint32_t){}
