#pragma once
#include <cstdint>
namespace navigation {
enum class Screen { Home, Schedule, Config, WiFi, WiFiIp, Ota };
// Never start HTTPS while configuring or while a user is still navigating.
inline bool allowFetch(Screen screen, bool portal, bool pending, uint32_t quietMs) {
  return !portal && !pending && quietMs>=1000 &&
      (screen==Screen::Home || screen==Screen::Schedule);
}
enum class Key { Menu, Back, Previous, Next, Ok };
enum class Action { None, Portal, RefreshInterval, RefreshNow, FrenchInfo, EnableOta, Swap };
enum class Event { None, Short, Long, Repeat, ChordLong };
struct InputEvent { Key key; Event event; };
// Producer and consumer must hold their platform lock. Fixed storage, no heap.
// Repeats coalesce, real taps remain ordered; one drain produces one paint.
class InputQueue {
public:
  static constexpr unsigned Capacity=24;
private:
  InputEvent entries[Capacity];
  unsigned count=0;
  uint32_t lost=0;
  void erase(unsigned index) {
    for(unsigned i=index+1;i<count;++i) entries[i-1]=entries[i];
    --count;
  }
public:
  unsigned size() const { return count; }
  uint32_t dropped() const { return lost; }
  void push(InputEvent input) {
    if(input.event==Event::None) return;
    if(input.event==Event::Repeat) {
      if(input.key!=Key::Previous && input.key!=Key::Next) return;
      if(count && entries[count-1].event==Event::Repeat && entries[count-1].key==input.key) return;
      if(count==Capacity) { ++lost; return; }
    }
    if(count==Capacity) {
      unsigned index=0;
      for(unsigned i=0;i<count;++i) if(entries[i].event==Event::Repeat) { index=i; break; }
      erase(index); ++lost; // Prefer evicting a repeat, otherwise the oldest event.
    }
    entries[count++]=input;
  }
  bool pop(InputEvent& output) {
    if(!count) return false;
    output=entries[0]; erase(0); return true;
  }
};
struct Controller {
  Screen screen = Screen::Home;
  unsigned selected = 0, page = 0;
  bool dualConfigured = false;
  unsigned homeView = 0;
  void setDualConfigured(bool value) { dualConfigured=value; if(!value) homeView=0; }
  Action event(Key key) {
    if(screen==Screen::Ota) return Action::None; // Only the long chord may leave.
    if(key == Key::Back) {
      if(dualConfigured && (screen==Screen::Home || screen==Screen::Schedule)) {
        homeView=0; page=0; screen=Screen::Home; return Action::None;
      }
      if(screen==Screen::WiFiIp) { screen=Screen::WiFi; selected=1; }
      else if(screen==Screen::WiFi) { screen=Screen::Config; selected=3; }
      else screen=Screen::Home;
      return Action::None;
    }
    if(key == Key::Menu) {
      const bool inMenu=screen==Screen::Config || screen==Screen::WiFi || screen==Screen::WiFiIp;
      screen=inMenu ? Screen::Home : Screen::Config; selected=0; homeView=0; return Action::None;
    }
    if(screen==Screen::WiFiIp) {
      if(key==Key::Ok) { screen=Screen::WiFi; selected=1; }
      return Action::None;
    }
    if(screen==Screen::WiFi) {
      if(key==Key::Next) selected=(selected+1)%3;
      if(key==Key::Previous) selected=(selected+2)%3;
      if(key!=Key::Ok) return Action::None;
      if(selected==0) return Action::Portal;
      if(selected==1) screen=Screen::WiFiIp;
      else { screen=Screen::Config; selected=3; }
      return Action::None;
    }
    if(screen == Screen::Home) {
      page=0;
      if(dualConfigured && homeView==0 && (key==Key::Previous || key==Key::Next)) {
        homeView=key==Key::Previous ? 1 : 2;
      } else screen=Screen::Schedule;
      return Action::None;
    }
    if(screen == Screen::Schedule) {
      if(key==Key::Next && page<2) ++page;
      if(key==Key::Previous) {
        if(page>0) --page;
        else screen=Screen::Home;
      }
      return key==Key::Ok ? Action::RefreshNow : Action::None;
    }
    const unsigned menuSize=dualConfigured ? 7 : 6;
    if(key==Key::Next) selected=(selected+1)%menuSize;
    if(key==Key::Previous) selected=(selected+menuSize-1)%menuSize;
    if(key!=Key::Ok) return Action::None;
    if(selected==3) { screen=Screen::WiFi; selected=0; return Action::None; }
    if(selected==2) return Action::RefreshInterval;
    if(selected==4) return Action::FrenchInfo;
    if(selected==5) return Action::EnableOta;
    if(selected==6 && dualConfigured) return Action::Swap;
    return Action::Portal;
  }
};
// Active-low GPIO level is converted to pressed=true by the caller.
// Directions fire after debounce on press, then repeat; other keys on release.
// MENU alone uses the long-hold policy. Ignore a key held at boot.
class Debouncer {
  bool raw=false, stable=false, armed=false, fired=false;
  bool directional, longEnabled;
  uint32_t changed=0, pressed=0, repeated=0;
  bool repeating=false;
public:
  explicit Debouncer(bool direction=false, bool longHold=true)
      : directional(direction), longEnabled(longHold) {}
  Event update(bool down,uint32_t now) {
    if(down!=raw) { raw=down; changed=now; }
    Event result=Event::None;
    if(now-changed>=40 && stable!=raw) {
      stable=raw;
      if(!stable) {
        if(armed && !fired && !directional) result=Event::Short;
        armed=true; fired=false;
      } else {
        pressed=now; repeated=now; repeating=false; fired=false;
        if(armed && directional) result=Event::Short;
      }
    }
    if(!stable && !raw && now-changed>=40) armed=true;
    if(armed && stable && raw) {
      if(directional && now-repeated >= (repeating ? 200u : 450u)) {
        repeated=now; repeating=true; result=Event::Repeat;
      } else if(!directional && longEnabled && !fired && now-pressed>=2000) {
        fired=true; result=Event::Long;
      }
    }
    return result;
  }
};
}
