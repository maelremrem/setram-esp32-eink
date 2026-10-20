#pragma once

#include <cstdint>
#include <esp_wifi_types.h>
class ArduinoOTAClass;

namespace otaMode {
enum class Phase { Off, Waiting, Receiving, Success, Error };

// Main-loop-owned; keep alive throughout synchronous handle(). ArduinoOTA
// transfers synchronously inside handle(); callbacks MUST NEVER call UI/SPI.
// Caller must first drain HTTPS/panel work, gate handle while workers are busy,
// and suspend new jobs throughout the active session. No persistence/idle timer.
// Uses a fresh owned ArduinoOTAClass per activation; the global stays unarmed.
// Password validation checks 12 bytes, not entropy: caller must generate a strong
// temporary secret. ArduinoOTA.begin() is void (no UDP/mDNS bind status getter).
// stop() returns false on restoration failure, remains Off, and permits retry
// via stop(); begin() refuses until that saved power state has been restored.
class Session {
 public:
  Session() = default;
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  bool begin(const char* hostname, const char* password);
  void handle();
  bool stop();
  bool active() const;
  bool transferring() const;
  Phase phase() const;
  const char* detail() const;
  unsigned progress() const;

 private:
  bool restorePower();
  bool restorePending_ = false;
  ArduinoOTAClass* ota_ = nullptr;
  Phase phase_ = Phase::Off;
  const char* detail_ = "OTA desactivee";
  unsigned progress_ = 0;
  wifi_ps_type_t priorSleep_ = WIFI_PS_NONE;
  uint32_t priorCpu_ = 0;
};
}  // namespace otaMode
