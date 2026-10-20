#include "Transit.h"
#include <climits>
#include <cstring>
#include <utility>

namespace transit {
namespace {
struct Slice { const uint8_t* data; size_t size; };
struct Field { unsigned number, wire; uint64_t value; Slice bytes; };
// Index/remaining-length arithmetic avoids overflow and never trusts feed lengths.
class Reader {
    Slice input;
    size_t pos;
public:
    bool good;
    explicit Reader(Slice s) : input(s), pos(0), good(true) {}
    bool varint(uint64_t& value) {
        value = 0;
        for (unsigned i = 0; i < 10; ++i) {
            if (pos == input.size) return good = false;
            const uint8_t byte = input.data[pos++];
            if (i == 9 && byte > 1) return good = false;
            value |= static_cast<uint64_t>(byte & 127) << (7 * i);
            if (!(byte & 128)) return true;
        }
        return good = false;
    }
    bool next(Field& f) { return field(f, 0, false); }
private:
    bool field(Field& f, unsigned depth, bool insideGroup) {
        if (!good || pos == input.size) return false;
        uint64_t key;
        if (!varint(key) || (key >> 3) == 0 || (key >> 3) > 0x1fffffff) return good = false;
        f.number = static_cast<unsigned>(key >> 3);
        f.wire = key & 7;
        f.value = 0;
        f.bytes = Slice{nullptr, 0};
        if (f.wire == 0) return varint(f.value);
        if (f.wire == 4) return insideGroup ? true : (good = false);
        if (f.wire == 3) {
            if (depth == 16) return good = false;
            Field nested;
            while (field(nested, depth + 1, true)) {
                if (nested.wire == 4) return nested.number == f.number ? true : (good = false);
            }
            return good = false; // unterminated group
        }
        uint64_t length;
        if (f.wire == 2) { if (!varint(length)) return false; }
        else if (f.wire == 1) length = 8;
        else if (f.wire == 5) length = 4;
        else return good = false;
        if (length > input.size - pos) return good = false;
        f.bytes = Slice{input.data + pos, static_cast<size_t>(length)};
        pos += static_cast<size_t>(length);
        return true;
    }
};
bool equal(Slice a, const std::string& b) {
    return a.size == b.size() && (a.size == 0 || std::memcmp(a.data, b.data(), a.size) == 0);
}
int64_t signedTime(uint64_t v) {
    // Defined conversion even for protobuf's negative, ten-byte int64 values.
    return v <= static_cast<uint64_t>(INT64_MAX) ? static_cast<int64_t>(v)
        : -1 - static_cast<int64_t>(~v);
}
bool header(Slice s, uint64_t& timestamp) {
    Reader r(s); Field f; bool version = false;
    while (r.next(f)) {
        if (f.number == 1) { if (f.wire != 2) return false; version = true; }
        else if (f.number == 2) { if (f.wire != 0 || f.value != 0) return false; }
        else if (f.number == 3) { if (f.wire != 0) return false; timestamp = f.value; }
        else if (f.number == 4 && f.wire != 2) return false;
    }
    return r.good && version;
}
struct Event { bool present; int64_t time; Event() : present(false), time(0) {} };
bool event(Slice s, Event& out) {
    Reader r(s); Field f;
    while (r.next(f)) {
        if (f.number == 2) {
            if (f.wire != 0) return false;
            out.present = true; out.time = signedTime(f.value);
        } else if ((f.number == 1 || f.number == 3 || f.number == 4) && f.wire != 0) return false;
    }
    return r.good;
}
bool stopTime(Slice s, const std::string& stop, Event& out) {
    Reader r(s); Field f; bool matches = false; uint64_t status = 0; Event arrival, departure;
    while (r.next(f)) {
        if (f.number == 4) { if (f.wire != 2) return false; matches = equal(f.bytes, stop); }
        else if (f.number == 2 || f.number == 3) {
            if (f.wire != 2 || !event(f.bytes, f.number == 2 ? arrival : departure)) return false;
        }
        else if (f.number == 5) { if (f.wire != 0) return false; status = f.value; }
        else if (f.number == 1) { if (f.wire != 0 || f.value > UINT32_MAX) return false; }
        else if (f.number == 6 && f.wire != 2) return false;
        else if (f.number == 7 && f.wire != 0) return false;
    }
    if (matches && status != 1 && status != 2) out = departure.present ? departure : arrival;
    return r.good;
}
bool descriptor(Slice s, const std::string& route, int direction, bool& matches) {
    Reader r(s); Field f; bool routeMatch = route.empty(), directionMatch = direction == -1;
    uint64_t status = 0;
    while (r.next(f)) {
        if (f.number == 5) { if (f.wire != 2) return false; routeMatch = route.empty() || equal(f.bytes, route); }
        else if (f.number == 6) { if (f.wire != 0 || f.value > UINT32_MAX) return false; directionMatch = direction == -1 || f.value == static_cast<uint64_t>(direction); }
        else if (f.number == 4) { if (f.wire != 0) return false; status = f.value; }
        else if ((f.number == 1 || f.number == 2 || f.number == 3 || f.number == 7) && f.wire != 2) return false;
    }
    matches = routeMatch && directionMatch && status != 3 && status != 7;
    return r.good;
}
void retain(Result& result, int64_t time, int64_t now) {
    if (time < now) return;
    size_t at = 0;
    while (at < result.departures.size() && result.departures[at].time < time) ++at;
    if (at < result.departures.size() && result.departures[at].time == time) return;
    if (at == result.maxDepartures) return;
    if (result.departures.size() < result.maxDepartures) result.departures.push_back(Departure{time});
    for (size_t i = result.departures.size() - 1; i > at; --i)
        result.departures[i] = result.departures[i - 1];
    result.departures[at] = Departure{time};
}
bool update(Slice s, const std::string& stop, const std::string& route, int direction, int64_t now, bool enabled, Result& result) {
    Reader first(s); Field f; bool hasTrip = false, matches = false;
    // Two bounded passes keep field order independent without retaining all stops.
    while (first.next(f)) {
        if (f.number == 1) {
            if (f.wire != 2 || !descriptor(f.bytes, route, direction, matches)) return false;
            hasTrip = true;
        } else if ((f.number == 3 || f.number == 6) && f.wire != 2) return false;
        else if ((f.number == 4 || f.number == 5) && f.wire != 0) return false;
    }
    if (!first.good || !hasTrip) return false;
    Reader r(s);
    while (r.next(f)) if (f.number == 2) {
        Event time;
        if (f.wire != 2 || !stopTime(f.bytes, stop, time)) return false;
        if (enabled && matches && time.present) retain(result, time.time, now);
    }
    return r.good;
}
bool entity(Slice s, const std::string& stop, const std::string& route, int direction, int64_t now, Result& result) {
    Reader first(s); Field f; bool id = false, deleted = false;
    while (first.next(f)) {
        if (f.number == 1) { if (f.wire != 2) return false; id = true; }
        else if (f.number == 2) { if (f.wire != 0) return false; deleted = f.value != 0; }
        else if (f.number >= 4 && f.number <= 8 && f.wire != 2) return false;
    }
    if (!first.good || !id) return false;
    Reader r(s);
    while (r.next(f)) if (f.number == 3) {
        if (f.wire != 2 || !update(f.bytes, stop, route, direction, now, !deleted, result)) return false;
    }
    return r.good;
}
} // namespace
bool decode(const uint8_t* data, size_t size, const std::string& stop,
            const std::string& route, int direction, int64_t now, Result& result) {
    if (!data || !size || result.maxDepartures < 1 || result.maxDepartures > 15) return false;
    Result candidate; candidate.timestamp = 0; candidate.maxDepartures = result.maxDepartures;
    candidate.departures.reserve(candidate.maxDepartures);
    Reader r(Slice{data, size}); Field f; bool hasHeader = false;
    while (r.next(f)) {
        if (f.number == 1) { if (f.wire != 2 || !header(f.bytes, candidate.timestamp)) return false; hasHeader = true; }
        else if (f.number == 2) { if (f.wire != 2 || !entity(f.bytes, stop, route, direction, now, candidate)) return false; }
    }
    if (!r.good || !hasHeader) return false;
    result = std::move(candidate);
    return true;
}
int minutesRemaining(int64_t target, int64_t now) {
    if (target <= now) return 0;
    const uint64_t seconds = static_cast<uint64_t>(target) - static_cast<uint64_t>(now);
    const uint64_t minutes = seconds / 60 + (seconds % 60 != 0);
    return minutes > static_cast<uint64_t>(INT_MAX) ? INT_MAX : static_cast<int>(minutes);
}
}
