#include "WiFiUdp.h"
#include "ESPmDNS.h"
#include "Update.h"
#include "esp32-hal-cpu.h"
namespace otaFixture {
std::vector<std::string> logs;
unsigned restarts=0, delayCalls=0;
void log(const char* fmt, ...) {
 char b[512]; va_list args; va_start(args,fmt); vsnprintf(b,sizeof b,fmt,args); va_end(args); logs.emplace_back(b);
}
int wifiStatus=WL_CONNECTED;
wifi_ps_type_t sleep=WIFI_PS_MAX_MODEM;
uint32_t cpu=80;
std::vector<wifi_ps_type_t> sleepCalls;
std::vector<uint32_t> cpuCalls;
unsigned failSleepCalls=0, failCpuCalls=0, ignoreSleepCalls=0, ignoreCpuCalls=0;
std::string tcpData;
bool connectOk=true;
unsigned tcpConnects=0;
bool bindOk=true;
unsigned udpBegins=0, udpStops=0;
uint16_t port=0;
std::string packet, reply, hostname;
bool advertisedAuth=false;
unsigned mdnsBegins=0, mdnsEnds=0;
unsigned updateBegins=0, aborts=0, writes=0, nextWriteReport=0;
int updateCommand=0;
bool updateBeginOk=true, updateEndOk=true, fsFallback=false;
std::string partitionLabel;
std::function<void()> updateObserver, writeObserver;
}
ESPClass ESP;
WiFiClass WiFi;
MDNSClass MDNS;
UpdateClass Update;
uint32_t getCpuFrequencyMhz() { return otaFixture::cpu; }
bool setCpuFrequencyMhz(uint32_t value) {
 otaFixture::cpuCalls.push_back(value);
 if(otaFixture::failCpuCalls) { --otaFixture::failCpuCalls; return false; }
 if(otaFixture::ignoreCpuCalls) { --otaFixture::ignoreCpuCalls; return true; }
 otaFixture::cpu=value; return true;
}
