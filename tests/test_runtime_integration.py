"""Production main wiring regression; complemented by executable host scenarios."""
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=(ROOT/'src/main.cpp').read_text()
class RuntimeWiring(unittest.TestCase):
    def test_boot_candidates_are_queued_before_wifi_and_first_request(self):
        setup=SOURCE.split('void setup()',1)[1].split('void loop()',1)[0]
        self.assertLess(setup.index('draw(0)'),setup.index('wifiManager.autoConnect('))
        draw=SOURCE.split('void draw(',1)[1].split('void setup()',1)[0]
        self.assertLess(draw.index('getConfigPortalActive()'),draw.index('busUI::renderBoot'))
        self.assertIn('!boot.complete && navigationState.screen==navigation::Screen::Home',draw)
        loop=SOURCE.split('void loop()',1)[1]
        self.assertLess(loop.index('boot.requestStarted()'),loop.index('requestFeed(now)'))
        consumer=SOURCE.split('void consumeFeed()',1)[1].split('bool requestFeed(',1)[0]
        self.assertLess(consumer.index('job->generation!=feedGeneration'),consumer.index('boot.requestFinished()'))
        self.assertIn('boot.requestFinished(); redrawRequested=true;',consumer)
        self.assertNotIn('boot.requestFinished()',loop)
    def test_actual_visible_frame_not_diagnostic_cache_decides_refresh(self):
        self.assertIn('composeSlot(now,selectedSlot)',SOURCE)
        self.assertIn('paintPolicy::feed(result,valid',SOURCE)
        self.assertNotIn('String lastView',SOURCE)
        self.assertNotIn('lastView ==',SOURCE)
        self.assertNotIn('std::string("Maj ")',SOURCE)
        self.assertNotIn('std::string("Temps reel - Maj ")',SOURCE)
        self.assertIn('crowpanel::present(frame.pixels)',SOURCE)
        self.assertIn('if(painted.changed(frame.pixels)) crowpanel::present(frame.pixels);',SOURCE)
        self.assertNotIn('CROWPANEL_ENABLE_PARTIAL',SOURCE)
    def test_clock_and_status_are_rechecked_independently_of_poll(self):
        self.assertIn('millis() - lastPaint >= 10000',SOURCE)
        self.assertIn('pollNow-lastPoll>=pollMs',SOURCE)
        self.assertIn('boot.prerequisites(',SOURCE)
        loop=SOURCE.split('void loop()',1)[1]
        self.assertLess(loop.index('pollButtons();'),loop.index('consumeFeed();'))
        self.assertLess(loop.index('consumeFeed();'),loop.index('draw(now)'))
        self.assertIn('ahead=%llus',SOURCE)
if __name__=='__main__': unittest.main()
