#include "OtaMode.h"
#include "WiFiUdp.h"
#include "MD5Builder.h"
#include "ESPmDNS.h"
#include "Update.h"
#include <cassert>
#include <iostream>
using otaMode::Session;
using otaMode::Phase;
namespace f = otaFixture;
static void authorize(Session& s, const char* password, int command=U_FLASH, unsigned size=3000) {
  f::packet=std::to_string(command)+" 1234 "+std::to_string(size)+" "+f::md5("fixture image")+"\n";
  s.handle();
  assert(f::reply.substr(0,5)=="AUTH ");
  const std::string nonce=f::reply.substr(5), cnonce=f::md5("fixture client");
  const std::string response=f::md5(f::md5(password)+":"+nonce+":"+cnonce);
  f::packet="200 "+cnonce+" "+response+"\n";
  s.handle();
}
int main() {
  Session s;
  assert(s.phase()==Phase::Off && !s.active() && !s.transferring());
  assert(s.progress()==0 && s.detail() && s.detail()[0]);
  s.handle();
  assert(f::udpBegins==0 && f::mdnsBegins==0 && f::sleepCalls.empty() && f::cpuCalls.empty());
  assert(s.stop());
  std::cout << "PASS: OTA default-off, no boot listener/platform changes\n";
  f::wifiStatus=0;
  assert(!s.begin("crowpanel-test", "first-secret-12345"));
  f::wifiStatus=WL_CONNECTED;
  assert(!s.begin(nullptr, "first-secret-12345"));
  assert(!s.begin("", "first-secret-12345"));
  assert(!s.begin("crowpanel-test", nullptr));
  assert(!s.begin("crowpanel-test", "short"));
  assert(!s.begin("crowpanel-test", "12345678901"));
  assert(f::udpBegins==0 && f::sleepCalls.empty() && f::cpuCalls.empty());
  assert(s.begin("crowpanel-test", "first-secret-12345"));
  assert(s.phase()==Phase::Waiting && s.active() && !s.transferring());
  assert(f::sleep==WIFI_PS_NONE && f::cpu==240);
  assert(f::udpBegins==1 && f::port==3232 && f::hostname=="crowpanel-test" && f::advertisedAuth);
  assert(!s.begin("other-host", "second-secret-12345"));
  s.handle();
  assert(s.phase()==Phase::Waiting);
  assert(s.stop());
  assert(!s.active() && s.phase()==Phase::Off);
  assert(f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80 && f::mdnsEnds==1);
  std::cout << "PASS: validation, authenticated settings, power override and exact restoration\n";
  unsigned binds=f::udpBegins;
  f::failSleepCalls=1;
  assert(!s.begin("crowpanel-test", "first-secret-12345"));
  assert(!s.active() && s.phase()==Phase::Off && f::udpBegins==binds);
  assert(f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  f::failCpuCalls=1;
  assert(!s.begin("crowpanel-test", "first-secret-12345"));
  assert(!s.active() && f::udpBegins==binds && f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  f::ignoreSleepCalls=1;
  assert(!s.begin("readback-check", "123456789012"));
  assert(!s.active() && f::udpBegins==binds && f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  f::ignoreCpuCalls=1;
  assert(!s.begin("readback-check", "123456789012"));
  assert(!s.active() && f::udpBegins==binds && f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  for (wifi_ps_type_t initial : {WIFI_PS_NONE, WIFI_PS_MIN_MODEM, WIFI_PS_MAX_MODEM}) {
    f::sleep=initial; f::cpu=160;
    assert(s.begin("power-check", "123456789012"));
    assert(f::sleep==WIFI_PS_NONE && f::cpu==240);
    assert(s.stop() && f::sleep==initial && f::cpu==160);
  }
  f::sleep=WIFI_PS_MAX_MODEM; f::cpu=80;
  assert(s.begin("restore-check", "123456789012"));
  f::failSleepCalls=1; f::failCpuCalls=1;
  assert(!s.stop() && s.phase()==Phase::Off && !s.active());
  binds=f::udpBegins;
  assert(!s.begin("blocked-restore", "123456789012") && f::udpBegins==binds);
  assert(s.stop() && f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  assert(s.begin("restore-check", "123456789012"));
  Session second;
  assert(!second.begin("not-owner", "123456789012"));
  assert(second.stop() && s.active());
  assert(s.stop());
  {
    Session scoped;
    assert(scoped.begin("scope-check", "123456789012"));
  }
  assert(f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  std::cout << "PASS: setup failures never arm, all sleep modes restore, failed restoration retry, sole ownership and RAII\n";
  assert(s.begin("transfer-check", "first-secret-12345"));
  unsigned updates=f::updateBegins;
  authorize(s, "wrong-secret-12345");
  assert(s.phase()==Phase::Error && s.active() && !s.transferring() && s.progress()==0);
  assert(std::string(s.detail()).find("authentification")!=std::string::npos);
  assert(f::updateBegins==updates);
  authorize(s, "first-secret-12345");
  assert(f::reply=="OK");
  f::updateBeginOk=false;
  s.handle();
  assert(s.phase()==Phase::Error && !s.transferring());
  assert(std::string(s.detail()).find("demarrage")!=std::string::npos);
  f::updateBeginOk=true;
  authorize(s, "first-secret-12345");
  f::tcpData=std::string(3000,'x');
  std::vector<unsigned> progressBeforeWrites;
  f::writeObserver=[&] {
    assert(s.phase()==Phase::Receiving && s.transferring() && s.active());
    progressBeforeWrites.push_back(s.progress());
    unsigned stops=f::udpStops;
    assert(!s.stop() && f::udpStops==stops && f::sleep==WIFI_PS_NONE && f::cpu==240);
  };
  s.handle();
  f::writeObserver=nullptr;
  assert((progressBeforeWrites==std::vector<unsigned>{0,48,97}));
  assert(s.phase()==Phase::Success && !s.transferring() && s.progress()==100 && f::restarts==1);
  assert(std::string(s.detail()).find("reussie")!=std::string::npos);
  assert(s.stop() && f::sleep==WIFI_PS_MAX_MODEM && f::cpu==80);
  assert(s.begin("fresh-host", "second-secret-12345"));
  assert(f::hostname=="fresh-host");
  updates=f::updateBegins;
  authorize(s, "first-secret-12345");
  assert(s.phase()==Phase::Error && f::updateBegins==updates);
  authorize(s, "second-secret-12345");
  assert(f::reply=="OK");
  f::updateEndOk=false;
  s.handle();
  assert(s.phase()==Phase::Error && s.active() && !s.transferring());
  assert(std::string(s.detail()).find("validation")!=std::string::npos);
  f::updateEndOk=true;
  assert(s.stop());
  for (const auto& message : f::logs) {
    assert(message.find("first-secret-12345")==std::string::npos);
    assert(message.find("second-secret-12345")==std::string::npos);
  }
  std::cout << "PASS: actual ArduinoOTA MD5 authentication, synchronous callbacks, progress, receiving stop guard, retry, success reboot and fresh credentials; no secret logs\n";
  assert(s.begin("firmware-only", "123456789012"));
  unsigned writes=f::writes, restarts=f::restarts, aborts=f::aborts;
  authorize(s, "123456789012", U_SPIFFS);
  // Update.begin precedes onStart in the real framework. No partition match
  // must fail before the callback; a FAT fallback must be aborted in onStart.
  f::updateObserver=[&] { assert(s.phase()!=Phase::Receiving); };
  s.handle();
  assert(f::partitionLabel=="ota_filesystem_disabled");
  assert(s.phase()==Phase::Error && !s.transferring() && f::writes==writes && f::restarts==restarts);
  assert(std::string(s.detail()).find("fichiers")!=std::string::npos);
  f::fsFallback=true;
  authorize(s, "123456789012", U_SPIFFS);
  s.handle();
  f::fsFallback=false; f::updateObserver=nullptr;
  assert(s.phase()==Phase::Error && f::aborts>aborts && f::writes==writes && f::restarts==restarts);
  assert(std::string(s.detail()).find("fichiers")!=std::string::npos);
  authorize(s, "123456789012");
  s.handle();
  assert(s.phase()==Phase::Success && f::restarts==restarts+1);
  assert(s.stop());
  std::cout << "PASS: filesystem preflight denial, FAT fallback onStart abort, no FS writes/reboot, firmware retry\n";
  assert(s.begin("error-check", "123456789012"));
  f::connectOk=false;
  authorize(s, "123456789012");
  s.handle();
  assert(s.phase()==Phase::Error && s.progress()==0 && !s.transferring());
  assert(std::string(s.detail()).find("Connexion")!=std::string::npos);
  f::connectOk=true;
  f::tcpData.clear();
  authorize(s, "123456789012");
  unsigned delays=f::delayCalls;
  s.handle();
  assert(f::delayCalls-delays==10000);
  assert(s.phase()==Phase::Error && std::string(s.detail()).find("Reception")!=std::string::npos);
  assert(s.stop());
  std::cout << "PASS: synchronous connection/receive failures keep useful French diagnostics and allow stop\n";
  assert(s.begin("bounds-check", "123456789012"));
  f::tcpData=std::string(3000,'x');
  // Deliberately invalid platform reporting exercises defensive arithmetic;
  // this does not represent a valid real image or actual Update.write result.
  f::nextWriteReport=UINT32_MAX;
  unsigned observedWrites=0;
  f::writeObserver=[&] {
    assert(s.progress()<=100);
    if (observedWrites++==1) assert(s.progress()==100);
  };
  authorize(s, "123456789012");
  s.handle();
  f::writeObserver=nullptr;
  assert(observedWrites==3 && s.phase()==Phase::Success);
  // Real Update refuses size zero. The controlled fixture admits it solely
  // to invoke actual ArduinoOTA's progress(0,0) and test divide-by-zero safety.
  authorize(s, "123456789012", U_FLASH, 0);
  s.handle();
  assert(s.phase()==Phase::Success && s.progress()==100);
  assert(s.stop());
  std::cout << "PASS: progress stays bounded on UINT_MAX and zero-total fixture reports\n";
}
