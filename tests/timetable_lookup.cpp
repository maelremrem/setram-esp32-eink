#include <Timetable.h>
#include <ctime>
#include <cstdlib>
#include <iostream>
void query(int64_t now, const std::string& stop, const std::string& route, int direction, size_t limit,
           bool explicitDay = false, unsigned offset = 1) {
    const auto result = explicitDay ? timetable::day(stop, route, direction, now, offset, limit)
                                    : timetable::tomorrow(stop, route, direction, now, limit);
    std::cout << '[' << static_cast<int>(result.status);
    for (const auto time : result.departures) std::cout << ',' << time;
    std::cout << "]\n";
}
int main(int argc, char** argv) {
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--day")) {
        // Batch mode for official numeric IDs only; argv mode preserves arbitrary IDs.
        int64_t now; std::string stop, route; int direction; size_t limit; unsigned offset = 1;
        while (std::cin >> now >> stop >> route >> direction >> limit) {
            if (argc == 2 && !(std::cin >> offset)) return 2;
            query(now, stop, route, direction, limit, argc == 2, offset);
        }
        return 0;
    }
    if (argc != 6 && argc != 7) return 2;
    query(std::strtoll(argv[1], nullptr, 10), argv[2], argv[3], std::atoi(argv[4]),
          std::strtoull(argv[5], nullptr, 10), argc == 7,
          argc == 7 ? std::strtoul(argv[6], nullptr, 10) : 1);
}
