#include "Timetable.h"
#include <algorithm>
#include <ctime>
#include <limits>
#ifndef TIMETABLE_DATA_HEADER
#define TIMETABLE_DATA_HEADER "TimetableData.h"
#endif
#include TIMETABLE_DATA_HEADER

namespace timetable {
namespace {
uint32_t varint(const uint8_t*& p) {
    uint32_t result = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
        byte = *p++;
        result |= uint32_t(byte & 127) << shift;
        shift += 7;
    } while (byte & 128);
    return result;
}
uint32_t dateKey(const std::tm& day) {
    return uint32_t(day.tm_year + 1900) * 10000 + uint32_t(day.tm_mon + 1) * 100 + day.tm_mday;
}
bool active(uint32_t index, const std::tm& day) {
    const auto* service = data::kServices + index * 5;
    const auto date = dateKey(day);
    for (uint32_t i = 0; i < service[4]; ++i) {
        const auto* exception = data::kExceptions + (service[3] + i) * 2;
        if (exception[0] == date) return exception[1] == 1;
        if (exception[0] > date) break;
    }
    return date >= service[0] && date <= service[1] &&
           (service[2] & (1u << ((day.tm_wday + 6) % 7)));
}
void insert(std::vector<int64_t>& out, int64_t epoch, size_t limit) {
    const auto at = std::lower_bound(out.begin(), out.end(), epoch);
    if (at != out.end() && *at == epoch) return;
    if (out.size() == limit && at == out.end()) return;
    const auto index = at - out.begin();
    if (out.size() == limit) out.pop_back();
    out.insert(out.begin() + index, epoch);
}
}
Result day(const std::string& stop, const std::string& route, int direction,
           int64_t now, unsigned offset, size_t limit) {
    Result result{Status::InvalidClock, {}};
    if (offset < 1 || offset > 2) return result;
    // No synchronized clock is available before 2020. Cap at year 9999.
    if (now < 1577836800 || now > 253402214400LL ||
        now > static_cast<int64_t>(std::numeric_limits<time_t>::max())) return result;
    const auto current = static_cast<time_t>(now);
    std::tm target{};
    if (!localtime_r(&current, &target)) return result;
    target.tm_mday += static_cast<int>(offset);
    target.tm_hour = 12; target.tm_min = 0; target.tm_sec = 0; target.tm_isdst = -1;
    const auto noon = std::mktime(&target);
    if (noon == time_t(-1)) return result;
    const auto date = dateKey(target);
    result.status = Status::Expired;
    if (date < data::kValidStart || date > data::kValidEnd) return result;
    result.status = Status::NoService;
    limit = std::min<size_t>(limit, 15);
    if (!limit || stop.empty() || direction < -1 || direction > 1) return result;
    result.departures.reserve(limit);
    // Civil midnight bounds are not noon-minus-12h on DST transition days.
    std::tm boundary = target;
    boundary.tm_hour = 0; boundary.tm_isdst = -1;
    const int64_t begin = std::mktime(&boundary);
    boundary.tm_mday += 1; boundary.tm_isdst = -1;
    const int64_t end = std::mktime(&boundary);
    // GTFS times are elapsed seconds since service-day noon minus 12 hours.
    // Include previous SERVICE days for >=24h times, plus DST margin. A
    // spring-transition service day can also start in the previous civil day.
    for (int ago = -1; ago <= int(data::kMaxSeconds / 86400 + 1); ++ago) {
        std::tm serviceDay = target;
        serviceDay.tm_mday -= ago; serviceDay.tm_isdst = -1;
        const int64_t serviceNoon = std::mktime(&serviceDay);
        if (serviceNoon == -1) continue;
        for (uint32_t i = 0; i < data::kPatternCount; ++i) {
            const auto* pattern = data::kPatterns + i * 4;
            const auto* stopId = reinterpret_cast<const char*>(data::kStrings + pattern[0]);
            const auto* routeId = reinterpret_cast<const char*>(data::kStrings + pattern[1]);
            if (stop != stopId || (!route.empty() && route != routeId) ||
                (direction != -1 && direction + 1 != int(pattern[2]))) continue;
            auto* group = data::kGroups + pattern[3];
            auto count = varint(group);
            while (count--) {
                const auto service = varint(group);
                const auto offset = varint(group);
                if (!active(service, serviceDay)) continue;
                auto* times = data::kTimes + offset;
                auto timeCount = varint(times);
                uint32_t seconds = 0;
                while (timeCount--) {
                    seconds += varint(times);
                    const int64_t departure = serviceNoon - 43200 + seconds;
                    if (departure >= begin && departure < end) insert(result.departures, departure, limit);
                }
            }
        }
    }
    if (!result.departures.empty()) result.status = Status::Ready;
    return result;
}
Result tomorrow(const std::string& stop, const std::string& route, int direction,
                int64_t now, size_t limit) {
    return day(stop, route, direction, now, 1, limit);
}
}
