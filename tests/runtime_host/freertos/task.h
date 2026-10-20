#pragma once
#include <freertos/FreeRTOS.h>
#include <cstring>
#include <functional>
extern std::function<void()> onInputSample;
struct HostTask { const char* name=nullptr; uint32_t notifications=0; };
using TaskHandle_t=HostTask*;
inline HostTask* hostTasks() { static HostTask tasks[8]; return tasks; }
inline TaskHandle_t hostTaskNamed(const char* name) {
  for(unsigned i=0;i<8;++i) if(hostTasks()[i].name && std::strcmp(hostTasks()[i].name,name)==0) return &hostTasks()[i];
  return nullptr;
}
extern TaskHandle_t hostCurrentTask;
extern TickType_t hostTaskWaitTicks;
struct HostTaskBlocked {};
struct HostTaskPolling {};
inline TickType_t xTaskGetTickCount(){ return 0; }
inline void vTaskDelayUntil(TickType_t*,TickType_t){if(onInputSample)onInputSample();}
inline void vTaskDelay(TickType_t){ if(hostCurrentTask) throw HostTaskPolling{}; }
inline int xTaskCreatePinnedToCore(void(*)(void*),const char* name,int,void*,int,TaskHandle_t* out,int){
  static unsigned next=0;
  if(next>=8) return 0;
  TaskHandle_t task=&hostTasks()[next++]; task->name=name;
  if(out) *out=task;
  return pdPASS;
}
inline void xTaskNotifyGive(TaskHandle_t task){ if(task) ++task->notifications; }
inline uint32_t ulTaskNotifyTake(int clear,TickType_t wait){
  hostTaskWaitTicks=wait;
  if(!hostCurrentTask || !hostCurrentTask->notifications) throw HostTaskBlocked{};
  uint32_t count=hostCurrentTask->notifications;
  if(clear) hostCurrentTask->notifications=0; else --hostCurrentTask->notifications;
  return count;
}
