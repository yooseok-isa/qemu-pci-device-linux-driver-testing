"""Check setup orchestration without downloading or building external sources."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO = Path(__file__).resolve().parents[2]


class SetupSourcesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="vnpu setup ")
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.repo = self.root / "repo"
        for relative in (
            "scripts/setup-sources.sh",
            "buildroot/configs/vnpu_qemu_x86_64_defconfig",
            "buildroot/configs/vnpu-linux.fragment",
            "qemu-device/patches/0001-hw-misc-add-vnpu-pci-device.patch",
        ):
            target = self.repo / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(REPO / relative, target)
        self.images = self.repo / "buildroot/output/images"
        self.images.mkdir(parents=True)
        (self.images / "rootfs.ext4").write_text("previous guest state")
        (self.images / "bzImage").write_text("previous kernel")
        self.buildroot = self.root / "external buildroot"
        self.log = self.root / "calls.jsonl"
        self.bin = self.root / "bin"
        self.bin.mkdir()
        # Git and make are replaced; the script's file copies use real tools.
        stub = """#!/usr/bin/env python3
import json, os, sys
from pathlib import Path
name = Path(sys.argv[0]).name
args = sys.argv[1:]
with open(os.environ['SETUP_TEST_LOG'], 'a') as log:
    log.write(json.dumps([name, args]) + '\\n')
if name == 'git' and args[0] == 'clone':
    (Path(args[-1]) / '.git').mkdir(parents=True)
if name == 'make' and any(arg.startswith('-j') for arg in args):
    if os.environ.get('SETUP_TEST_FAIL_BUILD'):
        sys.exit(9)
    images = Path(args[args.index('-C') + 1]) / 'output/images'
    images.mkdir(parents=True)
    (images / 'bzImage').write_text('new kernel')
    if not os.environ.get('SETUP_TEST_MISSING_IMAGE'):
        (images / 'rootfs.ext2').write_text('new guest filesystem')
"""
        for name in ("git", "make"):
            tool = self.bin / name
            tool.write_text(stub)
            tool.chmod(0o755)
        self.env = {
            **os.environ,
            "PATH": f"{self.bin}:{os.environ['PATH']}",
            "BUILDROOT_DIR": str(self.buildroot),
            "JOBS": "2",
            "SETUP_TEST_LOG": str(self.log),
        }

    def run_setup(self, *args):
        return subprocess.run(
            ["bash", str(self.repo / "scripts/setup-sources.sh"), *args],
            env=self.env, text=True, capture_output=True,
        )

    def make_calls(self):
        return [args for name, args in map(json.loads, self.log.read_text().splitlines())
                if name == "make"]

    def assert_old_images(self):
        self.assertEqual((self.images / "rootfs.ext4").read_text(), "previous guest state")
        self.assertEqual((self.images / "bzImage").read_text(), "previous kernel")

    def test_default_builds_copies_and_preserves_existing_guest(self):
        result = self.run_setup()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.make_calls(), [
            ["-C", str(self.buildroot), "vnpu_qemu_x86_64_defconfig"],
            ["-C", str(self.buildroot), "-j2"],
        ])
        self.assertEqual((self.images / "bzImage").read_text(), "new kernel")
        self.assertEqual((self.images / "rootfs.ext4").read_text(), "new guest filesystem")
        backups = list(self.images.glob("backup.*/rootfs.ext4"))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), "previous guest state")
        config = (self.buildroot / "configs/vnpu_qemu_x86_64_defconfig").read_text()
        for setting in ("BR2_TOOLCHAIN_BUILDROOT_CXX=y", "BR2_PACKAGE_PYTHON3=y",
                        "BR2_PACKAGE_PYTHON_PYTEST=y", "vnpu-linux.fragment"):
            self.assertIn(setting, config)
        self.assertEqual(
            (self.buildroot / "board/qemu/x86_64/vnpu-linux.fragment").read_text(),
            (REPO / "buildroot/configs/vnpu-linux.fragment").read_text(),
        )

    def test_configure_only_does_not_build_or_replace_images(self):
        result = self.run_setup("--configure-only")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(len(self.make_calls()), 1)
        self.assert_old_images()

    def test_no_configure_does_not_call_make(self):
        result = self.run_setup("--no-configure")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.make_calls(), [])
        self.assert_old_images()

    def test_build_failure_preserves_images(self):
        self.env["SETUP_TEST_FAIL_BUILD"] = "1"
        result = self.run_setup()
        self.assertEqual(result.returncode, 9)
        self.assert_old_images()

    def test_missing_image_preserves_images(self):
        self.env["SETUP_TEST_MISSING_IMAGE"] = "1"
        result = self.run_setup()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing or empty", result.stderr)
        self.assert_old_images()

    def test_invalid_jobs_fails_before_preparing_sources(self):
        self.env["JOBS"] = "0"
        result = self.run_setup()
        self.assertEqual(result.returncode, 2)
        self.assertFalse(self.log.exists())
        self.assert_old_images()


if __name__ == "__main__":
    unittest.main()
