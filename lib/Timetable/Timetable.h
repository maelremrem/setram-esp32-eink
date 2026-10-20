#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace timetable {
enum class Status { Ready, NoService, Expired, InvalidClock };
struct Result {
    Status status;
    std::vector<int64_t> departures;
};
// Planned (not realtime) departures in the requested LOCAL civil day.
// Caller configures TZ=CET-1CEST,M3.5.0,M10.5.0/3 before querying.
// Exact opaque IDs; empty route and direction -1 are wildcards as in Transit.
// Requests are clamped to 15. Outside feed validity fails closed as Expired.
// Offset is a civil-day increment (1 or 2); all other offsets return InvalidClock.
Result day(const std::string& stop, const std::string& route, int direction,
           int64_t now, unsigned offset, size_t limit = 15);
// Identical to day(..., offset=1).
Result tomorrow(const std::string& stop, const std::string& route, int direction,
                int64_t now, size_t limit = 15);
}
