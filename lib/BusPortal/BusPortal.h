#pragma once
#include <WiFiManager.h>
#include <BusCatalog.h>
#include "BusPortalPolicy.h"
#include "GeneratedPortal.h"
#include <esp_system.h>
namespace busPortal {
inline String json(const std::string& value) {
  String out="\"";
  for(unsigned char c : value) {
    if(c=='"' || c=='\\') { out+='\\'; out+=char(c); }
    else if(c<32) { char escaped[7]; snprintf(escaped,sizeof(escaped),"\\u%04x",c); out+=escaped; }
    else out+=char(c);
  }
  return out+"\"";
}
class Portal {
  WiFiManager& manager;
  portalSettings::Settings& current;
  uint32_t& interval;
  std::function<bool(const portalSettings::Settings&,uint32_t)> save;
  String token;
  bool active() { return manager.getConfigPortalActive(); }
  void headers() {
    auto& server=*manager.server;
    server.sendHeader("Cache-Control","no-store");
    server.sendHeader("X-Content-Type-Options","nosniff");
    server.sendHeader("Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'");
  }
  void message(int code, const char* text) {
    headers(); manager.server->send(code,"application/json; charset=utf-8","{\"message\":"+json(text)+"}");
  }
  void submit() {
    auto& server=*manager.server;
    if(!authorized(active(),token.c_str(),server.arg("csrf").c_str(),server.header("Origin").c_str(),
       server.hostHeader().c_str(),WiFi.softAPIP().toString().c_str(),WiFi.localIP().toString().c_str())) {
      message(403,"Portail expiré ou origine invalide. Rouvrez avec MENU 2s."); return;
    }
    if(server.args()>12 || server.header("Content-Length").toInt()>2048) { message(413,"Requête trop volumineuse."); return; }
    for(int i=0;i<server.args();++i) if(server.arg(i).length()>64) { message(400,"Champ trop long (64 octets maximum)."); return; }
    uint32_t nextInterval=frequency(server.arg("frequency").c_str());
    if(!nextInterval) { message(400,"Fréquence invalide : 30, 60 ou 120 secondes."); return; }
    portalSettings::Settings next;
    if(server.arg("mode")=="catalogue") {
      auto e=busCatalog::find(server.arg("stop").c_str(),server.arg("route").c_str(),server.arg("direction").c_str(),server.arg("destination").c_str());
      if(!e) { message(400,"Ce quai, cette ligne et ce sens ne correspondent pas au catalogue."); return; }
      next=busCatalog::settings(*e);
      if(server.hasArg("second_stop") && server.arg("second_stop").length()) {
        auto e2=busCatalog::find(server.arg("second_stop").c_str(),server.arg("second_route").c_str(),server.arg("second_direction").c_str(),server.arg("second_destination").c_str());
        if(!e2) { message(400,"Le second choix ne correspond pas au catalogue."); return; }
        auto s2=busCatalog::settings(*e2);
        next.secondStop=s2.stop; next.secondRoute=s2.route; next.secondDirection=s2.direction;
        next.secondStopLabel=s2.stopLabel; next.secondLineLabel=s2.lineLabel; next.secondDestinationLabel=s2.destinationLabel;
      }
    } else if(server.arg("mode")=="manual") {
      next={server.arg("stop").c_str(),server.arg("route").c_str(),server.arg("direction").c_str(),server.arg("stop_label").c_str(),server.arg("line_label").c_str(),server.arg("destination").c_str()};
    } else { message(400,"Mode de sélection invalide."); return; }
    if(!portalSettings::valid(next)) { message(400,"Identifiants ou libellés invalides. Évitez les signes < > & et les guillemets dans les libellés."); return; }
    if(!save(next,nextInterval)) { message(500,"Erreur NVS : configuration non confirmée. Réessayez."); return; }
    message(200,"Arrêt et fréquence enregistrés et relus depuis la mémoire du boîtier.");
  }
public:
  Portal(WiFiManager& wm, portalSettings::Settings& settings, uint32_t& poll,
         std::function<bool(const portalSettings::Settings&,uint32_t)> persist)
    :manager(wm),current(settings),interval(poll),save(persist) {}
  void attach() {
    char bytes[33]; snprintf(bytes,sizeof(bytes),"%08x%08x%08x%08x",unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random()),unsigned(esp_random())); token=bytes;
    const char* collected[]={"Origin","Content-Length"}; manager.server->collectHeaders(collected,2);
    manager.server->on("/bus",HTTP_GET,[this](){
      if(!active()) { message(403,"Portail fermé : MENU 2s."); return; }
      headers(); String html=FPSTR(portalHtml); html.replace("__CSRF__",token); manager.server->send(200,"text/html; charset=utf-8",html);
    });
    manager.server->on("/api/catalogue",HTTP_GET,[this](){
      if(!active()) { message(403,"Portail fermé : MENU 2s."); return; }
      headers(); manager.server->sendHeader("Content-Encoding","gzip");
      manager.server->send_P(200,"application/json; charset=utf-8",reinterpret_cast<const char*>(busCatalog::catalogueGzip),sizeof(busCatalog::catalogueGzip));
    });
    manager.server->on("/api/current",HTTP_GET,[this](){
      if(!active()) { message(403,"Portail fermé : MENU 2s."); return; }
      headers(); String body="{\"frequency\":"+String(interval/1000)+",\"stop\":"+json(current.stop)+",\"route\":"+json(current.route)+",\"direction\":"+json(current.direction)+",\"stop_label\":"+json(current.stopLabel)+",\"line_label\":"+json(current.lineLabel)+",\"destination\":"+json(current.destinationLabel)+",\"second_stop\":"+json(current.secondStop)+",\"second_route\":"+json(current.secondRoute)+",\"second_direction\":"+json(current.secondDirection)+",\"second_stop_label\":"+json(current.secondStopLabel)+",\"second_line_label\":"+json(current.secondLineLabel)+",\"second_destination\":"+json(current.secondDestinationLabel)+",\"swapped\":"+(current.swapped?String("true"):String("false"))+"}";
      manager.server->send(200,"application/json; charset=utf-8",body);
    });
    manager.server->on("/api/settings",HTTP_POST,[this](){submit();});
  }
};
}
