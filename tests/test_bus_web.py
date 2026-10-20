import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
class WebTests(unittest.TestCase):
    def test_offline_accessible_portal_has_search_and_manual_fallback(self):
        page = ROOT / 'lib/BusPortal/portal.html'
        self.assertTrue(page.exists(), 'French onboard web asset missing')
        text = page.read_text()
        for expected in ('lang="fr"', '/api/catalogue', 'id="search"', 'id="stop"', 'id="service"', 'name="frequency"', 'value="30"', 'value="60"', 'value="120"', '/wifi', 'Saisie manuelle', 'textContent', 'aria-live'):
            self.assertIn(expected, text)
        self.assertNotIn('innerHTML', text)
        self.assertNotIn('cdn.', text)
        self.assertIn('#source{overflow-wrap:anywhere}', text)
    def test_generated_flash_assets_match_browser_sources(self):
        import gzip
        import json
        page=(ROOT / 'lib/BusPortal/portal.html').read_text()
        header=(ROOT / 'lib/BusPortal/GeneratedPortal.h').read_text()
        self.assertIn('R"BUSPORTAL('+page+')BUSPORTAL"', header)
        payload=(ROOT / 'lib/BusCatalog/catalogue.json').read_bytes()
        self.assertEqual(gzip.decompress((ROOT / 'lib/BusCatalog/catalogue.json.gz').read_bytes()), payload)
        data=json.loads(payload)
        self.assertEqual(len(data['stops']),len({s['id'] for s in data['stops']}))
        self.assertTrue(data['source']['sha256'])
        self.assertTrue(data['source']['generated_utc'])
        self.assertTrue(data['source']['regenerate'])
    def test_firmware_registers_scoped_portal_and_frequency(self):
        text = (ROOT / 'src/main.cpp').read_text()
        self.assertIn('#include <BusPortal.h>', text)
        self.assertIn('setWebServerCallback', text)
        self.assertIn('frequencyParam', text)
        self.assertIn('onboardPortal.attach()', text)
        self.assertIn('prefs.getUInt("poll-ms", 0) == nextInterval', text)
if __name__ == '__main__': unittest.main()
