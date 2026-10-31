#pragma once
#include <stdint.h>
inline int64_t esp_timer_get_time(){static int64_t t=0;return t+=100;}
