"""Official artifact verification. No synthetic fixture substitutes for missing ZIP."""
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from zoneinfo import ZoneInfo
import test_timetable_generator as generator_tests
from test_timetable_generator import ROOT

PARIS = ZoneInfo('Europe/Paris')
SOURCE = ROOT / '.cache/setram/timetable-source.zip'


@unittest.skipUnless(SOURCE.exists(), 'Official source ZIP is absent; regenerate/download first')
class OfficialTimetableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = generator_tests.GeneratorTests().generator()
        cls.raw = SOURCE.read_bytes()
        cls.model = cls.module.parse_feed(cls.raw)
        cls.header = (ROOT / 'lib/Timetable/TimetableData.h').read_text()
        cls.meta = json.loads((ROOT / 'lib/Timetable/timetable-source.json').read_text())

    def test_generated_data_round_trips_every_official_departure(self):
        arrays = {name: list(map(int, re.findall(r'\d+', body)))
                  for name, body in re.findall(r'static const \w+ (k\w+)\[\] = \{(.*?)\};', self.header, re.S)}
        strings = bytes(arrays['kStrings'])

        def string(offset):
            return strings[offset:strings.index(b'\0', offset)].decode()

        def take(blob, position):
            value, shift = 0, 0
            while True:
                byte = blob[position]
                position += 1
                value |= (byte & 127) << shift
                if byte < 128:
                    return value, position
                shift += 7

        decoded = {}
        service_ids = sorted(self.model['services'])
        patterns = arrays['kPatterns']
        for i in range(0, len(patterns), 4):
            stop, route, direction, offset = patterns[i:i + 4]
            count, offset = take(arrays['kGroups'], offset)
            groups = {}
            for _ in range(count):
                sid, offset = take(arrays['kGroups'], offset)
                toffset, offset = take(arrays['kGroups'], offset)
                tcount, toffset = take(arrays['kTimes'], toffset)
                seconds, times = 0, []
                for _ in range(tcount):
                    delta, toffset = take(arrays['kTimes'], toffset)
                    seconds += delta
                    times.append(seconds)
                groups[service_ids[sid]] = times
            decoded[(string(stop), string(route), direction - 1)] = groups
        self.assertEqual(decoded, self.model['departures'])
        self.assertEqual(self.meta['source_sha256'], hashlib.sha256(self.raw).hexdigest())
        self.assertEqual(self.meta['pattern_count'], len(decoded))
        self.assertEqual(self.meta['departure_count'], sum(len(t) for g in decoded.values() for t in g.values()))
        actual_bytes = sum(len(values) * (1 if name in ('kStrings', 'kTimes', 'kGroups') else 4)
                           for name, values in arrays.items()) + 16
        self.assertEqual(self.meta['embedded_bytes'], actual_bytes)
        self.assertLess(actual_bytes, 1000000)
        self.assertEqual(self.meta['valid_end'], self.model['valid_end'])
        self.assertEqual(self.meta['valid_start'], self.model['valid_start'])
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as folder:
            header, metadata = Path(folder) / 'data.h', Path(folder) / 'source.json'
            self.module.emit(self.model, self.raw, header, metadata,
                source_url=self.meta['source_url'], generated_at=self.meta['generated_at'])
            self.assertEqual(header.read_text(), self.header)
            self.assertEqual(json.loads(metadata.read_text()), self.meta)

    def test_actual_cpp_matches_official_calendars_for_every_pattern(self):
        # All declared calendar exception dates, seven weekdays, DST, and boundaries.
        dates = {dt.datetime.strptime(value, '%Y%m%d').date()
                 for s in self.model['services'].values() for value in s['exceptions']}
        start = dt.datetime.strptime(self.model['valid_start'], '%Y%m%d').date()
        end = dt.datetime.strptime(self.model['valid_end'], '%Y%m%d').date()
        dates |= {start + dt.timedelta(days=i) for i in range(7)} | {start, end, end + dt.timedelta(days=1)}
        dates.add(dt.date(2026, 10, 25))
        max_seconds = self.meta['max_departure_seconds']
        cases, expected, day_cases = [], [], []
        for target in sorted(dates):
            key = target.strftime('%Y%m%d')
            now = int(dt.datetime.combine(target - dt.timedelta(days=1), dt.time(23, 59), PARIS).timestamp())
            begin = int(dt.datetime.combine(target, dt.time(), PARIS).timestamp())
            finish = int(dt.datetime.combine(target + dt.timedelta(days=1), dt.time(), PARIS).timestamp())
            active_services = []
            for ago in range(-1, max_seconds // 86400 + 2):
                day = target - dt.timedelta(days=ago)
                service_date = day.strftime('%Y%m%d')
                active = set()
                for sid, service in self.model['services'].items():
                    exception = service['exceptions'].get(service_date)
                    on = exception == 1 if exception else (service['start'] <= service_date <= service['end'] and
                                                          bool(service['weekdays'] & (1 << day.weekday())))
                    if on:
                        active.add(sid)
                base = int(dt.datetime.combine(day, dt.time(12), PARIS).timestamp()) - 43200
                active_services.append((base, active))
            for (stop, route, direction), groups in sorted(self.model['departures'].items()):
                self.assertFalse(any(c.isspace() for c in stop + route), 'Batch mode requires whitespace-free official IDs')
                cases.append(f'{now} {stop} {route} {direction} 15\n')
                for offset in (1, 2):
                    day_now = int(dt.datetime.combine(target - dt.timedelta(days=offset),
                                                      dt.time(23, 59), PARIS).timestamp())
                    day_cases.append(f'{day_now} {stop} {route} {direction} 15 {offset}\n')
                if not self.model['valid_start'] <= key <= self.model['valid_end']:
                    expected.append([2])
                    continue
                departures = set()
                for base, active in active_services:
                    for sid in active.intersection(groups):
                        departures.update(base + seconds for seconds in groups[sid] if begin <= base + seconds < finish)
                values = sorted(departures)[:15]
                expected.append([0] + values if values else [1])
        with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as folder:
            runner = Path(folder) / 'official-lookup'
            subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O2',
                '-I' + str(ROOT / 'lib/Timetable'), str(ROOT / 'tests/timetable_lookup.cpp'),
                str(ROOT / 'lib/Timetable/Timetable.cpp'), '-o', str(runner)], check=True)
            result = subprocess.run([str(runner)], input=''.join(cases), text=True, capture_output=True, check=True)
            day_result = subprocess.run([str(runner), '--day'], input=''.join(day_cases),
                                        text=True, capture_output=True, check=True)
        outputs = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(len(outputs), len(cases))
        for case, wanted, actual in zip(cases, expected, outputs):
            self.assertEqual(actual, wanted, case)
        day_outputs = [json.loads(line) for line in day_result.stdout.splitlines()]
        self.assertEqual(len(day_outputs), len(day_cases))
        day_expected = [wanted for wanted in expected for _ in (1, 2)]
        for case, wanted, actual in zip(day_cases, day_expected, day_outputs):
            self.assertEqual(actual, wanted, case)
        print(f'Official C++ verification: {len(cases)} queries, {len(dates)} civil dates, '
              f'{len(self.model["departures"])} exact stop/route/direction patterns')
        print(f'Official day(1)/day(2) C++ verification: {len(day_cases)} additional queries')


if __name__ == '__main__':
    unittest.main()
