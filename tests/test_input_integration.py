"""Firmware wiring checks, not hardware scheduling/optical latency proof."""
from pathlib import Path
import unittest
import re
SOURCE=(Path(__file__).resolve().parents[1]/'src/main.cpp').read_text()

class InputWiring(unittest.TestCase):
    def test_ota_exit_chord_uses_both_sampled_buttons_and_a_long_debouncer(self):
        task=SOURCE.split('void inputTask(',1)[1].split('bool inputPending()',1)[0]
        self.assertIn('navigation::Debouncer otaExitChord(false,true)',task)
        self.assertIn('digitalRead(buttonPins[0]) == LOW && digitalRead(buttonPins[1]) == LOW',task)
        self.assertIn('navigation::Event::ChordLong',task)

    def test_hidden_battery_does_not_schedule_adc_or_battery_only_repaints(self):
        self.assertNotIn('sampleBattery()',SOURCE)
        self.assertNotIn('analogReadMilliVolts',SOURCE)
        self.assertNotIn('BUS_BATTERY_ADC_PIN',SOURCE)
        self.assertIn('constexpr int batteryLevel = -1;',SOURCE)
    def test_partial_refresh_configuration_is_removed_not_hidden(self):
        for forbidden in ('partialChoice','partial-max','getUInt("partial"',
                          'setPartialRefresh','supportsPartialRefresh','changePartialRefresh',
                          'TogglePartialRefresh','frame.limit','frame.interactive'):
            self.assertNotIn(forbidden,SOURCE)
    def test_independent_gpio_sampler_owns_no_ui_or_network(self):
        self.assertIn('xTaskCreatePinnedToCore(inputTask', SOURCE)
        task=SOURCE.split('void inputTask(',1)[1].split('bool inputPending()',1)[0]
        self.assertIn('digitalRead(buttonPins[i])', task)
        self.assertIn('vTaskDelayUntil', task)
        for forbidden in ('openPortal(', 'present(', 'fetchFeed(', 'navigationState.event('):
            self.assertNotIn(forbidden, task)
        self.assertIn('portENTER_CRITICAL(&inputMux)', SOURCE)
        self.assertIn('navigation::InputQueue inputQueue', SOURCE)
    def test_display_worker_stack_fits_frame_and_driver_conversion(self):
        match=re.search(r'xTaskCreatePinnedToCore\(displayTask,"epaper",(\d+),',SOURCE)
        self.assertIsNotNone(match)
        # ESP32 task depth is bytes: both 4000-byte local buffers coexist.
        self.assertGreaterEqual(int(match.group(1)),3*4000)
    def test_user_paint_precedes_queued_download(self):
        loop=SOURCE.split('void loop()',1)[1]
        self.assertLess(loop.index('pollButtons()'), loop.index('draw(now)'))
        self.assertLess(loop.index('draw(now)'), loop.index('requestFeed(now)'))
        self.assertIn('inputPending()', loop)
        self.assertIn('navigation::allowFetch', loop)
        self.assertNotIn('delay(50)', loop)
    def test_paint_and_fetch_durations_are_logged_without_credentials(self):
        self.assertIn('E-paper paint: %lums', SOURCE)
        self.assertIn('SETRAM fetch: %lums', SOURCE)
        self.assertIn('Input batch: %u events, dropped=%lu', SOURCE)
        self.assertIn('presentFrame()', SOURCE)
    def test_worker_ownership_and_bounded_snapshots(self):
        self.assertIn('xTaskCreatePinnedToCore(displayTask', SOURCE)
        self.assertIn('xTaskCreatePinnedToCore(networkTask', SOURCE)
        painter=SOURCE.split('bool displayStep()',1)[1].split('void displayTask(',1)[0]
        self.assertIn('frame=pendingPanel; panelPending=false;', painter)
        self.assertNotIn('canvas.getBuffer()', painter)
        self.assertNotIn('navigationState', painter)
        self.assertNotIn('fetchFeed(', painter)
        fetcher=SOURCE.split('void fetchFeed(FeedJob& response)',1)[1].split('bool networkStep()',1)[0]
        for forbidden in ('navigationState', 'draw(', 'redrawRequested', 'configNotice'):
            self.assertNotIn(forbidden,fetcher)
        self.assertIn('const auto& settings=response.selection',fetcher)
        loop=SOURCE.split('void loop()',1)[1]
        self.assertNotIn('crowpanel::present(',loop)
        self.assertNotIn('fetchFeed(',loop)
    def test_tolerated_future_feed_is_explicit_and_keeps_absolute_events(self):
        self.assertNotIn('displayPolicy::fresh(', SOURCE)
        self.assertIn('displayPolicy::usable(candidate.timestamp, observedNow)', SOURCE)
        self.assertIn('composeSlot(now,selectedSlot)', SOURCE)
        self.assertIn('paintPolicy::feed(result,valid', SOURCE)
        policy=(Path(__file__).resolve().parents[1]/'lib/PaintPolicy/PaintPolicy.h').read_text()
        self.assertIn('displayPolicy::usable(result.timestamp,now)', policy)
        self.assertIn('displayPolicy::FeedState::FutureTolerated', SOURCE)
        self.assertIn('Flux avance +', SOURCE)
        self.assertIn('http.header("Date")', SOURCE)
        self.assertIn('age=%llds stop=%s route=%s direction=%s', SOURCE)
        self.assertIn('Aucun passage annonce pour les identifiants configures', SOURCE)
        self.assertNotRegex(SOURCE, r'd\.time\s*[-+]=')
    def test_ip_view_uses_live_sta_ip_and_queued_full_refresh(self):
        self.assertIn('navigation::Screen::WiFiIp', SOURCE)
        self.assertIn('WiFi.localIP().toString()', SOURCE)
        self.assertIn('busUI::renderWiFi(canvas,', SOURCE)
        self.assertIn('crowpanel::present(frame.pixels)', SOURCE)
        self.assertIn('memcpy(pendingPanel.pixels,canvas.getBuffer(),4000)', SOURCE)

if __name__=='__main__':
    unittest.main()
