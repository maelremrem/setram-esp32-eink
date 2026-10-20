#pragma once
#include "Arduino.h"
namespace otaFixture {
extern std::string hostname;
extern bool advertisedAuth;
extern unsigned mdnsBegins, mdnsEnds;
}
struct MDNSClass {
 void begin(const char* s) { otaFixture::hostname=s; ++otaFixture::mdnsBegins; }
 void enableArduino(uint16_t, bool auth) { otaFixture::advertisedAuth=auth; }
 void end() { ++otaFixture::mdnsEnds; }
};
extern MDNSClass MDNS;
