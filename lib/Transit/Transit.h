#ifndef TRANSIT_H
#define TRANSIT_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace transit {
struct Departure { int64_t time; };
struct Result { uint64_t timestamp; std::vector<Departure> departures; size_t maxDepartures = 3; };
// Decode a FULL_DATASET feed. On failure, result remains unchanged.
// Returns sorted unique departures >= now, default three. Set maxDepartures
// to 1..15 before decode. Storage is bounded; group nesting capped at 16.
// Unused length-delimited payloads are skipped as opaque bytes, not interpreted.
// Only absolute stop times are supported; freshness is the caller's responsibility.
bool decode(const uint8_t* data, size_t size, const std::string& stop,
            const std::string& route, int direction, int64_t now, Result& result);
// Nonnegative ceiling of seconds / 60, saturated to INT_MAX.
int minutesRemaining(int64_t target, int64_t now);
}
#endif
