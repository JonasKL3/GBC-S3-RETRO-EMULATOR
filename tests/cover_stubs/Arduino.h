#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
class SerialClass {public: template<class... A> int printf(const char*,A...){return 0;} void println(const char* = ""){} };
extern SerialClass Serial;
extern uint32_t coverTestNow;
inline uint32_t millis(){return coverTestNow;}
inline void delay(uint32_t ms){coverTestNow+=ms;}
