"""Developer regression tests for bounded runtime-copy recovery."""

import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest


HERE = Path(__file__).resolve().parent
CMAKE = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
MODULE = Path(
    os.environ.get(
        "COPY_RUNTIME_MODULE",
        str(HERE.parents[1] / "cmake" / "CopyRuntime.cmake"),
    )
).resolve()


def copy_command(source, destination):
    libraries = "" if source is None else str(source)
    return [
        CMAKE,
        "-DLIBRARIES=" + libraries,
        "-DDESTINATION=" + str(destination),
        "-P",
        str(MODULE),
    ]


def run_copy(source, destination):
    return subprocess.run(
        copy_command(source, destination),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        encoding="utf-8",
        errors="replace",
        check=False,
    )


def open_exclusive(path):
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.CreateFileW.argtypes = [
        ctypes.c_wchar_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_void_p,
        ctypes.c_uint32,
        ctypes.c_uint32,
        ctypes.c_void_p,
    ]
    kernel32.CreateFileW.restype = ctypes.c_void_p
    handle = kernel32.CreateFileW(str(path), 0x80000000, 0, None, 3, 0, None)
    if handle == ctypes.c_void_p(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())
    return kernel32, handle


def close_handle(kernel32, handle):
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.restype = ctypes.c_int
    if not kernel32.CloseHandle(handle):
        raise ctypes.WinError(ctypes.get_last_error())


@unittest.skipUnless(CMAKE, "cmake was not found")
class CopyRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="copy-runtime-")
        self.root = Path(self.temp.name)
        self.source = self.root / "source" / "runtime.dll"
        self.destination = self.root / "destination"
        self.source.parent.mkdir(parents=True)
        self.destination.mkdir()
        self.source.write_bytes(b"new-runtime")

    def tearDown(self):
        self.temp.cleanup()

    def assert_copy_succeeded(self, completed):
        if completed.returncode:
            self.fail("runtime copy failed:\n" + completed.stdout)

    def test_copies_runtime_and_preserves_unchanged_mtime(self):
        self.assert_copy_succeeded(run_copy(self.source, self.destination))
        target = self.destination / self.source.name
        self.assertEqual(target.read_bytes(), self.source.read_bytes())

        fixed_time = 946684800_000_000_000
        os.utime(target, ns=(fixed_time, fixed_time))
        before = target.stat().st_mtime_ns
        self.assert_copy_succeeded(run_copy(self.source, self.destination))
        self.assertEqual(target.stat().st_mtime_ns, before)

    def test_empty_runtime_list_is_a_noop(self):
        other = self.root / "unused-destination"
        self.assert_copy_succeeded(run_copy(None, other))
        self.assertFalse(other.exists())

    def test_missing_source_fails_clearly(self):
        missing = self.root / "missing" / "runtime.dll"
        completed = run_copy(missing, self.destination)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Runtime dependency does not exist", completed.stdout)

    @unittest.skipUnless(os.name == "nt", "Windows sharing semantics required")
    def test_recovers_after_transient_destination_lock(self):
        target = self.destination / self.source.name
        target.write_bytes(b"old-runtime")
        kernel32, handle = open_exclusive(target)
        process = subprocess.Popen(
            copy_command(self.source, self.destination),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            encoding="utf-8",
            errors="replace",
        )
        try:
            time.sleep(0.22)
        finally:
            close_handle(kernel32, handle)
        output, _ = process.communicate(timeout=10)
        if process.returncode:
            self.fail("runtime copy did not recover after lock release:\n" + output)
        self.assertEqual(target.read_bytes(), self.source.read_bytes())

    @unittest.skipUnless(os.name == "nt", "Windows sharing semantics required")
    def test_permanent_destination_lock_exhausts_retry_budget(self):
        target = self.destination / self.source.name
        target.write_bytes(b"old-runtime")
        kernel32, handle = open_exclusive(target)
        started = time.monotonic()
        try:
            completed = run_copy(self.source, self.destination)
        finally:
            close_handle(kernel32, handle)
        elapsed = time.monotonic() - started

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Permission denied", completed.stdout)
        self.assertIn("after 5 attempts", completed.stdout)
        self.assertGreaterEqual(elapsed, 0.25)
        self.assertEqual(target.read_bytes(), b"old-runtime")


if __name__ == "__main__":
    unittest.main()
