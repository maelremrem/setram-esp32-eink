#pragma once
#include <cstdint>
#include <string>
namespace displayPolicy {
enum class FeedState { Missing, ClockInvalid, Stale, FutureRejected, FutureTolerated, Fresh };
// The official producer currently leads a synchronized clock by about 120 s.
// Tolerate at most 180 s explicitly, without rewriting any event timestamps.
// 'fresh' remains strict; consumers of 'usable' must expose FutureTolerated.
inline FeedState classify(uint64_t timestamp, int64_t now) {
  if (!timestamp) return FeedState::Missing;
  if (now <= 1600000000) return FeedState::ClockInvalid;
  const uint64_t clock = static_cast<uint64_t>(now);
  if (timestamp > clock) {
    const uint64_t ahead = timestamp - clock;
    if (ahead > 180) return FeedState::FutureRejected;
    if (ahead > 60) return FeedState::FutureTolerated;
  } else if (clock - timestamp > 180) return FeedState::Stale;
  return FeedState::Fresh;
}
inline bool fresh(uint64_t timestamp, int64_t now) {
  return classify(timestamp, now) == FeedState::Fresh;
}
inline bool usable(uint64_t timestamp, int64_t now) {
  const FeedState status = classify(timestamp, now);
  return status == FeedState::Fresh || status == FeedState::FutureTolerated;
}
inline uint64_t aheadSeconds(uint64_t timestamp, int64_t now) {
  if (now <= 1600000000 || timestamp <= static_cast<uint64_t>(now)) return 0;
  return timestamp - static_cast<uint64_t>(now);
}
inline std::string label(const std::string& input, size_t limit) {
  std::string out;
  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char c = input[i];
    if (c >= 32 && c < 127) out += char(c);
    else if (c == 0xc3 && i + 1 < input.size()) {
      unsigned char d = input[++i];
      if (d >= 0xa8 && d <= 0xab) out += 'e';
      else if (d >= 0xa0 && d <= 0xa5) out += 'a';
      else if (d == 0xa7) out += 'c';
      else if (d >= 0xb9 && d <= 0xbc) out += 'u';
      else if (d >= 0xae && d <= 0xaf) out += 'i';
      else if (d >= 0xb2 && d <= 0xb6) out += 'o';
      else out += '?';
    } else if ((c & 0xc0) != 0x80) out += '?';
  }
  if (out.size() > limit) out = limit >= 3 ? out.substr(0, limit-3) + "..." : out.substr(0, limit);
  return out;
}
}
