# CrowPanel controller adapter: full refresh only

Partial refresh has been removed from the adapter, not merely disabled. There
are no capability getters, runtime setters, boolean modes, numeric budgets,
interactive thresholds or differential waveform decisions. Obsolete
`CROWPANEL_ENABLE_PARTIAL` definitions are ignored, including `=1`; no legacy
macro enables a different adapter path. Unmodified vendor files still contain
unused routines and tables, but the adapter never calls their partial/fast
refresh functions.

## Public API and controller behavior

```cpp
namespace crowpanel {
void begin();
void present(const uint8_t* blackBits);
}
```

`begin()` enables screen power on GPIO7. `present()` accepts 4000 native
128×250, MSB-first black-bit bytes (1=black) from the application's rotation-3
canvas, whose visible landscape area is 250×122. It never changes the caller's
buffer. There is no refresh-mode argument or configuration API.

### JD79661

- First presentation follows Elecrow's official lifecycle: `EPD_Init()`,
  `EPD_Clear()` initializing both old `0x10` and new `0x13` planes to 4000 white
  bytes, then `EPD_Update()` for the startup baseline.
- The image is converted to white bits (1=white), reversing the **250 native
  gate rows**, not bit order or source columns. This retains the adapter's
  response to the reported landscape reflection; physical orientation still
  requires panel confirmation.
- Hidden native source columns 122–127 are forced white before comparison
  and transfer, even when caller padding is dirty.
- The first image is presented with full GC even if it is already white.
  Every subsequent changed normalized image uses `EPD_DisplayImage()` and
  `EPD_Update()`, retaining the original five GC LUTs and alternating GC phase.
  The white old-plane baseline remains untouched after startup; only the new
  4000-byte plane is transferred for each changed image.
- Identical normalized images, including padding-only changes, emit no SPI,
  reset or refresh. They do not advance the vendor GC phase. The cache is
  committed only after the image update completes.
- Initialization happens once; the controller remains in standby between
  images, without per-image reset or deep sleep. Documented AUTO `0x17,A5`
  performs PON → DRF → POF, switching off the booster after each refresh.

### SSD1680

The original full-only `EPD_Init()` → `EPD_DisplayImage(blackBits)` →
`EPD_Update()` → `EPD_Sleep()` path is unchanged. The vendor routine inverts
black bits and does not reverse rows. SSD retains its original pass-through
padding and repeated-image behavior; JD's normalized no-op cache is not applied
to SSD.

Both controllers retain the wrapper's bounded/yielding BUSY reads: HIGH means
busy on SSD, LOW means busy on JD. A BUSY wait exceeding 15 seconds logs a
variant-check diagnostic and requests restart. The panel worker and application
paint policy are separate from this adapter; the synchronous presentation call
is intended to remain owned by that worker.

## Pinned vendor sources

Official Elecrow source is pinned to commit
`11291e3e9a868be8943232b065f3e264dfaeeae3`:

- [JD startup example](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/blob/11291e3e9a868be8943232b065f3e264dfaeeae3/example/arduino-v1.2/main/main.ino): initialize/clear/update once, then display/update without reset or sleep.
- [JD vendor driver](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/blob/11291e3e9a868be8943232b065f3e264dfaeeae3/example/arduino-v1.2/main/EPD_Init.cpp): white startup planes and alternating GC LUTs.
- [SSD full-refresh example](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/tree/11291e3e9a868be8943232b065f3e264dfaeeae3/example/arduino/Example%20Code/2.13_Global_refresh).
- [JD datasheet V1.0.4](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/blob/11291e3e9a868be8943232b065f3e264dfaeeae3/Datasheet/EK79029DS-JD79661_Datasheet.pdf): printed p34 documents AUTO `0x17,A5`.

All eight local vendor files remain byte-for-byte identical to their pinned
upstream counterparts (only local `.cpp.inc` filenames differ). The runner
checks both the local bytes and upstream working copies against pinned git
blobs before independently compiling the JD reference. The published datasheet
omits some split-plane/LUT details used by Elecrow's actual driver; preserving
that driver does not substitute for physical panel qualification.

## Host verification

```sh
CROWPANEL_UPSTREAM="$TMPDIR/jd-official" python3 tests/run_crowpanel_driver.py
CXX=clang++ CROWPANEL_TEST_CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' CROWPANEL_UPSTREAM="$TMPDIR/jd-official" python3 tests/run_crowpanel_driver.py
CXX=g++ CROWPANEL_TEST_CXXFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' CROWPANEL_UPSTREAM="$TMPDIR/jd-official" python3 tests/run_crowpanel_driver.py
```

Current host runs pass with GCC and with Clang ASan/UBSan, including both
controllers, both obsolete-macro configurations and the pinned upstream checks.
GCC ASan/UBSan was also attempted, but this host lacks `libasan.so.8.0.0` and
the static `libasan` archive; that sanitizer configuration could not link. This
is a toolchain/runtime limitation, not a completed GCC sanitizer test.

The runner also discovers `$TMPDIR/jd-official` automatically when present.
Without an upstream clone, it explicitly reports skipped pinned-byte/reference
checks; normal adapter tests still run.

The harness compiles the real adapter/vendor code and decodes GPIO bit-banged
SPI edges, CS framing and DC. It checks:

- Exact public signatures and absence of removed partial APIs.
- Both controllers in default builds and builds defining obsolete
  `CROWPANEL_ENABLE_PARTIAL=1`, with the same full-only behavior.
- All 4000 image bytes, gate-row mirroring, polarity and immutable caller buffers.
- White JD startup planes, normalized identical/padding-only no-ops, and hidden
  padding remaining white after a real changed frame.
- Twenty one-pixel changes using full GC every time, exact new-plane-only
  sequence, AUTO, all five complete GC LUTs and uninterrupted alternating phase.
- Twenty fixture images: JD one reset, 180 commands and no sleep; SSD twenty
  resets, 300 commands and its original sleep lifecycle.
- Independently compiled pristine upstream JD startup plus twenty-frame full GC
  control traces, comparing every control/LUT byte and image-transfer length.
  Image geometry is verified separately because the adapter reverses JD rows.

TDD removal first reproduced a one-pixel update selecting a differential waveform
when compiled with the obsolete enabled macro, then reproduced the public
signature still accepting a mode flag. Both regressions now pass with the
full-only adapter. Experimental waveform, threshold, interactive and budget
tests have been deleted, not retained as opt-in coverage.

**Host protocol verification is not optical verification.** No board is flashed
by these tests. BUSY is held idle; the suite cannot establish physical
orientation, display recovery, ghosting, analog drive, refresh duration,
temperature behavior or standby current. These require tests on the actual
panel. Removal of partial refresh is not a claim that the reported display loss
has been physically cured.
