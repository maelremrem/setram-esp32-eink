#include "PortalUI.h"
#include <qrcode.h>
#include <vector>

namespace portalUI {
namespace {
std::string escapeWiFi(const std::string& value) {
  std::string result;
  for(char ch : value) {
    if(ch=='\\' || ch==';' || ch==',' || ch==':' || ch=='"') result+='\\';
    result+=ch;
  }
  return result;
}
void text(GFXcanvas1& canvas, int x, int y, const std::string& value) {
  canvas.setCursor(x,y); canvas.print(value.substr(0,(250-x)/6).c_str());
}
}
bool render(GFXcanvas1& canvas, const View& view) {
  canvas.fillScreen(0); canvas.setFont(nullptr); canvas.setTextSize(1);
  canvas.setTextColor(1); canvas.setTextWrap(false);
  const bool join=view.step==Step::Join;
  if(join) {
    text(canvas,4,3,"1/2  CONNECTER LE TELEPHONE");
    text(canvas,94,24,"Scannez ce QR Wi-Fi");
    text(canvas,94,38,"Reseau :"); text(canvas,94,48,view.ssid);
    text(canvas,94,59,"Cle WPA :"); text(canvas,94,69,view.password);
    text(canvas,94,85,"Restez sur ce Wi-Fi");
    text(canvas,94,95,"meme sans Internet");
  } else {
    text(canvas,4,3,"2/2  CONFIGURER LE WI-FI");
    text(canvas,94,24,"Sur le Wi-Fi du bus,");
    text(canvas,94,34,"scannez ce QR lien");
    text(canvas,94,48,"http://192.168.4.1");
    text(canvas,94,62,"Configurer le Wi-Fi");
    text(canvas,94,72,"-> choisir le reseau");
    text(canvas,94,86,"Puis arret/ligne/sens");
    text(canvas,94,96,"et Enregistrer");
  }
  if(!view.notice.empty()) {
    canvas.fillRect(94,84,156,22,0);
    // Word-wrap notices across two fixed lines; never enter the QR quiet zone.
    size_t split=view.notice.size()>26 ? view.notice.rfind(' ',26) : view.notice.size();
    if(split==std::string::npos || split==0) split=26;
    text(canvas,94,85,view.notice.substr(0,split));
    if(split<view.notice.size()) {
      size_t next=split+(view.notice[split]==' ' ? 1 : 0);
      text(canvas,94,95,view.notice.substr(next));
    }
  }
  const std::string payload=join ? "WIFI:T:WPA;S:"+escapeWiFi(view.ssid)+";P:"+escapeWiFi(view.password)+";;"
                                 : "http://192.168.4.1";
  // QRCode 0.0.1 does not reliably reject oversized input: guard byte-mode
  // capacity before calling it (version 4/M = 62 bytes, version 2/M = 26).
  if(join && (view.ssid.empty() || view.password.size()<8 || payload.size()>62)) {
    text(canvas,4,52,"QR indisponible"); text(canvas,4,64,"Saisie manuelle");
    return false;
  }
  const uint8_t version=join ? 4 : 2;
  QRCode qr;
  std::vector<uint8_t> buffer(qrcode_getBufferSize(version));
  if(qrcode_initText(&qr,buffer.data(),version,ECC_MEDIUM,payload.c_str())!=0) return false;
  // Center in a 90x90 area (0..89, 20..109), preserving >=4 white modules.
  const int x0=(90-qr.size*2)/2, y0=20+(90-qr.size*2)/2;
  for(uint8_t y=0;y<qr.size;++y) for(uint8_t x=0;x<qr.size;++x)
    if(qrcode_getModule(&qr,x,y)) canvas.fillRect(x0+x*2,y0+y*2,2,2,1);
  return true;
}
}
