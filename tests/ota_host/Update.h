#pragma once
#include "Arduino.h"
#include <functional>
constexpr int U_FLASH=0, U_SPIFFS=100, U_AUTH=200;
namespace otaFixture {
extern unsigned updateBegins, aborts, writes, nextWriteReport;
extern int updateCommand;
extern bool updateBeginOk, updateEndOk, fsFallback;
extern std::string partitionLabel;
extern std::function<void()> updateObserver;
extern std::function<void()> writeObserver;
}
class UpdateClass {
 public:
  bool begin(unsigned size, int cmd, int, int, const char* label) {
    ++otaFixture::updateBegins; otaFixture::updateCommand=cmd;
    otaFixture::partitionLabel=label ? label : "";
    if(otaFixture::updateObserver) otaFixture::updateObserver();
    running_ = otaFixture::updateBeginOk && (cmd!=U_SPIFFS || !label || otaFixture::fsFallback);
    size_=size; written_=0; return running_;
  }
  void setMD5(const char*) {}
  bool isFinished() const { return !running_ || written_>=size_; }
  size_t write(uint8_t*,size_t n) {
    ++otaFixture::writes;
    if(otaFixture::writeObserver) otaFixture::writeObserver();
    if(!running_) return 0;
    written_+=n;
    if(otaFixture::nextWriteReport) {
      unsigned reported=otaFixture::nextWriteReport;
      otaFixture::nextWriteReport=0;
      return reported;
    }
    return n;
  }
  void abort() { running_=false; ++otaFixture::aborts; }
  bool end() { bool ok=running_ && written_==size_ && otaFixture::updateEndOk; running_=false; return ok; }
  const char* errorString() const { return "controlled update error"; }
  void printError(WiFiClient&) {}
 private: bool running_=false; unsigned size_=0, written_=0;
};
extern UpdateClass Update;
