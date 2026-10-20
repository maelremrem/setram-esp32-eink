#pragma once
#include <Transit.h>
#include <DisplayPolicy.h>
#include <string>
#include <ctime>
namespace paintPolicy {
// Local civil boundary, not now+86400: DST days may have 23 or 25 hours.
inline int64_t todayEnd(int64_t now) {
  if(now<=1600000000) return 0;
  time_t stamp=static_cast<time_t>(now);
  struct tm civil;
  if(!localtime_r(&stamp,&civil)) return 0;
  civil.tm_hour=0; civil.tm_min=0; civil.tm_sec=0;
  ++civil.tm_mday; civil.tm_isdst=-1;
  const time_t end=mktime(&civil);
  return end>now ? static_cast<int64_t>(end) : 0;
}
// Compare actual 128x250 native black-bit canvas, ignoring invisible source
// columns 122..127. This also protects SSD's full-only driver, which has no
// internal equality guard. Interactive hints never invalidate pixel equality.
class Frame {
  uint8_t previous[4000]={};
  bool initialized=false;
public:
  bool changed(const uint8_t* pixels) {
    bool different=!initialized;
    for(unsigned i=0;i<4000;++i) {
      const uint8_t value=pixels[i] & (i%16==15 ? 0xc0 : 0xff);
      if(previous[i]!=value) different=true;
      previous[i]=value;
    }
    initialized=true;
    return different;
  }
};
// Visible semantics only: feed timestamps and request durations are diagnostics,
// not a reason to flash an otherwise identical e-paper image.
struct FeedView {
  bool usable=false;
  bool tolerated=false;
  std::string primary="--", next[2]={"--","--"}, status="Indisponible", footer;
  std::string key() const {
    return primary+"|"+next[0]+"|"+next[1]+"|"+status+"|"+footer;
  }
};
inline FeedView feed(const transit::Result& result, bool valid, bool connected,
                     int64_t now, const std::string& error) {
  FeedView view;
  view.usable=valid && connected && displayPolicy::usable(result.timestamp,now);
  view.tolerated=view.usable && displayPolicy::classify(result.timestamp,now)==displayPolicy::FeedState::FutureTolerated;
  if(!view.usable) {
    view.footer=error;
    if(valid) {
      if(!connected) view.footer="Wi-Fi deconnecte";
      else switch(displayPolicy::classify(result.timestamp,now)) {
        case displayPolicy::FeedState::Missing: view.footer="Horodatage flux absent"; break;
        case displayPolicy::FeedState::ClockInvalid: view.footer="Synchronisation heure"; break;
        case displayPolicy::FeedState::FutureRejected: view.footer="Horloge flux avancee"; break;
        default: view.footer="Donnees perimees"; break;
      }
    }
    return view;
  }
  unsigned count=0;
  const int64_t end=todayEnd(now);
  for(const auto& departure : result.departures) {
    if(departure.time<now || departure.time>=end) continue;
    int minutes=transit::minutesRemaining(departure.time,now);
    if(count==0) view.primary=departure.time-now<60 ? "<1" : minutes>999 ? "999+" : std::to_string(minutes);
    else if(count<=2) view.next[count-1]=minutes>999 ? ">999 m" : std::to_string(minutes)+" min";
    ++count;
  }
  view.status=view.tolerated ? "Flux avance" : count ? "Temps reel" : "Non annonce";
  view.footer=view.tolerated ? (count ? "Flux avance" : "Flux avance - Aucun passage") : count ? "Temps reel" : "Aucun passage annonce";
  return view;
}
}
