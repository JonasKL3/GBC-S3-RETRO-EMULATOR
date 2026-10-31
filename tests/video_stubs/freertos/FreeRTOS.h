#pragma once
#include <mutex>
#include <stdint.h>
typedef int BaseType_t;typedef unsigned UBaseType_t;
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
typedef std::mutex portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) ((p)->lock())
#define portEXIT_CRITICAL(p) ((p)->unlock())
