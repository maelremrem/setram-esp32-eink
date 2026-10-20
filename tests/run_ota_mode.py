#!/usr/bin/env python3
"""Real Session + unmodified ArduinoOTA.cpp, controlled radio/TCP/flash.
No ESP32 radio, actual firmware upload, flash write, or physical timing claim.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sanitize', action='store_true')
args = parser.parse_args()
source = root / 'lib/OtaMode/OtaMode.cpp'
if not source.exists():
    raise SystemExit('FAIL: opt-in Session implementation missing (RED)')
framework = Path(os.environ.get('ARDUINO_ESP32_FRAMEWORK', str(Path.home()/'.platformio/packages/framework-arduinoespressif32')))
ota = framework / 'libraries/ArduinoOTA/src'
if not (ota/'ArduinoOTA.cpp').exists():
    raise SystemExit('ArduinoOTA framework source not found; set ARDUINO_ESP32_FRAMEWORK')
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp)/'ota-mode'
    flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g'] if args.sanitize else []
    cmd = [os.environ.get('CXX', 'clang++' if args.sanitize else 'g++'), '-std=c++11',
           '-Wall', '-Wextra', '-Werror', '-pedantic', *flags,
           '-I'+str(root/'tests/ota_host'), '-I'+str(ota), '-I'+str(source.parent),
           str(root/'tests/test_ota_mode.cpp'), str(root/'tests/ota_host/platform.cpp'),
           str(source), str(ota/'ArduinoOTA.cpp'), '-lcrypto', '-o', str(binary)]
    subprocess.run(cmd, check=True)
    subprocess.run([str(binary)], check=True)
