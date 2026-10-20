#include <PortalSettings.h>
#include <cassert>
#include <iostream>
int main() {
  portalSettings::Settings s{"1079", "C4", "0", "Gares", "C4", "BELLEVUE"};
  assert(portalSettings::valid(s));
  s.direction = "-1"; assert(portalSettings::valid(s));
  s.direction = "01"; assert(!portalSettings::valid(s));
  s.direction = "2"; assert(!portalSettings::valid(s));
  s.direction = "1"; s.stop = ""; assert(!portalSettings::valid(s));
  s.stop = " 1079"; assert(!portalSettings::valid(s));
  s.stop = "1079"; s.route = ""; assert(portalSettings::valid(s));
  s.stopLabel = "Universit\xc3\xa9"; assert(portalSettings::valid(s));
  s.stopLabel = "<script>"; assert(!portalSettings::valid(s));
  s.stopLabel = std::string(65,'x'); assert(!portalSettings::valid(s));
  s.stopLabel = "Gares\n"; assert(!portalSettings::valid(s));
  s.stopLabel = "Gares";
  std::string stored = portalSettings::serialize(s);
  portalSettings::Settings loaded;
  assert(portalSettings::deserialize(stored, loaded));
  assert(loaded.stop == s.stop && loaded.route == s.route && loaded.direction == s.direction);
  assert(loaded.destinationLabel == s.destinationLabel);
  portalSettings::Settings mono;
  assert(portalSettings::deserialize(portalSettings::serialize(s), mono));
  assert(mono.secondStop.empty() && !mono.swapped);
  portalSettings::Settings dual=s;
  dual.secondStop="1079"; dual.secondRoute="C5"; dual.secondDirection="1";
  dual.secondStopLabel="Gares"; dual.secondLineLabel="C5"; dual.secondDestinationLabel="Centre"; dual.swapped=true;
  assert(portalSettings::valid(dual));
  portalSettings::Settings dualLoaded;
  assert(portalSettings::deserialize(portalSettings::serialize(dual), dualLoaded));
  assert(dualLoaded.secondStop==dual.secondStop && dualLoaded.secondRoute==dual.secondRoute && dualLoaded.swapped);
  portalSettings::Settings duplicate=dual; duplicate.secondRoute=dual.route; duplicate.secondDirection=dual.direction;
  duplicate.secondDestinationLabel=dual.destinationLabel;
  assert(!portalSettings::valid(duplicate));
  portalSettings::Settings sameStop=dual; sameStop.secondDestinationLabel="Another service";
  assert(portalSettings::valid(sameStop));
  assert(!portalSettings::deserialize(portalSettings::serialize(dual)+"\nextra", loaded));
  assert(!portalSettings::deserialize(portalSettings::serialize(dual).substr(0, portalSettings::serialize(dual).size()-1)+"2", loaded));
  const std::string legacy="1079\nC4\n0\nGares\nC4\nBELLEVUE";
  assert(portalSettings::deserialize(legacy, loaded));
  assert(loaded.secondStop.empty() && !loaded.swapped);
  assert(!portalSettings::deserialize(stored + "\nextra", loaded));
  assert(!portalSettings::deserialize("1079\nC4", loaded));
  portalSettings::Settings oversized=dual; oversized.stop=std::string(64,'a'); oversized.route=std::string(64,'b'); oversized.direction="-1"; oversized.stopLabel=std::string(64,'c'); oversized.lineLabel=std::string(64,'d'); oversized.destinationLabel=std::string(64,'e'); oversized.secondStop=std::string(64,'f'); oversized.secondRoute=std::string(64,'g'); oversized.secondDirection="1"; oversized.secondStopLabel=std::string(64,'h'); oversized.secondLineLabel=std::string(64,'i'); oversized.secondDestinationLabel=std::string(64,'j');
  assert(!portalSettings::valid(oversized));
  std::cout << "Portal settings: OK\n";
}
