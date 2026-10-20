#include <Navigation.h>
#include <cassert>
#include <iostream>
using namespace navigation;
int main() {
  Debouncer direction(true, false);
  direction.update(false, 0); direction.update(false, 40);
  direction.update(true, 100);
  assert(direction.update(true, 139) == Event::None);
  assert(direction.update(true, 140) == Event::Short); // 40ms, no release wait
  assert(direction.update(true, 589) == Event::None);
  assert(direction.update(true, 590) == Event::Repeat);
  assert(direction.update(true, 790) == Event::Repeat);
  direction.update(false, 800);
  assert(direction.update(false, 840) == Event::None); // no duplicate release
  // A complete click during a 10s blocked paint/HTTPS call survives until drain.
  InputQueue queue;
  Debouncer ok(false, false);
  ok.update(false, 0); ok.update(false, 40);
  for(uint32_t ms=100; ms<=10000; ms+=5) {
    auto event=ok.update(ms>=200 && ms<300, ms);
    if(event!=Event::None) queue.push({Key::Ok, event});
  }
  InputEvent captured;
  assert(queue.pop(captured) && captured.key==Key::Ok && captured.event==Event::Short);
  assert(!queue.pop(captured));
  // Hold-repeat storm is bounded, retaining the actual tap and subsequent OK.
  queue.push({Key::Next, Event::Short});
  for(int i=0;i<1000;++i) queue.push({Key::Next, Event::Repeat});
  queue.push({Key::Ok, Event::Short});
  assert(queue.size()==3);
  assert(queue.pop(captured) && captured.event==Event::Short);
  assert(queue.pop(captured) && captured.event==Event::Repeat);
  assert(queue.pop(captured) && captured.key==Key::Ok);
  Debouncer bootDirection(true, false);
  assert(bootDirection.update(true,0)==Event::None);
  assert(bootDirection.update(true,5000)==Event::None);
  bootDirection.update(false,5010); bootDirection.update(false,5050);
  bootDirection.update(true,5100);
  assert(bootDirection.update(true,5140)==Event::Short);
  Debouncer heldOk(false, false);
  heldOk.update(false,0); heldOk.update(false,40);
  heldOk.update(true,100); heldOk.update(true,140);
  assert(heldOk.update(true,5000)==Event::None); // only MENU has long action
  heldOk.update(false,5010);
  assert(heldOk.update(false,5050)==Event::Short);
  // Saturation has a defined bounded policy; a control evicts repeat first.
  queue.push({Key::Next,Event::Repeat});
  for(unsigned i=1;i<InputQueue::Capacity;++i) queue.push({Key::Next,Event::Short});
  queue.push({Key::Back,Event::Short});
  assert(queue.size()==InputQueue::Capacity && queue.dropped()==1);
  for(unsigned i=1;i<InputQueue::Capacity;++i) {
    assert(queue.pop(captured) && captured.event==Event::Short && captured.key==Key::Next);
  }
  assert(queue.pop(captured) && captured.key==Key::Back);
  // Debounce arithmetic stays correct across uint32 millis wrap.
  Debouncer wrap(true,false);
  wrap.update(false,0xffffff00u); wrap.update(false,0xffffff40u);
  wrap.update(true,0xfffffff0u);
  assert(wrap.update(true,0x17u)==Event::None);
  assert(wrap.update(true,0x18u)==Event::Short);
  assert(!allowFetch(Screen::Config, false, false, 10000));
  assert(!allowFetch(Screen::WiFi, false, false, 10000));
  assert(!allowFetch(Screen::WiFiIp, false, false, 10000));
  assert(!allowFetch(Screen::Schedule, false, false, 999));
  assert(allowFetch(Screen::Schedule, false, false, 1000));
  assert(!allowFetch(Screen::Home, true, false, 10000));
  assert(!allowFetch(Screen::Home, false, true, 10000));
  assert(allowFetch(Screen::Home, false, false, 1000));
  std::cout << "Immediate directional press, blocked-consumer capture and bounded repeat: OK\n";
}
