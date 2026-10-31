#pragma once
#include <stdint.h>
#include <stdio.h>
class SerialClass {public:template<class...A>int printf(const char *s,A...a){return ::printf(s,a...);}void println(const char*s){puts(s);}};
extern SerialClass Serial;
