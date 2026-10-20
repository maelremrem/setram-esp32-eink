#pragma once
#include <PortalSettings.h>
#include <cstring>
namespace busCatalog {
struct Entry { const char *stop, *route, *direction, *stopLabel, *line, *destination; };
}
#include "GeneratedCatalogue.h"
namespace busCatalog {
inline const Entry* find(const std::string& stop, const std::string& route,
                         const std::string& direction, const std::string& destination) {
  for(const auto& e : entries)
    if(stop == e.stop && route == e.route && direction == e.direction && destination == e.destination) return &e;
  return nullptr;
}
inline portalSettings::Settings settings(const Entry& e) {
  return {e.stop,e.route,e.direction,e.stopLabel,e.line,e.destination};
}
}
