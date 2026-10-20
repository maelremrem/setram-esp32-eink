import subprocess
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class BrowserPortalTests(unittest.TestCase):
    def test_real_chrome_portal_roundtrip(self):
        result=subprocess.run(['node','tests/browser_portal_test.mjs'],cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr or result.stdout)
        self.assertIn('Browser portal behavioral tests: PASS',result.stdout)

if __name__=='__main__': unittest.main()
