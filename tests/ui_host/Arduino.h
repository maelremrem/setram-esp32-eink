#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
class __FlashStringHelper {};
class String : public std::string { public: using std::string::string; };
inline void yield() {}
