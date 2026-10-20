#pragma once
#include <string>
namespace portalSettings {
struct Settings {
  std::string stop, route, direction, stopLabel, lineLabel, destinationLabel;
  // Optional second tuple. Empty stop keeps the legacy single-tracking mode.
  std::string secondStop, secondRoute, secondDirection, secondStopLabel, secondLineLabel, secondDestinationLabel;
  bool swapped=false;
  Settings() = default;
  Settings(std::string a,std::string b,std::string c,std::string d,std::string e,std::string f)
    : stop(std::move(a)),route(std::move(b)),direction(std::move(c)),stopLabel(std::move(d)),lineLabel(std::move(e)),destinationLabel(std::move(f)) {}
};
inline bool identifier(const std::string& s) {
  if(s.size() > 64) return false;
  for(unsigned char c : s)
    if(!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
       (c >= 'a' && c <= 'z') || c == '_' || c == '-' || c == ':' || c == '.')) return false;
  return true;
}
inline bool label(const std::string& s) {
  if(s.size() > 64) return false;
  for(unsigned char c : s)
    if(c < 32 || c == 127 || c == '<' || c == '>' || c == '&' || c == '\"' || c == '\'') return false;
  return true;
}
inline size_t serializedSize(const Settings& s) {
  return s.stop.size()+s.route.size()+s.direction.size()+s.stopLabel.size()+s.lineLabel.size()+s.destinationLabel.size()+
    s.secondStop.size()+s.secondRoute.size()+s.secondDirection.size()+s.secondStopLabel.size()+s.secondLineLabel.size()+s.secondDestinationLabel.size()+13;
}
inline bool valid(const Settings& s) {
  const bool second = s.secondStop.empty() ?
    s.secondRoute.empty() && s.secondDirection.empty() && s.secondStopLabel.empty() && s.secondLineLabel.empty() && s.secondDestinationLabel.empty() :
    identifier(s.secondStop) && identifier(s.secondRoute) &&
    (s.secondDirection == "-1" || s.secondDirection == "0" || s.secondDirection == "1") &&
    label(s.secondStopLabel) && label(s.secondLineLabel) && label(s.secondDestinationLabel) &&
    !(s.secondStop == s.stop && s.secondRoute == s.route && s.secondDirection == s.direction);
  return serializedSize(s)<=512 && !s.stop.empty() && identifier(s.stop) && identifier(s.route) && second &&
    (s.direction == "-1" || s.direction == "0" || s.direction == "1") &&
    label(s.stopLabel) && label(s.lineLabel) && label(s.destinationLabel);
}
inline std::string serialize(const Settings& s) {
  return s.stop + "\n" + s.route + "\n" + s.direction + "\n" + s.stopLabel + "\n" + s.lineLabel + "\n" + s.destinationLabel + "\n" +
    s.secondStop + "\n" + s.secondRoute + "\n" + s.secondDirection + "\n" + s.secondStopLabel + "\n" + s.secondLineLabel + "\n" + s.secondDestinationLabel + "\n" + (s.swapped ? "1" : "0");
}
inline bool deserialize(const std::string& blob, Settings& out) {
  if(blob.size() > 512) return false;
  Settings candidate;
  std::string* fields[] = {&candidate.stop, &candidate.route, &candidate.direction,
    &candidate.stopLabel, &candidate.lineLabel, &candidate.destinationLabel,
    &candidate.secondStop, &candidate.secondRoute, &candidate.secondDirection,
    &candidate.secondStopLabel, &candidate.secondLineLabel, &candidate.secondDestinationLabel};
  size_t at = 0;
  for(size_t i=0; i<6; ++i) {
    size_t end = blob.find('\n', at);
    if(end == std::string::npos && i < 5) return false;
    *fields[i] = blob.substr(at, end == std::string::npos ? end : end-at);
    if(end == std::string::npos) { at = blob.size(); break; }
    at = end + 1;
  }
  if(at < blob.size()) {
    for(size_t i=6; i<12; ++i) {
      size_t end=blob.find('\n',at);
      if(end==std::string::npos) return false;
      *fields[i]=blob.substr(at,end-at); at=end+1;
    }
    const std::string marker=blob.substr(at);
    if(marker!="0" && marker!="1") return false;
    candidate.swapped=marker=="1";
  }
  if(!valid(candidate)) return false;
  out = candidate;
  return true;
}
}
