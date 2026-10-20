#include <BusUI.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
// Pixel-for-pixel text evidence from real GFX glyphs, including blank margins.
void assertFittedText(GFXcanvas1& c, const char* text, int x, int top, int width,
                      int height, const GFXfont* font, bool white=false, bool center=false) {
  GFXcanvas1 expected(250,128); expected.fillScreen(white ? 1 : 0);
  expected.setFont(font); expected.setTextColor(white ? 0 : 1); expected.setTextWrap(false);
  int16_t bx,by; uint16_t w,h;
  expected.getTextBounds(text,0,0,&bx,&by,&w,&h);
  assert(w<=width && h<=height);
  expected.setCursor(x+(center ? (width-int(w))/2 : 0)-bx,top+(height-h)/2-by); expected.print(text);
  for(int py=top;py<top+height;++py) for(int px=x;px<x+width;++px)
    if(c.getPixel(px,py)!=expected.getPixel(px,py)) {
      std::cerr << "Fitted text mismatch: " << text << " at " << px << ',' << py << '\n';
      assert(false);
    }
}
void save(GFXcanvas1& c, const char* path) {
  for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  std::ofstream out(path);
  out << "P1\n250 122\n";
  for(int y=0;y<122;++y) { for(int x=0;x<250;++x) out << (c.getPixel(x,y) ? '1':'0') << ' '; out << '\n'; }
}
void assertText(GFXcanvas1& c, const char* text, int x, int y, int width, int height, bool white=false) {
  GFXcanvas1 expected(250,128); expected.fillScreen(white ? 1 : 0);
  expected.setTextColor(white ? 0 : 1); expected.setCursor(x,y); expected.print(text);
  for(int py=y;py<y+height;++py) for(int px=x;px<x+width;++px)
    if(c.getPixel(px,py)!=expected.getPixel(px,py)) {
      std::cerr << "Text mismatch: " << text << " at " << px << ',' << py << '\n';
      assert(false);
    }
}
void assertBatteryHidden(GFXcanvas1& c) {
  // Battery remains hidden: only Wi-Fi and the clock occupy the right lanes.
  for(int y=0;y<20;++y) for(int x : {181,182,183,184,185,186,187,188,189,208,209,210,211,212,213,249}) {
    if(c.getPixel(x,y)!=1) {
      std::cerr << "Battery lane must be solid header black at " << x << ',' << y << '\n';
      assert(false);
    }
  }
}
void assertHeader(GFXcanvas1& c, const char* clock="09:30") {
  assertBatteryHidden(c);
  GFXcanvas1 expected(250,20); expected.fillScreen(1);
  expected.setTextColor(0);
  int16_t bx,by; uint16_t w,h;
  expected.getTextBounds(clock,0,0,&bx,&by,&w,&h);
  expected.setCursor(214+35-w-bx,2+(16-h)/2-by); expected.print(clock);
  for(int y=0;y<20;++y) for(int x=214;x<250;++x)
    assert(c.getPixel(x,y)==expected.getPixel(x,y)); // Right-justified clock, right edge 248.
  unsigned titleWhite=0;
  for(int y=1;y<19;++y) for(int x=21;x<155;++x) titleWhite+=!c.getPixel(x,y);
  assert(titleWhite>80);
}
template<typename View>
void assertBatteryIgnored(View view, void (*render)(Adafruit_GFX&, const View&), const char* clock) {
  // Real rotated GFX native planes, including controller padding, must ignore
  // the compatibility field for every screen/state exercised below.
  GFXcanvas1 c(128,250); c.setRotation(3);
  view.batteryPercent=-1; render(c,view);
  const std::string baseline(reinterpret_cast<const char*>(c.getBuffer()),4000);
  for(int percent : {-1,0,1,50,76,100,101}) {
    view.batteryPercent=percent; render(c,view);
    assertHeader(c,clock);
    if(baseline!=std::string(reinterpret_cast<const char*>(c.getBuffer()),4000)) {
      std::cerr << "Battery changed native frame for compatibility value " << percent << '\n';
      assert(false);
    }
    for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  }
}
int main(int argc, char** argv) {
  assert(argc == 2);
  GFXcanvas1 c(128,250); c.setRotation(3);
  busUI::View v{"Gare Saint-Jean", "L1", "Hotel de Ville", "09:30", "3", {"12 min", "18 min"}, "Temps reel", "Maj 09:30", 76};
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v);
  assertHeader(c);
  assert(c.getPixel(0,0) == 1 && c.getPixel(249,19) == 1);
  assert(c.getPixel(165,30) == 1);
  assert(c.getPixel(5,82) == 1 && c.getPixel(191,108) == 1);
  for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y) == 0); // Controller padding.
  save(c,(std::string(argv[1])+"/bus-ui-reference.pbm").c_str());
  auto first = c.getPixel(100,0);
  v.stop = std::string(64,'W'); v.destination = std::string(64,'W'); v.line = "123456789";
  v.primary = "999"; v.next[0] = "99999 min";
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v);
  assert(first == c.getPixel(100,0));
  save(c,(std::string(argv[1])+"/bus-ui-long-labels.pbm").c_str());
  v.primary = "--"; v.next[0] = "--"; v.next[1] = "--"; v.status = "Indisponible"; v.footer = "Horloge flux avancee";
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v);
  save(c,(std::string(argv[1])+"/bus-ui-offline.pbm").c_str());
  busUI::ScheduleView schedule{"09:30", "L1", "Centre Ville", {"3 min", "12 min", "18 min", "26 min", "10:05"}, "Temps reel - Maj 09:28", 1, 76};
  assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule);
  assert(c.getPixel(0,0)==1 && c.getPixel(4,39)==1);
  for(unsigned row=0;row<5;++row) {
    const int y=23+17*row;
    assertFittedText(c,schedule.line.c_str(),7,y+3,23,9,nullptr,true);
    assertFittedText(c,schedule.destination.c_str(),39,y+2,145,11,nullptr);
    assertFittedText(c,schedule.times[row].c_str(),196,y+2,49,11,nullptr);
    // No row bus: compact left-hand line badge, then a clean gap.
    for(int py=y+1;py<y+14;++py) for(int x=33;x<39;++x) assert(c.getPixel(x,py)==0);
  }
  save(c,(std::string(argv[1])+"/bus-ui-schedule.pbm").c_str());
  busUI::ConfigView config{"09:30", {"Gare Saint-Jean", "L1", "60 s", "Connecte", "Francais"}, 0, "Haut/Bas  OK  Retour", 76};
  assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config);
  assertHeader(c);
  for(int y=112;y<122;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  assert(c.getPixel(249,19)==1 && c.getPixel(2,24)==1);
  unsigned titleWhite=0;
  for(int y=1;y<19;++y) for(int x=21;x<198;++x) if(!c.getPixel(x,y)) ++titleWhite;
  assert(titleWhite>120); // Prevent silently missing the title due to font descenders.
  save(c,(std::string(argv[1])+"/bus-ui-config.pbm").c_str());
  config.selected=3; config.values[3]="Deconnecte";
  assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config);
  save(c,(std::string(argv[1])+"/bus-ui-config-wifi.pbm").c_str());
  for(int percentage : {-1,0,1,50,76,100,101}) {
    config.batteryPercent=percentage; assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config);
    assertHeader(c);
    for(int y=113;y<122;++y) for(int x=198;x<250;++x) assert(c.getPixel(x,y)==0);
    save(c,(std::string(argv[1])+"/bus-ui-battery-"+std::to_string(percentage)+".pbm").c_str());
  }
  const char* error="Erreur reseau : donnees indisponibles";
  v.footer=error; assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v); assertHeader(c);
  assertText(c,error,4,114,242,8);
  schedule.footer="Temps reel - Maj 09:28"; assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule); assertHeader(c);
  assertText(c,schedule.footer.c_str(),4,113,150,8);
  schedule.footer="Horloge flux avancee - indisponible";
  assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule);
  assertText(c,schedule.footer.c_str(),4,113,210,8);
  assertText(c,"1/3",132,6,23,8,true);
  save(c,(std::string(argv[1])+"/bus-ui-schedule-error.pbm").c_str());
  config.footer=error; assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config); assertHeader(c);
  assertText(c,error,4,113,242,8);
  config.footer="Haut/Bas OK Retour"; assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config);
  for(int y=112;y<122;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  config.footer="Haut/Bas  OK  Retour   Champs invalides"; assertBatteryIgnored(config,busUI::renderConfig,config.clock.c_str()); busUI::renderConfig(c,config);
  assertText(c,"Champs invalides",4,113,242,8);
  save(c,(std::string(argv[1])+"/bus-ui-config-error.pbm").c_str());
  busUI::WiFiView wifi{"09:30", "192.168.1.42", 0, false, 76};
  assertBatteryIgnored(wifi,busUI::renderWiFi,wifi.clock.c_str()); busUI::renderWiFi(c,wifi); assertHeader(c);
  assertText(c,"Reconnecter",12,29,210,8,true);
  assertText(c,"Portail captif",12,41,210,8,true);
  assertText(c,"Adresse IP",12,56,210,8);
  assertText(c,"Retour",12,83,210,8);
  save(c,(std::string(argv[1])+"/bus-ui-wifi-menu.pbm").c_str());
  for(unsigned selected=1;selected<3;++selected) {
    wifi.selected=selected; assertBatteryIgnored(wifi,busUI::renderWiFi,wifi.clock.c_str()); busUI::renderWiFi(c,wifi); assertHeader(c);
    assertText(c,selected==1 ? "Adresse IP" : "Retour",12,29+int(selected)*27,210,8,true);
    save(c,(std::string(argv[1])+"/bus-ui-wifi-selected-"+std::to_string(selected)+".pbm").c_str());
  }
  wifi.showIp=true; assertBatteryIgnored(wifi,busUI::renderWiFi,wifi.clock.c_str()); busUI::renderWiFi(c,wifi); assertHeader(c);
  assertText(c,"Connexion Wi-Fi",8,31,234,8);
  GFXcanvas1 ipExpected(250,128); ipExpected.setTextColor(1); ipExpected.setFont(&FreeSansBold9pt7b);
  int16_t bx,by; uint16_t w,h;
  ipExpected.getTextBounds(wifi.ip.c_str(),0,0,&bx,&by,&w,&h);
  ipExpected.setCursor(8-bx,45+(24-h)/2-by); ipExpected.print(wifi.ip.c_str());
  for(int y=45;y<69;++y) for(int x=8;x<242;++x) assert(c.getPixel(x,y)==ipExpected.getPixel(x,y));
  assertText(c,"Retour",8,102,226,8,true);
  save(c,(std::string(argv[1])+"/bus-ui-wifi-ip.pbm").c_str());
  wifi.ip=""; assertBatteryIgnored(wifi,busUI::renderWiFi,wifi.clock.c_str()); busUI::renderWiFi(c,wifi); assertHeader(c);
  assertText(c,"Aucune adresse attribuee",8,73,234,8);
  assertText(c,"Reconnecter via le portail",8,85,234,8);
  assertText(c,"Retour",8,102,226,8,true);
  save(c,(std::string(argv[1])+"/bus-ui-wifi-offline.pbm").c_str());
  wifi.ip="0.0.0.0"; assertBatteryIgnored(wifi,busUI::renderWiFi,wifi.clock.c_str()); busUI::renderWiFi(c,wifi);
  assertText(c,"Aucune adresse attribuee",8,73,234,8);
  for(int y=117;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  busUI::BootView boot{0, "Connexion au reseau", false, -1};
  assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot);
  assertHeader(c,"--:--");
  GFXcanvas1 bootTitle(250,20); bootTitle.fillScreen(1);
  bootTitle.setFont(&FreeSansBold9pt7b); bootTitle.setTextColor(0);
  bootTitle.getTextBounds("BusNotifier",0,0,&bx,&by,&w,&h);
  assert(w<=160 && h<=18);
  bootTitle.setCursor(21-bx,1+(18-h)/2-by); bootTitle.print("BusNotifier");
  for(int y=1;y<19;++y) for(int x=21;x<181;++x)
    assert(c.getPixel(x,y)==bootTitle.getPixel(x,y));
  assertBatteryHidden(c);
  assertText(c,"Wi-Fi",29,61,87,8);
  assertText(c,"Heure",29,77,87,8);
  assertText(c,"SETRAM",29,93,87,8);
  assertText(c,"Connexion",126,61,116,8);
  assertText(c,"En attente",126,77,116,8);
  assertText(c,"En attente",126,93,116,8);
  assertText(c,boot.detail.c_str(),8,109,234,8);
  save(c,(std::string(argv[1])+"/bus-ui-boot-wifi.pbm").c_str());
  const char* stepNames[]={"wifi", "time", "api", "ready"};
  const char* stepDetails[]={"Reseau : Maison", "Serveur NTP", "Premier flux SETRAM", "Pret"};
  const char* complete[]={"Connecte", "Synchronisee", "Flux recu"};
  const char* active[]={"Connexion", "Synchronisation", "Lecture du flux"};
  const int stations[]={24,92,159,226};
  const int segmentSamples[]={41,111,177};
  for(unsigned stage=0;stage<4;++stage) {
    boot.stage=stage; boot.detail=stepDetails[stage];
    assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot); assertHeader(c,"--:--");
    for(unsigned row=0;row<3;++row) {
      assertText(c,row<stage ? complete[row] : row==stage ? active[row] : "En attente",
                 126,61+16*row,116,8);
      assert(c.getPixel(segmentSamples[row],50)==(row<stage));
    }
    for(unsigned node=0;node<4;++node) assert(c.getPixel(stations[node],50)==(node<=stage));
    assert(c.getPixel(stations[stage]-6,30)==1); // Bus follows real events, no timer/percentage.
    for(int y=118;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
    save(c,(std::string(argv[1])+"/bus-ui-boot-"+stepNames[stage]+".pbm").c_str());
  }
  for(int percent : {-1,0,76,100,101}) {
    boot.batteryPercent=percent; assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot);
    assertHeader(c,"--:--");
    // Identical state produces identical native planes; there is no fake animation.
    std::string rendered(reinterpret_cast<const char*>(c.getBuffer()),4000);
    assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot);
    assert(rendered==std::string(reinterpret_cast<const char*>(c.getBuffer()),4000));
  }
  boot.batteryPercent=-1;
  for(unsigned stage=0;stage<3;++stage) {
    boot.stage=stage; boot.failed=true; boot.detail=stage==0 ? "Wi-Fi indisponible" : stage==1 ? "NTP : delai depasse" : "HTTP 503 : service indisponible";
    assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot); assertHeader(c,"--:--");
    for(unsigned row=0;row<3;++row)
      assertText(c,row<stage ? complete[row] : row==stage ? "Echec" : "En attente",
                 126,61+16*row,116,8);
    assertText(c,boot.detail.c_str(),8,109,234,8);
    assert(c.getPixel(stations[stage]-3,47)==1); // Failed station is a cross, not a successful dot.
    assert(c.getPixel(stations[stage],47)==0);
    save(c,(std::string(argv[1])+"/bus-ui-boot-error-"+stepNames[stage]+".pbm").c_str());
  }
  boot.detail=std::string(120,'W'); assertBatteryIgnored(boot,busUI::renderBoot,"--:--"); busUI::renderBoot(c,boot);
  assertText(c,(std::string(37,'W')+"..").c_str(),8,109,234,8);
  for(int y=118;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  save(c,(std::string(argv[1])+"/bus-ui-boot-error-long.pbm").c_str());
  // Public deterministic timetable fixture, never a claim about a live stop.
  busUI::TomorrowView tomorrow{"Gare Saint-Jean", "L1", "Hotel de Ville", "23:30",
    {"06:12", "06:42", "07:12"}, "Erreur reseau : donnees indisponibles", 76};
  assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow);
  assertHeader(c,"23:30");
  assertBatteryHidden(c);
  assertFittedText(c,"Demain",4,55,104,18,&FreeSansBold9pt7b);
  assertFittedText(c,"Horaires prevus",112,55,134,18,nullptr);
  assertFittedText(c,"06:12",4,77,112,29,&FreeSansBold18pt7b);
  assertFittedText(c,"06:42",141,77,105,15,&FreeSansBold9pt7b);
  assertFittedText(c,"07:12",141,94,105,15,&FreeSansBold9pt7b);
  assertText(c,tomorrow.footer.c_str(),4,114,242,8);
  // No minute/realtime label under the primary scheduled time.
  for(int y=107;y<113;++y) for(int x=4;x<116;++x) assert(c.getPixel(x,y)==0);
  save(c,(std::string(argv[1])+"/bus-ui-tomorrow.pbm").c_str());
  tomorrow.times[1]=""; tomorrow.times[2]="";
  tomorrow.footer="Haut/Bas OK Retour   HTTP 503 : service indisponible";
  assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow);
  assertFittedText(c,"06:12",4,77,112,29,&FreeSansBold18pt7b);
  assertFittedText(c,"--:--",141,77,105,15,&FreeSansBold9pt7b);
  assertFittedText(c,"--:--",141,94,105,15,&FreeSansBold9pt7b);
  assertText(c,"HTTP 503 : service indisponible",4,114,242,8);
  save(c,(std::string(argv[1])+"/bus-ui-tomorrow-no-extra.pbm").c_str());
  tomorrow.batteryPercent=-1; tomorrow.clock="--:--";
  tomorrow.times[0]=""; tomorrow.footer="Horaires indisponibles";
  assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow); assertHeader(c,"--:--");
  assertBatteryHidden(c);
  assertFittedText(c,"--:--",4,77,112,29,&FreeSansBold18pt7b);
  assertFittedText(c,"--:--",141,77,105,15,&FreeSansBold9pt7b);
  assertFittedText(c,"--:--",141,94,105,15,&FreeSansBold9pt7b);
  assertFittedText(c,"Demain",4,55,104,18,&FreeSansBold9pt7b);
  assertFittedText(c,"Horaires prevus",112,55,134,18,nullptr);
  save(c,(std::string(argv[1])+"/bus-ui-tomorrow-unknown.pbm").c_str());
  tomorrow.stop=std::string(120,'W'); tomorrow.destination=std::string(120,'W');
  tomorrow.line="123456789"; tomorrow.clock="23:30"; tomorrow.batteryPercent=100;
  tomorrow.times[0]="23:59"; tomorrow.times[1]="23:59"; tomorrow.times[2]="23:59";
  tomorrow.footer=std::string(120,'W');
  assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow); assertHeader(c,"23:30");
  assertFittedText(c,(std::string(24,'W')+"..").c_str(),21,1,160,18,nullptr,true);
  assertBatteryHidden(c);
  // Derive expected ellipsis from glyph width, then compare the whole reserved
  // lane. This catches erased text and clipping/ink bleeding, not just presence.
  GFXcanvas1 measure(250,128); measure.setFont(&FreeSansBold9pt7b);
  std::string longDestination;
  for(unsigned count=1;count<64;++count) {
    std::string candidate=std::string(count,'W')+"..";
    measure.getTextBounds(candidate.c_str(),0,0,&bx,&by,&w,&h);
    if(w>184) break;
    longDestination=candidate;
  }
  assertFittedText(c,longDestination.c_str(),62,25,184,25,&FreeSansBold9pt7b);
  assertFittedText(c,"Demain",4,55,104,18,&FreeSansBold9pt7b);
  assertFittedText(c,"Horaires prevus",112,55,134,18,nullptr);
  assertFittedText(c,"23:59",4,77,112,29,&FreeSansBold18pt7b);
  assertFittedText(c,"23:59",141,77,105,15,&FreeSansBold9pt7b);
  assertFittedText(c,"23:59",141,94,105,15,&FreeSansBold9pt7b);
  assertText(c,(std::string(38,'W')+"..").c_str(),4,114,242,8);
  for(int y=25;y<51;++y) for(int x=57;x<62;++x) assert(c.getPixel(x,y)==0);
  for(int y=51;y<55;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  save(c,(std::string(argv[1])+"/bus-ui-tomorrow-long-labels.pbm").c_str());
  std::string tomorrowPlane(reinterpret_cast<const char*>(c.getBuffer()),4000);
  assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow);
  assert(tomorrowPlane==std::string(reinterpret_cast<const char*>(c.getBuffer()),4000));
  // Existing full schedule also supports HH:MM with an explicit planned footer,
  // retaining its error and paging indicator rather than replacing it with hints.
  schedule.footer="Demain - Horaires prevus / HTTP 503";
  for(auto& time : schedule.times) time="06:12";
  assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule); assertHeader(c);
  assertText(c,schedule.footer.c_str(),4,113,210,8);
  for(unsigned row=0;row<5;++row) assertText(c,"06:12",196,26+17*row,49,8);
  assertText(c,"1/3",132,6,23,8,true);
  save(c,(std::string(argv[1])+"/bus-ui-schedule-tomorrow.pbm").c_str());
  for(const char* notice : {"Demain prevu - Flux avance", "Demain - Horaires prevus"}) {
    schedule.footer=notice;
    for(unsigned page=1;page<=3;++page) {
      schedule.page=page;
      assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule); assertHeader(c);
      assertText(c,"Demain prevu",21,6,106,8,true);
      assertText(c,(std::to_string(page)+"/3").c_str(),132,6,23,8,true);
      assertBatteryHidden(c);
      for(int y=112;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
      save(c,(std::string(argv[1])+"/bus-ui-schedule-clean-"+std::to_string(page)+".pbm").c_str());
    }
    tomorrow.footer=notice;
    assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow);
    for(int y=112;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
    assertFittedText(c,"Demain",4,55,104,18,&FreeSansBold9pt7b);
    assertFittedText(c,"Horaires prevus",112,55,134,18,nullptr);
    save(c,(std::string(argv[1])+"/bus-ui-tomorrow-clean.pbm").c_str());
  }
  busUI::ConfigView six{"09:30",{"Gare","L1","60 s","Connecte","Francais","Demarrer","1 puis 2"},0,"",76};
  assert(sizeof(six.values)/sizeof(six.values[0])==7); // Seven choices with conditional dual order.
  const char* titles[]={"Arret favori","Ligne suivie","Rafraichir","Wi-Fi","Langue","Mise a jour OTA","Inverser suivis"};
  for(unsigned selected=0;selected<7;++selected) {
    six.selected=selected; assertBatteryIgnored(six,busUI::renderConfig,six.clock.c_str()); busUI::renderConfig(c,six); assertHeader(c);
    const unsigned first=selected>=5 ? selected-4 : 0;
    for(unsigned row=0;row<5;++row) {
      const unsigned i=first+row;
      assertText(c,titles[i],23,26+17*row,95,8,i==selected);
      assertText(c,six.values[i].c_str(),123,26+17*row,112,8,i==selected);
    }
    for(int y=112;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
    save(c,(std::string(argv[1])+"/bus-ui-config-six-"+std::to_string(selected)+".pbm").c_str());
  }
  v.primary="J+1 06:12 P"; v.next[0]="J+1 06:42 P"; v.next[1]="J+2 07:12 P";
  v.status="Prevu"; v.footer="P=prevu R=reel";
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v); assertHeader(c);
  measure.setFont(&FreeSansBold9pt7b); measure.getTextBounds("06:12",0,0,&bx,&by,&w,&h);
  assertFittedText(c,"06:12",171+(76-int(w))/2,25,76,39,&FreeSansBold9pt7b);
  assertText(c,"J+1 prevu",171,67,76,8);
  assertFittedText(c,"J+1 06:42 P",112,86,76,12,nullptr);
  assertFittedText(c,"J+2 07:12 P",112,101,76,12,nullptr);
  for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  save(c,(std::string(argv[1])+"/bus-ui-mixed-planned.pbm").c_str());
  v.primary="6"; v.next[0]="J+1 06:42 R";
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v); assertHeader(c);
  assertFittedText(c,"J+1 06:42 R",112,86,76,12,nullptr);
  save(c,(std::string(argv[1])+"/bus-ui-mixed-live.pbm").c_str());
  schedule.footer="Flux avance / P=prevu";
  schedule.times[0]="6 min"; schedule.times[1]="J+1 06:12 P"; schedule.times[2]="J+2 07:12 R";
  assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule); assertHeader(c);
  assertText(c,"6 min",196,26,49,8);
  assertText(c,"J+1 06:12 P",154,43,91,8);
  assertText(c,"J+2 07:12 R",154,60,91,8);
  assertText(c,"P=prevu",4,113,242,8);
  save(c,(std::string(argv[1])+"/bus-ui-schedule-mixed.pbm").c_str());
  // Hide the tolerated producer-clock notice only in the bottom lane.
  // Source legends, true errors, expiry and no-service information survive.
  const std::pair<const char*,const char*> footers[]={
    {"Flux avance",""},
    {"Flux avance / P=prevu R=reel","P=prevu R=reel"},
    {"Flux avance - Plan expire","Plan expire"},
    {"Flux avance - Aucun passage","Aucun passage"},
    {"Demain: aucun horaire / Flux avance","Demain: aucun horaire"},
    {"Flux avance / J+1 J+2 sans horaire","J+1 J+2 sans horaire"},
    {"Horloge flux avancee - indisponible","Horloge flux avancee - indisponible"},
    {"HTTP 503 : service indisponible","HTTP 503 : service indisponible"}
  };
  v.status="Flux avance";
  tomorrow.clock="09:30"; tomorrow.batteryPercent=76;
  for(const auto& footer:footers) {
    v.footer=footer.first; assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v); assertHeader(c);
    assertText(c,"Flux avance",66,65,89,8);
    if(*footer.second) assertText(c,footer.second,4,114,242,8);
    else for(int y=114;y<122;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
    schedule.footer=footer.first; assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule); assertHeader(c);
    if(*footer.second) assertText(c,footer.second,4,113,242,8);
    else for(int y=112;y<122;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
    tomorrow.footer=footer.first; assertBatteryIgnored(tomorrow,busUI::renderTomorrow,tomorrow.clock.c_str()); busUI::renderTomorrow(c,tomorrow); assertHeader(c);
    if(*footer.second) assertText(c,footer.second,4,114,242,8);
    else for(int y=114;y<122;++y) for(int x=0;x<250;++x) assert(c.getPixel(x,y)==0);
  }
  schedule.footer="Flux avance / P=prevu R=reel";
  assertBatteryIgnored(schedule,busUI::renderSchedule,schedule.clock.c_str()); busUI::renderSchedule(c,schedule);
  save(c,(std::string(argv[1])+"/bus-ui-schedule-no-flux.pbm").c_str());
  v.footer="Flux avance / P=prevu R=reel";
  assertBatteryIgnored(v,busUI::render,v.clock.c_str()); busUI::render(c,v);
  save(c,(std::string(argv[1])+"/bus-ui-home-no-flux.pbm").c_str());
  // Default state is disconnected. Signal updates belong to main, not models.
  v={"Gare Saint-Jean","L1","Hotel de Ville","09:30","3",{"12 min","18 min"},"Temps reel","Maj 09:30",76};
  busUI::render(c,v);
  assert(c.getPixel(206,17)==0); // White disconnected slash end.
  const std::string disconnected(reinterpret_cast<const char*>(c.getBuffer()),4000);
  unsigned previousInk=0;
  for(int bars=0;bars<=3;++bars) {
    busUI::setWiFiSignal(bars); busUI::render(c,v); assertHeader(c);
    assert(c.getPixel(198,15)==0); // Connected dot, even when strength is zero.
    assert(c.getPixel(198,11)==(bars>=1 ? 0 : 1));
    assert(c.getPixel(198,7)==(bars>=2 ? 0 : 1));
    assert(c.getPixel(198,4)==(bars>=3 ? 0 : 1));
    assert(c.getPixel(206,17)==1); // Never a disconnected slash when connected.
    unsigned ink=0;
    for(int y=0;y<20;++y) for(int x=190;x<208;++x) ink+=!c.getPixel(x,y);
    assert(ink>previousInk); previousInk=ink;
    // Signal-only updates leave every title/body/clock/padding pixel unchanged.
    GFXcanvas1 baseline(128,250); baseline.setRotation(3);
    busUI::setWiFiSignal(-1); busUI::render(baseline,v);
    for(int y=0;y<128;++y) for(int x=0;x<250;++x)
      if(x<190 || x>=208 || y>=20) assert(c.getPixel(x,y)==baseline.getPixel(x,y));
    // Each arc spreads horizontally; this is a fan, never vertical strength bars.
    if(bars==3) assert(c.getPixel(190,7)==0 && c.getPixel(207,7)==0);
    busUI::setWiFiSignal(bars);
    save(c,(std::string(argv[1])+"/bus-ui-signal-"+std::to_string(bars)+".pbm").c_str());
  }
  for(int invalid : {-999,-2,-1,4,999}) {
    busUI::setWiFiSignal(invalid); busUI::render(c,v);
    assert(disconnected==std::string(reinterpret_cast<const char*>(c.getBuffer()),4000));
  }
  save(c,(std::string(argv[1])+"/bus-ui-signal-disconnected.pbm").c_str());
  // Public fixture credentials only: never export a real runtime OTA password.
  busUI::OtaView ota{"09:30","bus-fixture.local","192.168.1.42","demo-OTA-42","Port OTA disponible",1};
  busUI::renderOta(c,ota); assertHeader(c);
  assertFittedText(c,"Mode OTA",29,25,70,16,nullptr);
  assertFittedText(c,"En attente",111,25,130,16,nullptr,true);
  assertFittedText(c,"Hote",8,48,54,10,nullptr);
  assertFittedText(c,"IP",8,62,54,10,nullptr);
  assertFittedText(c,"Cle temp.",8,76,54,10,nullptr);
  assertFittedText(c,ota.hostname.c_str(),70,48,172,10,nullptr);
  assertFittedText(c,ota.ip.c_str(),70,62,172,10,nullptr);
  assertFittedText(c,ota.password.c_str(),70,76,172,10,nullptr);
  assertFittedText(c,ota.detail.c_str(),8,94,234,10,nullptr);
  assertFittedText(c,"MENU + EXIT 2 s : annuler",4,112,242,10,nullptr);
  std::string otaReady(reinterpret_cast<const char*>(c.getBuffer()),4000);
  busUI::renderOta(c,ota);
  assert(otaReady==std::string(reinterpret_cast<const char*>(c.getBuffer()),4000));
  save(c,(std::string(argv[1])+"/bus-ui-ota-ready.pbm").c_str());
  const char* otaStatus[]={"Preparation","En attente","Transfert","Termine","Erreur"};
  const char* otaNames[]={"preparing","ready","transfer","success","error"};
  const char* otaDetail[]={"Attente ecran / reseau","Port OTA disponible","Reception du firmware","Redemarrage imminent","OTA : authentification refusee"};
  for(unsigned stage=0;stage<5;++stage) {
    ota.stage=stage; ota.detail=otaDetail[stage]; busUI::renderOta(c,ota); assertHeader(c);
    assertFittedText(c,otaStatus[stage],111,25,130,16,nullptr,true);
    assertFittedText(c,stage==2 ? "Transfert : ne pas eteindre" : "MENU + EXIT 2 s : annuler",4,112,242,10,nullptr);
    assertFittedText(c,ota.hostname.c_str(),70,48,172,10,nullptr);
    assertFittedText(c,ota.ip.c_str(),70,62,172,10,nullptr);
    assertFittedText(c,ota.password.c_str(),70,76,172,10,nullptr);
    assertFittedText(c,ota.detail.c_str(),8,94,234,10,nullptr);
    std::string plane(reinterpret_cast<const char*>(c.getBuffer()),4000);
    busUI::renderOta(c,ota);
    assert(plane==std::string(reinterpret_cast<const char*>(c.getBuffer()),4000));
    save(c,(std::string(argv[1])+"/bus-ui-ota-"+otaNames[stage]+".pbm").c_str());
  }
  ota.stage=99; busUI::renderOta(c,ota);
  assertFittedText(c,"Erreur",111,25,130,16,nullptr,true);
  ota.hostname=std::string(120,'W'); ota.ip=ota.hostname; ota.password=ota.hostname; ota.detail=ota.hostname;
  busUI::renderOta(c,ota); assertHeader(c);
  for(int top : {48,62,76})
    assertFittedText(c,(std::string(26,'W')+"..").c_str(),70,top,172,10,nullptr);
  assertFittedText(c,(std::string(37,'W')+"..").c_str(),8,94,234,10,nullptr);
  save(c,(std::string(argv[1])+"/bus-ui-ota-error-long.pbm").c_str());
  // Clock alignment is by measured glyph bounds, including shorter clocks.
  for(const char* clock : {"9:30","--:--",""}) {
    ota.clock=clock; busUI::renderOta(c,ota); assertHeader(c,clock);
  }
  busUI::DualView dual{"Gare", "09:30", "Temps reel", "", {{"L1","Centre","4","12",true,""},{"L2","Universite","9","18",true,""}}, -1};
  busUI::renderDual(c,dual);
  assertFittedText(c,"Mes lignes favorites",21,1,160,18,nullptr,true);
  assertHeader(c);
  save(c,(std::string(argv[1])+"/bus-ui-dual.pbm").c_str());
  // Labels must survive actual font bounds, not merely exist in the view model.
  for(int row=0; row<2; ++row) {
    const int y=row ? 68 : 23;
    int badgeWhite=0,destinationInk=0;
    for(int yy=y+8; yy<y+28; ++yy) for(int xx=10; xx<34; ++xx) badgeWhite+=!c.getPixel(xx,yy);
    for(int yy=y+1; yy<y+24; ++yy) for(int xx=45; xx<146; ++xx) destinationInk+=c.getPixel(xx,yy);
    assert(badgeWhite>8 && "dual line badge text must be visible");
    assert(destinationInk>12 && "dual destination must be visible");
  }
  // Measure the actual fitted glyphs: badge and time text share fixed centres,
  // regardless of one/two digits or a wider route label.
  for(const char* label : {"1","11","C4"}) {
    for(const char* minutes : {"4","13","53","--"}) {
      for(unsigned row=0; row<2; ++row) {
        dual.rows[row].line=label;
        dual.rows[row].primary=minutes;
        dual.rows[row].next="54 min";
      }
      busUI::renderDual(c,dual);
      for(unsigned row=0; row<2; ++row) {
        const int y=row ? 68 : 23;
        struct Box { const char* text; int x,top,width,height; const GFXfont* font; bool white; };
        const Box boxes[]={{label,8,y+8,28,20,&FreeSansBold9pt7b,true},
                          {minutes,151,y+1,std::string(minutes)=="--" ? 92 : 68,27,&FreeSansBold18pt7b,false},
                          {"54 min",151,y+29,92,10,nullptr,false}};
        for(const auto& box : boxes) {
          c.setFont(box.font); c.setTextSize(1);
          int16_t bx,by; uint16_t w,h;
          c.getTextBounds(box.text,0,0,&bx,&by,&w,&h);
          const int offset=(box.width-w)/2;
          // Check margins as well as the glyphs, so left alignment cannot pass.
          GFXcanvas1 expected(250,128); expected.fillScreen(box.white ? 1 : 0);
          expected.setFont(box.font); expected.setTextColor(box.white ? 0 : 1);
          expected.setCursor(box.x+offset-bx,box.top+(box.height-h)/2-by);
          expected.print(box.text);
          for(int py=box.top;py<box.top+box.height;++py)
            for(int px=box.x;px<box.x+box.width;++px)
              assert(c.getPixel(px,py)==expected.getPixel(px,py) && "dual glyph must be centred in its lane");
        }
        // Exact unit pixels plus blank margins prevent an absent or oversized unit.
        assertFittedText(c,std::string(minutes)=="--" ? "" : "min",223,y+17,20,10,nullptr);
      }
    }
  }
  // Future absolute times keep the complete day/time/source label, never min.
  for(const char* planned : {"J+1 06:12 P","J+2 17:42 R"}) {
    for(auto& row : dual.rows) { row.primary=planned; row.next="J+2 18:12 P"; }
    busUI::renderDual(c,dual);
    for(int y : {23,68}) {
      assertFittedText(c,planned,151,y+1,92,27,nullptr,false,true);
      assertFittedText(c,"J+2 18:12 P",151,y+29,92,10,nullptr,false,true);
    }
    save(c,(std::string(argv[1])+"/bus-ui-dual-future-"+planned[2]+".pbm").c_str());
  }
  for(auto& row : dual.rows) { row.primary=""; row.next=""; }
  busUI::renderDual(c,dual);
  for(int y : {23,68}) {
    assertFittedText(c,"--",151,y+1,92,27,&FreeSansBold18pt7b,false,true);
    assertFittedText(c,"--",151,y+29,92,10,nullptr,false,true);
  }
  save(c,(std::string(argv[1])+"/bus-ui-dual-empty.pbm").c_str());
  dual.rows[0]={"11","MAIRIE LE MANS","13","54 min",true,""};
  dual.rows[1]={"C4","BELLEVUE","53","68 min",true,""};
  busUI::renderDual(c,dual);
  save(c,(std::string(argv[1])+"/bus-ui-dual-aligned.pbm").c_str());
  dual.rows[0].warning="API indisponible";
  dual.rows[1].warning="Horaires expires";
  busUI::renderDual(c,dual);
  assertFittedText(c,"API indisponible",45,46,101,9,nullptr);
  assertFittedText(c,"Horaires expires",45,91,101,9,nullptr);
  save(c,(std::string(argv[1])+"/bus-ui-dual-warnings.pbm").c_str());
  busUI::View single{"Gare","L1","Centre","09:30","4",{"12","18"},"Temps reel","",-1};
  busUI::render(c,single);
  assertFittedText(c,"L1 - Gare",21,1,160,18,&FreeSansBold9pt7b,true);
  assertHeader(c);
  save(c,(std::string(argv[1])+"/bus-ui-single.pbm").c_str());
  single.stop="Centre"; busUI::render(c,single);
  assertFittedText(c,"L1 - Centre",21,1,160,18,&FreeSansBold9pt7b,true);
  single.line="C4"; busUI::render(c,single);
  assertFittedText(c,"C4 - Centre",21,1,160,18,&FreeSansBold9pt7b,true);
  assertHeader(c);
  save(c,(std::string(argv[1])+"/bus-ui-single-line-stop.pbm").c_str());
  std::swap(dual.rows[0],dual.rows[1]); busUI::renderDual(c,dual); save(c,(std::string(argv[1])+"/bus-ui-reversed.pbm").c_str());
  std::cout << "Bus UI framebuffer: OK\n";
}
