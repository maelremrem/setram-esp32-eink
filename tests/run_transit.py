#!/usr/bin/env python3
"""Build and run the decoder host tests (C++11, no Arduino dependencies)."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
if not (root / 'lib/Transit/Transit.cpp').exists():
    raise SystemExit('FAIL: Transit decoder implementation is missing (RED)')
with tempfile.TemporaryDirectory(prefix='transit-', dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp) / 'test_transit'
    command = [os.environ.get('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra',
               '-Werror', '-pedantic', '-g', '-I' + str(root / 'lib/Transit'),
               str(root / 'tests/test_transit.cpp'),
               str(root / 'lib/Transit/Transit.cpp'), '-o', str(binary)]
    if os.environ.get('TRANSIT_SANITIZE') == '1':
        command[1:1] = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all']
    subprocess.run(command, check=True)
    subprocess.run([str(binary)], check=True)
