#pragma once
#include "FreeRTOS.h"
#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <thread>
struct TestTask {std::mutex mutex;std::condition_variable cv;unsigned notifications=0;};
typedef TestTask* TaskHandle_t;
extern thread_local TestTask *currentTestTask;
inline int xPortGetCoreID(){return currentTestTask?1:0;}
inline void vTaskDelay(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
inline int xTaskCreatePinnedToCore(void(*fn)(void*),const char*,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,int core){
  assert(stack==6144 && priority==1 && core==1);
  *out=new TestTask();const auto task=*out;
  std::thread([=](){currentTestTask=task;fn(arg);}).detach();return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t task){
  std::lock_guard<std::mutex> lock(task->mutex);++task->notifications;task->cv.notify_one();
}
inline unsigned ulTaskNotifyTake(int clear,unsigned timeout){
  TestTask *task=currentTestTask;assert(task);
  std::unique_lock<std::mutex> lock(task->mutex);
  if(!task->cv.wait_for(lock,std::chrono::milliseconds(timeout),[=](){return task->notifications!=0;}))return 0;
  const unsigned count=task->notifications;if(clear)task->notifications=0;else --task->notifications;return count;
}
