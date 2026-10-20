#pragma once
namespace wifiSignal {
inline int quality(int rssi) {
  if(rssi>=0 || rssi<-120) return 0;
  return rssi>=-60 ? 3 : rssi>=-70 ? 2 : rssi>=-80 ? 1 : 0;
}
class Meter {
  int level=-1;
  static int threshold(int target) { return target==3 ? -60 : target==2 ? -70 : -80; }
public:
  int observe(bool connected,int rssi) {
    if(!connected) return level=-1;
    if(rssi>=0 || rssi<-120) return level=0;
    const int wanted=quality(rssi);
    if(level<0) return level=wanted;
    // Keep the icon stable around boundaries: one frame of RSSI noise must not
    // trigger a full e-paper refresh. Significant changes can cross several bands.
    if(wanted>level) while(level<wanted && rssi>=threshold(level+1)+3) ++level;
    if(wanted<level) while(level>wanted && rssi<threshold(level)-3) --level;
    return level;
  }
};
}
