#pragma once
#include <Adafruit_GFX.h>
#include <string>
namespace busUI {
// Main updates this shared renderer state; view aggregates remain compatible.
// -1 disconnected, 0 connected dot, 1..3 fan arcs; all other values => -1.
void setWiFiSignal(int bars);
struct BootView {
  unsigned stage; // 0 Wi-Fi, 1 time sync, 2 first API request, 3 ready.
  std::string detail; // Actual event/error detail; compacted to the panel width.
  bool failed; // Marks the active step failed, never completes later steps.
  int batteryPercent; // -1 means no valid measurement.
};
void renderBoot(Adafruit_GFX& screen, const BootView& view);
struct View {
  std::string stop, line, destination, clock, primary;
  std::string next[2];
  std::string status, footer;
  int batteryPercent; // -1 means no valid measurement.
};
struct TrackingRow {
  std::string line, destination, primary, next;
  bool valid;
  // Per-tuple warning lane; dual mode must not hide the second feed's error
  // or planned-calendar expiry behind one shared footer.
  std::string warning;
};
struct DualView { std::string stop, clock, status, footer; TrackingRow rows[2]; int batteryPercent; };
void renderDual(Adafruit_GFX& screen, const DualView& view);
void render(Adafruit_GFX& screen, const View& view);
struct TomorrowView {
  std::string stop, line, destination, clock;
  std::string times[3]; // Scheduled HH:MM, never a realtime minute countdown.
  std::string footer;
  int batteryPercent;
};
void renderTomorrow(Adafruit_GFX& screen, const TomorrowView& view);
struct ScheduleView {
  std::string clock, line, destination, times[5], footer;
  unsigned page;
  int batteryPercent;
};
struct ConfigView {
  std::string clock, values[7];
  unsigned selected;
  std::string footer;
  int batteryPercent;
};
struct WiFiView {
  std::string clock, ip;
  unsigned selected; // 0 Reconnecter (portail captif), 1 Adresse IP, 2 Retour.
  bool showIp;
  int batteryPercent;
};
struct OtaView {
  std::string clock, hostname, ip, password, detail;
  unsigned stage; // 0 preparing, 1 waiting, 2 transfer, 3 success, 4 error.
};
void renderOta(GFXcanvas1& screen, const OtaView& view);
void renderWiFi(Adafruit_GFX& screen, const WiFiView& view);
void renderSchedule(Adafruit_GFX& screen, const ScheduleView& view);
void renderConfig(Adafruit_GFX& screen, const ConfigView& view);
}
