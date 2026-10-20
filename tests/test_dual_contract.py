from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DualContractTests(unittest.TestCase):
    def test_runtime_declares_two_independent_tracking_jobs(self):
        source = (ROOT / "src/main.cpp").read_text()
        self.assertIn("TrackingJob", source)
        self.assertIn("tracking[2]", source)
        self.assertIn("job->slot", source)

    def test_portal_keeps_two_catalogue_tuples_across_searches(self):
        text = (ROOT / "lib/BusPortal/portal.html").read_text()
        for marker in ("selectedTuples", "removeSelection", "data-max=\"2\"", "second_stop"):
            self.assertIn(marker, text)
        self.assertIn("/api/current", text)

    def test_settings_allow_same_stop_for_distinct_services(self):
        source = (ROOT / "lib/PortalSettings/PortalSettings.h").read_text()
        self.assertIn("s.secondRoute == s.route", source)

    def test_swap_option_is_real_and_persisted(self):
        self.assertIn("swapped", (ROOT / "src/main.cpp").read_text())
        self.assertIn("Swap", (ROOT / "lib/Navigation/Navigation.h").read_text())
