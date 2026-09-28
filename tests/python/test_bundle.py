"""Synthetic image headers below test packaging validation, not native loading."""

import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location(
    "bundle_bridge", Path(__file__).resolve().parents[2] / "tools/bundle_bridge.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name)
        self.windows, self.linux = root / "windows", root / "linux"
        self.output = root / "output.zip"
        common = [
            "Packages/nexus-bridge/Package.toml",
            "Packages/nexus-bridge/licenses/LICENSE",
            "Packages/nexus-bridge/licenses/LICENSE.txt",
            "Packages/nexus-bridge-check/Package.toml",
            "Packages/nexus-bridge-check/Server/Index.lua",
            "Packages/nexus-transport-check/Package.toml",
            "Packages/nexus-transport-check/Server/Index.lua",
        ]
        for platform in (self.windows, self.linux):
            for name in common:
                path = platform / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"test fixture\n")
        dll = bytearray(128)
        dll[:2] = b"MZ"
        struct.pack_into("<I", dll, 60, 64)
        dll[64:68] = b"PE\0\0"
        struct.pack_into("<H", dll, 68, 0x8664)
        struct.pack_into("<H", dll, 88, 0x20B)
        (self.windows / "Packages/nexus-bridge/nexus_bridge.dll").write_bytes(dll)
        so = bytearray(64)
        so[:6] = b"\x7fELF\x02\x01"
        struct.pack_into("<HH", so, 16, 3, 62)
        self.so_path = self.linux / "Packages/nexus-bridge/libnexus_bridge.so"
        self.so_path.write_bytes(so)
        (self.linux / "Packages/nexus-bridge/licenses/lua-5.4.9-readme.html").write_bytes(b"license fixture")

    def run_bundle(self, revision="a" * 40):
        module.bundle(self.windows, self.linux, revision, self.output)

    def test_archive_is_allowlisted_and_hashes_match(self):
        (self.windows / "secret.txt").write_text("must not ship")
        self.run_bundle()
        with zipfile.ZipFile(self.output) as archive:
            self.assertNotIn("secret.txt", archive.namelist())
            info = json.loads(archive.read("BUILD-INFO.json"))
            self.assertEqual(info["revision"], "a" * 40)
            for line in archive.read("SHA256SUMS").decode().splitlines():
                expected, name = line.split("  ", 1)
                self.assertEqual(expected, hashlib.sha256(archive.read(name)).hexdigest())

    def test_conflicting_common_file_is_rejected(self):
        (self.linux / "Packages/nexus-bridge/Package.toml").write_text("different")
        with self.assertRaisesRegex(ValueError, "disagree"):
            self.run_bundle()

    def test_platform_line_endings_are_normalized(self):
        (self.windows / "Packages/nexus-bridge/Package.toml").write_bytes(b"test fixture\r\n")
        self.run_bundle()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read("Packages/nexus-bridge/Package.toml"), b"test fixture\n")

    def test_wrong_linux_architecture_is_rejected(self):
        data = bytearray(self.so_path.read_bytes())
        struct.pack_into("<H", data, 18, 183)  # ELF AArch64, not x86_64.
        self.so_path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "x86_64 ELF"):
            self.run_bundle()

    def test_missing_linux_binary_is_rejected(self):
        self.so_path.unlink()
        with self.assertRaises(FileNotFoundError):
            self.run_bundle()

    def test_revision_must_be_full_hash(self):
        with self.assertRaisesRegex(ValueError, "full Git"):
            self.run_bundle("main")


if __name__ == "__main__":
    unittest.main()
