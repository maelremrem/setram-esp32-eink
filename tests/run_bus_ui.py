#!/usr/bin/env python3
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
gfx = root / '.pio/libdeps/crowpanel_ssd1680/Adafruit GFX Library'
out = root / os.environ.get('BUS_UI_OUT', 'docs/screens')
out.mkdir(parents=True, exist_ok=True)
san = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.getenv('BUS_UI_SANITIZE') else []
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    binary = Path(tmp) / 'ui-test'
    subprocess.run([os.environ.get('CXX','g++'), '-std=c++11', '-DARDUINO=100',
        '-Dword(...)=makeWord(__VA_ARGS__)', # Match Arduino's function-like word macro.
        '-Wall', '-Wextra', '-Werror', '-I'+str(root/'tests/ui_host'), '-I'+str(gfx), '-I'+str(root/'lib/BusUI'),
        '-I'+str(root/'lib/DisplayPolicy'), str(root/'tests/test_bus_ui.cpp'),
        str(root/'lib/BusUI/BusUI.cpp'), str(gfx/'Adafruit_GFX.cpp'), '-o', str(binary)]+san, check=True)
    subprocess.run([str(binary),str(out)],check=True)

# Convert the actual renderer output, not a separately drawn mockup.
import struct
import zlib

def chunk(kind, data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)

# Remove only superseded generated five-choice menu previews.
for stale in out.glob('bus-ui-config-five-*'):
    if stale.suffix in ('.pbm', '.png'):
        stale.unlink()
pbms = sorted(out.glob('bus-ui-*.pbm'))
for pbm in pbms:
    tokens = pbm.read_text().split()
    assert tokens[0] == 'P1', pbm
    width, height = map(int,tokens[1:3])
    assert (width, height) == (250, 122), pbm
    pixels = [0 if p=='1' else 255 for p in tokens[3:]]
    assert len(pixels) == width*height
    assert all(pixels[y*width+x] == 0 for y in range(20) for x in (*range(181,190), *range(208,214),249)), \
        f'Battery lane is not solid header black: {pbm}'
    for scale in (1,4):
        rows=[]
        for y in range(height):
            row=bytes(p for p in pixels[y*width:(y+1)*width] for _ in range(scale))
            rows.extend([b'\x00'+row]*scale)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width*scale,height*scale,8,0,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')
        target=pbm.with_name(pbm.stem+('-4x' if scale==4 else '')+'.png')
        target.write_bytes(png)
        if os.getenv('BUS_UI_VERBOSE'):
            print(target)

# Keep historical battery fixture names, but prove the exported images no
# longer depend on the compatibility field (native planes are checked in C++).
for suffix in ('.pbm', '.png', '-4x.png'):
    baseline = (out / ('bus-ui-battery--1' + suffix)).read_bytes()
    for percent in (0, 1, 50, 76, 100, 101):
        target = out / (f'bus-ui-battery-{percent}' + suffix)
        assert target.read_bytes() == baseline, f'Battery changed exported preview: {target}'
print(f'Battery hidden: {len(pbms)} real framebuffer fixtures, invariant exported battery variants')

boot_fixtures = ['wifi', 'time', 'api', 'ready', 'error-wifi', 'error-time', 'error-api', 'error-long']
for fixture in boot_fixtures:
    for suffix in ('.pbm', '.png', '-4x.png'):
        assert (out / ('bus-ui-boot-' + fixture + suffix)).is_file(), fixture
print(f'Boot previews: {len(boot_fixtures)} real framebuffer fixtures, native and nearest-neighbour 4x')
new_fixtures = ['reference', *['signal-' + quality for quality in ('disconnected','0','1','2','3')],
                *['config-six-' + str(selected) for selected in range(6)],
                *['ota-' + stage for stage in ('preparing','ready','transfer','success','error','error-long')]]
for fixture in new_fixtures:
    for suffix in ('.pbm', '.png', '-4x.png'):
        assert (out / ('bus-ui-' + fixture + suffix)).is_file(), fixture
print('Topbar: disconnected/dot/1/2/3 fan arcs, right-aligned clock, invariant non-signal pixels')
print('Configuration: all 6 choices, 5 visible rows, OTA selected without clipping')
print('OTA: preparing/waiting/transfer/success/error, real fixture credentials, no transfer cancellation')
print(f'Previews: {len(pbms)} PBM + {len(pbms)*2} PNG (250x122 and nearest-neighbour 1000x488), {out}')
