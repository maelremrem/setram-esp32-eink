#!/usr/bin/env python3
"""SETRAM static GTFS lookup and GTFS-RT health probe; standard library only."""
import csv
import io
import zipfile
import unicodedata
import argparse
import datetime
import json
from pathlib import Path
import sys
import time
import urllib.error
import urllib.request

STATIC_URL = ('https://eur.mecatran.com/utw/ws/gtfsfeed/static/lemans'
              '?apiKey=73334a124f3a1654141d6979113106450f291d15&type=gtfs_setram_urbsco')
REALTIME_URL = ('https://eur.mecatran.com/utw/ws/gtfsfeed/realtime/lemans'
                '?apiKey=73334a124f3a1654141d6979113106450f291d15')
DEFAULT_CACHE = Path(__file__).resolve().parents[1] / '.cache' / 'setram'


def inspect_header(data):
    """Inspect only FeedMessage.header; not a full GTFS-RT validator/decoder."""
    def varint(buf, pos):
        value = 0
        for shift in range(0, 70, 7):
            if pos >= len(buf):
                raise ValueError('Truncated protobuf varint')
            byte = buf[pos]
            pos += 1
            value |= (byte & 127) << shift
            if byte < 128:
                if value > (1 << 64) - 1:
                    raise ValueError('Protobuf varint overflow')
                return value, pos
        raise ValueError('Oversized protobuf varint')

    def fields(buf):
        pos = 0
        while pos < len(buf):
            tag, pos = varint(buf, pos)
            number, wire = tag >> 3, tag & 7
            if not number:
                raise ValueError('Invalid protobuf field zero')
            if wire == 0:
                value, pos = varint(buf, pos)
            elif wire in (1, 2, 5):
                if wire == 2:
                    size, pos = varint(buf, pos)
                else:
                    size = 8 if wire == 1 else 4
                end = pos + size
                if end > len(buf):
                    raise ValueError('Truncated protobuf field')
                value, pos = buf[pos:end], end
            else:
                raise ValueError('Unsupported protobuf wire type')
            yield number, wire, value

    header = None
    for number, wire, value in fields(data):
        if number == 1 and wire == 2:
            header = {}
            for n, w, v in fields(value):
                if n == 1 and w == 2:
                    header['version'] = v.decode('utf-8')
                elif n == 3 and w == 0:
                    header['timestamp'] = v
    if header is None or 'version' not in header or 'timestamp' not in header:
        raise ValueError('FeedHeader version/timestamp missing; freshness cannot be established')
    return header


def download(url, timeout):
    request = urllib.request.Request(url, headers={'User-Agent': 'setram-epaper-tool/1.0'})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read(), response.status, response.geturl()


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    search = sub.add_parser('search', help='Search static stop names and scheduled service patterns')
    search.add_argument('name')
    search.add_argument('--static-url', default=STATIC_URL)
    search.add_argument('--refresh', action='store_true', help='Force new static ZIP download')
    search.add_argument('--offline', action='store_true', help='Use cached ZIP only')
    probe = sub.add_parser('probe', help='Download realtime and inspect header freshness')
    probe.add_argument('--realtime-url', default=REALTIME_URL)
    probe.add_argument('--max-age', type=float, default=120, help='Freshness threshold in seconds (default: 120)')
    for cmd in (search, probe):
        cmd.add_argument('--cache-dir', type=Path, default=DEFAULT_CACHE)
        cmd.add_argument('--timeout', type=float, default=20, help='Socket timeout seconds (default: 20)')
    args = parser.parse_args(argv)
    if args.timeout <= 0 or (args.command == 'probe' and args.max_age <= 0):
        parser.error('timeout and max-age must be positive')
    try:
        args.cache_dir.mkdir(parents=True, exist_ok=True)
        if args.command == 'search':
            cache = args.cache_dir / 'static.zip'
            metadata = args.cache_dir / 'static-source.json'
            source = json.loads(metadata.read_text()) if metadata.exists() else {}
            cached = cache.exists() and (args.offline or source.get('requested_url') == args.static_url)
            if args.offline and (args.refresh or not cache.exists()):
                raise ValueError('--offline needs cached static.zip and cannot use --refresh')
            if cached and not args.refresh:
                data = cache.read_bytes()
                tables = parse_gtfs(data)
            else:
                data, status, final_url = download(args.static_url, args.timeout)
                tables = parse_gtfs(data)
                cache.write_bytes(data)
                source = {'requested_url': args.static_url, 'final_url': final_url,
                          'http_status': status, 'downloaded_at': int(time.time()), 'size_bytes': len(data)}
                metadata.write_text(json.dumps(source, indent=2) + '\n')
            output = {'source': source, 'cache_used': cached and not args.refresh,
                      'stops': search_stops(tables, args.name)}
        else:
            data, status, final_url = download(args.realtime_url, args.timeout)
            header = inspect_header(data)
            now = time.time()
            age = now - header['timestamp']
            output = dict(header, http_status=status, final_url=final_url, size_bytes=len(data),
                          timestamp_utc=datetime.datetime.fromtimestamp(header['timestamp'], datetime.timezone.utc).isoformat(),
                          fetched_at_utc=datetime.datetime.fromtimestamp(now, datetime.timezone.utc).isoformat(),
                          age_seconds=round(age, 1), max_age_seconds=args.max_age,
                          freshness='fresh' if 0 <= age <= args.max_age else ('future' if age < 0 else 'stale'))
            (args.cache_dir / 'realtime.pb').write_bytes(data)
        print(json.dumps(output, ensure_ascii=False, indent=2))
        return 0
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        output = {'error': str(error)}
        if isinstance(error, urllib.error.HTTPError):
            output['http_status'] = error.code
        print(json.dumps(output, ensure_ascii=False), file=sys.stderr)
        return 1


def search_stops(tables, query):
    """Find names case/accent-insensitively and join scheduled service patterns."""
    def normalized(text):
        return ''.join(c for c in unicodedata.normalize('NFKD', text.casefold())
                       if not unicodedata.combining(c))

    stops = [s for s in tables['stops'] if normalized(query) in normalized(s['stop_name'])]
    by_stop = {s['stop_id']: set() for s in stops}
    for row in tables['stop_times']:
        if row['stop_id'] in by_stop:
            by_stop[row['stop_id']].add(row['trip_id'])
    trips = {t['trip_id']: t for t in tables['trips']}
    routes = {r['route_id']: r for r in tables['routes']}
    result = []
    for stop in stops:
        patterns = set()
        for trip_id in by_stop[stop['stop_id']]:
            trip = trips[trip_id]
            route = routes[trip['route_id']]
            patterns.add((trip['route_id'], route.get('route_short_name', ''),
                          route.get('route_long_name', ''), trip.get('direction_id', ''),
                          trip.get('trip_headsign', '')))
        keys = ('route_id', 'route_short_name', 'route_long_name', 'direction_id', 'trip_headsign')
        result.append(dict(stop, services=[dict(zip(keys, p)) for p in sorted(patterns)]))
    return sorted(result, key=lambda s: (s['stop_name'], s['stop_id']))


def parse_gtfs(data):
    """Read the four lookup tables without extracting untrusted ZIP paths."""
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        return {
            name: list(csv.DictReader(io.TextIOWrapper(
                archive.open(name + '.txt'), encoding='utf-8-sig', newline='')))
            for name in ('stops', 'routes', 'trips', 'stop_times')
        }


if __name__ == '__main__':
    sys.exit(main())
