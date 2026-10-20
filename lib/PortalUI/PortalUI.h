#pragma once
#include <Adafruit_GFX.h>
#include <string>
#include <Navigation.h>

namespace portalUI {
enum class Step { Join, Portal };
enum class Action { None, Redraw, Close };
struct Controller {
  Step step=Step::Join;
  void reset() { step=Step::Join; }
  Action key(navigation::Key key) {
    if(key==navigation::Key::Back) return Action::Close;
    if(key!=navigation::Key::Ok) return Action::None;
    step=step==Step::Join ? Step::Portal : Step::Join;
    return Action::Redraw;
  }
};
struct View {
  Step step;
  std::string ssid;
  std::string password;
  std::string notice;
};
// GFX uses 1 as black, 0 as white. Logical area 250x122; padding stays white.
// False means no QR was generated; manual connection instructions remain visible.
bool render(GFXcanvas1& canvas, const View& view);
}
