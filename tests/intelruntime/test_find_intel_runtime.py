"""Developer regression tests for FindIntelRuntime.cmake."""

import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


HERE = Path(__file__).resolve().parent
FIXTURE = HERE / "fixture"
MODULE_DIR = HERE.parents[1] / "cmake"
CMAKE = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")

MACHINES = {
    "x86": 0x014C,
    "x64": 0x8664,
    "ARM": 0x01C4,
    "ARM64": 0xAA64,
    "ARM64EC": 0xA641,
}
RUNTIMES = {
    "MD": "MultiThreadedDLL",
    "MDd": "MultiThreadedDebugDLL",
    "MT": "MultiThreaded",
    "MTd": "MultiThreadedDebug",
}


def write_archive(path, machine="x64"):
    payload = struct.pack("<H", MACHINES[machine]) + b"\0" * 6
    fields = (
        b"object.obj/".ljust(16),
        b"0".ljust(12),
        b"0".ljust(6),
        b"0".ljust(6),
        b"100644".ljust(8),
        str(len(payload)).encode("ascii").ljust(10),
        b"`\n",
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"!<arch>\n" + b"".join(fields) + payload)


def make_sdk(root, machine="x64", libraries=None):
    if libraries is None:
        libraries = {
            "libircmt", "libirc", "libmmd", "libmmdd", "libmmt",
            "svml_dispmd", "svml_dispmt",
        }
    for library in libraries:
        write_archive(root / "lib" / (library + ".lib"), machine)


def write_record(directory, config, runtime, ipo="0", malformed=False):
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / ("PkgIntelRuntime-" + config + ".cmake")
    if malformed:
        path.write_text("this is not valid cmake (\n", encoding="utf-8")
    else:
        upper = config.upper()
        content = "set(Pkg_INTEL_RUNTIME_{} \"{}\")\n".format(upper, runtime)
        if ipo is not None:
            content += "set(Pkg_INTEL_IPO_{} \"{}\")\n".format(upper, ipo)
        path.write_text(content, encoding="utf-8")


@unittest.skipUnless(CMAKE, "cmake was not found")
class FindIntelRuntimeTests(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="intelruntime-")
        self.root = Path(self.temp.name)
        self.sdk = self.root / "sdk"
        self.build_index = 0

    def tearDown(self):
        self.temp.cleanup()

    def configure(self, case, expect_success=True, **definitions):
        self.build_index += 1
        build = self.root / ("build-{}-{}".format(self.build_index, case))
        result = build / "result.txt"
        args = [
            CMAKE, "-S", str(FIXTURE), "-B", str(build),
            "-DCASE=" + case,
            "-DMODULE_DIR=" + str(MODULE_DIR),
            "-DRESULT_FILE=" + str(result),
            "-DTARGET_ARCH=x64",
            "-DTARGET_POINTER_SIZE=8",
        ]
        for key, value in definitions.items():
            args.append("-D{}={}".format(key, value))
        completed = subprocess.run(
            args, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            encoding="utf-8", errors="replace", check=False,
        )
        if expect_success and completed.returncode:
            self.fail("CMake configure failed:\n" + completed.stdout)
        if not expect_success and not completed.returncode:
            self.fail("CMake configure unexpectedly succeeded:\n" + completed.stdout)
        return completed, build, result

    def test_discovers_each_supported_coff_machine(self):
        cases = (
            ("x86", "x86", 4),
            ("x64", "x64", 8),
            ("ARM", "ARM", 4),
            ("ARM64", "ARM64", 8),
            ("ARM64EC", "ARM64EC", 8),
        )
        for machine, target_arch, pointer_size in cases:
            with self.subTest(machine=machine):
                sdk = self.root / ("sdk-" + machine)
                make_sdk(sdk, machine=machine)
                _, _, result = self.configure(
                    "discover", IntelRuntime_ROOT=sdk, EXPECT_STATIC_IRC="ON",
                    TARGET_ARCH=target_arch, TARGET_POINTER_SIZE=pointer_size)
                text = result.read_text(encoding="utf-8")
                self.assertIn("libircmt.lib", text)
                self.assertIn("libirc.lib", text)

    def test_skips_wrong_architecture_directory_for_later_suffix(self):
        make_sdk(self.sdk, machine="x86")
        libraries = {
            "libircmt", "libirc", "libmmd", "libmmdd", "libmmt",
            "svml_dispmd", "svml_dispmt",
        }
        for library in libraries:
            write_archive(
                self.sdk / "lib/intel64" / (library + ".lib"), "x64")
        _, _, result = self.configure(
            "discover", IntelRuntime_ROOT=self.sdk, EXPECT_STATIC_IRC="ON")
        text = result.read_text(encoding="utf-8").replace("\\", "/")
        self.assertIn("/lib/intel64/libircmt.lib", text)
        self.assertIn("/lib/intel64/libirc.lib", text)

    def test_rejects_wrong_archive_architecture(self):
        make_sdk(self.sdk, machine="x86")
        completed, _, _ = self.configure(
            "discover", expect_success=False, IntelRuntime_ROOT=self.sdk)
        self.assertIn("Intel C runtime libraries for x64 are required", completed.stdout)

    def test_rejects_missing_required_libraries(self):
        make_sdk(self.sdk, libraries={"libmmd", "svml_dispmd"})
        completed, _, _ = self.configure(
            "discover", expect_success=False, IntelRuntime_ROOT=self.sdk)
        self.assertIn("IntelRuntime_IRC_LIBRARY", completed.stdout)

    def test_reuses_parent_aggregate(self):
        _, _, result = self.configure("parent")
        self.assertEqual(result.read_text(encoding="utf-8"), "parent-reused\n")

    def test_selects_each_crt_mode(self):
        make_sdk(self.sdk)
        for mode, runtime in RUNTIMES.items():
            with self.subTest(mode=mode):
                _, _, result = self.configure(
                    "select", IntelRuntime_ROOT=self.sdk, RUNTIME=runtime,
                    CMAKE_BUILD_TYPE="Debug" if mode.endswith("d") else "Release")
                links = result.read_text(encoding="utf-8")
                self.assertIn("IntelRuntime::C_" + mode, links)
                for other in RUNTIMES:
                    if other != mode:
                        self.assertNotIn("IntelRuntime::C_" + other + ";", links)

    def test_rejects_explicit_empty_runtime_policy(self):
        make_sdk(self.sdk)
        completed, _, _ = self.configure(
            "select", expect_success=False, IntelRuntime_ROOT=self.sdk,
            RUNTIME="", CMAKE_BUILD_TYPE="Release")
        self.assertIn(
            "IntelRuntime_RUNTIME_LIBRARY must be a nonempty CRT expression",
            completed.stdout,
        )

    def test_md_does_not_require_static_irc(self):
        make_sdk(self.sdk, libraries={"libircmt", "libmmd", "svml_dispmd"})
        _, _, result = self.configure(
            "select", IntelRuntime_ROOT=self.sdk,
            RUNTIME=RUNTIMES["MD"], CMAKE_BUILD_TYPE="Release")
        self.assertIn("IntelRuntime::C_MD", result.read_text(encoding="utf-8"))

    def test_mt_mode_requires_static_irc(self):
        make_sdk(self.sdk, libraries={"libircmt", "libmmt", "svml_dispmt"})
        completed, _, _ = self.configure(
            "select", expect_success=False, IntelRuntime_ROOT=self.sdk,
            RUNTIME=RUNTIMES["MT"], REQUIRE_STATIC_IRC="ON",
            CMAKE_BUILD_TYPE="Release")
        self.assertIn("IntelRuntime::STATIC_IRC", completed.stdout)

    def test_producer_links_and_installs_concrete_record(self):
        make_sdk(self.sdk)
        _, build, result = self.configure(
            "producer", IntelRuntime_ROOT=self.sdk,
            RUNTIME=RUNTIMES["MD"], CMAKE_BUILD_TYPE="Release")
        self.assertIn("IntelRuntime::C_MD", result.read_text(encoding="utf-8"))
        prefix = self.root / "install"
        completed = subprocess.run(
            [CMAKE, "--install", str(build), "--prefix", str(prefix),
             "--config", "Release"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            encoding="utf-8", errors="replace", check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout)
        record = prefix / "lib/cmake/Pkg/PkgIntelRuntime-Release.cmake"
        self.assertIn(
            'set(Pkg_INTEL_RUNTIME_RELEASE "MultiThreadedDLL")',
            record.read_text(encoding="utf-8"),
        )
        self.assertIn(
            'set(Pkg_INTEL_IPO_RELEASE "0")',
            record.read_text(encoding="utf-8"),
        )

    def test_release_ipo_import_maps_debug_link_option(self):
        make_sdk(self.sdk)
        _, build, _ = self.configure(
            "producer", IntelRuntime_ROOT=self.sdk,
            RUNTIME=RUNTIMES["MD"], IPO="ON", CMAKE_BUILD_TYPE="Release")
        prefix = self.root / "ipo-install"
        completed = subprocess.run(
            [CMAKE, "--install", str(build), "--prefix", str(prefix),
             "--config", "Release"],
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            encoding="utf-8", errors="replace", check=False,
        )
        self.assertEqual(completed.returncode, 0, completed.stdout)
        records = prefix / "lib/cmake/Pkg"
        record = records / "PkgIntelRuntime-Release.cmake"
        self.assertIn(
            'set(Pkg_INTEL_IPO_RELEASE "1")',
            record.read_text(encoding="utf-8"),
        )
        _, _, result = self.configure(
            "import", RECORD_DIR=records, IMPORTED_CONFIGS="Release",
            CMAKE_BUILD_TYPE="Debug")
        self.assertIn(
            "IntelRuntime::C_MD", result.read_text(encoding="utf-8"))
        options = Path(str(result) + ".raw").read_text(encoding="utf-8")
        self.assertIn("STREQUAL:$<UPPER_CASE:$<CONFIG>>,DEBUG", options)
        self.assertIn("LINK_LANG_AND_ID:C,IntelLLVM", options)
        self.assertIn(":-Qipo>", options)

    def test_release_only_import_maps_debug_to_release_runtime(self):
        records = self.root / "records"
        write_record(records, "Release", RUNTIMES["MD"])
        _, _, result = self.configure(
            "import", RECORD_DIR=records, IMPORTED_CONFIGS="Release",
            CMAKE_BUILD_TYPE="Debug")
        links = result.read_text(encoding="utf-8")
        self.assertIn("IntelRuntime::C_MD", links)
        self.assertNotIn("IntelRuntime::C_MDd", links)

    def test_existing_import_map_takes_precedence(self):
        records = self.root / "records"
        write_record(records, "Debug", RUNTIMES["MDd"])
        write_record(records, "Release", RUNTIMES["MD"])
        _, _, result = self.configure(
            "import", RECORD_DIR=records, IMPORTED_CONFIGS="Debug;Release",
            USER_MAP="Release", CMAKE_BUILD_TYPE="Debug")
        text = result.read_text(encoding="utf-8")
        self.assertIn("IntelRuntime::C_MD", text)
        self.assertNotIn("IntelRuntime::C_MDd", text)
        self.assertIn("map=Release", text)

    def test_import_is_idempotent_for_the_same_policy(self):
        records = self.root / "records"
        write_record(records, "Release", RUNTIMES["MD"])
        _, _, result = self.configure(
            "import", RECORD_DIR=records, IMPORTED_CONFIGS="Release",
            IMPORT_AGAIN="ON", CMAKE_BUILD_TYPE="Debug")
        raw = Path(str(result) + ".raw").read_text(encoding="utf-8")
        self.assertEqual(raw.count("IntelRuntime::C_MD"), 1)

    def test_import_rejects_conflicting_record_policy(self):
        first = self.root / "records-first"
        second = self.root / "records-second"
        write_record(first, "Release", RUNTIMES["MD"])
        write_record(second, "Release", RUNTIMES["MT"])
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=first,
            SECOND_RECORD_DIR=second, IMPORTED_CONFIGS="Release",
            IMPORT_AGAIN="ON", CMAKE_BUILD_TYPE="Debug")
        self.assertIn(
            "Conflicting Pkg Intel runtime records for an already imported target",
            completed.stdout,
        )

    def test_import_map_empty_element_falls_back_to_noconfig(self):
        records = self.root / "records"
        write_record(records, "NoConfig", RUNTIMES["MD"])
        _, _, result = self.configure(
            "import", RECORD_DIR=records, IMPORTED_CONFIGS="NOCONFIG",
            USER_MAP="Release;", CMAKE_BUILD_TYPE="Debug")
        text = result.read_text(encoding="utf-8")
        self.assertIn("IntelRuntime::C_MD", text)
        self.assertIn("map=Release;", text)

    def test_missing_import_record_fails_clearly(self):
        records = self.root / "records"
        records.mkdir()
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=records,
            IMPORTED_CONFIGS="Release", CMAKE_BUILD_TYPE="Debug")
        self.assertIn("Pkg has no Intel runtime record", completed.stdout)

    def test_missing_ipo_record_fails_clearly(self):
        records = self.root / "records"
        write_record(records, "Release", RUNTIMES["MD"], ipo=None)
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=records,
            IMPORTED_CONFIGS="Release", CMAKE_BUILD_TYPE="Debug")
        self.assertIn("Pkg has no Intel runtime record", completed.stdout)

    def test_invalid_ipo_record_fails_clearly(self):
        records = self.root / "records"
        write_record(records, "Release", RUNTIMES["MD"], ipo="AUTO")
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=records,
            IMPORTED_CONFIGS="Release", CMAKE_BUILD_TYPE="Debug")
        self.assertIn("Invalid Pkg Intel IPO record for RELEASE", completed.stdout)

    def test_invalid_import_record_fails_clearly(self):
        records = self.root / "records"
        write_record(records, "Release", "corrupt-runtime")
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=records,
            IMPORTED_CONFIGS="Release", CMAKE_BUILD_TYPE="Debug")
        self.assertIn("Invalid Pkg Intel CRT record: corrupt-runtime", completed.stdout)

    def test_malformed_import_record_fails_clearly(self):
        records = self.root / "records"
        write_record(records, "Release", "", malformed=True)
        completed, _, _ = self.configure(
            "import", expect_success=False, RECORD_DIR=records,
            IMPORTED_CONFIGS="Release", CMAKE_BUILD_TYPE="Debug")
        self.assertIn("Parse error", completed.stdout)


if __name__ == "__main__":
    unittest.main()
