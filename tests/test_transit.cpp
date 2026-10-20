#include "Transit.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#define CHECK(x) do { if (!(x)) { std::cerr << "FAIL line " << __LINE__ << ": " << #x << '\n'; std::exit(1); } } while (0)
typedef std::vector<uint8_t> Bytes;
static Bytes cat(Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; }
static Bytes var(uint64_t v) {
    Bytes b;
    do { uint8_t c = v & 127; v >>= 7; b.push_back(c | (v ? 128 : 0)); } while (v);
    return b;
}
static Bytes scalar(unsigned f, uint64_t v) { return cat(var(f * 8), var(v)); }
static Bytes message(unsigned f, const Bytes& v) { return cat(cat(var(f * 8 + 2), var(v.size())), v); }
static Bytes text(unsigned f, const std::string& s) { return message(f, Bytes(s.begin(), s.end())); }
static Bytes event(int64_t t) { return scalar(2, static_cast<uint64_t>(t)); }
static Bytes stopUpdate(int64_t t, const std::string& stop = "S", unsigned relationship = 0) {
    return cat(cat(text(4, stop), message(3, event(t))), scalar(5, relationship));
}
static Bytes trip(const std::string& route = "R", int direction = 1, unsigned relationship = 0) {
    Bytes b = cat(text(1, "trip"), cat(text(5, route), scalar(4, relationship)));
    return direction < 0 ? b : cat(b, scalar(6, direction));
}
static Bytes entity(const Bytes& stops, const Bytes& descriptor = trip(), bool deleted = false) {
    // Stops before descriptor deliberately exercises arbitrary protobuf field order.
    return cat(cat(text(1, "entity"), message(3, cat(stops, message(1, descriptor)))), scalar(2, deleted));
}
static Bytes feed(const Bytes& entities, int incrementality = -1) {
    Bytes header = cat(text(1, "2.0"), scalar(3, 5000000000ULL));
    if (incrementality >= 0) header = cat(header, scalar(2, incrementality));
    return cat(entities, message(1, header));
}
static bool decode(const Bytes& bytes, transit::Result& result, const std::string& route = "R", int direction = 1, int64_t now = 5000000000LL) {
    return transit::decode(bytes.data(), bytes.size(), "S", route, direction, now, result);
}
static void testAbsoluteDeparture() {
    transit::Result result;
    CHECK(decode(feed(message(2, entity(message(2, stopUpdate(5000000061LL))))), result));
    CHECK(result.timestamp == 5000000000ULL);
    CHECK(result.departures.size() == 1);
    CHECK(result.departures[0].time == 5000000061LL);
    std::cout << "PASS absolute departure and header\n";
}
static void testFilters() {
    transit::Result r;
    const Bytes stops = message(2, stopUpdate(5000000060LL));
    CHECK(decode(feed(message(2, entity(stops, trip("other")))), r));
    CHECK(r.departures.empty());
    CHECK(decode(feed(message(2, entity(stops, trip("R", 0)))), r));
    CHECK(r.departures.empty());
    CHECK(decode(feed(message(2, entity(stops, trip("R", -1)))), r));
    CHECK(r.departures.empty());
    CHECK(decode(feed(message(2, entity(stops, trip("other", -1)))), r, "", -1));
    CHECK(r.departures.size() == 1);
    for (unsigned status : {3u, 7u}) {
        CHECK(decode(feed(message(2, entity(stops, trip("R", 1, status)))), r));
        CHECK(r.departures.empty());
    }
    CHECK(decode(feed(message(2, entity(stops, trip(), true))), r));
    CHECK(r.departures.empty());
    for (unsigned status : {1u, 2u}) {
        CHECK(decode(feed(message(2, entity(message(2, stopUpdate(5000000060LL, "S", status))))), r));
        CHECK(r.departures.empty());
    }
    CHECK(decode(feed(message(2, entity(message(2, stopUpdate(5000000060LL, "OTHER"))))), r));
    CHECK(r.departures.empty());
    std::cout << "PASS route/direction/cancellation/deletion/stop filters\n";
}
static void testEarliestThree() {
    Bytes entities;
    for (int64_t offset : {400, 120, 120, -1, 60, 0, 300})
        entities = cat(entities, message(2, entity(message(2, stopUpdate(5000000000LL + offset)))));
    transit::Result r;
    CHECK(decode(feed(entities), r));
    CHECK(r.departures.size() == 3);
    CHECK(r.departures[0].time == 5000000000LL);
    CHECK(r.departures[1].time == 5000000060LL);
    CHECK(r.departures[2].time == 5000000120LL);
    // Thousands of matching stops must not grow decoder working storage.
    Bytes stops;
    for (int i = 5000; i >= 1; --i) stops = cat(stops, message(2, stopUpdate(5000000000LL + i)));
    CHECK(decode(feed(message(2, entity(stops))), r));
    CHECK(r.departures.size() == 3 && r.departures[0].time == 5000000001LL);
    r.maxDepartures = 15;
    CHECK(decode(feed(message(2, entity(stops))), r));
    CHECK(r.departures.size() == 15 && r.departures[14].time == 5000000015LL);
    r.maxDepartures = 0;
    CHECK(!decode(feed(entities), r));
    std::cout << "PASS sorted unique future departures with bounded configurable limit\n";
}
static void testEventChoice() {
    transit::Result r;
    Bytes stop = cat(text(4, "S"), cat(message(2, event(5000000100LL)), message(3, event(5000000200LL))));
    CHECK(decode(feed(message(2, entity(message(2, stop)))), r));
    CHECK(r.departures.size() == 1 && r.departures[0].time == 5000000200LL);
    stop = cat(text(4, "S"), cat(message(2, event(5000000100LL)), message(3, scalar(1, 60))));
    CHECK(decode(feed(message(2, entity(message(2, stop)))), r));
    CHECK(r.departures.size() == 1 && r.departures[0].time == 5000000100LL);
    stop = cat(text(4, "S"), message(3, scalar(1, 60)));
    CHECK(decode(feed(message(2, entity(message(2, stop)))), r));
    CHECK(r.departures.empty());
    CHECK(decode(feed(message(2, entity(message(2, stopUpdate(-1))))), r, "R", 1, -2));
    CHECK(r.departures.size() == 1 && r.departures[0].time == -1);
    CHECK(decode(feed(message(2, entity(message(2, stopUpdate(INT64_MAX))))), r));
    CHECK(r.departures[0].time == INT64_MAX);
    std::cout << "PASS departure preference, absolute arrival fallback, signed int64\n";
}
static void testMalformed() {
    transit::Result r; r.timestamp = 123; r.departures.push_back(transit::Departure{456});
    CHECK(!transit::decode(nullptr, 1, "S", "R", 1, 0, r));
    std::vector<Bytes> bad = {
        Bytes{}, Bytes{0}, Bytes{0x0a, 0x80}, Bytes{0x0a, 0x7f},
        feed(Bytes{}, 1), feed(Bytes{}, 2), message(1, Bytes{}),
        message(2, entity(message(2, stopUpdate(5000000100LL)))),
        feed(message(2, message(3, message(1, trip())))), // missing entity id
        feed(message(2, cat(text(1, "x"), message(3, message(2, stopUpdate(5000000100LL)))))), // missing trip
        cat(feed(Bytes{}), Bytes{0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2}),
        cat(feed(Bytes{}), cat(var(1000 * 8), Bytes(10, 0x80))),
        cat(feed(Bytes{}), cat(var(1000 * 8 + 2), var(UINT64_MAX))),
        cat(feed(Bytes{}), Bytes{0x0e}), // invalid wire type
        feed(scalar(2, 1)), // entity has wrong wire
        message(1, cat(text(1, "2.0"), text(3, "timestamp"))),
        feed(message(2, entity(message(2, cat(text(4, "S"), message(3, text(1, "invalid delay"))))))),
        message(1, cat(text(1, "2.0"), scalar(4, 1))),
        feed(message(2, cat(text(1, "x"), scalar(4, 1)))),
        feed(message(2, entity(Bytes{}, cat(trip(), scalar(1, 1))))),
        feed(message(2, entity(Bytes{}, cat(trip(), scalar(2, 1))))),
        feed(message(2, entity(Bytes{}, cat(trip(), scalar(3, 1))))),
        feed(message(2, entity(Bytes{}, cat(trip(), scalar(7, 1))))),
        feed(message(2, cat(text(1, "x"), message(3, cat(message(1, trip()), scalar(3, 1)))))),
        feed(message(2, cat(text(1, "x"), message(3, cat(message(1, trip()), text(4, "bad")))))),
        feed(message(2, entity(message(2, cat(stopUpdate(5000000100LL), text(1, "bad")))))),
        feed(message(2, entity(message(2, cat(stopUpdate(5000000100LL), scalar(1, UINT64_MAX)))))),
        feed(message(2, entity(message(2, cat(stopUpdate(5000000100LL), scalar(6, 1)))))),
        feed(message(2, entity(message(2, cat(stopUpdate(5000000100LL), text(7, "bad"))))))
    };
    for (const Bytes& b : bad) {
        CHECK(!decode(b, r));
        CHECK(r.timestamp == 123 && r.departures.size() == 1 && r.departures[0].time == 456);
    }
    const Bytes valid = feed(message(2, entity(message(2, stopUpdate(5000000100LL)))));
    for (size_t i = 0; i < valid.size(); ++i) CHECK(!decode(Bytes(valid.begin(), valid.begin() + i), r));
    CHECK(decode(feed(Bytes{}, 0), r) && r.departures.empty());
    CHECK(decode(message(1, text(1, "2.0")), r) && r.timestamp == 0);
    std::cout << "PASS malformed wire, truncations, required fields, differential rejection, atomic output\n";
}
static void testUnknownFields() {
    Bytes unknown = cat(scalar(1000, UINT64_MAX), message(1001, Bytes{0, 0xff, 0x80}));
    unknown = cat(unknown, cat(var(1002 * 8 + 1), Bytes(8, 0xaa)));
    unknown = cat(unknown, cat(var(1003 * 8 + 5), Bytes(4, 0xaa)));
    // Legacy protobuf groups can occur in unknown extensions. Nesting is capped at 16.
    const Bytes group = cat(cat(var(1004 * 8 + 3), scalar(1, 42)), var(1004 * 8 + 4));
    unknown = cat(unknown, group);
    const Bytes descriptor = cat(trip(), unknown);
    const Bytes stop = cat(cat(text(4, "S"), message(3, cat(event(5000000060LL), unknown))), unknown);
    const Bytes update = cat(cat(message(1, descriptor), message(2, stop)), unknown);
    const Bytes e = cat(cat(text(1, "x"), message(3, update)), unknown);
    const Bytes h = cat(cat(text(1, "2.0"), scalar(3, UINT64_MAX)), unknown);
    const Bytes b = cat(cat(message(1, h), message(2, e)), unknown);
    transit::Result r;
    CHECK(decode(b, r));
    CHECK(r.timestamp == UINT64_MAX && r.departures.size() == 1 && r.departures[0].time == 5000000060LL);
    CHECK(!decode(cat(feed(Bytes{}), var(1004 * 8 + 4)), r));
    CHECK(!decode(cat(feed(Bytes{}), var(1004 * 8 + 3)), r));
    CHECK(!decode(cat(feed(Bytes{}), cat(var(1004 * 8 + 3), var(1005 * 8 + 4))), r));
    Bytes nested;
    for (int i = 0; i < 16; ++i) nested = cat(nested, var(1004 * 8 + 3));
    for (int i = 0; i < 16; ++i) nested = cat(nested, var(1004 * 8 + 4));
    CHECK(decode(cat(feed(Bytes{}), nested), r));
    nested = cat(cat(var(1004 * 8 + 3), nested), var(1004 * 8 + 4));
    CHECK(!decode(cat(feed(Bytes{}), nested), r));
    // Invalid timing still fails even when the entity is filtered out.
    Bytes broken = message(2, cat(text(4, "OTHER"), message(3, Bytes{0x10, 0x80})));
    CHECK(!decode(feed(message(2, entity(broken, trip("other"), true))), r));
    std::cout << "PASS unknown wire fields at every level, bounded groups, filtered malformed data\n";
}
int main() {
    testAbsoluteDeparture();
    testFilters();
    testEarliestThree();
    testEventChoice();
    testMalformed();
    testUnknownFields();
    CHECK(transit::minutesRemaining(100, 100) == 0);
    CHECK(transit::minutesRemaining(99, 100) == 0);
    CHECK(transit::minutesRemaining(101, 100) == 1);
    CHECK(transit::minutesRemaining(160, 100) == 1);
    CHECK(transit::minutesRemaining(161, 100) == 2);
    CHECK(transit::minutesRemaining(5000000061LL, 5000000000LL) == 2);
    CHECK(transit::minutesRemaining(std::numeric_limits<int64_t>::max(), std::numeric_limits<int64_t>::min()) == std::numeric_limits<int>::max());
    std::cout << "PASS countdown\n";
}
