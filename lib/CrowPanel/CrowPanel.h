#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
// Visible landscape area: 250 x 122; controller memory: 128 x 250.
namespace crowpanel {
void begin();
// Present 4000 native MSB-first black-bit bytes using full refresh only.
// JD suppresses identical images after white-padding normalization.
void present(const uint8_t* blackBits);
}
