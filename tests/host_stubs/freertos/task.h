#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
static inline void vTaskDelay(unsigned){}
static inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 0;}
static inline BaseType_t xTaskCreatePinnedToCore(void (*fn)(void*),const char*,unsigned,void*,UBaseType_t,TaskHandle_t*,BaseType_t){(void)fn;return pdPASS;}
