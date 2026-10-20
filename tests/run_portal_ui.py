#!/usr/bin/env python3
"""Real Adafruit GFX + ricmoo QRCode host rendering and independent decoding.

Install test-only dependencies: python -m pip install -r tests/portal-ui-requirements.txt
Requires the project's PlatformIO lib_deps to have been installed.
All previews use public fixtures, never live AP credentials.
"""
import os
from pathlib import Path
import subprocess
import tempfile
from PIL import Image
import zxingcpp

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / '.pio/libdeps/crowpanel_ssd1680'
GFX = DEPS / 'Adafruit GFX Library'
QR = DEPS / 'QRCode/src'
OUT = ROOT / 'docs/screens'
OUT.mkdir(parents=True, exist_ok=True)
SANITIZE = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.getenv('PORTAL_UI_SANITIZE') else []
assert (ROOT / 'lib/PortalUI/PortalUI.h').exists(), 'Missing PortalUI renderer: onboarding not implemented'
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp) / 'portal-ui-test'
    qr_obj = Path(tmp) / 'qrcode.o'
    subprocess.run([os.environ.get('CC', 'gcc'), '-std=c99', '-I'+str(QR),
                    '-c', str(QR/'qrcode.c'), '-o', str(qr_obj)]+SANITIZE, check=True)
    subprocess.run([os.environ.get('CXX', 'g++'), '-std=c++11', '-DARDUINO=100',
                    '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'tests/ui_host'), '-I'+str(GFX),
                    '-I'+str(QR), '-I'+str(ROOT/'lib/PortalUI'), '-I'+str(ROOT/'lib/Navigation'),
                    str(ROOT/'tests/test_portal_ui.cpp'), str(ROOT/'lib/PortalUI/PortalUI.cpp'),
                    str(GFX/'Adafruit_GFX.cpp'), str(qr_obj), '-o', str(binary)]+SANITIZE, check=True)
    subprocess.run([str(binary), str(OUT)], check=True)

expected = {'join': 'WIFI:T:WPA;S:BusNotifier-ABCDEF;P:123456ABCDEF;;',
            'url': 'http://192.168.4.1', 'error': 'http://192.168.4.1',
            'escaped': r'WIFI:T:WPA;S:Bus\;QR\\Net;P:semi\;colon\:12;;'}
for fixture, payload in expected.items():
    pbm = OUT / ('portal-ui-'+fixture+'.pbm')
    with Image.open(pbm) as image:
        assert image.size == (250, 122)
        image.save(pbm.with_suffix('.png'))
        preview = image.resize((1000, 488), Image.Resampling.NEAREST)
        preview.save(pbm.with_name(pbm.stem+'-4x.png'))
        for frame in (image, preview):
            decoded = zxingcpp.read_barcodes(frame)
            assert len(decoded)==1 and decoded[0].text==payload, f'{fixture}: QR did not decode correctly'
    print(f'{fixture}: QR decoded at native and 4x sizes; preview {pbm.stem}-4x.png')
print('Portal UI tests: PASS')
