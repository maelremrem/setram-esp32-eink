#!/usr/bin/env python3
"""Execute actual main, renderer, decoder and full-only driver on host GPIO.
Wi-Fi/HTTP/clock/NVS/task primitives are controlled fixtures, not radio proof.
"""
import os
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
gfx=root/'.pio/libdeps/crowpanel_ssd1680/Adafruit GFX Library'
qr=root/'.pio/libdeps/crowpanel_ssd1680/QRCode'
# Small declared GTFS fixture: predictable schedules, not live SETRAM data.
import importlib.util
import csv
import io
import zipfile
spec=importlib.util.spec_from_file_location('runtime_timetable_generator',root/'tools/generate_timetable.py')
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
def timetable_fixture():
    tables={
        'agency':[dict(agency_timezone='Europe/Paris')],
        'stops':[dict(stop_id='1079'),dict(stop_id='other')],
        'routes':[dict(route_id='C4')],
        'trips':[dict(trip_id='a',route_id='C4',service_id='daily',direction_id='0'),
                 dict(trip_id='b',route_id='C4',service_id='daily',direction_id='1')],
        'stop_times':[dict(trip_id='a',stop_id='1079',departure_time=f'{6+i//2:02}:{30*(i%2):02}:00') for i in range(16)]
                    +[dict(trip_id='b',stop_id='1079',departure_time='05:30:00'),
                      dict(trip_id='a',stop_id='other',departure_time='09:45:00')],
        'calendar':[dict(service_id='daily',start_date='20260101',end_date='20261231',
            **{day:'1' for day in generator.WEEKDAYS})],
        'calendar_dates':[dict(service_id='daily',date='20261010',exception_type='2')],
        'feed_info':[dict(feed_start_date='20260101',feed_end_date='20261231')],
    }
    buffer=io.BytesIO()
    with zipfile.ZipFile(buffer,'w') as archive:
        for name,rows in tables.items():
            text=io.StringIO()
            writer=csv.DictWriter(text,fieldnames=list(rows[0]))
            writer.writeheader(); writer.writerows(rows)
            archive.writestr(name+'.txt',text.getvalue())
    return buffer.getvalue()
with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
    data=timetable_fixture()
    fixture_header=Path(tmp)/'RuntimeTimetableData.h'
    generator.emit(generator.parse_feed(data),data,fixture_header,Path(tmp)/'source.json',
        source_url='https://example.org/declared-runtime-fixture.zip',generated_at='2026-01-01T00:00:00Z')
    qr_object=Path(tmp)/'qrcode.o'
    subprocess.run(['gcc','-std=c99','-I'+str(qr/'src'),'-c',str(qr/'src/qrcode.c'),'-o',str(qr_object)],check=True)
    for variant in ('JD79661','SSD1680'):
        binary=Path(tmp)/variant
        includes=[root/'tests/runtime_host',root/'tests/ui_host',root/'tests/build_fixture',root/'include',gfx,qr/'src']
        includes += [root/'lib'/name for name in ('BusUI','CrowPanel','Transit','DisplayPolicy','PaintPolicy','BootFlow','PortalUI','PortalSettings','Navigation','Battery','Timetable','OtaMode','WiFiSignal')]
        sources=[root/'tests/test_main_runtime.cpp',root/'lib/BusUI/BusUI.cpp',root/'lib/CrowPanel/CrowPanel.cpp',root/'lib/Transit/Transit.cpp',root/'lib/PortalUI/PortalUI.cpp',root/'lib/Timetable/Timetable.cpp',root/'lib/OtaMode/OtaMode.cpp',gfx/'Adafruit_GFX.cpp',qr_object]
        subprocess.run([os.getenv('CXX','g++'),'-std=c++11','-DARDUINO=100','-Dword(...)=makeWord(__VA_ARGS__)',
                        '-DEPAPER_'+variant,'-DTIMETABLE_DATA_HEADER="'+str(fixture_header)+'"','-Wall','-Wextra','-Werror']+
                        ['-I'+str(p) for p in includes]+[str(p) for p in sources]+['-o',str(binary)],check=True)
        for scenario in (os.getenv('RUNTIME_SCENARIOS','').split(',') if os.getenv('RUNTIME_SCENARIOS') else ('ota-chord','ota-drain','ota-transfer','ota-offline','wifi-signal','ota-opt-in','idle-loop','portal-idle','worker-idle','mixed-tomorrow','mixed-after','mixed-expired','network-async','display-async','dual','config-transitions','dual-composition','transitions-deadlines','dual-repeat-back','dual-warnings','swap-nvs-failure','config-six','full-only-legacy-nvs','tomorrow','tomorrow-future','tomorrow-none','tomorrow-expired','schedule-back','success','failure','empty','rejected','malformed','portal','navigate','paint-input')):
            print(variant,scenario,flush=True)
            subprocess.run([str(binary),scenario],check=True)
