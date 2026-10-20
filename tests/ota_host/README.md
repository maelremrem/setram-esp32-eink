# OTA controlled-platform tests

Run from the project root:

```sh
python3 tests/run_ota_mode.py
python3 tests/run_ota_mode.py --sanitize
python3 tests/ota_host/compile_esp32.py
```

The first two commands compile the **actual OtaMode adapter and the installed,
unmodified Arduino ESP32 2.0.17 ArduinoOTA.cpp**. Wi-Fi/UDP/TCP/mDNS/CPU/Update
are controlled fixtures, not sockets or flash. MD5 uses OpenSSL EVP; the test
passes packets through real ArduinoOTA parsing and challenge-response code.
These checks do **not** establish actual ESP32 network uploads, flash integrity,
battery gains, physical panel behavior or timing. The sanitizer run uses clang
ASan + UBSan. Dependencies: C++ compiler, OpenSSL headers/libcrypto and the
PlatformIO framework (override location with ARDUINO_ESP32_FRAMEWORK).

The last command compiles only the adapter to a temporary ESP32-S3 object using
real Arduino/SDK headers and an existing compile_commands.json. It does not
modify application sources, trigger a full build, link firmware or flash.

## Integration and framework limits

- Public namespace/API: otaMode, Phase {Off, Waiting, Receiving, Success, Error},
  Session::begin(const char*, const char*), handle(), stop(), active() const,
  transferring() const, phase() const, detail() const, progress() const.
- Only call begin after prior HTTPS and panel work drains. Suspend new feeds
  while active, and gate handle while panel/network workers are busy.
  ArduinoOTA.handle() performs the transfer **synchronously**. Callbacks only
  mutate phase/detail/progress or defensively abort Update; **never UI/SPI**.
- There is no automatic enable, stored OTA preference or idle expiry. The
  caller supplies a fresh high-entropy temporary password of at least 12 bytes.
  Length validation is not a cryptographic strength estimator. ArduinoOTA
  challenge-response authentication is not encrypted transport or image signing.
- This adapter owns a fresh ArduinoOTAClass per activation, not the global
  ArduinoOTA object. Core 2.0.17 hostname/password setters are write-once;
  end() retains them, and setPassword("") does not clear an existing hash.
  stop still makes that requested call, then destroys the owned instance,
  releasing its hash/callbacks; the next begin gets fresh credentials/hostname.
  It does not promise allocator-memory zeroization. Do not run another OTA
  service against the shared Update/mDNS facilities.
- Save/restore uses the exact wifi_ps_type_t returned by WiFi.getSleep() and
  getCpuFrequencyMhz(), not a boolean sleep approximation. Setter results and
  readbacks are checked. Restoration failure returns false with a French detail,
  remains Off and blocks reentry until a subsequent stop restores the snapshot.
  Keep the session alive to retry; destructor cleanup cannot return failure.
- The pinned ArduinoOTA.begin() returns void and offers no initialized/bind
  getter. Adapter success confirms validated setup and invokes begin, but cannot
  independently certify UDP bind or mDNS advertisement success. Hardware LAN
  discovery/upload tests remain necessary. Timeout is explicitly 10000 ms.
- Before begin, setPartitionLabel("ota_filesystem_disabled") prevents a match
  to default_8MB's only filesystem, spiffs. That table has app0/app1 and no FAT
  partition. Actual Update.begin is called **before onStart**; thus onStart alone
  cannot be described as preflight rejection. Updater.cpp also falls back to a
  FAT partition regardless of that label on other tables. A defensive non-U_FLASH
  onStart abort handles that path before payload writes, preserves the French FS
  rejection through later errors, and never reports Success. The framework can
  still attempt TCP connection after the abort; it has no public start veto.
- The fixture deliberately permits size-zero updates and a UINT_MAX write
  report only to stress progress arithmetic. Those are not claims of valid real
  Update.begin/Update.write behavior. CPU/radio setters also have explicit
  failure and successful-but-unchanged readback modes for rollback tests.
