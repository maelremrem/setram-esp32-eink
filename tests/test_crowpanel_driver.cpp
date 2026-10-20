// Exercise the real wrapper, vendor initialization/LUT and bit-banged SPI.
#include "CrowPanel.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <type_traits>
static_assert(std::is_same_v<decltype(&crowpanel::begin), void (*)()>, "begin must have no configuration arguments");
static_assert(std::is_same_v<decltype(&crowpanel::present), void (*)(const uint8_t*)>,
              "present must expose only the framebuffer, without mode or budget flags");
#ifdef CROWPANEL_REFERENCE
#include "EPD_Init.h"
namespace crowpanel {
void begin() { pinMode(7, OUTPUT); digitalWrite(7, HIGH); }
void present(const uint8_t* blackBits) {
  static bool initialized = false;
  if (!initialized) { EPD_Init(); EPD_Clear(); EPD_Update(); initialized = true; }
  uint8_t image[4000];
  for (unsigned i=0; i<4000; ++i) image[i] = ~blackBits[i];
  EPD_DisplayImage(image); EPD_Update();
}
}
#endif
struct Command { uint8_t reg; std::vector<uint8_t> data; };
static std::vector<Command> trace;
static int pins[40] = {};
static uint8_t byte;
static unsigned bits, resets;
static uint32_t clockMs;
HostSerial Serial;
HostESP ESP;
void HostSerial::println(const char*) {}
void HostESP::restart() { throw std::runtime_error("unexpected BUSY timeout"); }
void pinMode(int, int) {}
uint32_t millis() { return clockMs; }
void delay(uint32_t ms) { clockMs += ms; }
int digitalRead(int) {
#ifdef EPAPER_JD79661
  return HIGH;
#else
  return LOW;
#endif
}
static void require(bool ok, const std::string& message) {
  if (!ok) throw std::runtime_error(message);
}
void digitalWrite(int pin, int level) {
  if (pin == 10 && level == LOW && pins[pin] != LOW) ++resets;
  if (pin == 14 && level == LOW) { byte = 0; bits = 0; }
  if (pin == 12 && level == HIGH && pins[pin] == LOW && pins[14] == LOW) {
    byte = (byte << 1) | pins[11]; ++bits;
  }
  if (pin == 14 && level == HIGH && pins[14] == LOW) {
    require(bits == 8, "SPI transfer did not contain exactly eight rising edges");
    if (pins[13] == LOW) trace.push_back({byte, {}});
    else { require(!trace.empty(), "data before command"); trace.back().data.push_back(byte); }
  }
  pins[pin] = level;
}
static const Command& findLast(uint8_t reg) {
  for (auto it = trace.rbegin(); it != trace.rend(); ++it) if (it->reg == reg) return *it;
  throw std::runtime_error("missing command " + std::to_string(reg));
}
static unsigned count(uint8_t reg) {
  return std::count_if(trace.begin(), trace.end(), [reg](const Command& c){ return c.reg == reg; });
}
static std::array<uint8_t, 4000> fixture(unsigned seed) {
  std::array<uint8_t, 4000> b{};
  // Raw GFXcanvas1(128,250), rotation 3: (logical x,y) -> (y,249-x).
  // Deliberately asymmetric sparse markers and all rows; padding stays white.
  for (unsigned x = 0; x < 250; ++x) {
    unsigned y = (x * 37 + seed) % 122;
    b[(249-x)*16 + y/8] |= 0x80 >> (y%8);
  }
  return b;
}
static void mirror() {
  auto b = fixture(9), original = b;
  crowpanel::begin(); crowpanel::present(b.data());
#ifdef EPAPER_JD79661
  const auto& output = findLast(0x13).data;
  require(output.size() == 4000, "JD frame must contain exactly 4000 bytes");
  // Correct reported landscape left/right reflection: reverse native gate rows,
  // NOT bit order or native source columns (those flip landscape vertically).
  for (unsigned row = 0; row < 250; ++row)
    for (unsigned col = 0; col < 16; ++col)
      require(output[row*16+col] == uint8_t(~b[(249-row)*16+col]),
              "JD landscape mirror regression: wrong gate row at " + std::to_string(row));
  require(findLast(0x00).data == std::vector<uint8_t>({0xF7,0x8A}), "vendor PSR changed");
#else
  const auto& output = findLast(0x24).data;
  require(output.size() == 4000, "SSD frame must contain exactly 4000 bytes");
  for (unsigned i=0; i<4000; ++i) require(output[i] == uint8_t(~b[i]), "SSD bytes changed");
#endif
  require(b == original, "caller framebuffer was mutated");
}
static void noop() {
  auto b = fixture(9);
  crowpanel::begin(); crowpanel::present(b.data());
  const auto commands = trace.size();
  for (unsigned i=0; i<20; ++i) crowpanel::present(b.data());
#ifdef EPAPER_JD79661
  require(trace.size() == commands, "identical JD image must not refresh");
#else
  require(trace.size() > commands, "SSD remains explicitly full-only");
#endif
}
static void padding() {
  crowpanel::begin();
  auto b = fixture(9), original = b;
  for (unsigned r=0; r<250; ++r) b[r*16+15] |= 0x3F;
  original = b;
  crowpanel::present(b.data());
  require(b==original, "padding caller buffer mutated");
#ifdef EPAPER_JD79661
  for (unsigned r=0; r<250; ++r)
    require((findLast(0x13).data[r*16+15] & 0x3F)==0x3F, "JD must force all six hidden source columns white");
  const auto n = trace.size();
  for (unsigned r=0; r<250; ++r) b[r*16+15] &= 0xC0;
  crowpanel::present(b.data());
  require(trace.size()==n, "padding-only changes must not refresh");
  // Padding stays white after a genuine visible change; old RAM stays at startup white.
  b[0] ^= 0x80;
  crowpanel::present(b.data());
  for (unsigned r=0; r<250; ++r) {
    require((findLast(0x10).data[r*16+15] & 0x3F)==0x3F, "startup old-plane padding not white");
    require((findLast(0x13).data[r*16+15] & 0x3F)==0x3F, "GC new padding not white");
  }
#else
  require(b==original, "SSD caller buffer mutated");
#endif
}
static void lifecycle() {
  crowpanel::begin();
  for (unsigned frame=0; frame<20; ++frame) {
    auto b = fixture(frame);
    crowpanel::present(b.data());
#ifdef EPAPER_JD79661
    require(resets == 1, "JD must initialize once, not reset between frames");
    require(count(0x10) == 1, "JD old RAM plane was never initialized to white");
    const auto& old = findLast(0x10).data;
    require(old.size()==4000 && std::all_of(old.begin(), old.end(), [](uint8_t v){return v==0xFF;}),
            "JD old plane must be completely initialized");
    require(count(0x07)==0, "JD enters deep sleep between successive frames");
    require(count(0x13)==frame+2, "missing startup new plane or frame transfer");
    const auto firstNew = std::find_if(trace.begin(), trace.end(), [](const Command& c){return c.reg==0x13;});
    require(firstNew!=trace.end() && firstNew->data.size()==4000 &&
            std::all_of(firstNew->data.begin(), firstNew->data.end(), [](uint8_t v){return v==0xFF;}),
            "JD startup new plane must be completely initialized white");
    require(count(0x17)==frame+2, "missing official startup clear refresh or frame refresh");
    require(findLast(0x17).data==std::vector<uint8_t>({0xA5}), "AUTO must power off after refresh");
    // Official startup clear uses GC once. Every image alternates the original LUTs.
    require(findLast(0x22).data.size()==56 && findLast(0x23).data.size()==56, "incomplete GC LUT");
    require(findLast(0x22).data[1]==(frame%2==0 ? 0x10 : 0x20), "GC alternation differs from upstream");
    require(findLast(0x23).data[1]==(frame%2==0 ? 0x20 : 0x10), "GC alternation differs from upstream");
    const auto& image = findLast(0x13).data;
    require(image.size()==4000, "repeated frame truncated");
    for (unsigned row=0; row<250; ++row) for (unsigned col=0; col<16; ++col)
      require(image[row*16+col]==uint8_t(~b[(249-row)*16+col]), "repeated frame data corrupted");
#else
    require(resets==frame+1, "SSD lifecycle changed");
    require(count(0x10)==frame+1, "SSD sleep lifecycle changed");
#endif
  }
  std::printf("20 frames, resets=%u, commands=%zu\n", resets, trace.size());
}
static void fullOnly() {
  crowpanel::begin();
  std::array<uint8_t,4000> b{};
  crowpanel::present(b.data());
#ifdef EPAPER_JD79661
  require(findLast(0x20).data[3]==0x14 && count(0x17)==2,
          "first white image requires startup clear and a full image refresh");
#endif
  for (unsigned frame=1; frame<=20; ++frame) {
    b[249*16] ^= 0x80;
    trace.clear();
    crowpanel::present(b.data());
#ifdef EPAPER_JD79661
    require(findLast(0x20).data[3]==0x14, "every changed JD image must use GC, even with obsolete macro enabled");
    require(count(0x10)==0 && count(0x13)==1 && count(0x17)==1,
            "full frame must retain vendor new-plane-only convention");
    for (uint8_t reg : {0x20,0x21,0x22,0x23,0x24})
      require(findLast(reg).data.size()==56 && findLast(reg).data[3]==0x14,
              "every frame must contain all five full GC LUTs");
    require(findLast(0x22).data[1]==(frame%2 ? 0x20 : 0x10) &&
            findLast(0x23).data[1]==(frame%2 ? 0x10 : 0x20), "GC phase must alternate forever");
    require(findLast(0x17).data==std::vector<uint8_t>({0xA5}), "GC must use vendor AUTO");
    require(trace.size()==8 && resets==1 && count(0x07)==0, "full refresh retains exact JD standby sequence");
    const auto commands = trace.size();
    crowpanel::present(b.data());
    require(trace.size()==commands, "identical image must not emit SPI or advance GC phase");
#else
    require(findLast(0x22).data==std::vector<uint8_t>({0xF4}), "SSD must retain full activation");
    require(resets==frame+1 && count(0x10)==1, "SSD must retain reset/update/sleep lifecycle");
#endif
  }
}
int main(int argc, char** argv) {
  try {
    require(argc==2, "usage: test_crowpanel_driver mirror|lifecycle");
    if (std::string(argv[1])=="full-only") fullOnly();
    else if (std::string(argv[1])=="mirror") mirror();
    else if (std::string(argv[1])=="padding") padding();
    else if (std::string(argv[1])=="noop") noop();
    else if (std::string(argv[1])=="lifecycle") lifecycle();
    else if (std::string(argv[1])=="trace") {
      crowpanel::begin();
      for (unsigned i=0; i<20; ++i) { auto b=fixture(i); crowpanel::present(b.data()); }
      for (const auto& c : trace) {
        std::printf("%02x:", c.reg);
        // Geometry is checked separately. Compare every command and all
        // initialization/LUT bytes against unmodified upstream sources.
        if (c.reg==0x10 || c.reg==0x13) std::printf("%zu bytes",c.data.size());
        else for (auto v : c.data) std::printf("%02x",v);
        std::printf("\n");
      }
    }
    else throw std::runtime_error("unknown test");
    std::printf("PASS %s\n",argv[1]);
    return 0;
  } catch (const std::exception& e) { std::fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
