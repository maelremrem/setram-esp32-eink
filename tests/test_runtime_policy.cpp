#include <PaintPolicy.h>
#include <BootFlow.h>
#include <cstdlib>
#include <ctime>
#include <iostream>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
int main() {
  const int64_t now=1800000000;
  transit::Result first; first.timestamp=now+120;
  first.departures.push_back(transit::Departure{now+301});
  auto a=paintPolicy::feed(first,true,true,now,"Temps reel");
  first.timestamp=now+150;
  auto b=paintPolicy::feed(first,true,true,now,"Flux avance +150s");
  CHECK(a.primary=="6");
  CHECK(a.status=="Flux avance");
  CHECK(a.footer=="Flux avance");
  CHECK(a.key()==b.key());
  std::cout << "PASS changing future offset/header diagnostics do not change visible predictions\n";
  first.timestamp=now;
  auto fresh=paintPolicy::feed(first,true,true,now,"Temps reel");
  first.timestamp=now+30;
  CHECK(fresh.key()==paintPolicy::feed(first,true,true,now,"fetch 321ms").key());
  CHECK(fresh.key()!=paintPolicy::feed(first,true,true,now+2,"Temps reel").key());
  CHECK(paintPolicy::feed(first,true,true,now+2,"Temps reel").primary=="5");
  CHECK(paintPolicy::feed(first,true,true,now+211,"Temps reel").footer=="Donnees perimees");
  CHECK(paintPolicy::feed(first,true,false,now,"Temps reel").footer=="Wi-Fi deconnecte");
  CHECK(paintPolicy::feed(first,false,true,now,"API indisponible").footer=="API indisponible");
  CHECK(paintPolicy::feed(first,false,true,now,"Flux invalide").key()!=
        paintPolicy::feed(first,false,true,now,"API indisponible").key());
  first.timestamp=now+181;
  CHECK(paintPolicy::feed(first,true,true,now,"Temps reel").footer=="Horloge flux avancee");
  first.timestamp=0;
  CHECK(paintPolicy::feed(first,true,true,now,"Temps reel").footer=="Horodatage flux absent");
  first.timestamp=now;
  CHECK(paintPolicy::feed(first,true,true,0,"Temps reel").footer=="Synchronisation heure");
  first.departures.clear(); first.timestamp=now+120;
  CHECK(paintPolicy::feed(first,true,true,now,"Temps reel").footer=="Flux avance - Aucun passage");
  CHECK(a.key()!=fresh.key());
  std::cout << "PASS countdown/classification/error/Wi-Fi/TTL invalidation and warning with no departures\n";
  bootFlow::Controller boot;
  CHECK(!boot.complete && boot.stage==0 && !boot.failed);
  boot.prerequisites(false,false,true);
  CHECK(boot.stage==0 && boot.failed && boot.detail=="Wi-Fi deconnecte");
  boot.prerequisites(true,false,true);
  CHECK(boot.stage==1 && !boot.failed);
  auto waiting=boot.detail;
  for(unsigned i=0;i<100;++i) boot.prerequisites(true,false,true);
  CHECK(boot.stage==1 && boot.detail==waiting);
  boot.prerequisites(true,true,true);
  CHECK(!boot.complete && boot.stage==1);
  boot.requestStarted();
  CHECK(boot.stage==2 && !boot.complete);
  // No result-specific fake success: completion means *any* real fetch result,
  // which the live model renders honestly (HTTP/clock errors or no departures).
  boot.requestFinished();
  CHECK(boot.complete);
  boot.prerequisites(false,false,false);
  CHECK(boot.complete);
  bootFlow::Controller unconfigured;
  unconfigured.prerequisites(true,false,false);
  CHECK(unconfigured.stage==1 && !unconfigured.failed); // Never claim NTP done before a real clock.
  unconfigured.prerequisites(true,true,false);
  CHECK(unconfigured.stage==2 && unconfigured.failed);
  unconfigured.prerequisites(true,true,true);
  CHECK(unconfigured.stage==1 && !unconfigured.failed);
  std::cout << "PASS event-only boot waits for prerequisites and first real API result\n";
  paintPolicy::Frame frame;
  uint8_t pixels[4000]={};
  CHECK(frame.changed(pixels));
  CHECK(!frame.changed(pixels));
  pixels[15]=0x3f;
  CHECK(!frame.changed(pixels)); // Controller padding is not a visible change.
  pixels[15]=0x80;
  CHECK(frame.changed(pixels));
  CHECK(!frame.changed(pixels));
  pixels[0]=1;
  CHECK(frame.changed(pixels));
  std::cout << "PASS actual normalized frame equality independent of interactive mode\n";
  setenv("TZ","CET-1CEST,M3.5.0,M10.5.0/3",1); tzset();
  struct tm civil={}; civil.tm_year=126; civil.tm_mon=9; civil.tm_mday=8;
  civil.tm_hour=23; civil.tm_min=50; civil.tm_isdst=-1;
  const int64_t evening=mktime(&civil);
  transit::Result overnight; overnight.timestamp=evening;
  overnight.departures.push_back(transit::Departure{evening+600}); // Exact next local midnight.
  CHECK(paintPolicy::feed(overnight,true,true,evening,"").primary=="--");
  CHECK(paintPolicy::feed(overnight,true,true,evening,"").status=="Non annonce");
  overnight.departures.insert(overnight.departures.begin(),transit::Departure{evening});
  CHECK(paintPolicy::feed(overnight,true,true,evening,"").primary=="<1");
  CHECK(paintPolicy::feed(overnight,true,true,evening,"").next[0]=="--");
  std::cout << "PASS real-time view excludes next civil day, including midnight boundary\n";
  for(const auto& day : {std::make_pair(3,23),std::make_pair(10,25)}) {
    struct tm midnight={}; midnight.tm_year=126; midnight.tm_mon=day.first-1;
    midnight.tm_mday=day.first==3 ? 29 : 25; midnight.tm_isdst=-1;
    const int64_t start=mktime(&midnight);
    CHECK(paintPolicy::todayEnd(start)-start==day.second*3600);
  }
  CHECK(paintPolicy::todayEnd(0)==0);
  std::cout << "PASS civil day boundary follows Paris 23/25-hour DST days\n";
}
