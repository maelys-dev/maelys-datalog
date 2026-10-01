"""Negative controls for release-wheel contents; no native code is executed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

from wheel.wheelfile import WheelFile, WheelError

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("wheel_check", ROOT / "tools/check_python_wheel.py")
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class WheelContents(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.version = (ROOT / "VERSION").read_text().strip()
        self.tag = "cp310-abi3-manylinux_2_28_aarch64"
        self.path = self.work / f"maelys_datalog-{self.version}-{self.tag}.whl"
        self.info = f"maelys_datalog-{self.version}.dist-info"
        self.payload = {
            f"maelys_datalog/{name}": (ROOT / "bindings/python/maelys_datalog" / name).read_bytes()
            for name in ("engine.py", "__init__.py")
        }
        self.payload.update({
            "maelys_datalog/_maelys_cffi.py": b"# fixture\n",
            "maelys_datalog/_wheel_profile.py": b"# fixture\n",
            f"{self.info}/METADATA": (
                f"Metadata-Version: 2.4\nName: maelys-datalog\nVersion: {self.version}\n"
                "Requires-Python: >=3.10,<3.15\nRequires-Dist: cffi>=2.1.1,<3\n").encode(),
            f"{self.info}/WHEEL": f"Wheel-Version: 1.0\nRoot-Is-Purelib: false\nTag: {self.tag}\n".encode(),
            f"{self.info}/top_level.txt": b"maelys_datalog\n",
            f"{self.info}/licenses/LICENSE": b"test license",
            f"{self.info}/licenses/yyjson/LICENSE": b"test license",
        })
        for profile in ("small", "large"):
            self.payload[f"maelys_datalog/_{profile}/__init__.py"] = b"# fixture\n"
            self.payload[f"maelys_datalog/_{profile}/_maelys_cffi.abi3.so"] = b"not executed"

    def write(self):
        with WheelFile(self.path, "w") as wheel:
            for name, data in self.payload.items():
                wheel.writestr(name, data)

    def inspect(self):
        return check.inspect_archive(self.path, self.work / "inspection")

    def test_complete_archive(self):
        self.write()
        self.assertEqual(len(self.inspect()), 2)

    def test_missing_profile_refused(self):
        del self.payload["maelys_datalog/_large/_maelys_cffi.abi3.so"]
        self.write()
        with self.assertRaisesRegex(AssertionError, "payload"):
            self.inspect()

    def test_extra_install_hook_refused(self):
        self.payload["extra.pth"] = b"import os\n"
        self.write()
        with self.assertRaisesRegex(AssertionError, "payload"):
            self.inspect()

    def test_modified_facade_refused_even_with_valid_record(self):
        self.payload["maelys_datalog/engine.py"] += b"\n# unexpected change"
        self.write()
        with self.assertRaises(AssertionError):
            self.inspect()

    def test_metadata_tag_disagreement_refused(self):
        self.payload[f"{self.info}/WHEEL"] = self.payload[f"{self.info}/WHEEL"].replace(b"cp310", b"cp311")
        self.write()
        with self.assertRaises(AssertionError):
            self.inspect()

    def test_missing_runtime_dependency_refused(self):
        self.payload[f"{self.info}/METADATA"] = self.payload[f"{self.info}/METADATA"].replace(b"Requires-Dist: cffi>=2.1.1,<3\n", b"")
        self.write()
        with self.assertRaises(AssertionError):
            self.inspect()

    def test_corruption_with_stale_record_refused(self):
        self.write()
        with zipfile.ZipFile(self.path) as archive:
            entries = {name: archive.read(name) for name in archive.namelist()}
        entries["maelys_datalog/_small/_maelys_cffi.abi3.so"] += b"tampered"
        with zipfile.ZipFile(self.path, "w") as archive:
            for name, data in entries.items():
                archive.writestr(name, data)
        with self.assertRaises(WheelError):
            self.inspect()


if __name__ == "__main__":
    unittest.main()
