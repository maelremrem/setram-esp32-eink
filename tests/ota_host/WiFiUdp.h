#pragma once
#include "WiFi.h"
namespace otaFixture {
extern bool bindOk;
extern unsigned udpBegins, udpStops;
extern uint16_t port;
extern std::string packet, reply;
}
class WiFiUDP {
 public:
  bool begin(uint16_t port) { ++otaFixture::udpBegins; otaFixture::port = port; return otaFixture::bindOk; }
  void stop() { ++otaFixture::udpStops; }
  int parsePacket() { return otaFixture::packet.size(); }
  int peek() { return offset_ < otaFixture::packet.size() ? otaFixture::packet[offset_] : -1; }
  int read() { int v=peek(); if(v>=0) ++offset_; return v; }
  IPAddress remoteIP() const { return IPAddress(); }
  uint16_t remotePort() const { return 1234; }
  void beginPacket(IPAddress, uint16_t) { otaFixture::reply.clear(); }
  void print(const char* s) { otaFixture::reply+=s; }
  int printf(const char* fmt, ...) {
    char b[256]; va_list args; va_start(args,fmt); int n=vsnprintf(b,sizeof b,fmt,args); va_end(args);
    otaFixture::reply+=b; return n;
  }
  void endPacket() {}
  void flush() { otaFixture::packet.clear(); offset_ = 0; }
 private: size_t offset_ = 0;
};
