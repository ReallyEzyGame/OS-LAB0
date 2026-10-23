#!/usr/bin/env python3
"""Host tests of user/sysinfo.c using synthetic snapshots, NOT kernel integration."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class SysinfoCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="sysinfo-cli-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "sysinfo"
        obj = Path(cls.temp.name) / "sysinfo.o"
        cc = os.environ.get("HOST_CC", "cc")
        flags = ["-std=gnu99", "-Wall", "-Werror", "-fno-builtin", "-I", str(ROOT)]
        # Compile the actual production main, retaining its format attributes.
        subprocess.run(
            [cc, *flags, "-c", str(ROOT / "user/sysinfo.c"), "-o", str(obj)],
            check=True,
        )
        # Rename just the undefined xv6 I/O/exit symbols in this test object.
        subprocess.run(
            [os.environ.get("HOST_OBJCOPY", "objcopy"),
             "--redefine-sym", "printf=xv6_printf",
             "--redefine-sym", "fprintf=xv6_fprintf",
             "--redefine-sym", "exit=xv6_exit", str(obj)], check=True,
        )
        subprocess.run(
            [cc, *flags, str(ROOT / "tests/fixtures/sysinfo_cli_host.c"), str(obj),
             "-o", str(cls.binary)], check=True,
        )

    def invoke(self, fixture, *args):
        return subprocess.run(
            [str(self.binary), *args], capture_output=True, text=True, timeout=5,
            env={**os.environ, "SYSINFO_FIXTURE": fixture},
        )

    def test_rows_and_64_bit_schedule_count(self):
        result = self.invoke("normal")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, (
            "FILES 1\nID TYPE MODE REF PIDS\n7 INODE rw 3 10 11\n"
            "PROCESSES 2\nPID STATE PPID NSCHED FDS(fd:file-id)\n"
            "10 RUNNING 1 5000000000 3:7 4:7\n11 SLEEPING 10 2 3:7\n"
        ))

    def test_syscall_failure(self):
        result = self.invoke("failure")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("syscall failed or is not implemented", result.stderr)

    def test_usage_does_not_call_syscall(self):
        result = self.invoke("failure", "extra")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(result.stderr, "Usage: sysinfo\n")

    def test_all_array_bounds(self):
        for kind in ("files", "procs", "pids", "fds"):
            for prefix in ("negative", "overflow"):
                fixture = f"{prefix}-{kind}"
                with self.subTest(fixture=fixture):
                    result = self.invoke(fixture)
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(result.stderr, "sysinfo: invalid snapshot bounds\n")

    def test_empty_arrays(self):
        result = self.invoke("empty")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(len(result.stdout.splitlines()), 4)
        self.assertIn("FILES 0\n", result.stdout)
        self.assertIn("PROCESSES 0\n", result.stdout)

    def test_unknown_enum_values(self):
        result = self.invoke("unknown")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout.count("UNKNOWN"), 2)

    def test_full_arrays_and_modes(self):
        result = self.invoke("full")
        self.assertEqual(result.returncode, 0)
        self.assertEqual(len(result.stdout.splitlines()), 100 + 64 + 4)
        for row in ("0 DEVICE --", "1 DEVICE r-", "2 DEVICE -w", "3 DEVICE rw"):
            self.assertIn(row, result.stdout)
        self.assertIn("99 DEVICE", result.stdout)
        self.assertIn("64 RUNNABLE 0 5000000000", result.stdout)
        self.assertIn("15:15\n", result.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
