#pragma once
#include "FreeRTOS.h"
typedef void* QueueHandle_t;
static inline QueueHandle_t xQueueCreate(unsigned n,unsigned sz){(void)n;(void)sz;return (void*)1;}
static inline BaseType_t xQueueReceive(QueueHandle_t,void*,TickType_t){return 0;}
static inline BaseType_t xQueueSend(QueueHandle_t,const void*,TickType_t){return 1;}
static inline BaseType_t xQueueReset(QueueHandle_t){return 1;}
static inline void vQueueDelete(QueueHandle_t){}
