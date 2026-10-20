#include "OtaMode.h"
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <esp32-hal-cpu.h>
#include <cstring>
#include <new>

namespace otaMode {
namespace { Session* owner = nullptr; }
Session::~Session() {
  stop();
  // Destruction during handle()/callbacks is unsupported (main-loop ownership).
  // Release the guard even if a platform restoration failed.
  if (owner == this) owner = nullptr;
}

bool Session::restorePower() {
  if (!restorePending_) return true;
  const bool wifiOk = WiFi.setSleep(priorSleep_);
  const bool cpuOk = setCpuFrequencyMhz(priorCpu_);
  restorePending_ = !(wifiOk && cpuOk && WiFi.getSleep() == priorSleep_ &&
                      getCpuFrequencyMhz() == priorCpu_);
  if (restorePending_) detail_ = "Restauration Wi-Fi/CPU impossible; reessayer";
  else if (owner == this) owner = nullptr;
  return !restorePending_;
}

bool Session::begin(const char* hostname, const char* password) {
  if (phase_ != Phase::Off || restorePending_ || owner) return false;
  if (WiFi.status() != WL_CONNECTED) {
    detail_ = "Wi-Fi non connecte";
    return false;
  }
  if (!hostname || !hostname[0] || !password || std::strlen(password) < 12) {
    detail_ = "Hote ou mot de passe invalide (12 octets minimum)";
    return false;
  }
  priorSleep_ = WiFi.getSleep();
  priorCpu_ = getCpuFrequencyMhz();
  owner = this;
  restorePending_ = true;
  if (!WiFi.setSleep(false) || WiFi.getSleep() != WIFI_PS_NONE) {
    detail_ = "Impossible de desactiver la veille Wi-Fi";
    restorePower();
    return false;
  }
  if (!setCpuFrequencyMhz(240) || getCpuFrequencyMhz() != 240) {
    detail_ = "Impossible de regler le CPU a 240 MHz";
    restorePower();
    return false;
  }
  // Own a fresh actual ArduinoOTAClass rather than changing the global object:
  // 2.0.17 setters are write-once, end() does not clear hostname/password,
  // and setPassword("") cannot clear an existing hash. Destruction resets them.
  ota_ = new (std::nothrow) ArduinoOTAClass;
  if (!ota_) {
    detail_ = "Memoire OTA insuffisante";
    restorePower();
    return false;
  }
  ota_->setHostname(hostname);
  ota_->setPassword(password);
  ota_->setPort(3232);
  // default_8MB has no such filesystem label. Update ignores this for U_FLASH.
  // Updater.cpp may still select a FAT fallback, so onStart also rejects FS.
  ota_->setPartitionLabel("ota_filesystem_disabled");
  ota_->setRebootOnSuccess(true);
  ota_->setTimeout(10000);
  ota_->onStart([this] {
    if (ota_->getCommand() != U_FLASH) {
      // ArduinoOTA has ALREADY called Update.begin here; abort before writes.
      // Its synchronous _runUpdate may still attempt TCP connect afterwards.
      Update.abort();
      phase_ = Phase::Error;
      detail_ = "Mise a jour du systeme de fichiers interdite";
      progress_ = 0;
      return;
    }
    phase_ = Phase::Receiving;
    detail_ = "Reception du micrologiciel";
    progress_ = 0;
  });
  ota_->onProgress([this](unsigned received, unsigned total) {
    if (phase_ != Phase::Receiving) return;
    const uint64_t percent = total ? uint64_t(received) * 100 / total : 0;
    progress_ = percent > 100 ? 100 : unsigned(percent);
  });
  ota_->onEnd([this] {
    if (phase_ != Phase::Receiving) return;
    phase_ = Phase::Success;
    detail_ = "Mise a jour reussie; redemarrage";
    progress_ = 100;
  });
  ota_->onError([this](ota_error_t error) {
    // _runUpdate can emit END after CONNECT; retain the root failure.
    if (error == OTA_END_ERROR && phase_ == Phase::Error) return;
    phase_ = Phase::Error;
    progress_ = 0;
    if (error != OTA_AUTH_ERROR && ota_->getCommand() != U_FLASH) {
      detail_ = "Mise a jour du systeme de fichiers interdite";
      return;
    }
    switch (error) {
      case OTA_AUTH_ERROR: detail_ = "Echec d'authentification OTA"; break;
      case OTA_BEGIN_ERROR: detail_ = "Echec du demarrage de la mise a jour"; break;
      case OTA_CONNECT_ERROR: detail_ = "Connexion de transfert impossible"; break;
      case OTA_RECEIVE_ERROR: detail_ = "Reception interrompue"; break;
      case OTA_END_ERROR: detail_ = "Echec de validation du micrologiciel"; break;
      default: detail_ = "Erreur de mise a jour OTA"; break;
    }
  });
  ota_->begin();
  phase_ = Phase::Waiting;
  detail_ = "En attente de mise a jour";
  progress_ = 0;
  return true;
}
void Session::handle() { if (ota_) ota_->handle(); }
bool Session::stop() {
  if (phase_ == Phase::Receiving) return false;
  if (ota_) {
    ota_->end();
    ota_->setPassword("");
    delete ota_;
    ota_ = nullptr;
  }
  phase_ = Phase::Off;
  detail_ = "OTA desactivee";
  progress_ = 0;
  return restorePower();
}
bool Session::active() const { return phase_ != Phase::Off; }
bool Session::transferring() const { return phase_ == Phase::Receiving; }
Phase Session::phase() const { return phase_; }
const char* Session::detail() const { return detail_; }
unsigned Session::progress() const { return progress_; }
}  // namespace otaMode
