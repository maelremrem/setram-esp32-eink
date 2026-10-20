#pragma once
#include "Arduino.h"
#include "esp_wifi_types.h"
constexpr int WL_CONNECTED = 3;
struct IPAddress {};
namespace otaFixture {
extern int wifiStatus;
extern wifi_ps_type_t sleep;
extern uint32_t cpu;
extern std::vector<wifi_ps_type_t> sleepCalls;
extern std::vector<uint32_t> cpuCalls;
extern unsigned failSleepCalls, failCpuCalls, ignoreSleepCalls, ignoreCpuCalls;
extern std::string tcpData;
extern bool connectOk;
extern unsigned tcpConnects;
}
class WiFiClass {
 public:
  int status() const { return otaFixture::wifiStatus; }
  wifi_ps_type_t getSleep() const { return otaFixture::sleep; }
  bool setSleep(bool enabled) { return setSleep(enabled ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE); }
  bool setSleep(wifi_ps_type_t type) {
    otaFixture::sleepCalls.push_back(type);
    if (otaFixture::failSleepCalls) { --otaFixture::failSleepCalls; return false; }
    if (otaFixture::ignoreSleepCalls) { --otaFixture::ignoreSleepCalls; return true; }
    otaFixture::sleep = type; return true;
  }
  void macAddress(uint8_t* mac) { std::memset(mac, 0, 6); }
};
extern WiFiClass WiFi;
class WiFiClient {
 public:
  bool connect(IPAddress, int) { ++otaFixture::tcpConnects; connected_ = otaFixture::connectOk; return connected_; }
  bool connected() const { return connected_; }
  unsigned available() const { return otaFixture::tcpData.size() - offset_; }
  size_t read(uint8_t* out, size_t n) {
    n = std::min(n, otaFixture::tcpData.size()-offset_);
    std::memcpy(out, otaFixture::tcpData.data()+offset_, n); offset_ += n; return n;
  }
  int printf(const char*, ...) { return 1; }
  void print(const char*) {}
  void stop() { connected_ = false; }
 private: bool connected_ = false; size_t offset_ = 0;
};
#include "WiFiUdp.h"
