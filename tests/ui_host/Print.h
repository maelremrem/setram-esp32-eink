#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
class Print { public: virtual ~Print() {} virtual size_t write(uint8_t)=0;
size_t print(const char* s) { size_t n=0; while(*s) n+=write(uint8_t(*s++)); return n; }
};
