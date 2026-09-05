import tempfile
import unittest
from pathlib import Path
from baseline_manifest import inventory, manifest


class ManifestTests(unittest.TestCase):
    def test_content_change_same_size_is_detected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            p = root / "sample"
            p.write_bytes(b"one")
            before = inventory(root, ["sample"])
            p.write_bytes(b"two")
            self.assertNotEqual(before, inventory(root, ["sample"]))

    def test_absent_data_fails_closed_and_no_err_substitution(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "src/generated").mkdir(parents=True)
            (root / "src/generated/wrong.cpp").write_text("ERR")
            result = manifest(root, root)
            self.assertEqual(len(result["missing_groups"]), 7)
            self.assertEqual(result["generated"], {})


if __name__ == "__main__":
    unittest.main()
