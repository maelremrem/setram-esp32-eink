#pragma once
#include <string>
namespace bootFlow {
// No timer, spinner, percentage or artificial wait: transitions come from radio,
// synchronized system time, and the first actual request. Portal/navigation are
// higher-priority views, never destroyed by boot completion.
struct Controller {
  unsigned stage=0;
  std::string detail="Connexion Wi-Fi";
  bool failed=false, complete=false;
  void prerequisites(bool connected, bool clockReady, bool settingsReady) {
    if(complete) return;
    if(!connected) { stage=0; detail="Wi-Fi deconnecte"; failed=true; }
    else if(!clockReady) { stage=1; detail="Wi-Fi connecte - attente NTP"; failed=false; }
    else if(!settingsReady) { stage=2; detail="Configurer arret : MENU 2s"; failed=true; }
    else if(stage==0 || failed) {
      stage=1; detail="Wi-Fi connecte - heure OK"; failed=false;
    }
  }
  void requestStarted() { if(!complete) { stage=2; detail="Requete SETRAM"; failed=false; } }
  void requestFinished() { complete=true; }
};
}
