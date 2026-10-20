#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <Print.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t*)(p))
#define LOW 0
#define HIGH 1
#define INPUT 0
#define INPUT_PULLUP 2
#define OUTPUT 1
class __FlashStringHelper {};
class String : public std::string {
public:
 using std::string::string;
 String()=default;
 String(const std::string& s):std::string(s){}
 explicit String(unsigned long x):std::string(std::to_string(x)){}
 explicit String(unsigned x):std::string(std::to_string(x)){}
 explicit String(int x):std::string(std::to_string(x)){}
 using std::string::operator=;
};
class Stream : public Print {
public:
 using Print::write;
 virtual size_t write(const uint8_t*, size_t)=0;
 virtual int available()=0;
 virtual int read()=0;
 virtual int peek()=0;
 virtual void flush()=0;
};
extern uint32_t hostMillis;
inline uint32_t millis(){ return hostMillis; }
inline void delay(uint32_t ms){ hostMillis+=ms; }
inline void pinMode(int,int){}
void digitalWrite(int,int);
extern bool hostPressed[48];
inline int digitalRead(int pin){
 if(pin!=9) return hostPressed[pin] ? LOW : HIGH;
#ifdef EPAPER_SSD1680
 return pin==9 ? LOW : HIGH;
#else
 (void)pin; return HIGH;
#endif
}
inline void delayMicroseconds(uint32_t){}
inline void yield(){}
inline void configTzTime(const char*,const char*,const char*){}
struct HostSerial {
 void begin(int){}
 void println(const char*){}
 void printf(const char*, ...){}
};
struct HostESP { uint64_t getEfuseMac(){return 1;} void restart(){std::abort();} };
extern HostSerial Serial;
extern HostESP ESP;
