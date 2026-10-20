#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <PortalSettings.h>
#include <BusPortal.h>
#include <PortalUI.h>
#include <esp_system.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <memory>
#include <algorithm>
#include <new>
#include <CrowPanel.h>
#include <Transit.h>
#include <DisplayPolicy.h>
#include <PaintPolicy.h>
#include <Timetable.h>
#include <BootFlow.h>
#include <BusUI.h>
#include <Navigation.h>
#include <WiFiSignal.h>
#include <OtaMode.h>
#include "RootCA.h"
#if __has_include("bus_config.h")
#include "bus_config.h"
#else
#include "../include/config.example.h"
#endif

namespace {
constexpr size_t MAX_FEED_BYTES = 2 * 1024 * 1024;
uint32_t pollMs = 60000;
navigation::Controller navigationState;
wifiSignal::Meter wifiMeter;
otaMode::Session otaSession;
bool otaRequested=false, otaAttempted=false, otaFailed=false;
String otaHostname, otaPassword, otaDetail;
int64_t otaStartedEpoch=0;
navigation::Debouncer buttons[5] = {navigation::Debouncer(false, true),
    navigation::Debouncer(false, false), navigation::Debouncer(true, false),
    navigation::Debouncer(true, false), navigation::Debouncer(false, false)};
navigation::InputQueue inputQueue;
portMUX_TYPE inputMux = portMUX_INITIALIZER_UNLOCKED;
uint32_t lastInputMs = 0;
// Official 2.13_key + Eagle schematic: K3 MENU2, K4 EXIT1.
// K5 TM_2024A wheel contacts UP6 / DOWN4 / CONF5 (not A/B quadrature).
// https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250
constexpr uint8_t buttonPins[] = {2, 1, 6, 4, 5};
constexpr navigation::Key buttonKeys[] = {navigation::Key::Menu, navigation::Key::Back,
    navigation::Key::Previous, navigation::Key::Next, navigation::Key::Ok};
// Only this task samples/debounces GPIO; only loop() owns UI/settings/feed state.
void inputTask(void*) {
  navigation::Debouncer otaExitChord(false,true);
  TickType_t wake = xTaskGetTickCount();
  for(;;) {
    const uint32_t now = millis();
    for(unsigned i=0; i<5; ++i) {
      auto event = buttons[i].update(digitalRead(buttonPins[i]) == LOW, now);
      if(event == navigation::Event::None) continue;
      portENTER_CRITICAL(&inputMux);
      inputQueue.push({buttonKeys[i], event});
      portEXIT_CRITICAL(&inputMux);
    }
    const bool both=digitalRead(buttonPins[0]) == LOW && digitalRead(buttonPins[1]) == LOW;
    if(otaExitChord.update(both,now)==navigation::Event::Long) {
      portENTER_CRITICAL(&inputMux);
      inputQueue.push({navigation::Key::Menu,navigation::Event::ChordLong});
      portEXIT_CRITICAL(&inputMux);
    }
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
  }
}
bool inputPending() {
  portENTER_CRITICAL(&inputMux);
  bool pending = inputQueue.size() != 0;
  portEXIT_CRITICAL(&inputMux);
  return pending;
}
bool redrawRequested = false;
std::string configNotice = "Haut/Bas  OK  Retour";
// Battery display/sampling suspended until measurement hardware is available.
constexpr int batteryLevel = -1;
// One owner of the panel; at most one immutable in-flight image and one pending
// latest image. Rendering/navigation never waits for SPI or the BUSY pin.
struct PanelFrame {
  uint8_t pixels[4000];
  unsigned screen=0;
};
PanelFrame pendingPanel;
bool panelPending=false, panelBusy=false;
portMUX_TYPE panelMux=portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t displayHandle=nullptr, networkHandle=nullptr;
GFXcanvas1 canvas(128, 250);
void presentFrame() {
  static paintPolicy::Frame painted;
  if(!painted.changed(canvas.getBuffer())) return;
  portENTER_CRITICAL(&panelMux);
  memcpy(pendingPanel.pixels,canvas.getBuffer(),4000);
  pendingPanel.screen=unsigned(navigationState.screen); panelPending=true;
  portEXIT_CRITICAL(&panelMux);
  if(displayHandle) xTaskNotifyGive(displayHandle);
}
bool panelIdle() {
  portENTER_CRITICAL(&panelMux);
  const bool idle=!panelPending && !panelBusy;
  portEXIT_CRITICAL(&panelMux);
  return idle;
}
bool displayStep() {
  PanelFrame frame;
  portENTER_CRITICAL(&panelMux);
  const bool pending=panelPending;
  if(pending) { frame=pendingPanel; panelPending=false; panelBusy=true; }
  portEXIT_CRITICAL(&panelMux);
  if(!pending) return false;
  // The sole panel owner performs full refreshes only on changed pixels.
  static paintPolicy::Frame painted;
  const uint32_t started=millis();
  if(painted.changed(frame.pixels)) crowpanel::present(frame.pixels);
  portENTER_CRITICAL(&panelMux);
  panelBusy=false;
  portEXIT_CRITICAL(&panelMux);
  Serial.printf("E-paper paint: %lums screen=%u\n",
      static_cast<unsigned long>(millis()-started), frame.screen);
  return true;
}
void displayTask(void*) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    // Drain the latest snapshot, including any submitted while SPI was busy.
    while(displayStep()) {}
  }
}
transit::Result latest;
bool validFeed = false;
String state = "Demarrage";
bootFlow::Controller boot;
uint32_t lastPoll = 0, lastPaint = 0;
uint32_t slotDue[2] = {0, 0};
WiFiManager wifiManager;
portalUI::Controller portalNavigation;
portalSettings::Settings settings{SETRAM_STOP_ID, SETRAM_ROUTE_ID,
    std::to_string(SETRAM_DIRECTION), STOP_LABEL, LINE_LABEL, DESTINATION_LABEL};
bool dualConfigured() { return !settings.secondStop.empty(); }
WiFiManagerParameter stopParam("stop", "SETRAM stop_id (quai physique)", "", 64);
WiFiManagerParameter routeParam("route", "route_id exact (vide = toutes)", "", 64);
WiFiManagerParameter directionParam("direction", "direction_id : -1, 0 ou 1", "-1", 3);
WiFiManagerParameter stopLabelParam("stop_label", "Nom de l'arret", "", 64);
WiFiManagerParameter lineLabelParam("line_label", "Ligne affichee", "Bus", 64);
WiFiManagerParameter destinationParam("destination", "Destination affichee", "", 64);
WiFiManagerParameter frequencyParam("frequency", "Frequence : 30, 60 ou 120 secondes", "60", 3);
WiFiManagerParameter busLink("<p><a href='/bus'>Choisir mon arret SETRAM (recherche, ligne et sens)</a></p>");
String apName, apPassword;
uint32_t portalStarted = 0;
bool savePending = false;
constexpr uint32_t PORTAL_TIMEOUT_MS = 300000;
struct FeedJob {
  unsigned slot=0;
  portalSettings::Settings selection;
  int64_t now=0;
  uint32_t generation=0;
  transit::Result result;
  bool valid=false;
  String status;
};
// Two independent selections share the network worker but never share the
// decoded result or generation. This is deliberately a real job type so a
// late reply can only update the slot that requested it.
struct TrackingJob {
  unsigned slot=0;
  portalSettings::Settings selection;
  uint32_t generation=0;
  transit::Result result;
  bool valid=false;
  String status;
};
TrackingJob tracking[2];
FeedJob* feedRequest=nullptr;
FeedJob* feedReply=nullptr;
portMUX_TYPE feedMux=portMUX_INITIALIZER_UNLOCKED;
bool feedBusy=false; // Main owns scheduler state; worker exchanges only pointers.
uint32_t feedGeneration=0;
bool linkKnown=false;
bool linkConnected=false;

// A deadline is valid for at most half the uint32_t millis() range (our
// intervals are at most two minutes), so signed subtraction is the portable
// wrap-safe comparison.  Raw `now >= deadline` fails immediately after wrap.
bool deadlineReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
void forcePollNow() {
  lastPoll = millis() - pollMs;
  slotDue[0]=slotDue[1]=0;
}
void invalidateFeedState(bool newGeneration=true) {
  if(newGeneration) ++feedGeneration;
  validFeed=false;
  latest=transit::Result{};
  tracking[0]=TrackingJob{};
  tracking[1]=TrackingJob{};
  forcePollNow();
}

void fillParams() {
  stopParam.setValue(settings.stop.c_str(), 64);
  routeParam.setValue(settings.route.c_str(), 64);
  directionParam.setValue(settings.direction.c_str(), 3);
  stopLabelParam.setValue(settings.stopLabel.c_str(), 64);
  lineLabelParam.setValue(settings.lineLabel.c_str(), 64);
  destinationParam.setValue(settings.destinationLabel.c_str(), 64);
  frequencyParam.setValue(std::to_string(pollMs/1000).c_str(), 3);
}
bool persistSettings(const portalSettings::Settings& next, uint32_t nextInterval) {
  if(!portalSettings::valid(next) || (nextInterval!=30000 && nextInterval!=60000 && nextInterval!=120000)) return false;
  Preferences prefs;
  const std::string blob=portalSettings::serialize(next);
  bool saved=false;
  if(prefs.begin("bus-notifier",false)) {
    saved = prefs.putString("settings", blob.c_str()) != 0 && prefs.getString("settings", "") == blob.c_str();
    saved=saved && prefs.putUInt("poll-ms",nextInterval)!=0 && prefs.getUInt("poll-ms", 0) == nextInterval;
    prefs.end();
  }
  if(!saved) { state="Erreur sauvegarde NVS"; return false; }
  settings=next; pollMs=nextInterval; fillParams();
  invalidateFeedState();
  navigationState.setDualConfigured(dualConfigured());
  state="Configuration sauvegardee";
  return true;
}
busPortal::Portal onboardPortal(wifiManager,settings,pollMs,persistSettings);
void openPortal() {
  if(wifiManager.getConfigPortalActive()) return;
  invalidateFeedState();
  fillParams();
  portalNavigation.reset();
  state = "Portail configuration";
  portalStarted = millis();
  wifiManager.startConfigPortal(apName.c_str(), apPassword.c_str());
}
void saveParams() {
  savePending = false;
  portalSettings::Settings next{stopParam.getValue(), routeParam.getValue(),
    directionParam.getValue(), stopLabelParam.getValue(), lineLabelParam.getValue(), destinationParam.getValue()};
  uint32_t nextInterval=busPortal::frequency(frequencyParam.getValue());
  if(!portalSettings::valid(next) || !nextInterval) {
    state = "Champs invalides : recommencer";
    Serial.println("Configuration bus invalide (stop requis, sens -1/0/1)");
    fillParams();
    if(!wifiManager.getConfigPortalActive()) openPortal();
    return;
  }
  if(!persistSettings(next,nextInterval)) return;
  Serial.println("Configuration bus sauvegardee et relue depuis NVS");
}
void changeInterval() {
  uint32_t next = pollMs == 30000 ? 60000 : pollMs == 60000 ? 120000 : 30000;
  Preferences prefs;
  bool saved = false;
  if(prefs.begin("bus-notifier", false)) {
    saved = prefs.putUInt("poll-ms", next) != 0 && prefs.getUInt("poll-ms", 0) == next;
    prefs.end();
  }
  if(saved) {
    pollMs = next; forcePollNow();
    configNotice = "Frequence sauvegardee";
  } else configNotice = "Erreur sauvegarde NVS";
}
void wipeOtaPassword() {
  for(unsigned i=0;i<otaPassword.length();++i) otaPassword[i]=0;
  otaPassword="";
}
void startOta() {
  if(otaRequested || wifiManager.getConfigPortalActive()) return;
  invalidateFeedState();
  char host[32], secret[17];
  snprintf(host,sizeof(host),"busnotifier-%06X",unsigned(ESP.getEfuseMac() & 0xffffff));
  snprintf(secret,sizeof(secret),"%08X%08X",unsigned(esp_random()),unsigned(esp_random()));
  otaHostname=host; otaPassword=secret;
  memset(secret,0,sizeof(secret));
  otaStartedEpoch=time(nullptr);
  otaRequested=true; otaAttempted=false; otaFailed=false;
  otaDetail="Preparation : attendre les workers";
  navigationState.screen=navigation::Screen::Ota;
  redrawRequested=true;
}
void stopOta() {
  if(otaSession.transferring()) return;
  const bool restored=otaSession.stop();
  wipeOtaPassword();
  if(!restored) {
    otaFailed=true; otaDetail=otaSession.detail(); redrawRequested=true;
    return; // Keep failure visible; the same long chord retries restoration.
  }
  otaRequested=false; otaAttempted=false; otaFailed=false;
  invalidateFeedState(); state="En attente API";
  navigationState.screen=navigation::Screen::Home;
  lastPoll=millis()-pollMs; redrawRequested=true;
}
void pollButtons() {
  navigation::InputEvent batch[navigation::InputQueue::Capacity];
  unsigned count=0;
  portENTER_CRITICAL(&inputMux);
  while(count<navigation::InputQueue::Capacity && inputQueue.pop(batch[count])) ++count;
  const uint32_t dropped=inputQueue.dropped();
  portEXIT_CRITICAL(&inputMux);
  if(count) {
    lastInputMs=millis();
    Serial.printf("Input batch: %u events, dropped=%lu\n", count, static_cast<unsigned long>(dropped));
  }
  // Drain the captured batch, then paint the final state once (no paint backlog).
  for(unsigned index=0; index<count; ++index) {
    unsigned i=0;
    while(i<5 && buttonKeys[i]!=batch[index].key) ++i;
    if(i==5) continue;
    auto event = batch[index].event;
    if(otaRequested) {
      if(event==navigation::Event::ChordLong) stopOta();
      continue; // No portal/menu navigation while OTA owns the application.
    }
    if(i == 0 && event == navigation::Event::Long) openPortal();
    else if(event == navigation::Event::Short || event == navigation::Event::Repeat) {
      if(wifiManager.getConfigPortalActive()) {
        auto portalAction = portalNavigation.key(buttonKeys[i]);
        if(portalAction == portalUI::Action::None) continue;
        if(portalAction == portalUI::Action::Redraw) {
          redrawRequested = true;
          continue; // Do not navigate the bus UI underneath the portal.
        }
        if(portalAction != portalUI::Action::Close) continue;
        wifiManager.stopConfigPortal(); invalidateFeedState(); state = "Portail ferme";
        redrawRequested = true;
        continue; // Return to the originating screen, never also navigate underneath.
      }
      // A held direction must not re-open the list after page-one Up returns Home.
      // A new short press still opens Schedule normally from Home.
      if(event==navigation::Event::Repeat && navigationState.screen==navigation::Screen::Home) continue;
      auto action = navigationState.event(buttonKeys[i]);
      configNotice = "Haut/Bas  OK  Retour";
      if(action == navigation::Action::Portal) openPortal();
      else if(action == navigation::Action::RefreshInterval) changeInterval();
      else if(action == navigation::Action::RefreshNow) forcePollNow();
      else if(action == navigation::Action::FrenchInfo) configNotice = "Francais uniquement";
      else if(action == navigation::Action::EnableOta) startOta();
      else if(action == navigation::Action::Swap && dualConfigured()) {
        portalSettings::Settings next=settings; next.swapped=!next.swapped;
        const std::string blob=portalSettings::serialize(next); bool saved=false;
        Preferences prefs; if(prefs.begin("bus-notifier",false)) {
          saved=prefs.putString("settings",blob.c_str())!=0 && prefs.getString("settings","")==blob.c_str();
          prefs.end();
        }
        if(saved) { settings=next; navigationState.setDualConfigured(true); configNotice="Ordre des suivis sauvegarde"; }
        else { configNotice="Erreur sauvegarde NVS"; state="Erreur sauvegarde NVS"; }
      }
    } else continue;
    redrawRequested = true; // Pixel comparison makes clamped moves no-ops.
  }
}


// HTTPClient removes chunked transfer framing before writing into this sink.
class FeedSink : public Stream {
public:
  uint8_t* data;
  size_t used = 0;
  FeedSink() : data(static_cast<uint8_t*>(heap_caps_malloc(MAX_FEED_BYTES,
       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))) {}
  ~FeedSink() { heap_caps_free(data); }
  size_t write(uint8_t c) override { return write(&c, 1); }
  size_t write(const uint8_t* bytes, size_t size) override {
    if (!data || size > MAX_FEED_BYTES - used) return 0;
    memcpy(data + used, bytes, size); used += size; return size;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

void fetchFeed(FeedJob& response) {
  // Worker only touches this job and local socket/decoder memory, never UI state.
  const auto& settings=response.selection;
  const int64_t now=response.now;
  auto& latest=response.result;
  auto& validFeed=response.valid;
  auto& state=response.status;
  validFeed = false; // Failed requests are never committed as live predictions.
  WiFiClientSecure tls;
  tls.setCACert(ROOT_CA);
  tls.setHandshakeTimeout(10);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(10000);
  if (!http.begin(tls, SETRAM_FEED_URL)) { state = "Erreur connexion API"; return; }
  http.addHeader("Accept", "application/x-protobuf");
  const char* responseHeaders[] = {"Date"};
  http.collectHeaders(responseHeaders, 1);
  int status = http.GET();
  Serial.printf("HTTP SETRAM: %d epoch=%lld Date=%s\n", status,
      static_cast<long long>(time(nullptr)), http.header("Date").c_str());
  if (status != HTTP_CODE_OK) {
    state = "API indisponible"; http.end(); return;
  }
  if (http.getSize() > static_cast<int>(MAX_FEED_BYTES)) {
    state = "Flux trop volumineux"; http.end(); return;
  }
  FeedSink sink;
  if (!sink.data) { state = "PSRAM indisponible"; http.end(); return; }
  int received = http.writeToStream(&sink);
  http.end();
  transit::Result candidate;
  candidate.maxDepartures = 15;
  if (received <= 0 || static_cast<size_t>(received) != sink.used ||
      !transit::decode(sink.data, sink.used, settings.stop, settings.route,
                       atoi(settings.direction.c_str()), now, candidate)) {
    state = "Flux invalide"; return;
  }
  const int64_t observedNow=time(nullptr);
  const auto feedState=displayPolicy::classify(candidate.timestamp, observedNow);
  Serial.printf("GTFS-RT: %u octets, %u passages, timestamp=%llu epoch=%lld age=%llds stop=%s route=%s direction=%s\n",
      unsigned(sink.used), unsigned(candidate.departures.size()),
      static_cast<unsigned long long>(candidate.timestamp), static_cast<long long>(observedNow),
      static_cast<long long>(observedNow-static_cast<int64_t>(candidate.timestamp)),
      settings.stop.c_str(), settings.route.c_str(), settings.direction.c_str());
  if (!displayPolicy::usable(candidate.timestamp, observedNow)) {
    if(feedState==displayPolicy::FeedState::FutureRejected) state="Horloge flux avancee";
    else if(feedState==displayPolicy::FeedState::Missing) state="Horodatage flux absent";
    else if(feedState==displayPolicy::FeedState::ClockInvalid) state="Synchronisation heure";
    else state="Donnees perimees";
    Serial.printf("GTFS-RT rejete: %s\n", state.c_str()); return;
  }
  latest = std::move(candidate);
  validFeed = true;
  if(feedState==displayPolicy::FeedState::FutureTolerated) {
    state="Flux avance";
    Serial.printf("GTFS-RT tolere sans correction: Flux avance +%llus ahead=%llus\n",
        static_cast<unsigned long long>(displayPolicy::aheadSeconds(latest.timestamp,observedNow)),
        static_cast<unsigned long long>(displayPolicy::aheadSeconds(latest.timestamp,observedNow)));
  } else state="Temps reel";
  if(latest.departures.empty()) Serial.println("Aucun passage annonce pour les identifiants configures");
}

bool networkStep() {
  portENTER_CRITICAL(&feedMux);
  FeedJob* raw=feedRequest; feedRequest=nullptr;
  portEXIT_CRITICAL(&feedMux);
  if(!raw) return false;
  std::unique_ptr<FeedJob> job(raw);
  const uint32_t started=millis();
  fetchFeed(*job);
  Serial.printf("SETRAM fetch: %lums\n",static_cast<unsigned long>(millis()-started));
  portENTER_CRITICAL(&feedMux);
  feedReply=job.release();
  portEXIT_CRITICAL(&feedMux);
  return true;
}
void networkTask(void*) {
  for(;;) {
    ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    networkStep();
  }
}
void consumeFeed() {
  portENTER_CRITICAL(&feedMux);
  FeedJob* raw=feedReply; feedReply=nullptr;
  portEXIT_CRITICAL(&feedMux);
  if(!raw) return;
  std::unique_ptr<FeedJob> job(raw);
  const unsigned slot=job->slot;
  feedBusy=false;
  // Portal/configuration changes invalidate in-flight work; closing the portal
  // does not resurrect an older result. Compare opaque query IDs too.
  const auto expectedStop = slot ? settings.secondStop : settings.stop;
  const auto expectedRoute = slot ? settings.secondRoute : settings.route;
  const auto expectedDirection = slot ? settings.secondDirection : settings.direction;
  if(job->generation!=feedGeneration || job->selection.stop!=expectedStop ||
     job->selection.route!=expectedRoute || job->selection.direction!=expectedDirection ||
     wifiManager.getConfigPortalActive() || WiFi.status()!=WL_CONNECTED || time(nullptr)<=1600000000) return;
  tracking[slot].slot=slot; tracking[slot].selection=job->selection;
  tracking[slot].generation=job->generation; tracking[slot].result=job->result;
  tracking[slot].valid=job->valid; tracking[slot].status=job->status;
  if(slot==0) { latest=std::move(job->result); validFeed=job->valid; state=job->status; }
  boot.requestFinished(); redrawRequested=true;
}
bool requestFeed(int64_t now) {
  if(feedBusy) return false;
  std::unique_ptr<FeedJob> job(new(std::nothrow) FeedJob);
  if(!job) { validFeed=false; state="Memoire requete indisponible"; redrawRequested=true; return false; }
  job->slot=0;
  if(dualConfigured()) {
    const uint32_t nowMs=millis();
    const bool due0=deadlineReached(nowMs,slotDue[0]);
    const bool due1=deadlineReached(nowMs,slotDue[1]);
    // When both are due, slot 0 wins this turn and slot 1 becomes the next
    // due slot as soon as slot 0 receives its new deadline.
    job->slot = due1 && !due0 ? 1 : 0;
  }
  job->selection=job->slot ? portalSettings::Settings{settings.secondStop,settings.secondRoute,settings.secondDirection,settings.secondStopLabel,settings.secondLineLabel,settings.secondDestinationLabel} : settings;
  job->now=now; job->generation=feedGeneration;
  slotDue[job->slot]=millis()+pollMs;
  feedBusy=true;
  portENTER_CRITICAL(&feedMux);
  feedRequest=job.release();
  portEXIT_CRITICAL(&feedMux);
  if(networkHandle) xTaskNotifyGive(networkHandle);
  return true;
}

unsigned selectedTrackingSlot() {
  if(!dualConfigured()) return 0;
  // Home view 0 is the dual overview; when a schedule is opened from it the
  // first row in the current order is the active tuple.  Views 1 and 2 are
  // the same ordered pair, so one helper keeps live/timetable/labels aligned.
  if(navigationState.homeView==2) return settings.swapped ? 0 : 1;
  return settings.swapped ? 1 : 0;
}
portalSettings::Settings selectionForSlot(unsigned slot) {
  return slot ? portalSettings::Settings{settings.secondStop,settings.secondRoute,
    settings.secondDirection,settings.secondStopLabel,settings.secondLineLabel,
    settings.secondDestinationLabel} : settings;
}

// Scanning the flash timetable happens once per selected stop/route/direction
// and local civil date, never once per ten-second candidate repaint. Expiry is
// rechecked on the next local day; an invalid clock must not reuse cached plans.
const timetable::Result& futurePlansForSlot(int64_t now,unsigned slot,unsigned offset) {
  static int64_t dayEnd[2]={0,0};
  static std::string stop[2], route[2], direction[2];
  static timetable::Result cached[2][2]={{ {timetable::Status::InvalidClock,{}},{timetable::Status::InvalidClock,{}} },{ {timetable::Status::InvalidClock,{}},{timetable::Status::InvalidClock,{}} }};
  static const timetable::Result invalid{timetable::Status::InvalidClock,{}};
  if(slot>1 || offset<1 || offset>2) return invalid;
  const int64_t end=paintPolicy::todayEnd(now);
  if(!end) return invalid;
  const auto selected=selectionForSlot(slot);
  if(end!=dayEnd[slot] || stop[slot]!=selected.stop || route[slot]!=selected.route || direction[slot]!=selected.direction) {
    for(unsigned day=1;day<=2;++day)
      cached[slot][day-1]=timetable::day(selected.stop,selected.route,atoi(selected.direction.c_str()),now,day,15);
    dayEnd[slot]=end; stop[slot]=selected.stop; route[slot]=selected.route; direction[slot]=selected.direction;
  }
  return cached[slot][offset-1];
}
const timetable::Result& tomorrowPlansForSlot(int64_t now,unsigned slot) {
  return futurePlansForSlot(now,slot,1);
}
const timetable::Result& tomorrowPlans(int64_t now) { return tomorrowPlansForSlot(now,selectedTrackingSlot()); }
std::string departureClock(int64_t epoch) {
  time_t stamp=static_cast<time_t>(epoch); struct tm civil;
  char clock[8]="--:--";
  if(localtime_r(&stamp,&civil)) strftime(clock,sizeof(clock),"%H:%M",&civil);
  return clock;
}
struct Passage { int64_t epoch; unsigned day; bool planned; };
std::vector<Passage> upcomingForSlot(int64_t now,unsigned slot,bool& expired) {
  std::vector<Passage> rows; rows.reserve(45);
  const int64_t ends[3]={paintPolicy::todayEnd(now),
    paintPolicy::todayEnd(paintPolicy::todayEnd(now)),
    paintPolicy::todayEnd(paintPolicy::todayEnd(paintPolicy::todayEnd(now)))};
  int64_t lastReal[3]={0,0,0};
  const transit::Result& live=(dualConfigured() ? tracking[slot].result : latest);
  for(const auto& departure:live.departures) {
    if(departure.time<now || departure.time>=ends[2]) continue;
    unsigned day=departure.time<ends[0] ? 0 : departure.time<ends[1] ? 1 : 2;
    rows.push_back({departure.time,day,false});
    lastReal[day]=std::max(lastReal[day],departure.time);
  }
  // Only future civil days are supplemented. Within a day, plans begin after
  // the last announced realtime departure; never invent today's missing service.
  for(unsigned day=1;day<=2;++day) {
    const auto& plans=futurePlansForSlot(now,slot,day);
    if(plans.status==timetable::Status::Expired) expired=true;
    if(plans.status!=timetable::Status::Ready) continue;
    for(auto epoch:plans.departures)
      if(epoch>lastReal[day]) rows.push_back({epoch,day,true});
  }
  std::sort(rows.begin(),rows.end(),[](const Passage& a,const Passage& b){
    return a.epoch<b.epoch || (a.epoch==b.epoch && a.planned<b.planned);
  });
  rows.erase(std::unique(rows.begin(),rows.end(),[](const Passage& a,const Passage& b){return a.epoch==b.epoch;}),rows.end());
  if(rows.size()>15) rows.resize(15);
  return rows;
}
std::string passageText(const Passage& entry,int64_t now,bool hero=false,bool homeNext=false) {
  if(entry.day) return "J+"+std::to_string(entry.day)+" "+departureClock(entry.epoch)+(entry.planned ? " P" : " R");
  const int minutes=transit::minutesRemaining(entry.epoch,now);
  if(hero) return entry.epoch-now<60 ? "<1" : minutes>999 ? "999+" : std::to_string(minutes);
  if(homeNext) return minutes>999 ? ">999 m" : std::to_string(minutes)+" min";
  return entry.epoch-now<60 ? "<1 min" : minutes<=30 ? std::to_string(minutes)+" min" : departureClock(entry.epoch);
}
struct SlotComposition {
  unsigned slot=0;
  std::string stop,line,destination;
  paintPolicy::FeedView visible;
  std::vector<Passage> entries;
  bool expired=false;
};
SlotComposition composeSlot(int64_t now,unsigned slot) {
  SlotComposition composition;
  composition.slot=slot;
  const auto selected=selectionForSlot(slot);
  composition.stop=selected.stopLabel;
  composition.line=selected.lineLabel;
  composition.destination=selected.destinationLabel;
  const transit::Result& result=dualConfigured() ? tracking[slot].result : latest;
  const bool valid=dualConfigured() ? tracking[slot].valid : validFeed;
  const std::string error=dualConfigured() ? std::string(tracking[slot].status.c_str()) :
      std::string(state.c_str());
  composition.visible=paintPolicy::feed(result,valid,WiFi.status()==WL_CONNECTED,now,error);
  if(!composition.visible.usable) return composition;
  composition.entries=upcomingForSlot(now,slot,composition.expired);
  if(!composition.entries.empty()) {
    composition.visible.primary=passageText(composition.entries[0],now,true);
    for(unsigned row=0;row<2;++row)
      composition.visible.next[row]=row+1<composition.entries.size() ?
          passageText(composition.entries[row+1],now,false,true) : "--";
    composition.visible.status=composition.entries[0].planned ? "Prevu" :
        (composition.visible.tolerated ? "Flux avance" : "Temps reel");
    const bool future=std::any_of(composition.entries.begin(),composition.entries.end(),
        [](const Passage& p){return p.day!=0;});
    if(future) composition.visible.footer=composition.visible.tolerated ?
        "Flux avance / P=prevu R=reel" : "P=prevu R=reel";
    if(composition.expired && composition.entries.size()<15) {
      composition.visible.footer=composition.visible.tolerated ?
          "Flux avance - Plan expire" : "Horaires prevus expires";
      if(future) composition.visible.footer+=" P=prevu R=reel";
    }
  } else {
    composition.visible.footer=composition.expired ?
        (composition.visible.tolerated ? "Flux avance - Plan expire" : "Horaires prevus expires") :
        (composition.visible.tolerated ? "Flux avance / J+1 J+2 sans horaire" :
         "J+1 J+2 sans horaire prevu");
  }
  return composition;
}
void draw(int64_t now) {
  // Live candidates keep real time/freshness checks. Only static QR inputs are
  // cached; normalized pixel guards still reject identical transfers.
  static bool portalCached=false;
  static portalUI::View lastPortal{portalUI::Step::Join,"","",""};
  if(wifiManager.getConfigPortalActive()) {
    const char* notice=state == "Portail configuration" ? "" : state.c_str();
    // QR screens have no clock/countdown. Encode only on visible input changes;
    // WiFiManager.process() and queued buttons still run every loop.
    if(portalCached && lastPortal.step==portalNavigation.step &&
       lastPortal.ssid==apName.c_str() && lastPortal.password==apPassword.c_str() &&
       lastPortal.notice==notice) return;
    lastPortal={portalNavigation.step,apName.c_str(),apPassword.c_str(),notice};
    portalUI::render(canvas,lastPortal);
    portalCached=true;
    presentFrame();
    return;
  }
  portalCached=false; // Returning from another screen must restore the QR image.
  busUI::setWiFiSignal(wifiMeter.observe(WiFi.status()==WL_CONNECTED,WiFi.RSSI()));
  if(otaRequested) {
    if(otaSession.transferring()) return; // Never enqueue e-paper during flash.
    unsigned stage=!otaAttempted ? 0 : otaFailed ? 4 : 1;
    if(otaSession.phase()==otaMode::Phase::Error) stage=4;
    if(otaSession.phase()==otaMode::Phase::Success) stage=3;
    const String ip=WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : String("Non connecte");
    const char* detail=otaAttempted && !otaFailed ? otaSession.detail() : otaDetail.c_str();
    busUI::OtaView view{otaStartedEpoch>1600000000 ? departureClock(otaStartedEpoch) : "--:--",
      otaHostname.c_str(),ip.c_str(),otaPassword.c_str(),detail,stage};
    busUI::renderOta(canvas,view); presentFrame(); return;
  }
  if(!boot.complete && navigationState.screen==navigation::Screen::Home) {
    busUI::renderBoot(canvas, {boot.stage, boot.detail, boot.failed, batteryLevel});
    presentFrame();
    return;
  }
  const unsigned selectedSlot=selectedTrackingSlot();
  const SlotComposition active=composeSlot(now,selectedSlot);
  const auto& visible=active.visible;
  const bool usable=visible.usable;
  const bool tolerated=visible.tolerated;
  busUI::View model;
  model.batteryPercent = batteryLevel;
  model.stop = active.stop;
  model.line = active.line;
  model.destination = active.destination;
  model.primary = visible.primary;
  model.next[0] = visible.next[0]; model.next[1] = visible.next[1];
  char clock[8] = "--:--";
  struct tm local;
  if(now > 1600000000) {
    time_t stamp = now; localtime_r(&stamp, &local);
    strftime(clock, sizeof(clock), "%H:%M", &local);
  }
  model.clock = clock;
  model.status=visible.status;
  model.footer=visible.footer;
  if(navigationState.screen == navigation::Screen::Config) {
    busUI::ConfigView config{clock, {settings.stopLabel, settings.lineLabel,
      std::to_string(pollMs/1000) + " s", WiFi.status()==WL_CONNECTED ? "Connecte" : "Deconnecte", "Francais", "Desactivee", dualConfigured() ? (settings.swapped ? "2 puis 1" : "1 puis 2") : "Indisponible"},
      navigationState.selected,configNotice,batteryLevel};
    busUI::renderConfig(canvas, config); presentFrame();
    return;
  }
  if(navigationState.screen == navigation::Screen::WiFi ||
      navigationState.screen == navigation::Screen::WiFiIp) {
    String ip=WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : String("Non connecte");
    busUI::WiFiView wifi{clock, ip.c_str(), navigationState.selected,
        navigationState.screen == navigation::Screen::WiFiIp, batteryLevel};
    busUI::renderWiFi(canvas, wifi); presentFrame();
    return;
  }
  // Supplement the usable live list with explicitly marked planned J+1/J+2
  // departures, not a wholesale alternate screen. Network/clock errors stay errors.
  const std::vector<Passage>& entries=active.entries;
  const bool expired=active.expired;
  model.primary=visible.primary; model.next[0]=visible.next[0]; model.next[1]=visible.next[1];
  model.status=visible.status; model.footer=visible.footer;
  if(navigationState.screen == navigation::Screen::Schedule) {
    busUI::ScheduleView schedule{clock,active.line,active.destination,
        {"--","--","--","--","--"},model.footer,navigationState.page+1,batteryLevel};
    for(unsigned row=0;row<5;++row) {
      const unsigned index=navigationState.page*5+row;
      if(index<entries.size()) schedule.times[row]=passageText(entries[index],now);
    }
    if(usable && !expired && !entries.empty() && entries.size()<=navigationState.page*5)
      schedule.footer=tolerated ? "Flux avance - Aucun passage" : "Aucun autre passage annonce";
    busUI::renderSchedule(canvas,schedule); presentFrame(); return;
  }
  if(dualConfigured() && navigationState.screen == navigation::Screen::Home && navigationState.homeView==0) {
    const unsigned first=settings.swapped ? 1 : 0, second=1-first;
    const auto firstView=composeSlot(now,first), secondView=composeSlot(now,second);
    auto warning=[](const SlotComposition& composition) {
      return composition.visible.footer=="Temps reel" ? std::string() : composition.visible.footer;
    };
    std::string dualFooter="1: "+firstView.visible.footer;
    if(secondView.visible.footer!=firstView.visible.footer)
      dualFooter += " | 2: "+secondView.visible.footer;
    busUI::DualView dual{firstView.stop,clock,firstView.visible.status,dualFooter,
      {{firstView.line,firstView.destination,firstView.visible.primary,firstView.visible.next[0],firstView.visible.usable,warning(firstView)},
       {secondView.line,secondView.destination,secondView.visible.primary,secondView.visible.next[0],secondView.visible.usable,warning(secondView)}},batteryLevel};
    busUI::renderDual(canvas,dual); presentFrame(); return;
  }
  busUI::render(canvas,model);
  presentFrame();
}
}
void setup() {
  Serial.begin(115200);
  for(uint8_t pin : buttonPins) pinMode(pin, INPUT_PULLUP);
  if(xTaskCreatePinnedToCore(inputTask, "buttons", 2048, nullptr, 2, nullptr, 0)!=pdPASS) {
    Serial.println("Erreur tache boutons"); while(true) delay(1000);
  }
  crowpanel::begin(); canvas.setRotation(3);
  if (!canvas.getBuffer()) { Serial.println("Erreur framebuffer"); while(true) delay(1000); }
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1); tzset();
  Preferences prefs;
  if(prefs.begin("bus-notifier", true)) {
    String blob = prefs.getString("settings", "");
    portalSettings::Settings loaded;
    if(portalSettings::deserialize(blob.c_str(), loaded)) settings = std::move(loaded);
    uint32_t storedInterval = prefs.getUInt("poll-ms", 60000);
    if(storedInterval == 30000 || storedInterval == 60000 || storedInterval == 120000) pollMs = storedInterval;
    prefs.end();
  }
  navigationState.setDualConfigured(dualConfigured());
  if(xTaskCreatePinnedToCore(displayTask,"epaper",16384,nullptr,1,&displayHandle,0)!=pdPASS) {
    Serial.println("Erreur tache e-paper"); while(true) delay(1000);
  }
  if(xTaskCreatePinnedToCore(networkTask,"setram",16384,nullptr,1,&networkHandle,0)!=pdPASS) {
    Serial.println("Erreur tache SETRAM"); while(true) delay(1000);
  }
  fillParams();
  wifiManager.setDebugOutput(false); // Do not log Wi-Fi credentials.
  wifiManager.setConfigPortalBlocking(false);
  // "wifi" opens /wifi with scan=true (not the no-scan "wifinoscan" page).
  // Do not add "param": WiFiManager would remove bus fields from /wifi.
  const char* portalMenu[] = {"custom", "wifi", "exit"};
  wifiManager.setCustomMenuHTML("<form action='/bus' method='get'><button>Mon arret SETRAM</button></form>");
  wifiManager.setMenu(portalMenu, 3);
  wifiManager.setWebServerCallback([](){ onboardPortal.attach(); });
  wifiManager.setRemoveDuplicateAPs(true);
  wifiManager.setConnectTimeout(15);
  wifiManager.setSaveParamsCallback([]() { savePending = true; });
  wifiManager.addParameter(&stopParam);
  wifiManager.addParameter(&busLink);
  wifiManager.addParameter(&routeParam);
  wifiManager.addParameter(&directionParam);
  wifiManager.addParameter(&stopLabelParam);
  wifiManager.addParameter(&lineLabelParam);
  wifiManager.addParameter(&destinationParam);
  wifiManager.addParameter(&frequencyParam);
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true);
  char name[32], password[16];
  snprintf(name, sizeof(name), "BusNotifier-%06X", unsigned(ESP.getEfuseMac() & 0xffffff));
  snprintf(password, sizeof(password), "%08X%04X", unsigned(esp_random()), unsigned(esp_random() & 0xffff));
  apName = name; apPassword = password;
  state = "Connexion Wi-Fi"; draw(0);
  portalStarted = millis();
  portalNavigation.reset();
  wifiManager.autoConnect(apName.c_str(), apPassword.c_str());
  linkKnown=true; linkConnected=WiFi.status()==WL_CONNECTED;
  if(wifiManager.getConfigPortalActive()) state = "Portail configuration";
  else if(!portalSettings::valid(settings)) openPortal();
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.cloudflare.com");
  boot.prerequisites(WiFi.status()==WL_CONNECTED,time(nullptr)>1600000000,portalSettings::valid(settings));
  draw(time(nullptr));
  lastPaint=millis();
  lastPoll = millis() - pollMs;
}
void loop() {
  wifiManager.process();
  if(savePending) saveParams();
  pollButtons();
  consumeFeed();
  if(otaRequested) {
    // Publish preparation/ready/error before servicing OTA. Service may block
    // synchronously for flash; never overlap it with HTTPS or physical SPI.
    if(redrawRequested && !otaSession.transferring()) {
      redrawRequested=false; draw(time(nullptr)); lastPaint=millis();
    }
    if(!otaAttempted && !feedBusy && panelIdle()) {
      otaAttempted=true;
      otaFailed=!otaSession.begin(otaHostname.c_str(),otaPassword.c_str());
      otaDetail=otaSession.detail();
      redrawRequested=true;
    } else if(otaSession.active() && !otaSession.transferring() && !feedBusy && panelIdle()) {
      const auto before=otaSession.phase();
      const char* previousDetail=otaSession.detail();
      otaSession.handle();
      if(before!=otaSession.phase() || strcmp(previousDetail,otaSession.detail())!=0) redrawRequested=true;
    }
    // No automatic clock/progress refreshes or 20ms battery-idle cadence here.
    delay(1); return;
  }
  if(wifiManager.getConfigPortalActive()) {
    if(millis() - portalStarted >= PORTAL_TIMEOUT_MS) {
      wifiManager.stopConfigPortal(); invalidateFeedState(); state = "Portail expire : MENU 2s";
    }
    draw(time(nullptr)); delay(10); return;
  }
  int64_t now = time(nullptr);
  const bool connected=WiFi.status()==WL_CONNECTED;
  if(!linkKnown || linkConnected!=connected) {
    linkKnown=true; linkConnected=connected; invalidateFeedState();
  }
  if (!connected) { state = "Wi-Fi deconnecte"; }
  else if (!portalSettings::valid(settings)) { validFeed = false; state = "Configurer arret : MENU 2s"; }
  else if (now <= 1600000000) { validFeed = false; state = "Synchronisation heure"; }
  else if(!validFeed && (state=="Wi-Fi deconnecte" || state=="Synchronisation heure" ||
      state=="Configurer arret : MENU 2s")) state="En attente API";
  boot.prerequisites(connected,now>1600000000,portalSettings::valid(settings));
  // Publish input-driven state before a potentially blocking TLS/feed operation.
  if (redrawRequested || millis() - lastPaint >= 10000) {
    redrawRequested = false; draw(now); lastPaint = millis();
  }
  // A press captured during painting must not wait behind a new HTTP request.
  const uint32_t pollNow=millis();
  const bool pollDue = dualConfigured() ?
      (deadlineReached(pollNow,slotDue[0]) || deadlineReached(pollNow,slotDue[1])) :
      pollNow-lastPoll>=pollMs;
  if(navigation::allowFetch(navigationState.screen, wifiManager.getConfigPortalActive(),
      inputPending(), millis()-lastInputMs) && WiFi.status()==WL_CONNECTED &&
      !feedBusy && portalSettings::valid(settings) && now>1600000000 && pollDue) {
    if(!boot.complete) {
      boot.requestStarted(); draw(now);
      // A key captured during the API boot frame still gets priority over TLS.
      if(inputPending()) { redrawRequested=true; delay(5); return; }
    }
    if(requestFeed(now)) lastPoll=millis();
  }
  // Keep fast checks while interacting; yield longer when idle. The dedicated
  // 5ms GPIO sampler still captures/debounces keys throughout this short wait.
  delay(inputPending() || millis()-lastInputMs<500 ? 5 : 20);
}
