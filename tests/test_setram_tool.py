"""Offline tests: fixtures are synthetic, not published SETRAM identifiers."""
import importlib.util
import io
from pathlib import Path
import unittest
import zipfile

PATH = Path(__file__).resolve().parents[1] / 'tools' / 'setram.py'


def load_tool():
    assert PATH.exists(), 'tools/setram.py has not been implemented'
    spec = importlib.util.spec_from_file_location('setram', PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fixture():
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        z.writestr('stops.txt', '\ufeffstop_id,stop_name\ns1,République\ns2,Other\n')
        z.writestr('routes.txt', 'route_id,route_short_name,route_long_name\nr1,12,Bus twelve\n')
        z.writestr('trips.txt', 'route_id,trip_id,direction_id,trip_headsign\nr1,t1,0,University\nr1,t2,1,Station\n')
        z.writestr('stop_times.txt', 'trip_id,stop_id,stop_sequence\nt1,s1,1\nt1,s1,2\nt2,s1,1\n')
    return out.getvalue()


class StaticTests(unittest.TestCase):
    def test_parse_zip_handles_utf8_bom(self):
        tool = load_tool()
        tables = tool.parse_gtfs(fixture())
        self.assertEqual(tables['stops'][0]['stop_id'], 's1')
        self.assertEqual(tables['stops'][0]['stop_name'], 'République')
        self.assertEqual(len(tables['stop_times']), 3)

    def test_search_normalizes_accents_and_deduplicates_patterns(self):
        tool = load_tool()
        matches = tool.search_stops(tool.parse_gtfs(fixture()), 'REPUBLIQUE')
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]['stop_id'], 's1')
        self.assertEqual(matches[0]['services'], [
            {'route_id': 'r1', 'route_short_name': '12', 'route_long_name': 'Bus twelve',
             'direction_id': '0', 'trip_headsign': 'University'},
            {'route_id': 'r1', 'route_short_name': '12', 'route_long_name': 'Bus twelve',
             'direction_id': '1', 'trip_headsign': 'Station'}])
        self.assertEqual(tool.search_stops(tool.parse_gtfs(fixture()), 'absent'), [])

    def test_header_probe_skips_entities_and_rejects_truncation(self):
        tool = load_tool()
        # FeedHeader {gtfs_realtime_version: '2.0', timestamp: 150}
        data = b'\x0a\x08\x0a\x03' + b'2.0' + b'\x18\x96\x01'
        header = tool.inspect_header(data)
        self.assertEqual(header, {'version': '2.0', 'timestamp': 150})
        self.assertEqual(tool.inspect_header(data + b'\x12\x02xx'), header)
        for bad in (b'', data[:-1], b'\x0a\x02\x18\x80', b'\x0f'):
            with self.assertRaises(ValueError):
                tool.inspect_header(bad)

    def test_commands_fetch_cache_and_probe_with_local_http(self):
        import contextlib
        import http.server
        import json
        import tempfile
        import threading
        tool = load_tool()
        archive = fixture()
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = archive if self.path == '/static' else b'\x0a\x08\x0a\x032.0\x18\x96\x01'
                self.send_response(200)
                self.end_headers()
                self.wfile.write(body)
            def log_message(self, *args):
                pass
        with http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, daemon=True)
            thread.start()
            url = 'http://127.0.0.1:' + str(server.server_port)
            try:
                with tempfile.TemporaryDirectory() as cache:
                    for command in (['search', 'republique', '--static-url', url + '/static'],
                                    ['probe', '--realtime-url', url + '/rt']):
                        output = io.StringIO()
                        with contextlib.redirect_stdout(output):
                            code = tool.main(command + ['--cache-dir', cache, '--timeout', '2'])
                        self.assertEqual(code, 0)
                        payload = json.loads(output.getvalue())
                        if command[0] == 'search':
                            self.assertEqual(payload['stops'][0]['stop_id'], 's1')
                            self.assertTrue((Path(cache) / 'static.zip').exists())
                        else:
                            self.assertEqual(payload['http_status'], 200)
                            self.assertEqual(payload['timestamp'], 150)
                            self.assertEqual(payload['size_bytes'], 10)
                            self.assertEqual(payload['freshness'], 'stale')
            finally:
                server.shutdown()
                thread.join()


if __name__ == '__main__':
    unittest.main()
