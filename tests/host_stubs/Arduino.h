#pragma once
#define SET_LOOP_TASK_STACK_SIZE(sz) static_assert((sz)>=16384,"loop stack too small")
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <algorithm>
#include <chrono>
using std::size_t;
#define OUTPUT 1
#define HIGH 1
#define LOW 0
#define TFT_BLACK 0
#define TFT_NAVY 1
#define TFT_CYAN 2
#define TFT_WHITE 3
#define TFT_YELLOW 4
#define TFT_GREEN 5
#define TFT_RED 6
#define TFT_LIGHTGREY 7
#define TFT_BLUE 8
#define TFT_ORANGE 9
#define TFT_MAGENTA 10
#define TFT_DARKGREY 11
#define TFT_PURPLE 12
class String {
 std::string s;
 public:
 String(){} String(const char *v):s(v?v:""){} String(const std::string&v):s(v){}
 size_t length()const{return s.size();} const char*c_str()const{return s.c_str();}
 int lastIndexOf(char c)const{auto p=s.find_last_of(c);return p==std::string::npos?-1:(int)p;}
 String substring(size_t p)const{return String(p<s.size()?s.substr(p):std::string());}
 void remove(size_t p){if(p<s.size())s.erase(p);}
 String operator+(const String&o)const{return String(s+o.s);} String operator+(const char*o)const{return String(s+(o?o:""));}
 friend String operator+(const char*a,const String&b){return String(std::string(a?a:"")+b.s);}
};
class SerialClass {public: void begin(int){} template<class... A> int printf(const char*,A...){return 0;} void println(const char* = ""){} void print(const char*){} };
extern SerialClass Serial;
inline uint32_t millis(){static uint32_t t=0;return ++t;}
inline void delay(uint32_t){} inline void delayMicroseconds(uint32_t){}
inline void pinMode(int,int){} inline void digitalWrite(int,int){}
class ESPClass {public: uint32_t getFreeHeap(){return 100000;} uint32_t getFreePsram(){return 4000000;}};
extern ESPClass ESP;
