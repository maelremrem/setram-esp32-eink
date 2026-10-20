#include <PortalUI.h>
#include <cassert>
#include <fstream>
#include <iostream>

void save(GFXcanvas1& canvas, const std::string& path) {
  for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(canvas.getPixel(x,y)==0);
  // Onboarding no longer displays hardware-control hints along the bottom.
  for(int y=110;y<122;++y) for(int x=0;x<250;++x) assert(canvas.getPixel(x,y)==0);
  std::ofstream out(path);
  out << "P1\n250 122\n";
  for(int y=0;y<122;++y) {
    for(int x=0;x<250;++x) out << (canvas.getPixel(x,y) ? '1':'0') << ' ';
    out << '\n';
  }
}
int main(int argc, char** argv) {
  assert(argc==2);
  GFXcanvas1 canvas(128,250); canvas.setRotation(3);
  // Public, fixed fixtures only. Never export runtime credentials.
  portalUI::View view{portalUI::Step::Join, "BusNotifier-ABCDEF", "123456ABCDEF", ""};
  assert(portalUI::render(canvas,view));
  unsigned titleInk=0, passwordInk=0;
  for(int y=3;y<11;++y) for(int x=0;x<250;++x) titleInk+=canvas.getPixel(x,y);
  for(int y=69;y<77;++y) for(int x=94;x<250;++x) passwordInk+=canvas.getPixel(x,y);
  assert(titleInk>80 && passwordInk>80);
  // The complete four-module quiet zone is white.
  for(int y=24;y<106;++y) for(int x=4;x<86;++x)
    if(x<12 || x>=78 || y<32 || y>=98) assert(canvas.getPixel(x,y)==0);
  save(canvas,std::string(argv[1])+"/portal-ui-join.pbm");
  view.step=portalUI::Step::Portal;
  assert(portalUI::render(canvas,view));
  save(canvas,std::string(argv[1])+"/portal-ui-url.pbm");
  view.step=portalUI::Step::Join;
  view.ssid="Bus;QR\\Net"; view.password="semi;colon:12";
  assert(portalUI::render(canvas,view));
  save(canvas,std::string(argv[1])+"/portal-ui-escaped.pbm");
  // Portal errors must remain visible after replacing the old text-only UI.
  view.step=portalUI::Step::Portal;
  view.notice=""; assert(portalUI::render(canvas,view));
  unsigned before=0; for(int y=86;y<104;++y) for(int x=94;x<250;++x) before+=canvas.getPixel(x,y);
  view.notice="Champs invalides : recommencer";
  assert(portalUI::render(canvas,view));
  unsigned after=0; for(int y=86;y<104;++y) for(int x=94;x<250;++x) after+=canvas.getPixel(x,y);
  assert(before!=after);
  save(canvas,std::string(argv[1])+"/portal-ui-error.pbm");
  view.notice=""; view.step=portalUI::Step::Join;
  view.ssid=std::string(64,'W'); view.password=std::string(64,'P');
  assert(!portalUI::render(canvas,view));
  for(int y=122;y<128;++y) for(int x=0;x<250;++x) assert(canvas.getPixel(x,y)==0);
  view.ssid=""; view.password="12345678";
  assert(!portalUI::render(canvas,view));
  portalUI::Controller controller;
  assert(controller.step==portalUI::Step::Join);
  assert(controller.key(navigation::Key::Ok)==portalUI::Action::Redraw);
  assert(controller.step==portalUI::Step::Portal);
  assert(controller.key(navigation::Key::Next)==portalUI::Action::None);
  assert(controller.key(navigation::Key::Previous)==portalUI::Action::None);
  assert(controller.key(navigation::Key::Menu)==portalUI::Action::None);
  assert(controller.step==portalUI::Step::Portal);
  assert(controller.key(navigation::Key::Ok)==portalUI::Action::Redraw);
  assert(controller.step==portalUI::Step::Join);
  assert(controller.key(navigation::Key::Back)==portalUI::Action::Close);
  controller.key(navigation::Key::Ok); controller.reset();
  assert(controller.step==portalUI::Step::Join);
  std::cout << "Portal join/URL framebuffer and OK/Back navigation: OK\n";
}
