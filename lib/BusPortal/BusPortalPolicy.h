#pragma once
#include <string>
#include <stdint.h>
namespace busPortal {
inline uint32_t frequency(const std::string& seconds) {
  return seconds == "30" ? 30000 : seconds == "60" ? 60000 : seconds == "120" ? 120000 : 0;
}
inline bool authorized(bool active, const std::string& expected, const std::string& token,
    const std::string& origin, const std::string& host, const std::string& ap, const std::string& sta) {
  return active && !expected.empty() && expected == token &&
    (host == ap || (!sta.empty() && host == sta)) &&
    (origin.empty() || origin == "http://" + host);
}
}
