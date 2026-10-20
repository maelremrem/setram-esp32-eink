#!/usr/bin/env python3
"""Test the production RSSI meter without radio simulation claims."""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    binary=Path(tmp)/'wifi-signal'
    subprocess.run([os.getenv('CXX','g++'),'-std=c++11','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'lib/WiFiSignal'),str(root/'tests/test_wifi_signal.cpp'),
                    '-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
