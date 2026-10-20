#!/usr/bin/env python3
"""Compile ONLY OtaMode against installed ESP32 headers, without build/flash.
Uses an existing PlatformIO compilation database; produces a temporary object.
This is an object compilation, NOT firmware link or hardware validation.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
root = Path(__file__).resolve().parents[2]
entries = json.loads((root / 'compile_commands.json').read_text())
entry = next(e for e in entries if e['file'].endswith('src/main.cpp'))
args = shlex.split(entry['command'])
compiler = Path.home()/'.platformio/packages/toolchain-xtensa-esp32s3/bin'/args[0]
args[0] = str(compiler)
framework = Path.home()/'.platformio/packages/framework-arduinoespressif32'
source = root/'lib/OtaMode/OtaMode.cpp'
# SCons compilation databases keep literal escaped quotes in -D flags.
args = [arg.replace('\\"', '"') for arg in args]
args[args.index(entry['file'])] = str(source)
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    args[args.index('-o')+1] = str(Path(tmp)/'OtaMode.o')
    args += ['-I'+str(framework/'libraries/ArduinoOTA/src'),
             '-I'+str(framework/'libraries/Update/src'),
             '-I'+str(framework/'libraries/WiFi/src'),
             '-Wall', '-Wextra', '-Werror']
    subprocess.run(args, cwd=root, check=True)
    print('PASS: OtaMode ESP32-S3 object compile against actual installed Arduino headers')
