#pragma once
#include <cstdint>
// Declared deterministic entropy fixture; production uses ESP32 hardware RNG.
inline uint32_t esp_random(){static uint32_t next=123;return ++next;}
