"""Source-level firmware wiring checks; NOT radio/captive-browser tests.
Renderer/QR decoding and portal key behavior are exercised by run_portal_ui.py.
"""
from pathlib import Path
import re
import unittest
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src/main.cpp').read_text()
WM = ROOT / '.pio/libdeps/crowpanel_ssd1680/WiFiManager'

class PortalWiringTests(unittest.TestCase):
    def test_scan_menu_is_explicit_and_reaches_real_wifi_scan_handler(self):
        self.assertRegex(SOURCE, r'const char\* portalMenu\[\]\s*=\s*\{\s*"custom",\s*"wifi",\s*"exit"\s*\}')
        self.assertIn('wifiManager.setMenu(portalMenu, 3)', SOURCE)
        self.assertIn('setCustomMenuHTML', SOURCE)
        # WiFiManager 2.0.17 disables _paramsInWifi if "param" is in setMenu.
        self.assertNotIn('setParamsPage(', SOURCE)
        if (WM / 'WiFiManager.cpp').exists():
            upstream = (WM / 'WiFiManager.cpp').read_text()
            self.assertTrue(re.search(r'server->on\(WM_G\(R_wifi\),.*handleWifi, this, true', upstream))
            self.assertIn('WiFi_scanNetworks(server->hasArg(F("refresh")),false)', upstream)

    def test_portal_has_renderer_and_key_controller_wired(self):
        self.assertIn('#include <PortalUI.h>', SOURCE)
        self.assertIn('portalUI::render(canvas,', SOURCE)
        self.assertIn('portalNavigation.key(buttonKeys[i])', SOURCE)
        self.assertIn('portalUI::Action::Close', SOURCE)
        self.assertIn('portalUI::Action::Redraw', SOURCE)
        # Opening the portal always starts with the join QR; cache includes step.
        self.assertIn('portalNavigation.reset()', SOURCE)
        self.assertIn('portalNavigation.step', SOURCE)
        self.assertNotIn('if(buttonKeys[i] != navigation::Key::Back) continue;', SOURCE)

    def test_existing_privacy_lifecycle_and_settings_are_preserved(self):
        for fragment in ('wifiManager.setDebugOutput(false)',
                         'wifiManager.setConfigPortalBlocking(false)',
                         'wifiManager.setSaveParamsCallback(', 'wifiManager.process()',
                         'PORTAL_TIMEOUT_MS = 300000',
                         'event == navigation::Event::Long) openPortal()',
                         'prefs.getString("settings", "") == blob.c_str()',
                         'wifiManager.addParameter(&stopParam)',
                         'wifiManager.addParameter(&routeParam)',
                         'wifiManager.addParameter(&directionParam)'):
            self.assertIn(fragment, SOURCE)
        self.assertNotRegex(SOURCE, r'Serial\.(?:print|printf|println)\([^;]*(?:apPassword|apName)')

if __name__ == '__main__':
    unittest.main()
