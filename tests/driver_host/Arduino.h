#pragma once
#include <cstddef>
#include <cstdint>
#define LOW 0
#define HIGH 1
#define INPUT 0
#define OUTPUT 1
void pinMode(int pin, int mode);
void digitalWrite(int pin, int value);
int digitalRead(int pin);
uint32_t millis();
void delay(uint32_t ms);
inline void delayMicroseconds(uint32_t) {}
struct HostSerial { void println(const char* text); };
struct HostESP { void restart(); };
extern HostSerial Serial;
extern HostESP ESP;
