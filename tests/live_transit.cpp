#include <Transit.h>
#include <DisplayPolicy.h>
#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>
#include <ctime>
int main(int argc, char** argv) {
  if(argc != 4) { std::cerr << "Usage: live_transit feed.pb stop_id route_id\n"; return 2; }
  std::ifstream file(argv[1], std::ios::binary);
  if(!file) return 2;
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), {});
  transit::Result result;
  auto now = std::time(nullptr);
  if(!transit::decode(data.data(), data.size(), argv[2], argv[3], -1, now, result)) {
    std::cerr << "Real feed decoding FAILED\n"; return 1;
  }
  std::cout << "Decoded " << data.size() << " real bytes; timestamp " << result.timestamp
            << "; age " << (int64_t(now) - int64_t(result.timestamp))
            << " s; fresh=" << displayPolicy::fresh(result.timestamp, now)
            << "; stop " << argv[2] << "; upcoming " << result.departures.size() << '\n';
  for(const auto& d : result.departures)
    std::cout << "Departure " << d.time << "; " << transit::minutesRemaining(d.time, now) << " min\n";
}
