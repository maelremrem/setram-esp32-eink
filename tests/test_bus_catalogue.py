import importlib.util
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

class CatalogueTests(unittest.TestCase):
    def test_join_only_physical_quays_and_exact_patterns(self):
        path = ROOT / 'tools/generate_bus_catalogue.py'
        self.assertTrue(path.exists(), 'official catalogue generator missing')
        spec = importlib.util.spec_from_file_location('catalogue', path)
        mod = importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
        tables = {'stops': [{'stop_id':'parent','stop_name':'Station','location_type':'1'}, {'stop_id':'quay','stop_name':"L’arrêt d'essai",'location_type':'0','parent_station':'parent'}],
          'routes':[{'route_id':'route:exact','route_short_name':'T1'}],
          'trips':[{'trip_id':'trip','route_id':'route:exact','direction_id':'1','trip_headsign':"L'université"}],
          'stop_times':[{'trip_id':'trip','stop_id':'parent'},{'trip_id':'trip','stop_id':'quay'}]}
        data = mod.build_catalogue(tables)
        self.assertEqual([s['id'] for s in data['stops']], ['quay'])
        self.assertEqual(data['stops'][0]['services'][0], {'route':'route:exact','line':'T1','direction':'1','destination':"L'université"})
        self.assertEqual(mod.safe_label("L'université & <gare>"), 'L université    gare ')

if __name__ == '__main__': unittest.main()
