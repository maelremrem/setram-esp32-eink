#!/usr/bin/env python3
"""Generate the embedded SETRAM calendar timetable (standard library only)."""
import csv
import datetime as dt
import io
import re
import zipfile

WEEKDAYS = ('monday', 'tuesday', 'wednesday', 'thursday', 'friday', 'saturday', 'sunday')


def checked_date(value):
    if not re.fullmatch(r'[0-9]{8}', value):
        raise ValueError('Invalid GTFS date: ' + value)
    dt.datetime.strptime(value, '%Y%m%d')
    return value


def identifier(value):
    # IDs are opaque: never strip, normalize, case-fold, or infer them.
    if not value or '\0' in value:
        raise ValueError('Missing ID or embedded NUL')
    return value


def unique_rows(rows, field):
    result = {}
    for row in rows:
        key = identifier(row[field])
        if key in result:
            raise ValueError('Duplicate ' + field + ': ' + key)
        result[key] = row
    return result


def parse_feed(data):
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        tables = {name: list(csv.DictReader(io.TextIOWrapper(archive.open(name + '.txt'),
                  encoding='utf-8-sig', newline=''))) for name in
                  ('agency', 'stops', 'routes', 'trips', 'stop_times', 'calendar', 'calendar_dates', 'feed_info', 'frequencies')
                  if name + '.txt' in archive.namelist()}
    if not tables.get('agency') or any(row.get('agency_timezone') != 'Europe/Paris' for row in tables['agency']):
        raise ValueError('Only agency_timezone Europe/Paris is supported')
    if tables.get('frequencies'):
        raise ValueError('Frequency-based service cannot be represented as exact planned departures')
    if any(name not in tables for name in ('stops', 'routes', 'trips', 'stop_times')):
        raise ValueError('Missing required GTFS table')
    services = {}
    for sid, row in unique_rows(tables.get('calendar', []), 'service_id').items():
        start, end = checked_date(row['start_date']), checked_date(row['end_date'])
        if start > end or any(row[day] not in ('0', '1') for day in WEEKDAYS):
            raise ValueError('Invalid calendar range or weekday')
        services[sid] = dict(start=start, end=end,
            weekdays=sum(int(row[day]) << i for i, day in enumerate(WEEKDAYS)), exceptions={})
    for row in tables.get('calendar_dates', []):
        sid, date = identifier(row['service_id']), checked_date(row['date'])
        if row['exception_type'] not in ('1', '2'):
            raise ValueError('Invalid calendar exception type')
        service = services.setdefault(sid, dict(start='', end='', weekdays=0, exceptions={}))
        if date in service['exceptions']:
            raise ValueError('Duplicate calendar exception')
        service['exceptions'][date] = int(row['exception_type'])
    if not services:
        raise ValueError('Service calendar is required; never invent validity from download date')
    stops = unique_rows(tables['stops'], 'stop_id')
    routes = unique_rows(tables['routes'], 'route_id')
    trips = unique_rows(tables['trips'], 'trip_id')
    for trip in trips.values():
        if trip['route_id'] not in routes or trip['service_id'] not in services:
            raise ValueError('Unknown trip route/service')
        if trip.get('direction_id', '') not in ('', '0', '1'):
            raise ValueError('Invalid trip direction')
    departures = {}
    excluded = 0
    for row in tables['stop_times']:
        if row['trip_id'] not in trips or row['stop_id'] not in stops:
            raise ValueError('Unknown stop_time trip/stop')
        trip = trips[row['trip_id']]
        pickup = row.get('pickup_type', '') or '0'
        if pickup not in ('0', '1', '2', '3'):
            raise ValueError('Invalid pickup_type')
        if pickup != '0':
            # No boarding and reservation/driver arrangements are not normal buses.
            excluded += 1
            continue
        time = row['departure_time']
        if not re.fullmatch(r'[0-9]{1,3}:[0-9]{2}:[0-9]{2}', time):
            raise ValueError('Missing/invalid exact departure_time; interpolation is not supported')
        hours, minutes, seconds = map(int, time.split(':'))
        if hours > 167 or minutes > 59 or seconds > 59:
            raise ValueError('GTFS time outside supported 0..167:59:59 bound')
        direction = int(trip['direction_id']) if trip.get('direction_id') else -1
        key = (row['stop_id'], trip['route_id'], direction)
        departures.setdefault(key, {}).setdefault(trip['service_id'], set()).add(hours * 3600 + minutes * 60 + seconds)
    departures = {key: {service: sorted(seconds) for service, seconds in groups.items()}
                  for key, groups in departures.items()}
    info = next(iter(tables.get('feed_info', [])), {})
    calendar_bounds = [value for service in services.values() for value in (service['start'], service['end']) if value]
    addition_dates = [date for service in services.values() for date, kind in service['exceptions'].items() if kind == 1]
    bounds = calendar_bounds + addition_dates
    if not bounds:
        raise ValueError('No bounded service dates')
    start = checked_date(info.get('feed_start_date') or min(bounds))
    end = checked_date(info.get('feed_end_date') or max(bounds))
    if start > end:
        raise ValueError('Invalid feed validity range')
    return dict(timezone='Europe/Paris', services=services, departures=departures,
                valid_start=start, valid_end=end, feed_info=info,
                validity_source='feed_info/calendar' if info.get('feed_start_date') or info.get('feed_end_date') else 'calendar',
                source_stop_times=len(tables['stop_times']), excluded_pickups=excluded)


def emit(model, raw, header, metadata, *, source_url, generated_at):
    """Intern exact time lists and store unsigned LEB128 delta seconds in flash."""
    import hashlib
    import json
    from pathlib import Path

    def varint(value):
        out = bytearray()
        while value >= 128:
            out.append((value & 127) | 128)
            value >>= 7
        out.append(value)
        return out

    strings = bytearray()
    offsets = {}
    for value in sorted({s for key in model['departures'] for s in key[:2]}):
        offsets[value] = len(strings)
        strings.extend(value.encode('utf-8') + b'\0')
    services = []
    exceptions = []
    service_indices = {sid: i for i, sid in enumerate(sorted(model['services']))}
    for sid in sorted(model['services']):
        service = model['services'][sid]
        first = len(exceptions) // 2
        for date, kind in sorted(service['exceptions'].items()):
            exceptions.extend((int(date), kind))
        services.extend((int(service['start'] or 0), int(service['end'] or 0),
                         service['weekdays'], first, len(service['exceptions'])))
    times = bytearray()
    time_offsets = {}
    for values in sorted({tuple(t) for groups in model['departures'].values() for t in groups.values()}):
        time_offsets[values] = len(times)
        times.extend(varint(len(values)))
        previous = 0
        for value in values:
            times.extend(varint(value - previous))
            previous = value
    groups_blob = bytearray()
    patterns = []
    for (stop, route, direction), groups in sorted(model['departures'].items()):
        # Direction + 1 fits an unsigned byte and keeps unknown (-1) distinct.
        patterns.extend((offsets[stop], offsets[route], direction + 1, len(groups_blob)))
        groups_blob.extend(varint(len(groups)))
        for sid, values in sorted(groups.items()):
            groups_blob.extend(varint(service_indices[sid]))
            groups_blob.extend(varint(time_offsets[tuple(values)]))
    arrays = [('kStrings', 'uint8_t', strings), ('kServices', 'uint32_t', services),
              ('kExceptions', 'uint32_t', exceptions or [0, 0]), ('kTimes', 'uint8_t', times),
              ('kGroups', 'uint8_t', groups_blob), ('kPatterns', 'uint32_t', patterns or [0, 0, 0, 0])]
    embedded_bytes = sum((1 if typ == 'uint8_t' else 4) * max(1, len(values))
                         for _, typ, values in arrays) + 16
    if embedded_bytes > 1000000:
        raise ValueError('Embedded timetable exceeds 1 MB flash budget')
    meta = dict(source_url=source_url, generated_at=generated_at,
                source_sha256=hashlib.sha256(raw).hexdigest(), source_zip_bytes=len(raw),
                timezone=model['timezone'], valid_start=model['valid_start'], valid_end=model['valid_end'],
                validity_source=model['validity_source'], feed_info=model['feed_info'],
                service_count=len(service_indices), pattern_count=len(patterns) // 4,
                departure_count=sum(len(t) for groups in model['departures'].values() for t in groups.values()),
                group_count=sum(len(g) for g in model['departures'].values()),
                unique_time_list_count=len(time_offsets), exception_count=len(exceptions) // 2,
                source_stop_times=model['source_stop_times'], excluded_pickups=model['excluded_pickups'],
                max_departure_seconds=max((t[-1] for groups in model['departures'].values() for t in groups.values()), default=0),
                embedded_bytes=embedded_bytes, encoding='interned LEB128 delta seconds v1')
    lines = ['// Generated by tools/generate_timetable.py; do not edit.',
             '// Source: ' + source_url.replace('\n', ''), '// SHA256: ' + meta['source_sha256'],
             '#pragma once', '#include <cstdint>', 'namespace timetable { namespace data {',
             'static constexpr uint32_t kValidStart = ' + model['valid_start'] + ';',
             'static constexpr uint32_t kValidEnd = ' + model['valid_end'] + ';',
             'static constexpr uint32_t kMaxSeconds = ' + str(meta['max_departure_seconds']) + ';',
             'static constexpr uint32_t kPatternCount = ' + str(meta['pattern_count']) + ';']
    for name, typ, values in arrays:
        lines.append('static const ' + typ + ' ' + name + '[] = {')
        values = values or [0]
        for i in range(0, len(values), 24):
            lines.append('  ' + ','.join(map(str, values[i:i + 24])) + ',')
        lines.append('};')
    lines.append('}} // namespace timetable::data')
    for path, content in ((header, '\n'.join(lines) + '\n'),
                          (metadata, json.dumps(meta, ensure_ascii=False, sort_keys=True, indent=2) + '\n')):
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
    return meta


def main():
    import argparse
    import json
    from pathlib import Path
    from setram import STATIC_URL, download
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=root / '.cache/setram/timetable-source.zip')
    parser.add_argument('--refresh', action='store_true', help='Download the current official GTFS ZIP')
    parser.add_argument('--source-url', default=STATIC_URL)
    parser.add_argument('--header', type=Path, default=root / 'lib/Timetable/TimetableData.h')
    parser.add_argument('--metadata', type=Path, default=root / 'lib/Timetable/timetable-source.json')
    parser.add_argument('--generated-at', help='UTC ISO8601 provenance date; supply to reproduce metadata')
    args = parser.parse_args()
    if args.refresh:
        raw, _, _ = download(args.source_url, 60)
        parse_feed(raw)  # Validate before replacing a usable cached source.
        args.input.parent.mkdir(parents=True, exist_ok=True)
        args.input.write_bytes(raw)
    raw = args.input.read_bytes()
    meta = emit(parse_feed(raw), raw, args.header, args.metadata, source_url=args.source_url,
                generated_at=args.generated_at or dt.datetime.now(dt.timezone.utc).isoformat())
    print(json.dumps(meta, indent=2, sort_keys=True))


if __name__ == '__main__':
    main()
