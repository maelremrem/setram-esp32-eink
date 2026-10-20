#pragma once
// Main orchestration platform seam. Actual unmodified ArduinoOTA.cpp is tested
// independently by run_ota_mode.py; this fixture never opens sockets/writes flash.
#include <functional>
#include <string>
#include <cstdint>
constexpr int U_FLASH=0;
enum ota_error_t { OTA_AUTH_ERROR, OTA_BEGIN_ERROR, OTA_CONNECT_ERROR, OTA_RECEIVE_ERROR, OTA_END_ERROR };
class ArduinoOTAClass;
struct HostOtaPlatform {
  unsigned begins=0,ends=0,handles=0;
  bool armed=false;
  ArduinoOTAClass* current=nullptr;
  std::function<void()> duringHandle;
};
extern HostOtaPlatform hostOta;
struct HostUpdate {void abort(){}};
extern HostUpdate Update;
class ArduinoOTAClass {
public:
  std::string hostname,password,label;
  uint16_t port=0; unsigned timeout=0; bool reboot=false;
  std::function<void()> start,finish;
  std::function<void(unsigned,unsigned)> progress;
  std::function<void(ota_error_t)> error;
  ArduinoOTAClass(){hostOta.current=this;}
  ~ArduinoOTAClass(){hostOta.current=nullptr;}
  void setHostname(const char* x){hostname=x;}
  void setPassword(const char* x){password=x;}
  void setPartitionLabel(const char* x){label=x;}
  void setPort(uint16_t x){port=x;}
  void setTimeout(unsigned x){timeout=x;}
  void setRebootOnSuccess(bool x){reboot=x;}
  void onStart(std::function<void()> x){start=x;}
  void onEnd(std::function<void()> x){finish=x;}
  void onProgress(std::function<void(unsigned,unsigned)> x){progress=x;}
  void onError(std::function<void(ota_error_t)> x){error=x;}
  int getCommand()const{return U_FLASH;}
  void begin(){++hostOta.begins;hostOta.armed=true;}
  void end(){++hostOta.ends;hostOta.armed=false;}
  void handle(){++hostOta.handles;if(hostOta.duringHandle){auto cb=std::move(hostOta.duringHandle);hostOta.duringHandle=nullptr;cb();}}
};
