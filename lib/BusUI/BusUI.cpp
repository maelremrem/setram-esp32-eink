#include "BusUI.h"
#include <DisplayPolicy.h>
#include <cctype>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
namespace busUI {
namespace {
constexpr uint16_t BLACK=1, WHITE=0;
int wifiSignal=-1; // Single main-owned renderer state, never changed by render.
// Fonts are measured before placement, never clipped into a neighbouring box.
void fitted(Adafruit_GFX& g, const std::string& value, int x, int top, int width,
            int height, const GFXfont* font=nullptr, uint16_t color=BLACK, bool right=false,
            bool center=false) {
  g.setFont(font); g.setTextSize(1); g.setTextColor(color); g.setTextWrap(false);
  std::string text=displayPolicy::label(value, 64);
  int16_t bx,by; uint16_t w,h;
  g.getTextBounds(text.c_str(),0,0,&bx,&by,&w,&h);
  if(w>width) {
    while(!text.empty()) {
      text.pop_back();
      std::string shortened=text+"..";
      g.getTextBounds(shortened.c_str(),0,0,&bx,&by,&w,&h);
      if(w<=width) { text=shortened; break; }
    }
  }
  if(text.empty()) return;
  g.getTextBounds(text.c_str(),0,0,&bx,&by,&w,&h);
  if(h>height) return;
  const int offset=right ? width-int(w) : center ? (width-int(w))/2 : 0;
  g.setCursor(x+offset-bx,top+(height-h)/2-by); g.print(text.c_str());
}
std::string statusFooter(std::string value) {
  // Suppress legacy controls even if an old caller still supplies them. Keep
  // notices/errors intact, including text surrounding the control sequence.
  size_t at=value.find("Haut/Bas");
  while(at!=std::string::npos) {
    size_t end=at+8;
    for(const char* token : {"OK", "Retour"}) {
      size_t before=end;
      while(end<value.size() && std::isspace(static_cast<unsigned char>(value[end]))) ++end;
      std::string tokenText(token);
      if(end==before || value.compare(end,tokenText.size(),tokenText)!=0) { end=at; break; }
      end+=tokenText.size();
    }
    if(end>at) value.erase(at,end-at);
    else ++at;
    at=value.find("Haut/Bas",at);
  }
  size_t first=value.find_first_not_of(" \t\r\n"), last=value.find_last_not_of(" \t\r\n");
  return first==std::string::npos ? "" : value.substr(first,last-first+1);
}
std::string visibleFooter(const std::string& value) {
  const auto text=statusFooter(value);
  const std::string notice="Flux avance";
  if(text==notice) return "";
  // Only the tolerated notice is hidden in the footer, not the rejected-clock
  // error, source legend, calendar expiry or other adjacent information.
  for(const char* separator : {" / "," - "}) {
    const std::string prefix=notice+separator, suffix=std::string(separator)+notice;
    if(text.compare(0,prefix.size(),prefix)==0) return text.substr(prefix.size());
    if(text.size()>=suffix.size() && text.compare(text.size()-suffix.size(),suffix.size(),suffix)==0)
      return text.substr(0,text.size()-suffix.size());
  }
  return text;
}
std::string visiblePlannedFooter(const std::string& value) {
  const auto text=statusFooter(value);
  // The planned label remains in the content/header. Remove only redundant
  // normal-state sentences, never errors, expiry or missing-service notices.
  if(text=="Demain prevu - Flux avance" || text=="Demain - Horaires prevus") return "";
  return visibleFooter(text);
}
void bus(Adafruit_GFX& g,int x,int y,uint16_t color,int scale=1) {
  g.fillRoundRect(x,y,12*scale,13*scale,2*scale,color);
  g.fillRect(x+2*scale,y+3*scale,8*scale,5*scale,!color);
  g.fillRect(x+3*scale,y+scale,6*scale,scale,!color);
  g.fillCircle(x+3*scale,y+10*scale,scale,!color);
  g.fillCircle(x+9*scale,y+10*scale,scale,!color);
  g.fillRect(x+scale,y+12*scale,2*scale,3*scale,color);
  g.fillRect(x+9*scale,y+12*scale,2*scale,3*scale,color);
}
// Compact fan: rounded polyline arcs widen above a single connection dot.
// Every primitive stays in x190..207, with clean gaps around the clock/title.
void wifiFan(Adafruit_GFX& g) {
  const int arcs=wifiSignal<0 ? 3 : wifiSignal;
  g.fillCircle(198,15,1,WHITE);
  if(arcs>=1) {
    g.drawLine(196,12,198,11,WHITE); g.drawLine(198,11,200,12,WHITE);
    g.drawLine(196,13,198,12,WHITE); g.drawLine(198,12,200,13,WHITE);
  }
  if(arcs>=2) {
    g.drawLine(193,10,195,8,WHITE); g.drawLine(195,8,198,7,WHITE);
    g.drawLine(198,7,201,8,WHITE); g.drawLine(201,8,204,10,WHITE);
    g.drawLine(193,11,195,9,WHITE); g.drawLine(195,9,198,8,WHITE);
    g.drawLine(198,8,201,9,WHITE); g.drawLine(201,9,204,11,WHITE);
  }
  if(arcs>=3) {
    g.drawLine(190,7,193,5,WHITE); g.drawLine(193,5,198,4,WHITE);
    g.drawLine(198,4,203,5,WHITE); g.drawLine(203,5,207,7,WHITE);
    g.drawLine(190,8,193,6,WHITE); g.drawLine(193,6,198,5,WHITE);
    g.drawLine(198,5,203,6,WHITE); g.drawLine(203,6,207,8,WHITE);
  }
  if(wifiSignal<0) {
    // Black keyline keeps the slash distinct where it crosses the white fan.
    g.drawLine(191,4,205,18,BLACK); g.drawLine(193,2,207,16,BLACK);
    g.drawLine(192,3,206,17,WHITE);
  }
}
void header(Adafruit_GFX& g, const std::string& title, const std::string& clock,
            bool settings=false, const std::string& page="") {
  g.fillRect(0,0,250,20,BLACK);
  if(settings) { g.fillCircle(10,10,7,WHITE); g.fillCircle(10,10,3,BLACK); }
  else bus(g,4,2,WHITE);
  // Independent lanes; compact long titles instead of crushing the clock.
  g.setFont(&FreeSansBold9pt7b); g.setTextSize(1);
  int16_t bx,by; uint16_t w,h;
  g.getTextBounds(displayPolicy::label(title,64).c_str(),0,0,&bx,&by,&w,&h);
  if(page.empty()) fitted(g,title,21,1,160,18,w<=160 && h<=18 ? &FreeSansBold9pt7b : nullptr,WHITE);
  else {
    fitted(g,title,21,1,106,18,nullptr,WHITE);
    fitted(g,page,132,2,23,16,nullptr,WHITE);
  }
  wifiFan(g);
  fitted(g,clock,214,2,35,16,nullptr,WHITE,true);
  // Battery remains hidden; the right-hand lane is reserved for the clock.
}
}
void setWiFiSignal(int bars) {
  wifiSignal=bars>=0 && bars<=3 ? bars : -1;
}
void renderBoot(Adafruit_GFX& g, const BootView& v) {
  g.fillScreen(WHITE);
  header(g,"BusNotifier","--:--");
  // One position per actual boot event. Future track is dotted, never a
  // timer-driven progress estimate; the final stop means the first usable feed.
  const unsigned stage=v.stage<3 ? v.stage : 3;
  const int stations[]={24,92,159,226};
  bus(g,stations[stage]-6,28,BLACK);
  for(unsigned i=0;i<3;++i) {
    if(i<stage) g.drawFastHLine(stations[i],50,stations[i+1]-stations[i],BLACK);
    else for(int x=stations[i];x<stations[i+1];x+=4) g.drawPixel(x,50,BLACK);
  }
  for(unsigned i=0;i<4;++i) {
    if(v.failed && i==stage) {
      g.fillRect(stations[i]-3,47,7,7,WHITE);
      g.drawLine(stations[i]-3,47,stations[i]+3,53,BLACK);
      g.drawLine(stations[i]-3,53,stations[i]+3,47,BLACK);
    } else {
      g.fillCircle(stations[i],50,3,i<=stage ? BLACK : WHITE);
      g.drawCircle(stations[i],50,3,BLACK);
    }
  }
  const char* labels[]={"Wi-Fi", "Heure", "SETRAM"};
  const char* complete[]={"Connecte", "Synchronisee", "Flux recu"};
  const char* active[]={"Connexion", "Synchronisation", "Lecture du flux"};
  for(unsigned i=0;i<3;++i) {
    int y=60+16*i;
    // Recognisable network, clock and feed icons stay within their own lane.
    if(i==0) {
      g.drawCircle(13,y+8,7,BLACK); g.drawCircle(13,y+8,4,BLACK);
      g.fillRect(5,y+8,17,8,WHITE); g.fillCircle(13,y+8,1,BLACK);
    } else if(i==1) {
      g.drawCircle(13,y+5,5,BLACK);
      g.drawLine(13,y+5,13,y+2,BLACK); g.drawLine(13,y+5,16,y+6,BLACK);
    } else {
      g.drawRect(7,y,13,11,BLACK);
      g.drawFastHLine(7,y+5,13,BLACK);
      g.drawPixel(9,y+2,BLACK); g.drawPixel(9,y+8,BLACK);
    }
    fitted(g,labels[i],29,y,87,10);
    fitted(g,i<stage ? complete[i] : i==stage ? (v.failed ? "Echec" : active[i]) : "En attente",126,y,116,10);
  }
  fitted(g,v.detail,8,108,234,10);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void render(Adafruit_GFX& g, const View& v) {
  g.fillScreen(WHITE); g.setTextWrap(false);
  // 250x122 layout; rows 122..127 are deliberately left blank.
  header(g,v.line+" - "+v.stop,v.clock);

  g.fillRoundRect(4,27,52,47,5,BLACK);
  g.setFont(&FreeSansBold18pt7b); g.setTextSize(1);
  int16_t bx,by; uint16_t w,h;
  auto line=displayPolicy::label(v.line,8);
  g.getTextBounds(line.c_str(),0,0,&bx,&by,&w,&h);
  const GFXfont* badgeFont=w<=46? &FreeSansBold18pt7b : &FreeSansBold9pt7b;
  g.setFont(badgeFont); g.getTextBounds(line.c_str(),0,0,&bx,&by,&w,&h);
  fitted(g,line,7+(46-int(w>46?46:w))/2,31,46,38,badgeFont,WHITE);

  fitted(g,"Direction",62,24,99,9);
  // Keep ordinary destinations readable in two lines on the small panel.
  std::string destination=displayPolicy::label(v.destination,64);
  g.setFont(&FreeSansBold9pt7b); g.setTextSize(1);
  g.getTextBounds(destination.c_str(),0,0,&bx,&by,&w,&h);
  size_t split=std::string::npos;
  if(w>99) {
    for(size_t at=destination.find(' '); at!=std::string::npos; at=destination.find(' ',at+1)) {
      std::string prefix=destination.substr(0,at);
      g.getTextBounds(prefix.c_str(),0,0,&bx,&by,&w,&h);
      if(w<=99) split=at; else break;
    }
  }
  if(split!=std::string::npos) {
    fitted(g,destination.substr(0,split),62,34,99,14,&FreeSansBold9pt7b);
    fitted(g,destination.substr(split+1),62,48,99,14,&FreeSansBold9pt7b);
  } else fitted(g,destination,62,34,99,28,&FreeSansBold9pt7b);
  // No invented punctuality or wheelchair availability.
  g.drawRoundRect(62,63,97,13,4,BLACK);
  fitted(g,v.status,66,65,89,9);
  g.drawFastVLine(165,27,49,BLACK);
  const bool future=v.primary.compare(0,2,"J+")==0;
  const size_t separator=future ? v.primary.find(' ') : std::string::npos;
  const std::string primary=future && separator!=std::string::npos ? v.primary.substr(separator+1,5) : v.primary;
  const std::string unit=future ? v.primary.substr(0,separator)+(v.primary.back()=='P' ? " prevu" : " reel") : "min";
  g.setFont(&FreeSansBold24pt7b);
  g.getTextBounds(primary.c_str(),0,0,&bx,&by,&w,&h);
  const GFXfont* numberFont=w<=76? &FreeSansBold24pt7b : &FreeSansBold18pt7b;
  g.setFont(numberFont); g.getTextBounds(primary.c_str(),0,0,&bx,&by,&w,&h);
  if(w>76) { numberFont=&FreeSansBold9pt7b; g.setFont(numberFont); g.getTextBounds(primary.c_str(),0,0,&bx,&by,&w,&h); }
  fitted(g,primary,171+(76-int(w>76?76:w))/2,25,76,39,numberFont);
  if(future) fitted(g,unit,171,66,76,10);
  else fitted(g,"min",199,66,31,10);

  g.drawFastHLine(4,82,242,BLACK);
  g.drawFastVLine(191,86,24,BLACK);
  for(int i=0;i<2;++i) {
    int y=86+i*15;
    // Compact bus fits the 12px row; wheels must not intrude into status.
    g.fillRoundRect(5,y,12,10,2,BLACK);
    g.fillRect(7,y+2,8,4,WHITE);
    g.fillCircle(8,y+8,1,WHITE); g.fillCircle(14,y+8,1,WHITE);
    g.fillRect(6,y+10,2,2,BLACK); g.fillRect(14,y+10,2,2,BLACK);
    fitted(g,v.line,22,y,26,12);
    const bool futureNext=v.next[i].compare(0,2,"J+")==0;
    fitted(g,v.destination,51,y,futureNext ? 57 : 90,12);
    fitted(g,v.next[i],futureNext ? 112 : 144,y,futureNext ? 76 : 44,12);
  }
  g.drawFastHLine(4,99,183,BLACK);
  g.drawCircle(220,91,6,BLACK);
  g.drawLine(220,91,220,87,BLACK); g.drawLine(220,91,223,93,BLACK);
  fitted(g,"Suivants",197,101,49,9);
  fitted(g,visibleFooter(v.footer),4,114,242,8);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderDual(Adafruit_GFX& g, const DualView& v) {
  g.fillScreen(WHITE); g.setTextWrap(false); header(g,"Mes lignes favorites",v.clock);
  for(unsigned row=0; row<2; ++row) {
    const int y=row ? 68 : 23;
    g.drawFastHLine(4,y+42,242,BLACK);
    g.fillRoundRect(5,y+4,34,28,4,BLACK);
    fitted(g,v.rows[row].line,8,y+8,28,20,&FreeSansBold9pt7b,WHITE,false,true);
    fitted(g,v.rows[row].destination,45,y+2,101,19,&FreeSansBold9pt7b);
    fitted(g,v.rows[row].warning,45,y+23,101,9);
    const std::string primary=v.rows[row].primary.empty()?"--":v.rows[row].primary;
    const bool countdown=!primary.empty() && primary.find_first_not_of("0123456789")==std::string::npos;
    const bool future=primary.compare(0,2,"J+")==0;
    fitted(g,primary,151,y+1,countdown ? 68 : 92,27,future ? nullptr : &FreeSansBold18pt7b,BLACK,false,true);
    if(countdown) fitted(g,"min",223,y+17,20,10);
    fitted(g,v.rows[row].next.empty()?"--":v.rows[row].next,151,y+29,92,10,nullptr,BLACK,false,true);
  }
  fitted(g,v.footer,4,114,242,8);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderTomorrow(Adafruit_GFX& g, const TomorrowView& v) {
  g.fillScreen(WHITE);
  header(g,v.stop,v.clock);
  // A compact route row leaves a dedicated, permanent planned-service label.
  // Unlike the live Home screen the hero is an HH:MM, with no minute unit.
  g.fillRoundRect(4,25,52,25,4,BLACK);
  fitted(g,v.line,8,28,44,19,&FreeSansBold9pt7b,WHITE);
  fitted(g,v.destination,62,25,184,25,&FreeSansBold9pt7b);
  fitted(g,"Demain",4,55,104,18,&FreeSansBold9pt7b);
  fitted(g,"Horaires prevus",112,55,134,18);
  g.drawFastVLine(126,78,30,BLACK);
  fitted(g,v.times[0].empty() ? "--:--" : v.times[0],4,77,112,29,&FreeSansBold18pt7b);
  fitted(g,v.times[1].empty() ? "--:--" : v.times[1],141,77,105,15,&FreeSansBold9pt7b);
  fitted(g,v.times[2].empty() ? "--:--" : v.times[2],141,94,105,15,&FreeSansBold9pt7b);
  fitted(g,visiblePlannedFooter(v.footer),4,114,242,8);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderSchedule(Adafruit_GFX& g, const ScheduleView& v) {
  g.fillScreen(WHITE);
  const bool planned=v.footer.compare(0,6,"Demain")==0;
  header(g,planned ? "Demain prevu" : "Horaires",v.clock,false,std::to_string(v.page)+"/3");
  for(unsigned i=0;i<5;++i) {
    int y=23+i*17;
    g.fillRoundRect(4,y+1,29,13,2,BLACK);
    fitted(g,v.line,7,y+3,23,9,nullptr,WHITE);
    const bool future=v.times[i].compare(0,2,"J+")==0;
    fitted(g,v.destination,39,y+2,future ? 109 : 145,11);
    fitted(g,v.times[i],future ? 154 : 196,y+2,future ? 91 : 49,11);
    g.drawFastHLine(4,y+16,242,BLACK);
  }
  fitted(g,visiblePlannedFooter(v.footer),4,112,242,10);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderWiFi(Adafruit_GFX& g, const WiFiView& v) {
  g.fillScreen(WHITE);
  header(g,v.showIp ? "Adresse IP" : "Wi-Fi",v.clock,true);
  if(v.showIp) {
    const bool connected=!v.ip.empty() && v.ip!="0.0.0.0";
    fitted(g,"Connexion Wi-Fi",8,29,234,12);
    fitted(g,connected ? v.ip : "Hors ligne",8,45,234,24,&FreeSansBold9pt7b);
    if(!connected) {
      fitted(g,"Aucune adresse attribuee",8,71,234,12);
      fitted(g,"Reconnecter via le portail",8,83,234,12);
    }
    // This is an actionable menu item, not a hardware-controls footer.
    g.fillRoundRect(2,97,246,20,3,BLACK);
    fitted(g,"Retour",8,100,226,12,nullptr,WHITE);
    g.setFont(nullptr); g.setTextColor(BLACK);
    return;
  }
  const char* titles[]={"Reconnecter", "Adresse IP", "Retour"};
  for(unsigned i=0;i<3;++i) {
    int y=27+i*27;
    bool selected=i==v.selected;
    uint16_t ink=selected?WHITE:BLACK;
    if(selected) g.fillRoundRect(2,y-2,246,25,3,BLACK);
    fitted(g,titles[i],12,y,218,12,nullptr,ink);
    if(i==0) fitted(g,"Portail captif",12,y+14,218,8,nullptr,ink);
    g.drawLine(238,y+4,242,y+7,ink); g.drawLine(242,y+7,238,y+10,ink);
  }
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderOta(GFXcanvas1& g, const OtaView& v) {
  g.fillScreen(WHITE);
  header(g,"Mise a jour OTA",v.clock,true);
  // Dedicated event/status screen; no synthetic percentages or timed animation.
  g.drawFastVLine(15,27,12,BLACK);
  g.drawLine(10,32,15,27,BLACK); g.drawLine(15,27,20,32,BLACK);
  g.drawFastHLine(9,41,13,BLACK);
  fitted(g,"Mode OTA",29,25,70,16);
  g.fillRoundRect(105,24,141,18,3,BLACK);
  const char* status[]={"Preparation","En attente","Transfert","Termine","Erreur"};
  const unsigned stage=v.stage<=4 ? v.stage : 4;
  fitted(g,status[stage],111,25,130,16,nullptr,WHITE);
  g.drawFastHLine(4,44,242,BLACK);
  fitted(g,"Hote",8,48,54,10); fitted(g,v.hostname,70,48,172,10);
  fitted(g,"IP",8,62,54,10); fitted(g,v.ip,70,62,172,10);
  fitted(g,"Cle temp.",8,76,54,10); fitted(g,v.password,70,76,172,10);
  fitted(g,v.detail,8,94,234,10);
  fitted(g,stage==2 ? "Transfert : ne pas eteindre" : "MENU + EXIT 2 s : annuler",4,112,242,10);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
void renderConfig(Adafruit_GFX& g, const ConfigView& v) {
  g.fillScreen(WHITE);
  header(g,"Configuration",v.clock,true);
  const char* titles[]={"Arret favori", "Ligne suivie", "Rafraichir", "Wi-Fi", "Langue", "Mise a jour OTA", "Inverser suivis"};
  const unsigned selected=v.selected<7 ? v.selected : 6;
  const unsigned first=selected>=5 ? selected-4 : 0;
  for(unsigned row=0;row<5;++row) {
    const unsigned i=first+row;
    int y=23+row*17;
    bool active=i==selected;
    uint16_t ink=active?WHITE:BLACK;
    if(active) g.fillRoundRect(2,y-1,246,16,3,BLACK);
    if(i==1) bus(g,5,y,ink);
    else if(i==0) { g.fillTriangle(6,y+10,10,y+1,14,y+10,ink); g.fillTriangle(5,y+4,15,y+4,10,y+13,ink); }
    else if(i==2) { g.drawCircle(10,y+7,5,ink); g.drawLine(12,y+2,15,y+3,ink); }
    else if(i==3) { g.drawCircle(10,y+7,5,ink); g.fillCircle(10,y+11,1,ink); }
    else if(i==4) { g.drawCircle(10,y+7,6,ink); g.drawFastVLine(10,y+1,12,ink); g.drawFastHLine(4,y+7,12,ink); }
    else { g.drawFastVLine(10,y+2,10,ink); g.drawLine(6,y+6,10,y+2,ink); g.drawLine(10,y+2,14,y+6,ink); g.drawFastHLine(5,y+13,11,ink); }
    fitted(g,titles[i],23,y+2,95,11,nullptr,ink);
    fitted(g,v.values[i],123,y+2,112,11,nullptr,ink);
    g.drawLine(241,y+4,245,y+7,ink); g.drawLine(245,y+7,241,y+10,ink);
    g.drawFastHLine(3,y+16,244,BLACK);
  }
  fitted(g,visibleFooter(v.footer),4,112,242,10);
  g.setFont(nullptr); g.setTextColor(BLACK);
}
}
