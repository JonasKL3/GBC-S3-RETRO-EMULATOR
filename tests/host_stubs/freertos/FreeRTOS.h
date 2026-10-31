#pragma once
#include <stdint.h>
typedef int BaseType_t; typedef unsigned int UBaseType_t; typedef uint32_t TickType_t;
#define pdTRUE 1
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
