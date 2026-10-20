#include <WiFiSignal.h>
#include <cassert>
#include <iostream>
int main() {
  wifiSignal::Meter signal;
  assert(signal.observe(false,-40)==-1);
  assert(signal.observe(true,-65)==2);
  assert(signal.observe(true,-60)==2); // Boundary noise must not flash the panel.
  assert(signal.observe(true,-57)==3);
  assert(signal.observe(true,-62)==3);
  assert(signal.observe(true,-64)==2);
  assert(signal.observe(true,-72)==2);
  assert(signal.observe(true,-74)==1);
  assert(signal.observe(true,-84)==0);
  assert(signal.observe(true,-77)==1);
  assert(signal.observe(false,-45)==-1);
  assert(signal.observe(true,-60)==3); // Reconnection starts from actual RSSI.
  assert(signal.observe(true,0)==0); // Invalid readings never look excellent.
  assert(signal.observe(true,-127)==0);
  assert(wifiSignal::quality(-60)==3 && wifiSignal::quality(-70)==2);
  assert(wifiSignal::quality(-80)==1 && wifiSignal::quality(-81)==0);
  std::cout<<"PASS actual RSSI quality, 3dB hysteresis, disconnect/reconnect and invalid readings\n";
}
