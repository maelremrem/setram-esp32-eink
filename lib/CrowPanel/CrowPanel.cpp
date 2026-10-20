#include "CrowPanel.h"
#include <cstring>
#if defined(EPAPER_JD79661)
#include "vendor/jd79661/EPD_Init.h"
#else
#include "vendor/ssd1680/EPD_Init.h"
#endif
// Vendor busy loops lack timeout/yield. Override their pin read, retaining
// exact official initialization and waveform bytes for each controller.
static int checkedBusy() {
  static uint32_t since = 0;
  int level = digitalRead(BUSY);
#ifdef EPAPER_JD79661
  bool busy = level == LOW;
#else
  bool busy = level == HIGH;
#endif
  if (!busy) since = 0;
  else {
    if (!since) since = millis();
    if (millis() - since > 15000) {
      Serial.println("EPD BUSY timeout: verifier la variante du panneau");
      ESP.restart();
    }
    delay(1);
  }
  return level;
}
#undef EPD_ReadBUSY
#define EPD_ReadBUSY checkedBusy()
#ifdef EPAPER_JD79661
#include "vendor/jd79661/spi.cpp.inc"
#include "vendor/jd79661/EPD_Init.cpp.inc"
#else
#include "vendor/ssd1680/spi.cpp.inc"
#include "vendor/ssd1680/EPD_Init.cpp.inc"
#endif
namespace crowpanel {
void begin() { pinMode(7, OUTPUT); digitalWrite(7, HIGH); }
void present(const uint8_t* blackBits) {
#ifdef EPAPER_JD79661
  // Follow Elecrow arduino-v1.2 startup: initialize both RAM planes and
  // refresh the white baseline once, then keep the controller in standby.
  // AUTO 0x17/A5 already powers the booster off after each full refresh.
  static bool initialized = false;
  if (!initialized) {
    EPD_Init();
    EPD_Clear();
    EPD_Update();
    initialized = true;
  }
  // JD uses 1=white. Reverse 250 native gate rows for the rotation-3 canvas,
  // retaining source/bit order and forcing hidden columns 122..127 white.
  uint8_t whiteBits[4000];
  for (size_t row = 0; row < 250; ++row) {
    for (size_t col = 0; col < 16; ++col)
      whiteBits[row * 16 + col] = ~blackBits[(249 - row) * 16 + col];
    whiteBits[row * 16 + 15] |= 0x3F;
  }
  static uint8_t previous[4000];
  static bool havePrevious = false;
  if (havePrevious && std::memcmp(previous, whiteBits, sizeof(previous)) == 0) return;
  EPD_DisplayImage(whiteBits);
  EPD_Update();
  std::memcpy(previous, whiteBits, sizeof(previous));
  havePrevious = true;
#else
  EPD_Init();
  EPD_DisplayImage(blackBits);
  EPD_Update();
  EPD_Sleep();
#endif
}
}
