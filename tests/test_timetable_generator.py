"""Generator tests use tiny declared GTFS fixtures, never masquerading as real data."""
import csv
import importlib.util
import io
from pathlib import Path
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('generate_timetable', ROOT / 'tools/generate_timetable.py')


def fixture(**overrides):
    tables = {
        'agency': [dict(agency_id='a', agency_timezone='Europe/Paris')],
        'stops': [dict(stop_id='stop:01')],
        'routes': [dict(route_id='route:01', agency_id='a')],
        'trips': [dict(trip_id='trip', route_id='route:01', service_id='week', direction_id='0')],
        'stop_times': [dict(trip_id='trip', stop_id='stop:01', departure_time='08:30:00', pickup_type='0')],
        'calendar': [dict(service_id='week', start_date='20260101', end_date='20261231',
                          monday='1', tuesday='1', wednesday='1', thursday='1', friday='1', saturday='0', sunday='0')],
        'calendar_dates': [],
        'feed_info': [dict(feed_start_date='20260101', feed_end_date='20261231', feed_version='fixture')],
    }
    tables.update(overrides)
    data = io.BytesIO()
    with zipfile.ZipFile(data, 'w') as archive:
        for name, rows in tables.items():
            if rows is None:
                continue
            output = io.StringIO()
            if rows:
                writer = csv.DictWriter(output, fieldnames=rows[0].keys())
                writer.writeheader()
                writer.writerows(rows)
            archive.writestr(name + '.txt', output.getvalue())
    return data.getvalue()


class GeneratorTests(unittest.TestCase):
    def generator(self):
        self.assertTrue(Path(SPEC.origin).exists(), 'calendar-aware timetable generator is missing')
        module = importlib.util.module_from_spec(SPEC)
        SPEC.loader.exec_module(module)
        return module

    def test_weekly_calendar_and_literal_ids(self):
        model = self.generator().parse_feed(fixture())
        self.assertEqual(model['timezone'], 'Europe/Paris')
        self.assertEqual(model['services']['week']['weekdays'], 31)
        self.assertEqual(model['departures'][('stop:01', 'route:01', 0)]['week'], [30600])
        self.assertEqual(model['valid_start'], '20260101')
        self.assertEqual(model['valid_end'], '20261231')

    def test_exception_only_service_preserves_additions_removals(self):
        model = self.generator().parse_feed(fixture(calendar=None, feed_info=None,
            calendar_dates=[dict(service_id='week', date='20260329', exception_type='1'),
                            dict(service_id='week', date='20260330', exception_type='2')]))
        self.assertEqual(model['services']['week']['weekdays'], 0)
        self.assertEqual(model['services']['week']['exceptions'], {'20260329': 1, '20260330': 2})
        self.assertEqual((model['valid_start'], model['valid_end']), ('20260329', '20260329'))

    def test_missing_direction_stays_unknown_and_nonboarding_is_excluded(self):
        data = fixture(trips=[dict(trip_id='trip', route_id='route:01', service_id='week', direction_id='')],
            stop_times=[dict(trip_id='trip', stop_id='stop:01', departure_time='25:00:07', pickup_type='0'),
                        dict(trip_id='trip', stop_id='stop:01', departure_time='08:30:00', pickup_type='1')])
        model = self.generator().parse_feed(data)
        self.assertEqual(model['departures'], {('stop:01', 'route:01', -1): {'week': [90007]}})

    def test_invalid_inputs_fail_closed(self):
        cases = [dict(agency=[dict(agency_id='a', agency_timezone='UTC')]),
                 dict(calendar=None, calendar_dates=None, feed_info=None),
                 dict(calendar_dates=[dict(service_id='week', date='20260230', exception_type='1')]),
                 dict(calendar_dates=[dict(service_id='week', date='20260329', exception_type='3')]),
                 dict(trips=[dict(trip_id='trip', route_id='route:01', service_id='missing', direction_id='0')]),
                 dict(stop_times=[dict(trip_id='trip', stop_id='stop:01', departure_time='08:60:00')]),
                 dict(frequencies=[dict(trip_id='trip', start_time='08:00:00', end_time='10:00:00', headway_secs='600')]),
                 dict(trips=[dict(trip_id='trip', route_id='route:01', service_id='week', direction_id='2')]),
                 dict(stop_times=[dict(trip_id='trip', stop_id='stop:01', departure_time='')])]
        for override in cases:
            with self.subTest(override=override):
                with self.assertRaises(ValueError):
                    self.generator().parse_feed(fixture(**override))

    def test_generated_flash_artifact_is_deterministic_bounded_and_public(self):
        import tempfile
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            module = self.generator()
            raw = fixture()
            model = module.parse_feed(raw)
            meta = module.emit(model, raw, out / 'data.h', out / 'source.json',
                               source_url='https://example.org/official.zip', generated_at='2026-01-01T00:00:00Z')
            first = (out / 'data.h').read_bytes()
            module.emit(model, raw, out / 'data.h', out / 'source.json',
                        source_url='https://example.org/official.zip', generated_at='2026-01-01T00:00:00Z')
            self.assertEqual(first, (out / 'data.h').read_bytes())
            import hashlib
            self.assertEqual(meta['source_sha256'], hashlib.sha256(raw).hexdigest())
            self.assertEqual(meta['departure_count'], 1)
            self.assertLess(meta['embedded_bytes'], 1000000)
            self.assertIn('https://example.org/official.zip', first.decode())
            self.assertEqual(meta['valid_end'], '20261231')


if __name__ == '__main__':
    unittest.main()
