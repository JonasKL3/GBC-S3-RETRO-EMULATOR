#include "gbc_video_output.h"
#include "gbc_frame_mailbox.h"
#include "gbc_video_scale.h"
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdlib.h>

namespace GbcVideoOutput {
static TFT_eSPI *panel=nullptr;
static uint16_t *snapshot=nullptr,*staging=nullptr,*lastDisplayed=nullptr;
static TaskHandle_t worker=nullptr;
static GbcFrameMailbox mailbox;
static bool jobEnlarged=false;
static bool referenceValid=false,lastEnlarged=false;

static void displayTask(void*){
  // CPU1's idle task is not watched in this SDK configuration. Register the
  // worker itself, and wake periodically even when no game is being displayed.
  const bool watched=esp_task_wdt_add(nullptr)==ESP_OK;
  if(!watched)Serial.println("[VIDEO][WARN] watchdog da tarefa nao foi registrado");
  for(;;){
    if(watched)esp_task_wdt_reset();
    if(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(1000))==0)continue;
    if(!mailbox.beginRead())continue;
    bool changed=false;
    const bool compare=lastDisplayed && referenceValid && lastEnlarged==jobEnlarged;
    if(jobEnlarged){
      for(unsigned y=0;y<216;y+=8){
        const unsigned rows=216-y<8?216-y:8;
        // Each output stripe reads a contiguous range of native source lines.
        // Exact comparison avoids hash collisions; duplicated lines crossing a
        // stripe boundary are compared in both stripes and both are updated.
        const unsigned firstSource=(y*2U)/3U;
        const unsigned lastSource=((y+rows-1U)*2U)/3U;
        if(compare && memcmp(snapshot+firstSource*160U,lastDisplayed+firstSource*160U,
            (lastSource-firstSource+1U)*160U*sizeof(uint16_t))==0)continue;

        gbc_scale_rgb565_stripe(snapshot,staging,y,rows);

        
        panel->pushImage(40,12+y,240,rows,staging);

        changed=true;
      }
    }else{
      if(!compare || memcmp(snapshot,lastDisplayed,160U*144U*sizeof(uint16_t))!=0){

        panel->pushImage(80,48,160,144,snapshot);

        changed=true;
      }
    }
    if(lastDisplayed){
      if(changed || !compare)memcpy(lastDisplayed,snapshot,160U*144U*sizeof(uint16_t));
      lastEnlarged=jobEnlarged;referenceValid=true;
    }
    // pushImage is synchronous: SPI is released before ownership goes idle.
    mailbox.finishRead();
    vTaskDelay(1); // Give this core's idle/system tasks a guaranteed opportunity.
  }
}

bool begin(TFT_eSPI &display,uint16_t *stripe){
  if(worker)return true;
  if(!stripe)return false;
  snapshot=(uint16_t*)heap_caps_malloc(160U*144U*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(!snapshot)return false;
  // This reference cache is optional: low-memory boards retain full transfers.
  lastDisplayed=(uint16_t*)heap_caps_malloc(160U*144U*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  panel=&display;staging=stripe;
  const BaseType_t loopCore=xPortGetCoreID();
  const BaseType_t displayCore=loopCore==0?1:0;
  if(xTaskCreatePinnedToCore(displayTask,"GBC_LCD",6144,nullptr,1,&worker,displayCore)!=pdPASS){
    heap_caps_free(snapshot);heap_caps_free(lastDisplayed);snapshot=nullptr;lastDisplayed=nullptr;
    panel=nullptr;staging=nullptr;return false;
  }
  Serial.printf("[VIDEO] ASYNC loopCore=%d lcdCore=%d snapshot=46080B stripe=3840B DMA=OFF\n",(int)loopCore,(int)displayCore);
  Serial.printf("[VIDEO] DIRTY_STRIPES=%s reference=%uB\n",lastDisplayed?"ON":"OFF",lastDisplayed?46080U:0U);
  return true;
}

bool submit(const uint16_t *frame,bool enlarged){
  if(!worker || !frame || !mailbox.beginWrite())return false;
  // A busy worker never blocks the core and its frame can never be overwritten.
  memcpy(snapshot,frame,160U*144U*sizeof(uint16_t));
  jobEnlarged=enlarged;
  mailbox.publish();
  xTaskNotifyGive(worker);
  return true;
}

void drain(){
  const int64_t drainStarted=esp_timer_get_time();
  while(!mailbox.idle()){
    // Never proceed to SD/TFT if an asynchronous transfer failed to finish.
    if(esp_timer_get_time()-drainStarted>10000000LL){
      Serial.println("[VIDEO][FATAL] SPI nao terminou em 10s; acesso ao SD bloqueado");
      abort();
    }
    vTaskDelay(1);
  }
}
void invalidate(){drain();referenceValid=false;}

}
