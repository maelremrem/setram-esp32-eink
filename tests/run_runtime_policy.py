#!/usr/bin/env python3
"""Portable visible-feed and boot state tests; no panel/radio claims."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
header = root / 'lib/PaintPolicy/PaintPolicy.h'
if not header.exists():
    raise SystemExit('FAIL: stable visible-feed policy is missing (RED)')
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp) / 'runtime-policy'
    subprocess.run([os.getenv('CXX', 'g++'), '-std=c++11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-I'+str(root/'lib/PaintPolicy'), '-I'+str(root/'lib/DisplayPolicy'),
                    '-I'+str(root/'lib/Transit'), '-I'+str(root/'lib/BootFlow'),
                    str(root/'tests/test_runtime_policy.cpp'), str(root/'lib/Transit/Transit.cpp'),
                    '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
