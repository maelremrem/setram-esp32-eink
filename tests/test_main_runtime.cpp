// Real firmware TU and real GFX/decoder/driver. Only platform I/O is simulated.
#include "../src/main.cpp"
#include <iostream>
#include <ArduinoOTA.h>
#include <functional>
#include <vector>
#define CHECK(x) do { if(!(x)) { std::cerr<<"FAIL line "<<__LINE__<<": "<<#x<<'\n'; std::exit(1); } } while(0)
uint32_t hostMillis=2000;
time_t hostEpoch=0;
HostSerial Serial;
HostESP ESP;
HostWiFi WiFi;
uint32_t hostCpuMhz=80;
HostOtaPlatform hostOta;
HostUpdate Update;
bool hostPressed[48]={};
std::function<void()> onInputSample;
TaskHandle_t hostCurrentTask=nullptr;
TickType_t hostTaskWaitTicks=0;
std::function<void()> onAutoConnect,onRequest;
std::vector<uint8_t> hostPayload;
int hostHTTPStatus=200,hostRequests=0;
uint64_t gpioWrites=0;
bool captureDuringPaint=false;
std::function<void()> onPanelIO;
struct HostCommand { uint8_t reg; std::vector<uint8_t> data; };
std::vector<HostCommand> hostTrace;
int hostPins[48]={}; unsigned spiBits=0; uint8_t spiByte=0;
extern "C" time_t time(time_t* out) noexcept { if(out) *out=hostEpoch; return hostEpoch; }
void digitalWrite(int pin,int level) {
  ++gpioWrites;
  if(onPanelIO) { auto callback=std::move(onPanelIO); onPanelIO=nullptr; callback(); }
  if(pin==14 && level==LOW) { spiBits=0; spiByte=0; }
  if(pin==12 && level==HIGH && hostPins[12]==LOW && hostPins[14]==LOW) {
    spiByte=(spiByte<<1)|hostPins[11]; ++spiBits;
  }
  if(pin==14 && level==HIGH && hostPins[14]==LOW && spiBits==8) {
    if(hostPins[13]==LOW) hostTrace.push_back({spiByte,{}});
    else if(!hostTrace.empty()) hostTrace.back().data.push_back(spiByte);
  }
  hostPins[pin]=level;
  if(captureDuringPaint && boot.stage==2 && !boot.complete && hostRequests==0) {
    captureDuringPaint=false;
    inputQueue.push({navigation::Key::Menu,navigation::Event::Short});
  }
}
#ifdef EPAPER_JD79661
static constexpr bool jdController=true;
#else
static constexpr bool jdController=false;
#endif
using Bytes=std::vector<uint8_t>;
static Bytes cat(Bytes a,const Bytes& b){a.insert(a.end(),b.begin(),b.end());return a;}
static Bytes var(uint64_t v){ Bytes b; do {uint8_t c=v&127; v>>=7; b.push_back(c|(v?128:0));}while(v); return b; }
static Bytes scalar(unsigned field,uint64_t v){return cat(var(field*8),var(v));}
static Bytes msg(unsigned field,const Bytes& b){return cat(cat(var(field*8+2),var(b.size())),b);}
static Bytes text(unsigned field,const std::string& s){return msg(field,Bytes(s.begin(),s.end()));}
// Valid GTFS-RT wire fixtures decoded by production Transit, not decoder mocks.
static Bytes feedFor(const std::string& stopId,const std::string& routeId,int direction,uint64_t stamp,int64_t departure) {
  Bytes header=cat(text(1,"2.0"),scalar(3,stamp));
  Bytes result=msg(1,header);
  if(departure) {
    Bytes descriptor=cat(cat(text(1,"trip"),text(5,routeId)),scalar(6,direction));
    Bytes stop=cat(text(4,stopId),msg(3,scalar(2,departure)));
    Bytes update=cat(msg(1,descriptor),msg(2,stop));
    result=cat(result,msg(2,cat(text(1,"entry"),msg(3,update))));
  }
  return result;
}
static Bytes feed(uint64_t stamp,int64_t departure) {
  return feedFor(SETRAM_STOP_ID,SETRAM_ROUTE_ID,SETRAM_DIRECTION,stamp,departure);
}
static bool equal(const GFXcanvas1& a,const GFXcanvas1& b){return std::memcmp(a.getBuffer(),b.getBuffer(),4000)==0;}
static void expectBoot(unsigned stage,const std::string& detail,bool failed=false) {
  GFXcanvas1 expected(128,250); expected.setRotation(3);
  busUI::renderBoot(expected,{stage,detail,failed,-1});
  if(!equal(canvas,expected)) std::cerr<<"Boot mismatch expected stage="<<stage<<" detail="<<detail<<" actual stage="<<boot.stage<<" detail="<<boot.detail<<'\n';
  CHECK(equal(canvas,expected));
}
static bool isOrdinaryHome(const std::string& footerOverride="") {
  auto v=paintPolicy::feed(latest,validFeed,WiFi.status()==WL_CONNECTED,hostEpoch,state.c_str());
  if(!footerOverride.empty()) v.footer=footerOverride;
  char clock[8]="--:--";
  if(hostEpoch>1600000000){
    struct tm local; localtime_r(&hostEpoch,&local); strftime(clock,sizeof(clock),"%H:%M",&local);
    // The declared runtime fixture ends on 2026-12-31. Default 2027 live-only
    // scenarios retain the live departure but also expose calendar expiry.
    if(v.usable && local.tm_year>126 && footerOverride.empty())
      v.footer=v.tolerated ? "Flux avance - Plan expire" : "Horaires prevus expires";
  }
  busUI::View model{settings.stopLabel,settings.lineLabel,settings.destinationLabel,clock,v.primary,{v.next[0],v.next[1]},v.status,v.footer,-1};
  GFXcanvas1 expected(128,250);expected.setRotation(3);busUI::render(expected,model);
  return equal(canvas,expected);
}
static void expectHome() { CHECK(isOrdinaryHome()); }
static void expectTomorrow(const std::string& first="06:00",const std::string& second="06:30",const std::string& third="07:00",
                           unsigned offset=1,bool firstRealtime=false) {
  const std::string prefix="J+"+std::to_string(offset)+" ";
  const bool tolerated=displayPolicy::classify(latest.timestamp,hostEpoch)==displayPolicy::FeedState::FutureTolerated;
  busUI::View view{settings.stopLabel,settings.lineLabel,settings.destinationLabel,
      departureClock(hostEpoch),prefix+first+(firstRealtime ? " R" : " P"),
      {second=="--:--" ? "--" : prefix+second+" P",third=="--:--" ? "--" : prefix+third+" P"},
      firstRealtime ? "Flux avance" : "Prevu",tolerated ? "Flux avance / P=prevu R=reel" : "P=prevu R=reel",-1};
  GFXcanvas1 expected(128,250); expected.setRotation(3); busUI::render(expected,view);
  CHECK(equal(canvas,expected));
}
static void expectRealtimeWithPlans(const std::string& minutes) {
  busUI::View view{settings.stopLabel,settings.lineLabel,settings.destinationLabel,
      departureClock(hostEpoch),minutes,{"J+1 06:00 P","J+1 06:30 P"},"Flux avance","Flux avance / P=prevu R=reel",-1};
  GFXcanvas1 expected(128,250); expected.setRotation(3); busUI::render(expected,view);
  CHECK(equal(canvas,expected));
}
static void expectPlannedSchedule(unsigned page,bool futureRealtime=false) {
  busUI::ScheduleView view{departureClock(hostEpoch),settings.lineLabel,settings.destinationLabel,
      {"--","--","--","--","--"},"Flux avance / P=prevu R=reel",page+1,-1};
  for(unsigned row=0;row<5;++row) {
    unsigned i=page*5+row;
    if(futureRealtime && i==0) { view.times[row]="J+1 00:06 R"; continue; }
    if(futureRealtime) --i;
    char value[32]; std::snprintf(value,sizeof(value),"J+1 %02u:%02u P",6+i/2,30*(i%2));
    view.times[row]=value;
  }
  GFXcanvas1 expected(128,250); expected.setRotation(3); busUI::renderSchedule(expected,view);
  CHECK(equal(canvas,expected));
}
static void paint(int64_t now){draw(now);displayStep();}
static void tick(unsigned ms=10000){
  hostMillis+=ms; loop(); displayStep();
  // Controlled scheduling of the same worker steps as real FreeRTOS tasks.
  if(networkStep()) { loop(); displayStep(); }
}
int main(int argc,char** argv) {
  CHECK(argc==2); const std::string scenario=argv[1];
  onAutoConnect=[&](){
    expectBoot(0,"Connexion Wi-Fi"); // Frame genuinely precedes radio connection.
    if(scenario=="portal" || scenario=="portal-idle") wifiManager.active=true;
    else WiFi.connection=WL_CONNECTED;
  };
  if(scenario=="full-only-legacy-nvs") {
    Preferences::values()["bus-notifier|partial-max"]=50;
    Preferences::values()["bus-notifier|partial"]=1;
  }
  setup();
  if(scenario!="display-async") displayStep();
  if(scenario=="ota-chord") {
    boot.complete=true;startOta();for(unsigned i=0;i<6;++i){loop();displayStep();}
    CHECK(otaSession.active());
    const uint32_t start=hostMillis;
    onInputSample=[&](){
      hostMillis+=5;const uint32_t elapsed=hostMillis-start;
      // Arm after boot release, then hold MENU alone, then both buttons for 2s.
      hostPressed[2]=elapsed>=100 && elapsed<2700;
      hostPressed[1]=elapsed>=500 && elapsed<2700;
      pollButtons(); // Real sampled/debounced events, not injected chord.
      if(elapsed<2540)CHECK(otaRequested);
      if(elapsed>=2800)throw HostTaskBlocked{};
    };
    try{inputTask(nullptr);CHECK(false);}catch(const HostTaskBlocked&){}
    onInputSample=nullptr;
    CHECK(!otaRequested && !hostOta.armed && otaPassword.empty());
    CHECK(navigationState.screen==navigation::Screen::Home && !wifiManager.active);
    CHECK(hostCpuMhz==80 && WiFi.sleep==WIFI_PS_MAX_MODEM);
    std::cout<<"PASS real GPIO sampler requires overlapping MENU+EXIT hold, no release/portal leak\n";return 0;
  }
  if(scenario=="ota-drain") {
    boot.complete=true; hostEpoch=1800000005; hostPayload=feed(hostEpoch+120,hostEpoch+360);
    CHECK(requestFeed(hostEpoch));
    onRequest=[&](){
      inputQueue.push({navigation::Key::Menu,navigation::Event::Short});loop();displayStep();
      inputQueue.push({navigation::Key::Previous,navigation::Event::Short});loop();displayStep();
      inputQueue.push({navigation::Key::Ok,navigation::Event::Short});loop();
      CHECK(feedBusy && otaRequested && hostOta.begins==0 && panelPending);
      onPanelIO=[&](){loop();CHECK(panelBusy && hostOta.begins==0 && hostOta.handles==0);};
      displayStep();
      CHECK(hostOta.begins==0); // HTTPS still executing after SPI completed.
    };
    CHECK(networkStep());onRequest=nullptr;CHECK(feedReply && feedBusy);
    loop();CHECK(!feedBusy && !validFeed && otaSession.active());
    displayStep();loop();displayStep();loop();
    CHECK(hostRequests==1 && hostOta.handles>0 && !feedRequest);
    inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});loop();displayStep();
    hostMillis+=1000;loop();CHECK(feedBusy);CHECK(networkStep());loop();displayStep();
    CHECK(validFeed && hostRequests==2); // A fresh generation, not the pre-OTA reply.
    std::cout<<"PASS OTA drains actual in-flight HTTPS/SPI, rejects stale reply, fresh feed after exit\n";return 0;
  }
  if(scenario=="ota-transfer") {
    boot.complete=true;lastPoll=lastPaint=hostMillis;startOta();
    for(unsigned i=0;i<6;++i){loop();displayStep();}
    CHECK(otaSession.active() && hostOta.handles>0);
    const String firstPassword=otaPassword;
    uint8_t readyPixels[4000];memcpy(readyPixels,canvas.getBuffer(),4000);
    const uint64_t writes=gpioWrites;
    hostOta.duringHandle=[&](){
      CHECK(panelIdle() && !feedBusy);hostOta.current->start();hostOta.current->progress(1024,4096);
      CHECK(otaSession.transferring() && otaSession.progress()==25);
      draw(time(nullptr));CHECK(!panelPending && gpioWrites==writes && memcmp(readyPixels,canvas.getBuffer(),4000)==0);
      inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});pollButtons();
      CHECK(otaRequested && hostOta.armed); // Cannot tear down flash transfer.
      hostOta.current->error(OTA_RECEIVE_ERROR);
    };
    loop();CHECK(otaSession.phase()==otaMode::Phase::Error && gpioWrites==writes);
    loop();CHECK(panelPending);
    GFXcanvas1 expected(128,250);expected.setRotation(3);
    busUI::renderOta(expected,{"--:--",otaHostname.c_str(),"192.0.2.1",otaPassword.c_str(),otaSession.detail(),4});
    CHECK(equal(canvas,expected));displayStep();
    hostOta.duringHandle=[&](){hostOta.current->start();hostOta.current->progress(4096,4096);hostOta.current->finish();};
    loop();CHECK(otaSession.phase()==otaMode::Phase::Success && otaSession.progress()==100);
    loop();displayStep();
    inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});loop();displayStep();
    CHECK(!otaRequested && otaPassword.empty() && !hostOta.armed);
    startOta();for(unsigned i=0;i<6;++i){loop();displayStep();}
    CHECK(otaPassword!=firstPassword && hostOta.current->password==otaPassword.c_str());
    inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});loop();displayStep();
    CHECK(WiFi.sleep==WIFI_PS_MAX_MODEM && hostCpuMhz==80);
    std::cout<<"PASS synchronous transfer has no concurrent paint/cancel, error/retry/success, fresh next-session key\n";return 0;
  }
  if(scenario=="wifi-signal") {
    boot.complete=true;paint(0);CHECK(!panelPending);
    uint8_t before[4000];memcpy(before,canvas.getBuffer(),4000);const uint64_t writes=gpioWrites;
    WiFi.rssi=-60;draw(0);CHECK(!panelPending && gpioWrites==writes); // 3dB hysteresis from -65.
    WiFi.rssi=-57;draw(0);CHECK(panelPending);
    GFXcanvas1 old(128,250);old.setRotation(3);memcpy(old.getBuffer(),before,4000);
    for(int y=0;y<128;++y)for(int x=0;x<250;++x)
      if(!(x>=190 && x<=207 && y<20))CHECK(old.getPixel(x,y)==canvas.getPixel(x,y));
    displayStep();CHECK(gpioWrites>writes);
    WiFi.connection=0;draw(0);CHECK(panelPending);displayStep();
    std::cout<<"PASS main uses actual RSSI and disconnection, stable noise and signal-only header lane\n";return 0;
  }
  if(scenario=="ota-offline") {
    boot.complete=true;WiFi.connection=0;startOta();
    for(unsigned i=0;i<6;++i){loop();displayStep();}
    CHECK(otaRequested && otaFailed && !otaSession.active() && hostOta.begins==0);
    CHECK(WiFi.sleep==WIFI_PS_MAX_MODEM && hostCpuMhz==80 && hostRequests==0);
    inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});loop();displayStep();
    CHECK(!otaRequested && otaPassword.empty());
    std::cout<<"PASS offline activation shows failure without arming OTA or changing power\n";return 0;
  }
  if(scenario=="ota-opt-in") {
    CHECK(!otaSession.active() && hostOta.begins==0 && WiFi.sleep==WIFI_PS_MAX_MODEM && hostCpuMhz==80);
    boot.complete=true; lastPoll=lastPaint=hostMillis;
    inputQueue.push({navigation::Key::Menu,navigation::Event::Short}); loop(); displayStep();
    inputQueue.push({navigation::Key::Previous,navigation::Event::Short}); loop(); displayStep();
    CHECK(navigationState.selected==5 && hostOta.begins==0);
    inputQueue.push({navigation::Key::Ok,navigation::Event::Short}); loop();
    CHECK(otaRequested && navigationState.screen==navigation::Screen::Ota && hostOta.begins==0 && panelPending);
    displayStep(); loop(); displayStep(); loop();
    CHECK(otaSession.active() && hostOta.begins==1 && hostOta.armed);
    CHECK(WiFi.sleep==WIFI_PS_NONE && hostCpuMhz==240 && hostRequests==0);
    CHECK(hostOta.current->password.size()>=12 && hostOta.current->port==3232 && hostOta.current->timeout==10000);
    CHECK(hostOta.handles==0 && panelPending); // Ready frame must finish before receiver runs.
    displayStep(); loop();
    const uint64_t writes=gpioWrites; const unsigned handles=hostOta.handles;
    for(unsigned i=0;i<100;++i) {const uint32_t before=hostMillis;loop();displayStep();CHECK(hostMillis-before==1);}
    CHECK(hostOta.handles>=handles+100 && gpioWrites==writes && hostRequests==0);
    inputQueue.push({navigation::Key::Menu,navigation::Event::Long}); loop();
    inputQueue.push({navigation::Key::Menu,navigation::Event::Short}); loop();
    inputQueue.push({navigation::Key::Back,navigation::Event::Short}); loop();
    CHECK(otaRequested && !wifiManager.active && navigationState.screen==navigation::Screen::Ota);
    inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong}); loop(); displayStep();
    CHECK(!otaRequested && !otaSession.active() && !hostOta.armed && hostOta.ends==1);
    CHECK(WiFi.sleep==WIFI_PS_MAX_MODEM && hostCpuMhz==80 && otaPassword.empty());
    CHECK(navigationState.screen==navigation::Screen::Home);
    std::cout<<"PASS physical opt-in, idle OTA loop, no feed/SPI churn, protected keys/chord and exact power restore\n";return 0;
  }
  if(scenario=="idle-loop") {
    boot.complete=true; lastPoll=lastPaint=hostMillis; lastInputMs=0;
    const uint32_t before=hostMillis;
    loop();
    CHECK(hostMillis-before==20 && hostRequests==0);
    inputQueue.push({navigation::Key::Menu,navigation::Event::Short});
    const uint32_t press=hostMillis; loop();
    CHECK(navigationState.screen==navigation::Screen::Config && hostMillis-press==5);
    CHECK(hostRequests==0);
    // The quiet-input interval restores low-frequency idle checks, not a sleep
    // of the radio or GPIO sampler. Millis subtraction remains wrap-safe.
    hostMillis=lastInputMs+500; lastPaint=lastPoll=hostMillis;
    const uint32_t quiet=hostMillis; loop(); CHECK(hostMillis-quiet==20);
    hostMillis=UINT32_MAX-10; lastInputMs=hostMillis-600;
    lastPaint=lastPoll=hostMillis;
    loop(); CHECK(hostMillis==9);
    std::cout<<"PASS idle main checks slow to 20ms; interactive checks stay 5ms, wrap-safe\n";
    return 0;
  }
  if(scenario=="portal-idle") {
    CHECK(wifiManager.active);
    // This marker is outside content: a repeated QR render would erase it.
    canvas.drawPixel(249,121,1);
    for(unsigned i=0;i<100;++i) loop();
    CHECK(canvas.getPixel(249,121)==1 && hostRequests==0);
    inputQueue.push({navigation::Key::Ok,navigation::Event::Short}); loop();
    GFXcanvas1 expected(128,250); expected.setRotation(3);
    portalUI::render(expected,{portalUI::Step::Portal,apName.c_str(),apPassword.c_str(),""});
    CHECK(equal(canvas,expected));
    state="Erreur sauvegarde NVS"; loop();
    portalUI::render(expected,{portalUI::Step::Portal,apName.c_str(),apPassword.c_str(),state.c_str()});
    CHECK(equal(canvas,expected));
    // Cache must include QR credentials, and must be invalidated by another UI.
    apName="Changed-test-SSID"; loop();
    portalUI::render(expected,{portalUI::Step::Portal,apName.c_str(),apPassword.c_str(),state.c_str()});
    CHECK(equal(canvas,expected));
    wifiManager.active=false; boot.complete=true; draw(0);
    wifiManager.active=true; loop();
    CHECK(equal(canvas,expected));
    inputQueue.push({navigation::Key::Back,navigation::Event::Short}); loop();
    CHECK(!wifiManager.active && hostRequests==0);
    std::cout<<"PASS static portal does not regenerate QR while idle; actions/errors/reopening repaint\n";
    return 0;
  }
  if(scenario=="worker-idle") {
    auto panel=hostTaskNamed("epaper"), network=hostTaskNamed("setram");
    CHECK(panel && network);
    panel->notifications=network->notifications=0;
    auto runUntilBlocked=[&](void(*task)(void*),TaskHandle_t handle) {
      hostCurrentTask=handle; hostTaskWaitTicks=0;
      bool blocked=false;
      try { task(nullptr); } catch(const HostTaskBlocked&) { blocked=true; }
      catch(const HostTaskPolling&) {}
      hostCurrentTask=nullptr;
      CHECK(blocked && hostTaskWaitTicks==portMAX_DELAY);
    };
    const uint64_t writes=gpioWrites;
    runUntilBlocked(displayTask,panel); runUntilBlocked(networkTask,network);
    CHECK(gpioWrites==writes && hostRequests==0);
    boot.complete=true; navigationState.screen=navigation::Screen::Config;
    for(unsigned i=0;i<5;++i) { navigationState.selected=i; draw(0); }
    CHECK(panelPending && panel->notifications>0);
    PanelFrame final=pendingPanel;
    runUntilBlocked(displayTask,panel);
    CHECK(!panelPending && panel->notifications==0 && gpioWrites>writes);
    CHECK(memcmp(final.pixels,canvas.getBuffer(),4000)==0);
    const uint64_t after=gpioWrites;
    draw(0); CHECK(!panelPending && panel->notifications==0);
    runUntilBlocked(displayTask,panel); CHECK(gpioWrites==after);
    hostEpoch=1800000005; hostPayload=feed(hostEpoch+120,hostEpoch+360);
    CHECK(requestFeed(hostEpoch) && network->notifications==1);
    CHECK(!requestFeed(hostEpoch) && network->notifications==1);
    runUntilBlocked(networkTask,network);
    CHECK(hostRequests==1 && feedReply && feedBusy && network->notifications==0);
    consumeFeed(); CHECK(validFeed && !feedBusy);
    runUntilBlocked(networkTask,network); CHECK(hostRequests==1);
    std::cout<<"PASS event-driven panel/network workers block idle, coalesce frames, never lose queued work\n";
    return 0;
  }
  if(scenario=="full-only-legacy-nvs") {
    static_assert(sizeof(busUI::ConfigView{}.values)/sizeof(std::string)==7,"Five normal settings, opt-in OTA, and dual order");
    CHECK(Preferences::values()["bus-notifier|partial-max"]==50);
    CHECK(Preferences::values()["bus-notifier|partial"]==1);
    // Obsolete keys are ignored without touching unrelated persisted settings.
    std::cout<<"PASS legacy partial-refresh NVS has no effect, six settings, no partial-refresh choice\n";return 0;
  }
  if(scenario=="network-async") {
    hostEpoch=1800000005; hostPayload=feed(hostEpoch+120,hostEpoch+360);
    loop();
    CHECK(hostRequests==0 && feedBusy && feedRequest); // Main only queues HTTPS.
    onRequest=[&](){
      inputQueue.push({navigation::Key::Menu,navigation::Event::Short}); loop();
      inputQueue.push({navigation::Key::Next,navigation::Event::Short}); loop();
      CHECK(navigationState.screen==navigation::Screen::Config && navigationState.selected==1);
      CHECK(feedBusy && hostRequests==1); // No competing requests while socket blocks.
    };
    CHECK(networkStep());
    CHECK(!validFeed && feedBusy); // Worker publishes a reply, never writes live state.
    loop();
    CHECK(validFeed && boot.complete && !feedBusy);
    CHECK(navigationState.screen==navigation::Screen::Config && navigationState.selected==1);
    navigationState.screen=navigation::Screen::Home; lastInputMs=0; lastPoll=hostMillis-pollMs;
    loop(); CHECK(feedBusy);
    onRequest=[&](){
      inputQueue.push({navigation::Key::Menu,navigation::Event::Long}); loop();
      CHECK(wifiManager.active);
      inputQueue.push({navigation::Key::Back,navigation::Event::Short}); loop();
      CHECK(!wifiManager.active); // Portal opens/closes before HTTP returns.
    };
    CHECK(networkStep()); loop();
    CHECK(!validFeed && !feedBusy && state=="Portail ferme"); // Old generation discarded.
    std::cout<<"PASS async HTTPS, responsive queued navigation, immutable job, stale portal result discarded\n"; return 0;
  }
  if(scenario=="display-async") {
    boot.complete=true;
    const uint64_t writes=gpioWrites;
    navigationState.screen=navigation::Screen::Config; draw(0);
    CHECK(gpioWrites==writes); // Rendering must enqueue, never block on EPD SPI/BUSY.
    PanelFrame inFlight=pendingPanel;
    onPanelIO=[&](){
      for(unsigned i=0;i<12;++i) {
        inputQueue.push({navigation::Key::Next,navigation::Event::Short}); loop();
        CHECK(panelPending); // Main continues processing input during physical SPI/BUSY.
      }
      inputQueue.push({navigation::Key::Next,navigation::Event::Short}); loop();
      CHECK(navigationState.selected==1 && panelPending);
    };
    CHECK(displayStep());
    CHECK(navigationState.selected==1 && panelPending);
    // Decode actual GPIO SPI: in-flight image must not tear when canvas changes.
    const unsigned reg=jdController ? 0x13 : 0x24;
    const std::vector<uint8_t>* plane=nullptr;
    for(const auto& command:hostTrace) if(command.reg==reg) plane=&command.data;
    CHECK(plane && plane->size()==4000);
    for(unsigned row=0;row<250;++row) for(unsigned col=0;col<16;++col) {
      const unsigned source=jdController ? 249-row : row;
      uint8_t expected=~inFlight.pixels[source*16+col];
      if(col==15 && jdController) expected|=0x3F;
      CHECK((*plane)[row*16+col]==expected);
    }
    PanelFrame final=pendingPanel;
    CHECK(final.screen==unsigned(navigation::Screen::Config));
    CHECK(memcmp(final.pixels,canvas.getBuffer(),4000)==0);
    CHECK(displayStep() && !panelPending); // Only latest final image, no twelve-frame backlog.
    const uint64_t after=gpioWrites; draw(0);
    CHECK(!panelPending && gpioWrites==after);
    std::cout<<"PASS asynchronous panel, responsive main during SPI, immutable snapshot, latest-only queue\n"; return 0;
  }
  if(scenario=="dual") {
    boot.complete=true; struct tm dualCivil={}; dualCivil.tm_year=126; dualCivil.tm_mon=9; dualCivil.tm_mday=8; dualCivil.tm_hour=12; dualCivil.tm_isdst=-1; hostEpoch=mktime(&dualCivil);
    settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4"; settings.secondDestinationLabel="Other destination";
    settings.swapped=false; navigationState.setDualConfigured(true); feedGeneration++;
    slotDue[0]=0; slotDue[1]=0;
    hostPayload=feedFor(settings.stop,settings.route,atoi(settings.direction.c_str()),hostEpoch+120,hostEpoch+360);
    CHECK(requestFeed(hostEpoch)); CHECK(feedRequest && feedRequest->slot==0); CHECK(networkStep()); consumeFeed(); CHECK(tracking[0].valid && tracking[0].selection.stop==settings.stop);
    slotDue[0]=hostMillis+pollMs; slotDue[1]=0;
    hostPayload=feedFor(settings.secondStop,settings.secondRoute,atoi(settings.secondDirection.c_str()),hostEpoch+120,hostEpoch+540);
    CHECK(requestFeed(hostEpoch)); CHECK(networkStep()); consumeFeed(); CHECK(tracking[1].valid && tracking[1].selection.stop==settings.secondStop);
    const auto oldSecond=tracking[1].result;
    slotDue[0]=hostMillis+pollMs; slotDue[1]=0; hostPayload=feedFor(settings.secondStop,settings.secondRoute,0,hostEpoch+120,hostEpoch+600);
    CHECK(requestFeed(hostEpoch)); CHECK(networkStep()); ++feedGeneration; consumeFeed();
    CHECK(!feedBusy && tracking[1].result.departures.size()==oldSecond.departures.size());
    for(size_t i=0;i<oldSecond.departures.size();++i) CHECK(tracking[1].result.departures[i].time==oldSecond.departures[i].time);
    navigationState.screen=navigation::Screen::Home; navigationState.homeView=0; draw(hostEpoch);
    CHECK(tracking[0].selection.stop==settings.stop && tracking[1].selection.stop==settings.secondStop);
    settings.swapped=true; navigationState.homeView=0; draw(hostEpoch);
    CHECK(tomorrowPlans(hostEpoch).departures.size()>0); navigationState.homeView=1;
    CHECK(tomorrowPlans(hostEpoch).departures.size()>0 && departureClock(tomorrowPlans(hostEpoch).departures[0])=="09:45");
    navigationState.homeView=2;
    CHECK(tomorrowPlans(hostEpoch).departures.size()>0 && departureClock(tomorrowPlans(hostEpoch).departures[0])=="06:00");
    std::cout<<"PASS dual main jobs, stale reply isolation, swap order and per-slot timetable cache\n"; return 0;
  }
  if(scenario=="config-transitions") {
    boot.complete=true;
    Preferences::writable()=true;
    WiFi.connection=WL_CONNECTED;
    settings.secondStop.clear(); settings.secondRoute.clear(); settings.secondDirection.clear();
    settings.secondStopLabel.clear(); settings.secondLineLabel.clear(); settings.secondDestinationLabel.clear();
    navigationState.setDualConfigured(false);
    tracking[0].valid=true; tracking[0].selection.stop="old-primary";
    tracking[1].valid=true; tracking[1].selection.stop="old-secondary";
    tracking[1].result=transit::Result{}; tracking[1].result.timestamp=hostEpoch;
    tracking[1].result.departures={{hostEpoch+60}};
    slotDue[0]=hostMillis+pollMs; slotDue[1]=hostMillis+pollMs;
    portalSettings::Settings dual=settings;
    dual.secondStop="other"; dual.secondRoute="C4"; dual.secondDirection="0";
    dual.secondStopLabel="Other"; dual.secondLineLabel="C4"; dual.secondDestinationLabel="Other destination";
    CHECK(persistSettings(dual,60000));
    CHECK(navigationState.dualConfigured);
    CHECK(!tracking[0].valid && !tracking[1].valid);
    CHECK(slotDue[0]==0 && slotDue[1]==0);
    navigationState.screen=navigation::Screen::Home; navigationState.homeView=0;
    draw(hostEpoch); displayStep();
    GFXcanvas1 expected(128,250); expected.setRotation(3);
    busUI::renderDual(expected,{settings.stopLabel,"--:--","Indisponible",state+" | 2: Indisponible",
      {{settings.lineLabel,settings.destinationLabel,"--","--",false,""},
       {settings.secondLineLabel,settings.secondDestinationLabel,"--","--",false,""}},-1});
    for(int y=0;y<110;++y) for(int x=0;x<250;++x) {
      CHECK(canvas.getPixel(x,y)==expected.getPixel(x,y));
    }
    portalSettings::Settings mono=settings;
    mono.secondStop.clear(); mono.secondRoute.clear(); mono.secondDirection.clear();
    mono.secondStopLabel.clear(); mono.secondLineLabel.clear(); mono.secondDestinationLabel.clear();
    CHECK(persistSettings(mono,30000));
    CHECK(!navigationState.dualConfigured && navigationState.homeView==0);
    CHECK(!tracking[0].valid && !tracking[1].valid && slotDue[0]==0 && slotDue[1]==0);
    std::cout<<"PASS config transitions invalidate both tuples, deadlines and dual navigation\n"; return 0;
  }
  if(scenario=="dual-composition") {
    boot.complete=true;
    struct tm dualCivil={}; dualCivil.tm_year=126; dualCivil.tm_mon=9; dualCivil.tm_mday=8;
    dualCivil.tm_hour=12; dualCivil.tm_isdst=-1; hostEpoch=mktime(&dualCivil);
    settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4"; settings.secondDestinationLabel="Other destination";
    settings.swapped=true; navigationState.setDualConfigured(true); feedGeneration++;
    slotDue[0]=slotDue[1]=0;
    hostPayload=feedFor(settings.stop,settings.route,atoi(settings.direction.c_str()),hostEpoch+120,hostEpoch+120);
    CHECK(requestFeed(hostEpoch)); CHECK(networkStep()); consumeFeed();
    slotDue[0]=hostMillis+pollMs; slotDue[1]=0;
    hostPayload=feedFor(settings.secondStop,settings.secondRoute,0,hostEpoch+120,hostEpoch+600);
    CHECK(requestFeed(hostEpoch)); CHECK(networkStep()); consumeFeed();
    navigationState.screen=navigation::Screen::Home; navigationState.homeView=1;
    draw(hostEpoch); displayStep();
    busUI::View selected{settings.secondStopLabel,settings.secondLineLabel,settings.secondDestinationLabel,
      departureClock(hostEpoch),"10",{"J+1 09:45 P","--"},"Flux avance","Flux avance / P=prevu R=reel",-1};
    GFXcanvas1 expected(128,250); expected.setRotation(3); busUI::render(expected,selected);
    CHECK(equal(canvas,expected));
    navigationState.screen=navigation::Screen::Schedule; navigationState.page=0;
    draw(hostEpoch); displayStep();
    busUI::ScheduleView schedule{departureClock(hostEpoch),settings.secondLineLabel,settings.secondDestinationLabel,
      {"10 min","J+1 09:45 P","--","--","--"},"Flux avance / P=prevu R=reel",1,-1};
    busUI::renderSchedule(expected,schedule);
    CHECK(equal(canvas,expected));
    std::cout<<"PASS shared dual selected tuple composes live, timetable, labels and schedule\n"; return 0;
  }
  if(scenario=="transitions-deadlines") {
    boot.complete=true; hostEpoch=1800000005; WiFi.connection=WL_CONNECTED;
    settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4"; settings.secondDestinationLabel="Other destination";
    navigationState.setDualConfigured(true); tracking[0].valid=tracking[1].valid=true;
    hostPayload=feedFor(settings.stop,settings.route,atoi(settings.direction.c_str()),hostEpoch+120,hostEpoch+120);
    hostMillis=10; lastInputMs=hostMillis-1000; slotDue[0]=UINT32_MAX-20; slotDue[1]=hostMillis+100000;
    CHECK(deadlineReached(hostMillis,slotDue[0]));
    loop(); CHECK(feedBusy && feedRequest && feedRequest->slot==0);
    CHECK(networkStep()); consumeFeed();
    slotDue[0]=10000; slotDue[1]=20000; navigationState.screen=navigation::Screen::Schedule;
    inputQueue.push({navigation::Key::Ok,navigation::Event::Short}); loop();
    CHECK(slotDue[0]==0 && slotDue[1]==0);
    Preferences::writable()=true; slotDue[0]=10000; slotDue[1]=20000; changeInterval();
    CHECK(slotDue[0]==0 && slotDue[1]==0);
    openPortal(); tracking[0].valid=tracking[1].valid=true; slotDue[0]=10000; slotDue[1]=20000;
    inputQueue.push({navigation::Key::Back,navigation::Event::Short}); loop();
    CHECK(!wifiManager.getConfigPortalActive() && !tracking[0].valid && !tracking[1].valid);
    tracking[0].valid=tracking[1].valid=true; slotDue[0]=10000; slotDue[1]=20000;
    WiFi.connection=0; loop(); CHECK(!tracking[0].valid && !tracking[1].valid);
    tracking[0].valid=tracking[1].valid=true; slotDue[0]=10000; slotDue[1]=20000;
    WiFi.connection=WL_CONNECTED; loop(); CHECK(!tracking[0].valid && !tracking[1].valid);
    tracking[0].valid=tracking[1].valid=true; slotDue[0]=10000; slotDue[1]=20000;
    startOta(); CHECK(!tracking[0].valid && !tracking[1].valid && slotDue[0]==0 && slotDue[1]==0);
    stopOta(); CHECK(!tracking[0].valid && !tracking[1].valid && slotDue[0]==0 && slotDue[1]==0);
    std::cout<<"PASS wrap-safe dual deadlines and config/portal/link/OTA invalidation\n"; return 0;
  }
  if(scenario=="dual-repeat-back") {
    boot.complete=true;
    settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4";
    navigationState.setDualConfigured(true);
    for(bool swapped : {false,true}) {
      settings.swapped=swapped;
      navigationState.screen=navigation::Screen::Home; navigationState.homeView=0;
      auto press=[](navigation::Key key,navigation::Event event=navigation::Event::Short) {
        inputQueue.push({key,event}); pollButtons();
      };
      press(navigation::Key::Back); CHECK(navigationState.homeView==0);
      for(auto direction : {navigation::Key::Previous,navigation::Key::Next}) {
        const unsigned view=direction==navigation::Key::Previous ? 1 : 2;
        press(direction);
        CHECK(navigationState.screen==navigation::Screen::Home && navigationState.homeView==view);
        const unsigned slot=(view==1 ? (swapped ? 1u : 0u) : (swapped ? 0u : 1u));
        CHECK(selectedTrackingSlot()==slot);
        press(direction,navigation::Event::Repeat);
        CHECK(navigationState.screen==navigation::Screen::Home && navigationState.homeView==view);
        press(navigation::Key::Next);
        CHECK(navigationState.screen==navigation::Screen::Schedule && navigationState.page==0 && selectedTrackingSlot()==slot);
        press(navigation::Key::Next,navigation::Event::Repeat);
        CHECK(navigationState.page==1 && selectedTrackingSlot()==slot);
        press(navigation::Key::Back);
        CHECK(navigationState.screen==navigation::Screen::Home && navigationState.homeView==0 && navigationState.page==0);
        press(navigation::Key::Back,navigation::Event::Repeat);
        CHECK(navigationState.homeView==0);
        press(direction); CHECK(navigationState.homeView==view);
        press(navigation::Key::Back); CHECK(navigationState.homeView==0);
      }
    }
    std::cout<<"PASS queued Up/Down choose displayed tuple, held directions stay Home, EXIT returns overview without cycling, swapped order respected\n"; return 0;
  }
  if(scenario=="dual-warnings") {
    boot.complete=true; struct tm warningCivil={}; warningCivil.tm_year=126; warningCivil.tm_mon=9;
    warningCivil.tm_mday=8; warningCivil.tm_hour=12; warningCivil.tm_isdst=-1; hostEpoch=mktime(&warningCivil);
    settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4"; settings.secondDestinationLabel="Other destination";
    navigationState.setDualConfigured(true); navigationState.screen=navigation::Screen::Home; navigationState.homeView=0;
    tracking[0].valid=false; tracking[0].status="API indisponible";
    tracking[1].valid=false; tracking[1].status="Horaires prevus expires";
    draw(hostEpoch); displayStep();
    GFXcanvas1 expected(128,250); expected.setRotation(3);
    busUI::renderDual(expected,{settings.stopLabel,departureClock(hostEpoch),"Indisponible",
      "1: API indisponible | 2: Horaires prevus expires",
      {{settings.lineLabel,settings.destinationLabel,"--","--",false,"API indisponible"},
       {settings.secondLineLabel,settings.secondDestinationLabel,"--","--",false,"Horaires prevus expires"}},-1});
    CHECK(equal(canvas,expected));
    std::cout<<"PASS dual renders both tuple warnings in real framebuffer\n"; return 0;
  }
  if(scenario=="swap-nvs-failure") {
    boot.complete=true; settings.secondStop="other"; settings.secondRoute="C4"; settings.secondDirection="0";
    settings.secondStopLabel="Other"; settings.secondLineLabel="C4"; settings.secondDestinationLabel="Other destination";
    settings.swapped=false; navigationState.setDualConfigured(true); navigationState.screen=navigation::Screen::Config;
    navigationState.selected=6; Preferences::writable()=true; Preferences::stringMismatch()=true;
    inputQueue.push({navigation::Key::Ok,navigation::Event::Short}); loop();
    CHECK(!settings.swapped && configNotice=="Erreur sauvegarde NVS");
    Preferences::stringMismatch()=false;
    std::cout<<"PASS swap keeps live order unchanged when NVS readback fails\n"; return 0;
  }
  if(scenario=="portal") {
    CHECK(wifiManager.active && !boot.complete);
    GFXcanvas1 expected(128,250);expected.setRotation(3);
    portalUI::render(expected,{portalNavigation.step,apName.c_str(),apPassword.c_str(),""});
    CHECK(equal(canvas,expected));
    uint64_t writes=gpioWrites;
    for(unsigned i=0;i<5;++i) tick();
    CHECK(hostRequests==0 && gpioWrites==writes);
    inputQueue.push({navigation::Key::Back,navigation::Event::Short});
    WiFi.connection=WL_CONNECTED; tick();
    CHECK(!wifiManager.active && !boot.complete);
  }
  expectBoot(1,"Wi-Fi connecte - attente NTP");
  uint64_t waitingWrites=gpioWrites;
  for(unsigned i=0;i<3;++i) tick();
  CHECK(hostRequests==0 && !boot.complete);
  CHECK(gpioWrites==waitingWrites); // Repeated waits emit no SPI on either variant.
  hostEpoch=1600000000; tick();
  CHECK(hostRequests==0 && !boot.complete); // Same clock threshold as feed classification.
  hostEpoch=1800000005;
  const bool plannedScenario=scenario.find("tomorrow")==0 || scenario=="empty";
  const bool mixedScenario=scenario.find("mixed")==0;
  if(plannedScenario || mixedScenario) {
    struct tm civil={}; civil.tm_year=126; civil.tm_mon=9; civil.tm_mday=8;
    if(scenario=="tomorrow-none" || scenario=="mixed-after") civil.tm_mday=9;
    if(scenario=="tomorrow-expired" || scenario=="mixed-expired") {civil.tm_mon=11; civil.tm_mday=31;}
    civil.tm_hour=23; civil.tm_min=50; civil.tm_sec=5; civil.tm_isdst=-1;
    hostEpoch=mktime(&civil);
  }
  const time_t base=hostEpoch;
  hostHTTPStatus=scenario=="failure" ? 500 : 200;
  hostPayload=feed(base+(scenario=="rejected" ? 181 : 120),
      plannedScenario ? (scenario=="tomorrow-future" ? paintPolicy::todayEnd(base)+360 : 0) : base+360);
  if(scenario=="malformed") hostPayload=Bytes{0};
  onRequest=[&](){
    if(!boot.complete) expectBoot(2,"Requete SETRAM"); // First real API call cannot precede its frame.
    if(scenario=="navigate") inputQueue.push({navigation::Key::Menu,navigation::Event::Short});
  };
  captureDuringPaint=scenario=="paint-input";
  tick();
  if(scenario=="paint-input") {
    CHECK(!captureDuringPaint); tick(5);
    CHECK(navigationState.screen==navigation::Screen::Config && hostRequests==1 && boot.complete);
    std::cout<<"PASS input during boot panel refresh reaches menu without blocking on HTTPS\n";return 0;
  }
  CHECK(hostRequests==1 && boot.complete);
  if(scenario=="navigate") {
    CHECK(navigationState.screen==navigation::Screen::Config);
    uint64_t writes=gpioWrites; tick(); CHECK(gpioWrites==writes && hostRequests==1);
    std::cout<<"PASS captured HTTPS navigation survives boot completion\n";return 0;
  }
  if(scenario=="mixed-expired") {
    busUI::View expected{settings.stopLabel,settings.lineLabel,settings.destinationLabel,
      departureClock(hostEpoch),"6",{"--","--"},"Flux avance","Flux avance - Plan expire",-1};
    GFXcanvas1 model(128,250); model.setRotation(3); busUI::render(model,expected);
    CHECK(equal(canvas,model));
    navigationState.screen=navigation::Screen::Schedule; navigationState.page=2; paint(hostEpoch);
    busUI::ScheduleView list{departureClock(hostEpoch),settings.lineLabel,settings.destinationLabel,
      {"--","--","--","--","--"},"Flux avance - Plan expire",3,-1};
    busUI::renderSchedule(model,list); CHECK(equal(canvas,model));
    std::cout<<"PASS live departure retained with expired planned-calendar warning on Home/empty page\n";
    return 0;
  }
  if(mixedScenario) {
    const unsigned offset=scenario=="mixed-after" ? 2 : 1;
    const std::string prefix="J+"+std::to_string(offset)+" ";
    busUI::View expected{settings.stopLabel,settings.lineLabel,settings.destinationLabel,
      departureClock(hostEpoch),"6",{prefix+"06:00 P",prefix+"06:30 P"},"Flux avance","Flux avance / P=prevu R=reel",-1};
    GFXcanvas1 model(128,250); model.setRotation(3); busUI::render(model,expected);
    CHECK(equal(canvas,model));
    if(scenario=="mixed-after") {
      latest.departures.clear(); paint(hostEpoch);
      expected.primary=prefix+"06:00 P"; expected.next[0]=prefix+"06:30 P"; expected.next[1]=prefix+"07:00 P";
      expected.status="Prevu"; busUI::render(model,expected); CHECK(equal(canvas,model));
    } else {
      const auto plan=timetable::day(settings.stop,settings.route,0,hostEpoch,1);
      latest.departures.push_back({plan.departures[0]}); paint(hostEpoch);
      expected.next[0]="J+1 06:00 R"; busUI::render(model,expected); CHECK(equal(canvas,model));
      navigationState.screen=navigation::Screen::Schedule; navigationState.page=0; paint(hostEpoch);
      busUI::ScheduleView list{departureClock(hostEpoch),settings.lineLabel,settings.destinationLabel,
        {"6 min","J+1 06:00 R","J+1 06:30 P","J+1 07:00 P","J+1 07:30 P"},"Flux avance / P=prevu R=reel",1,-1};
      busUI::renderSchedule(model,list); CHECK(equal(canvas,model));
    }
    navigationState.screen=navigation::Screen::Home; validFeed=false; state="API indisponible";
    paint(hostEpoch); expectHome();
    std::cout<<"PASS mixed realtime/planned Home and list, J+1/J+2, realtime wins duplicates, errors retained\n";
    return 0;
  }
  if(scenario=="tomorrow-expired") {
    CHECK(isOrdinaryHome("Flux avance - Plan expire"));
    CHECK(tomorrowPlans(hostEpoch).status==timetable::Status::Expired);
    std::cout<<"PASS expired plans never produce invented tomorrow times\n"; return 0;
  }
  if(scenario=="tomorrow-none") {
    expectTomorrow("06:00","06:30","07:00",2);
    CHECK(tomorrowPlans(hostEpoch).status==timetable::Status::NoService);
    std::cout<<"PASS calendar exception removes all tomorrow service\n"; return 0;
  }
  if(plannedScenario) {
    CHECK(validFeed && paintPolicy::feed(latest,true,true,hostEpoch,"").primary=="--");
    if(scenario=="tomorrow-future") expectTomorrow("00:06","06:00","06:30",1,true);
    else expectTomorrow();
    CHECK(tomorrowPlans(hostEpoch).departures.size()==15);
    uint64_t writes=gpioWrites;
    pollMs=30000; hostEpoch=base+30;
    hostPayload=feed(base+150,scenario=="tomorrow-future" ? paintPolicy::todayEnd(base)+360 : 0);
    tick(30000); CHECK(hostRequests==2 && gpioWrites==writes);
    if(scenario=="tomorrow-future") expectTomorrow("00:06","06:00","06:30",1,true); else expectTomorrow();
    for(unsigned page=0;page<3;++page) {
      navigationState.screen=navigation::Screen::Schedule; navigationState.page=page;
      paint(hostEpoch); expectPlannedSchedule(page,scenario=="tomorrow-future"); writes=gpioWrites;
      latest.timestamp+=5; paint(hostEpoch); CHECK(gpioWrites==writes);
    }
    navigationState.screen=navigation::Screen::Home;
    latest.departures.clear();
    settings.stop="other"; paint(hostEpoch); expectTomorrow("09:45","--:--","--:--");
    settings.stop="1079"; settings.direction="1";
    paint(hostEpoch); expectTomorrow("05:30","--:--","--:--");
    settings.direction="0"; paint(hostEpoch); expectTomorrow();
    // Today's actual prediction takes priority over the cached planned fallback.
    latest.departures={transit::Departure{hostEpoch+120}}; paint(hostEpoch); expectRealtimeWithPlans("2");
    latest.departures.clear(); paint(hostEpoch); expectTomorrow();
    validFeed=false; state="API indisponible"; paint(hostEpoch); expectHome();
    CHECK(paintPolicy::feed(latest,false,true,hostEpoch,state.c_str()).footer=="API indisponible");
    validFeed=true;
    // Cache must follow LOCAL midnight: Oct 10 is removed, Oct 11 operates.
    hostEpoch=paintPolicy::todayEnd(base); latest.timestamp=hostEpoch;
    paint(hostEpoch); expectTomorrow("06:00","06:30","07:00",2);
    hostEpoch=paintPolicy::todayEnd(hostEpoch); latest.timestamp=hostEpoch;
    paint(hostEpoch); expectTomorrow("06:00","06:30","07:00");
    std::cout<<"PASS planned Home/pages, no header-only SPI, settings/midnight cache, realtime/error priority\n";
    return 0;
  }
  expectHome();
  if(scenario=="config-six") {
    auto press=[&](navigation::Key key){inputQueue.push({key,navigation::Event::Short});tick(5);};
    press(navigation::Key::Menu); press(navigation::Key::Previous); press(navigation::Key::Previous);
    CHECK(navigationState.screen==navigation::Screen::Config && navigationState.selected==4);
    auto expectConfig=[&](){
      busUI::ConfigView view{departureClock(hostEpoch),{settings.stopLabel,settings.lineLabel,
        "60 s","Connecte","Francais","Desactivee"},navigationState.selected,configNotice,-1};
      GFXcanvas1 expected(128,250); expected.setRotation(3); busUI::renderConfig(expected,view);
      CHECK(equal(canvas,expected));
    };
    expectConfig(); press(navigation::Key::Ok);
    CHECK(configNotice=="Francais uniquement"); expectConfig();
    press(navigation::Key::Next); CHECK(navigationState.selected==5); expectConfig();
    CHECK(hostOta.begins==0); // Selecting OTA alone must not activate it.
    press(navigation::Key::Next); CHECK(navigationState.selected==0); expectConfig();
    for(unsigned selected=1;selected<6;++selected) {
      press(navigation::Key::Next); CHECK(navigationState.selected==selected); expectConfig();
    }
    press(navigation::Key::Next); CHECK(navigationState.selected==0); expectConfig();
    press(navigation::Key::Menu);CHECK(navigationState.screen==navigation::Screen::Home);expectHome();
    press(navigation::Key::Menu);CHECK(navigationState.screen==navigation::Screen::Config && navigationState.selected==0);expectConfig();
    CHECK(hostRequests==1 && Preferences::values().empty());
    std::cout<<"PASS real queued six-option menu, wrap, language/Wi-Fi retained, no partial setting or NVS write\n";return 0;
  }
  if(scenario=="schedule-back") {
    navigationState.screen=navigation::Screen::Schedule; navigationState.page=0; paint(hostEpoch);
    inputQueue.push({navigation::Key::Previous,navigation::Event::Short}); tick(5);
    CHECK(navigationState.screen==navigation::Screen::Home); expectHome();
    const uint64_t writes=gpioWrites;
    // Holding Up after returning must not bounce back into the list.
    inputQueue.push({navigation::Key::Previous,navigation::Event::Repeat}); tick(5);
    CHECK(navigationState.screen==navigation::Screen::Home && gpioWrites==writes); expectHome();
    inputQueue.push({navigation::Key::Previous,navigation::Event::Short}); tick(5);
    CHECK(navigationState.screen==navigation::Screen::Schedule && navigationState.page==0);
    std::cout<<"PASS page-one Up returns Home, repeat stays Home, fresh press opens list\n"; return 0;
  }
  if(scenario=="failure") {
    CHECK(!validFeed && state=="API indisponible");
    std::cout<<"PASS real HTTP error ends boot without invented predictions\n";return 0;
  }
  if(scenario=="rejected" || scenario=="malformed") {
    CHECK(!validFeed);
    CHECK(state==(scenario=="rejected" ? "Horloge flux avancee" : "Flux invalide"));
    std::cout<<"PASS rejected/malformed first API result ends boot honestly\n";return 0;
  }
  uint64_t previous=gpioWrites;
  // Full decoded samples differ in header timestamp, not absolute predictions.
  pollMs=30000; hostEpoch=base+30; hostPayload=feed(base+150,base+360);
  tick(30000);
  CHECK(hostRequests==2 && latest.timestamp==uint64_t(base+150));
  CHECK(gpioWrites==previous); expectHome();
  tick(10000); CHECK(gpioWrites==previous);
  navigationState.screen=navigation::Screen::Schedule; paint(hostEpoch);
  uint64_t scheduleWrites=gpioWrites;
  // Decode another real wire sample with only a changed header, then redraw
  // the current page. The actual schedule pixels must also remain stable.
  auto payload=feed(base+160,base+360); transit::Result scheduleSample;
  scheduleSample.maxDepartures=15;
  CHECK(transit::decode(payload.data(),payload.size(),settings.stop,settings.route,
      atoi(settings.direction.c_str()),hostEpoch,scheduleSample));
  latest=std::move(scheduleSample); paint(hostEpoch);
  CHECK(gpioWrites==scheduleWrites);
  navigationState.screen=navigation::Screen::Home; paint(hostEpoch); previous=gpioWrites;
  hostEpoch=base+60; paint(hostEpoch); CHECK(gpioWrites>previous); expectHome();
  previous=gpioWrites;
  state="fetch duration 2000ms"; paint(hostEpoch); CHECK(gpioWrites==previous);
  // Expiration is reevaluated even if polling is postponed (e.g. navigation).
  hostEpoch=time_t(latest.timestamp)+181; paint(hostEpoch);
  CHECK(paintPolicy::feed(latest,true,true,hostEpoch,"").footer=="Donnees perimees");
  CHECK(gpioWrites>previous);expectHome();
  previous=gpioWrites; validFeed=false; state="API indisponible"; paint(hostEpoch);
  CHECK(gpioWrites>previous);expectHome();
  previous=gpioWrites; state="Flux invalide"; paint(hostEpoch); CHECK(gpioWrites>previous);
  // Single-view header follows both the line and physical stop label.
  previous=gpioWrites; settings.stopLabel="Autre arret"; paint(hostEpoch); CHECK(gpioWrites>previous);
  previous=gpioWrites; settings.lineLabel="L11"; paint(hostEpoch); CHECK(gpioWrites>previous); expectHome();
  lastPoll=hostMillis; pollMs=120000;
  WiFi.connection=0; redrawRequested=true; tick(5);
  CHECK(state=="Wi-Fi deconnecte" && !validFeed);
  WiFi.connection=WL_CONNECTED; redrawRequested=true; tick(5);
  CHECK(!validFeed && state!="Temps reel"); // Reconnected does not resurrect old predictions.
  expectHome();
  std::cout<<"PASS main: identical 30s polls emit no SPI; countdown/clock/TTL/errors do repaint\n";
}
