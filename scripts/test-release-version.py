import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from importlib.machinery import SourceFileLoader
mod = SourceFileLoader("versions", str(Path(__file__).with_name("next-release-version.py"))).load_module()


class Versioning(unittest.TestCase):
    def test_first_release(self):
        self.assertEqual(mod.choose([], ""), "1.0")

    def test_increment_minor(self):
        self.assertEqual(mod.choose(["1.0"], ""), "1.1")
        self.assertEqual(mod.choose(["1.9", "1.10", "1.2"], ""), "1.11")

    def test_override(self):
        self.assertEqual(mod.choose([], "3.0"), "3.0")
        self.assertEqual(mod.choose(["1.2"], "5.7"), "5.7")

    def test_reject_old_or_invalid(self):
        for value in ("1.0", "0.0", "1.1.0", "v1.2", "abc", "01.3", "2", "1.-1"):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    mod.choose(["1.0"], value)

    def test_ignored_legacy_tags(self):
        self.assertEqual(mod.choose(["v0.1.0", "other"], ""), "1.0")


if __name__ == "__main__":
    unittest.main()
