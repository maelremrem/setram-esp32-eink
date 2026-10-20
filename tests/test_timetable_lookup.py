"""Exercise real embedded C++ lookup with explicitly synthetic GTFS fixtures."""
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from zoneinfo import ZoneInfo
import test_timetable_generator as generator_tests
from test_timetable_generator import fixture, ROOT

PARIS = ZoneInfo('Europe/Paris')


def epoch(value):
    return int(dt.datetime.fromisoformat(value).replace(tzinfo=PARIS).timestamp())


class Harness(unittest.TestCase):
    @classmethod
    def raw_fixture(cls):
        return fixture()

    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR'))
        cls.out = Path(cls.temp.name)
        module = generator_tests.GeneratorTests().generator()
        raw = cls.raw_fixture()
        module.emit(module.parse_feed(raw), raw, cls.out / 'fixture.h', cls.out / 'fixture.json',
                    source_url='https://example.org/SYNTHETIC.zip', generated_at='fixture')
        cls.runner = cls.out / 'lookup'
        cls.compilation = subprocess.run(['g++', '-std=c++11', '-Wall', '-Wextra', '-Werror', '-O2',
            '-I' + str(ROOT / 'lib/Timetable'), '-DTIMETABLE_DATA_HEADER="' + str(cls.out / 'fixture.h') + '"',
            str(ROOT / 'tests/timetable_lookup.cpp'), str(ROOT / 'lib/Timetable/Timetable.cpp'),
            '-o', str(cls.runner)], capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def query(self, now, stop='stop:01', route='route:01', direction=0, limit=15, offset=None):
        self.assertEqual(self.compilation.returncode, 0, self.compilation.stderr)
        now = epoch(now) if isinstance(now, str) else now
        args = [str(self.runner), str(now), stop, route, str(direction), str(limit)]
        if offset is not None:
            args.append(str(offset))
        return json.loads(subprocess.check_output(args, text=True))


class LookupTests(Harness):
    @classmethod
    def raw_fixture(cls):
        return fixture(calendar_dates=[
            dict(service_id='week', date='20260103', exception_type='1'),
            dict(service_id='week', date='20260105', exception_type='2')])

    def test_weekday_tomorrow(self):
        self.assertEqual(self.query('2026-01-01T23:59:00'), [0, epoch('2026-01-02T08:30:00')])

    def test_day_two_weekday_uses_requested_civil_date(self):
        self.assertEqual(self.query('2026-01-06T23:59:00', offset=2),
                         [0, epoch('2026-01-08T08:30:00')])

    def test_unsupported_day_offsets_fail_closed(self):
        for offset in (0, 3, 365, 2**32 - 1):
            with self.subTest(offset=offset):
                self.assertEqual(self.query('2026-01-06T12:00:00', offset=offset), [3])
                self.assertEqual(self.query('2026-12-31T12:00:00', offset=offset), [3])

    def test_calendar_exception_addition(self):
        self.assertEqual(self.query('2026-01-02T23:59:00'), [0, epoch('2026-01-03T08:30:00')])

    def test_calendar_exception_removal(self):
        self.assertEqual(self.query('2026-01-04T23:59:00'), [1])

    def test_day_two_calendar_exception_addition(self):
        self.assertEqual(self.query('2026-01-01T23:59:00', offset=2),
                         [0, epoch('2026-01-03T08:30:00')])

    def test_day_two_calendar_exception_removal(self):
        self.assertEqual(self.query('2026-01-03T23:59:00', offset=2), [1])

    def test_day_two_weekend_has_no_service(self):
        self.assertEqual(self.query('2026-01-08T23:59:00', offset=2), [1])

    def test_day_two_validity_is_exact_requested_date(self):
        self.assertEqual(self.query('2025-12-29T12:00:00', offset=2), [2])
        self.assertEqual(self.query('2025-12-30T12:00:00', offset=2),
                         [0, epoch('2026-01-01T08:30:00')])
        self.assertEqual(self.query('2026-12-29T12:00:00', offset=2),
                         [0, epoch('2026-12-31T08:30:00')])
        self.assertEqual(self.query('2026-12-30T12:00:00', offset=2), [2])

    def test_day_two_local_midnight_changes_target(self):
        self.assertEqual(self.query('2026-01-05T23:59:59', offset=2),
                         [0, epoch('2026-01-07T08:30:00')])
        self.assertEqual(self.query('2026-01-06T00:00:00', offset=2),
                         [0, epoch('2026-01-08T08:30:00')])

    def test_day_two_invalid_clock_is_empty(self):
        for now in (0, -1, 2**63 - 1):
            with self.subTest(now=now):
                self.assertEqual(self.query(now, offset=2), [3])

    def test_tomorrow_is_identical_to_day_one(self):
        for now in (0, '2025-12-30T12:00:00', '2026-01-01T23:59:00',
                    '2026-01-02T23:59:00', '2026-01-04T23:59:00', '2026-12-31T23:59:00'):
            with self.subTest(now=now):
                self.assertEqual(self.query(now), self.query(now, offset=1))

    def test_weekend_has_no_service(self):
        self.assertEqual(self.query('2026-01-09T23:59:00'), [1])

    def test_literal_identifiers_and_direction(self):
        for stop, route, direction in [('stop:1', 'route:01', 0), ('stop:01 ', 'route:01', 0),
                                       ('stop:01', 'route:1', 0), ('stop:01', 'route:01', 1),
                                       ('stop:01', 'route:01', 2), ('', '', -1)]:
            with self.subTest(ids=(stop, route, direction)):
                self.assertEqual(self.query('2026-01-01T12:00:00', stop, route, direction), [1])
        self.assertEqual(self.query('2026-01-01T12:00:00', route='', direction=-1),
                         [0, epoch('2026-01-02T08:30:00')])

    def test_clock_and_validity_fail_closed(self):
        self.assertEqual(self.query(0), [3])
        self.assertEqual(self.query(-1), [3])
        self.assertEqual(self.query(2**63 - 1), [3])
        self.assertEqual(self.query('2025-12-30T12:00:00'), [2])
        self.assertEqual(self.query('2026-12-31T12:00:00'), [2])
        self.assertEqual(self.query('2026-12-30T12:00:00'), [0, epoch('2026-12-31T08:30:00')])
        self.assertEqual(self.query('2026-01-01T12:00:00', limit=0), [1])


class SpilloverTests(Harness):
    @classmethod
    def raw_fixture(cls):
        return fixture(stop_times=[dict(trip_id='trip', stop_id='stop:01',
            departure_time=time, pickup_type='0') for time in ('00:30:00', '08:30:00', '25:00:07', '49:00:09')])

    def test_previous_service_days_midnight_spillover(self):
        self.assertEqual(self.query('2026-01-02T23:59:00'),
            [0, epoch('2026-01-03T01:00:07'), epoch('2026-01-03T01:00:09')])

    def test_only_next_civil_day_in_sorted_unique_order(self):
        self.assertEqual(self.query('2026-01-08T23:59:00'),
            [0, epoch('2026-01-09T00:30:00'), epoch('2026-01-09T01:00:07'),
             epoch('2026-01-09T01:00:09'), epoch('2026-01-09T08:30:00')])

    def test_no_spillover_past_feed_end(self):
        self.assertEqual(self.query('2026-12-31T23:59:00'), [2])

    def test_day_two_includes_two_previous_service_days(self):
        self.assertEqual(self.query('2026-01-01T23:59:00', offset=2),
            [0, epoch('2026-01-03T01:00:07'), epoch('2026-01-03T01:00:09')])

    def test_day_two_does_not_leak_spillover_past_feed_end(self):
        self.assertEqual(self.query('2026-12-30T23:59:00', offset=2), [2])


class DSTTests(Harness):
    @classmethod
    def raw_fixture(cls):
        return fixture(calendar=[dict(service_id='week', start_date='20260101', end_date='20261231',
            monday='0', tuesday='0', wednesday='0', thursday='0', friday='0', saturday='0', sunday='1')],
            stop_times=[dict(trip_id='trip', stop_id='stop:01', departure_time=time, pickup_type='0')
                        for time in ('00:30:00', '01:30:00', '02:30:00', '03:30:00', '23:30:00', '25:30:00')])

    def test_spring_noon_minus_12h_not_wall_clock_midnight(self):
        self.assertEqual(self.query('2026-03-28T23:59:00'),
            [0, epoch('2026-03-29T00:30:00'), epoch('2026-03-29T01:30:00'),
             epoch('2026-03-29T03:30:00'), epoch('2026-03-29T23:30:00')])

    def test_spring_early_service_time_belongs_to_previous_civil_day(self):
        self.assertEqual(self.query('2026-03-27T23:59:00'), [0, epoch('2026-03-28T23:30:00')])

    def test_fall_repeated_hour_keeps_two_distinct_epochs(self):
        repeated_second = int(dt.datetime(2026, 10, 25, 2, 30, tzinfo=PARIS, fold=1).timestamp())
        self.assertEqual(self.query('2026-10-24T23:59:00'),
            [0, epoch('2026-10-25T01:30:00'), epoch('2026-10-25T02:30:00'), repeated_second,
             epoch('2026-10-25T03:30:00'), epoch('2026-10-25T23:30:00')])

    def test_local_tomorrow_is_not_utc_tomorrow_or_now_plus_24h(self):
        self.assertEqual(self.query('2026-03-29T00:30:00'), [0, epoch('2026-03-30T01:30:00')])
        self.assertEqual(self.query('2026-10-25T00:30:00'), [0, epoch('2026-10-26T01:30:00')])

    def test_day_two_spring_preserves_noon_minus_12h(self):
        self.assertEqual(self.query('2026-03-27T23:59:00', offset=2),
            [0, epoch('2026-03-29T00:30:00'), epoch('2026-03-29T01:30:00'),
             epoch('2026-03-29T03:30:00'), epoch('2026-03-29T23:30:00')])

    def test_day_two_spring_early_service_uses_adjacent_calendar(self):
        self.assertEqual(self.query('2026-03-26T23:59:00', offset=2),
                         [0, epoch('2026-03-28T23:30:00')])

    def test_day_two_fall_preserves_both_repeated_hour_epochs(self):
        second = int(dt.datetime(2026, 10, 25, 2, 30, tzinfo=PARIS, fold=1).timestamp())
        self.assertEqual(self.query('2026-10-23T23:59:00', offset=2),
            [0, epoch('2026-10-25T01:30:00'), epoch('2026-10-25T02:30:00'), second,
             epoch('2026-10-25T03:30:00'), epoch('2026-10-25T23:30:00')])

    def test_day_two_is_not_now_plus_48_elapsed_hours(self):
        self.assertEqual(self.query('2026-03-28T23:59:00', offset=2),
                         [0, epoch('2026-03-30T01:30:00')])
        self.assertEqual(self.query('2026-10-24T00:30:00', offset=2),
                         [0, epoch('2026-10-26T01:30:00')])


class FeedWindowTests(Harness):
    @classmethod
    def raw_fixture(cls):
        return fixture(feed_info=[dict(feed_start_date='20260102', feed_end_date='20260131')],
            stop_times=[dict(trip_id='trip', stop_id='stop:01', departure_time='25:00:00', pickup_type='0')])

    def test_explicit_previous_service_still_contributes_on_first_valid_civil_day(self):
        self.assertEqual(self.query('2026-01-01T23:59:00'), [0, epoch('2026-01-02T01:00:00')])

    def test_expiry_applies_to_requested_civil_date_not_service_date(self):
        self.assertEqual(self.query('2026-01-31T23:59:00'), [2])

    def test_day_two_first_valid_date_accepts_previous_service(self):
        self.assertEqual(self.query('2025-12-31T23:59:00', offset=2),
                         [0, epoch('2026-01-02T01:00:00')])

    def test_day_two_feed_window_expiry_precedes_spillover(self):
        self.assertEqual(self.query('2026-01-30T23:59:00', offset=2), [2])


class BoundedTests(Harness):
    @classmethod
    def raw_fixture(cls):
        trips = [dict(trip_id=name, route_id='route:01', service_id='week', direction_id=direction)
                 for name, direction in [('zero', '0'), ('one', '1'), ('unknown', '')]]
        return fixture(trips=trips, stop_times=[dict(trip_id=trip['trip_id'], stop_id='stop:01',
            departure_time=f'{hour:02d}:00:00', pickup_type='0')
            for trip in trips for hour in range(23, -1, -1)])

    def test_sorted_unique_capped_to_15_for_any_direction(self):
        expected = [0] + [epoch(f'2026-01-02T{hour:02d}:00:00') for hour in range(15)]
        self.assertEqual(self.query('2026-01-01T20:00:00', direction=-1, limit=999), expected)
        self.assertEqual(self.query('2026-01-01T20:00:00', direction=-1, limit=3), expected[:4])
        self.assertEqual(self.query('2026-01-01T20:00:00', direction=0), expected)
        self.assertEqual(self.query('2026-01-01T20:00:00', direction=1), expected)

    def test_day_two_sort_dedupe_limit_and_direction_are_preserved(self):
        expected = [0] + [epoch(f'2026-01-08T{hour:02d}:00:00') for hour in range(15)]
        for direction in (-1, 0, 1):
            with self.subTest(direction=direction):
                self.assertEqual(self.query('2026-01-06T20:00:00', direction=direction,
                                            offset=2, limit=999), expected)
        self.assertEqual(self.query('2026-01-06T20:00:00', direction=-1, offset=2, limit=3), expected[:4])
        self.assertEqual(self.query('2026-01-06T20:00:00', offset=2, limit=0), [1])


class ExceptionOnlyTests(Harness):
    @classmethod
    def raw_fixture(cls):
        return fixture(calendar=None, calendar_dates=[dict(service_id='week', date='20260607', exception_type='1')],
            trips=[dict(trip_id='trip', route_id='route:01', service_id='week', direction_id='')])

    def test_unknown_direction_only_matches_wildcard(self):
        self.assertEqual(self.query('2026-06-06T20:00:00', direction=0), [1])
        self.assertEqual(self.query('2026-06-06T20:00:00', direction=1), [1])
        self.assertEqual(self.query('2026-06-06T20:00:00', direction=-1), [0, epoch('2026-06-07T08:30:00')])
        self.assertEqual(self.query('2026-06-07T20:00:00', direction=-1), [1])

    def test_day_two_exception_only_service_preserves_unknown_direction(self):
        self.assertEqual(self.query('2026-06-05T20:00:00', direction=0, offset=2), [1])
        self.assertEqual(self.query('2026-06-05T20:00:00', direction=-1, offset=2),
                         [0, epoch('2026-06-07T08:30:00')])


if __name__ == '__main__':
    unittest.main()
