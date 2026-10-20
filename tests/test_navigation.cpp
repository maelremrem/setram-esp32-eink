#include <Navigation.h>
#include <cassert>
#include <iostream>
using namespace navigation;
int main() {
  Controller dual;
  dual.setDualConfigured(true);
  dual.event(Key::Back); assert(dual.homeView==0 && dual.screen==Screen::Home);
  dual.event(Key::Previous); assert(dual.homeView==1 && dual.screen==Screen::Home);
  dual.event(Key::Next); assert(dual.homeView==1 && dual.screen==Screen::Schedule);
  dual.event(Key::Next); assert(dual.page==1);
  dual.event(Key::Back); assert(dual.homeView==0 && dual.screen==Screen::Home && dual.page==0);
  dual.event(Key::Next); assert(dual.homeView==2 && dual.screen==Screen::Home);
  dual.event(Key::Previous); assert(dual.homeView==2 && dual.screen==Screen::Schedule);
  dual.event(Key::Previous); assert(dual.homeView==2 && dual.screen==Screen::Home);
  dual.event(Key::Back); assert(dual.homeView==0 && dual.screen==Screen::Home);
  dual.event(Key::Back); assert(dual.homeView==0 && dual.screen==Screen::Home);
  Controller toggle;
  toggle.event(Key::Menu); toggle.event(Key::Next);
  assert(toggle.screen==Screen::Config);
  toggle.event(Key::Menu); assert(toggle.screen==Screen::Home);
  toggle.event(Key::Menu); assert(toggle.screen==Screen::Config && toggle.selected==0);
  for(auto menu : {Screen::WiFi,Screen::WiFiIp}) {
    toggle.screen=menu; toggle.event(Key::Menu); assert(toggle.screen==Screen::Home);
  }
  Controller protectedOta; protectedOta.screen=Screen::Ota;
  for(auto key : {Key::Menu,Key::Back,Key::Previous,Key::Next,Key::Ok}) {
    assert(protectedOta.event(key)==Action::None && protectedOta.screen==Screen::Ota);
  }
  assert(!allowFetch(Screen::Ota,false,false,10000));
  Controller n;
  assert(n.screen == Screen::Home);
  assert(n.event(Key::Menu) == Action::None && n.screen == Screen::Config);
  n.event(Key::Next); assert(n.selected == 1);
  assert(n.event(Key::Ok) == Action::Portal);
  n.event(Key::Next); assert(n.selected == 2);
  assert(n.event(Key::Ok) == Action::RefreshInterval);
  n.event(Key::Back); assert(n.screen == Screen::Home);
  n.event(Key::Ok); assert(n.screen == Screen::Schedule);
  n.event(Key::Next); assert(n.page == 1);
  for(int i=0;i<20;++i) n.event(Key::Next);
  assert(n.page == 2);
  n.event(Key::Previous); assert(n.page == 1);
  assert(n.event(Key::Ok) == Action::RefreshNow);
  n.event(Key::Previous); assert(n.page==0 && n.screen==Screen::Schedule);
  assert(n.event(Key::Previous)==Action::None && n.screen==Screen::Home);
  n.event(Key::Ok); assert(n.screen==Screen::Schedule && n.page==0);
  n.event(Key::Next); assert(n.page==1);
  n.event(Key::Previous); assert(n.page==0 && n.screen==Screen::Schedule);
  n.event(Key::Previous); assert(n.screen==Screen::Home);
  n.event(Key::Menu); for(int i=0;i<5;++i) n.event(Key::Next); assert(n.selected == 5);
  const auto otaAction=n.event(Key::Ok);
  assert(otaAction==Action::EnableOta);
  n.event(Key::Previous); assert(n.selected==4);
  assert(n.event(Key::Ok) == Action::FrenchInfo);
  n.event(Key::Next); assert(n.selected == 5 && n.screen==Screen::Config);
  n.setDualConfigured(true);
  n.event(Key::Next); assert(n.selected == 6 && n.screen==Screen::Config);
  for(unsigned index=0;index<6;++index) {
    n.event(Key::Next); assert(n.selected==index);
  }
  n.event(Key::Next); assert(n.selected==6); // Seven settings including swap.
  n.event(Key::Menu); assert(n.selected==0 && n.screen==Screen::Home);
  // Wi-Fi opens a submenu instead of immediately starting the portal.
  n.event(Key::Menu);
  n.event(Key::Next); n.event(Key::Next); n.event(Key::Next);
  assert(n.event(Key::Ok) == Action::None);
  assert(n.screen != Screen::Config && n.screen != Screen::Home);
  assert(n.selected == 0);
  assert(n.event(Key::Ok) == Action::Portal); // Reconnecter
  n.event(Key::Next);
  assert(n.event(Key::Ok) == Action::None); // Adresse IP (view state only)
  n.event(Key::Back); // IP -> Wi-Fi list
  assert(n.selected == 1);
  n.event(Key::Next); n.event(Key::Ok); // Retour -> Configuration/Wi-Fi
  assert(n.screen == Screen::Config && n.selected == 3);
  // Combined contact: neither single button nor a short overlap can exit OTA.
  Debouncer chord(false,true);
  chord.update(false,0); chord.update(false,40);
  bool menu=true, exit=false;
  assert(chord.update(menu && exit,100)==Event::None);
  exit=true; chord.update(menu && exit,200); chord.update(menu && exit,240);
  assert(chord.update(menu && exit,2239)==Event::None);
  assert(chord.update(menu && exit,2240)==Event::Long);
  assert(chord.update(menu && exit,4000)==Event::None);
  chord.update(false,4010); assert(chord.update(false,4050)==Event::None);
  Debouncer button;
  assert(button.update(true,0) == Event::None); // Held at boot ignored.
  assert(button.update(true,4000) == Event::None);
  button.update(false,4010); button.update(false,4070);
  assert(button.update(true,4080) == Event::None);
  button.update(true,4140);
  assert(button.update(false,4200) == Event::None);
  assert(button.update(false,4260) == Event::Short);
  button.update(true,4300); button.update(true,4360);
  assert(button.update(true,6400) == Event::Long);
  assert(button.update(true,8000) == Event::None);
  button.update(false,8050); assert(button.update(false,8110) == Event::None);
  std::cout << "Navigation and buttons: OK\n";
}
