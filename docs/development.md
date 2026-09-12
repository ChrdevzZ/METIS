# Development checks

For current build instructions see [building](building.md). For the fixed
upstream source, compatibility changes and Unreleased history see
[fork changes](../FORK_CHANGES.md).


Ordinary builds require CMake 3.24 and a supported C compiler. GKlib tests also
use C++11. Python 3.9 and Git are required only when developer testing is enabled.
Fortran is needed only for the optional installed-package consumer fixture.
Use `METIS_BUILD_TESTING`, `METIS_BUILD_INTEGRATION_TESTING`, and
`METIS_BUILD_DEVELOPER_TESTING` together for the full development suite.

CTest covers API behavior, build/install consumers and optional maintenance
tool regressions. Run the upstream mapping audit explicitly when changing
source provenance; it is not a prerequisite for building or testing a source
archive, which may not contain the recorded Git objects.

```sh
python tools/upstream.py check
git diff --check
```

Review changed declarations, comments and macro layout against the mapped
upstream source. Keep the existing style without bulk reformatting. There
are no text-presence or documentation-layout gates in CTest. To exercise
the documented installation workflows, enable integration testing and run
the installation and consumer tests directly through CTest.

Use an initialized compiler environment and a separate build/install directory
for each toolchain and ABI. Keep MSVC runtime selection consistent across the
library, dependencies and consumers. Keep producer builds isolated to one
MSYS2 environment. Installed C-library consumption between UCRT64 and CLANG64
can be compatible when architecture, C ABI, UCRT ownership and external
runtimes match; their libstdc++ and libc++ object interfaces do not interchange.

Matching `/MT` flags do not make CRT-owned objects portable between DLLs.
Shared GKlib exposes `FILE*` and signal state, and METIS programs exchange
private allocations with shared METIS. These paths require `/MD[d]`; the CMake
CRT fixtures cover valid and rejected policies without executing target code.
Static libraries retain `/MT[d]` support. Library-only shared METIS with static
GKlib retains its public C API under `/MT[d]`, with library-owned deallocation.
See Microsoft's explanation of
[CRT objects across DLL boundaries](https://learn.microsoft.com/en-us/cpp/c-runtime-library/potential-errors-passing-crt-objects-across-dll-boundaries).
Cross-compiling probes compile and link without running target executables;
execute CTest only when a target runtime or emulator is available.

Use the `portable` preset for Release checks with IPO and native CPU tuning
disabled in METIS and GKlib. Use `optimized` when IPO support is required in
both projects; it also keeps native CPU tuning disabled. Neither preset changes
the library type. Static `AUTO` builds do not enable IPO unless inherited from
an explicit parent CMake policy, while shared Release-like configurations under
`AUTO` probe support. Keep all existing preset names available for established
cases.

For Windows cross-compiler static consumers, validate object architecture,
calling ABI, `CMAKE_MSVC_RUNTIME_LIBRARY`, and METIS width settings as separate
compatibility boundaries. Intel runtime libraries are external inputs selected
with `IntelRuntime_ROOT` or `CMAKE_PREFIX_PATH`; they are never copied into a
source package or installed by METIS. Mixed-language fixtures must verify the
final C, C++, or Fortran link language independently.

Keep compiler inventories, local paths, complete logs, and run summaries below
an ignored build directory. Public documentation describes repeatable commands
and expected contracts rather than the outcome of one workstation run.

Do not change the accepted upstream baseline during a check. Review both the
source candidate and the mapping candidate after an upstream update. Deleted
legacy build files remain recorded and must not be restored automatically.

The retained `utils/listunescapedsymbols.csh` script is an optional audit for
internal symbols in a directory of METIS object files. Despite its suffix it is
a `/bin/sh` script; it requires `nm`, `grep`, `egrep` and `awk`, and prints
candidate `libmetis__` renaming definitions for manual review. It does not edit
files and is not a build or test gate.

## Performance validation

The performance harness requires Bash and built `gpmetis` and `ndmetis`
programs. Configure a Release build with programs enabled, then keep references
and all run output in the binary tree:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DMETIS_BUILD_PROGRAMS=ON
cmake --build build/release --parallel 2
METIS_BUILD_DIR="$PWD/build/release" \
  bash perf/harness.sh ref "$PWD/build/release/perf/reference"
METIS_BUILD_DIR="$PWD/build/release" \
  bash perf/harness.sh verify "$PWD/build/release/perf/reference"
METIS_BUILD_DIR="$PWD/build/release" \
  bash perf/harness.sh bench candidate 3
```

The reference directory contains `manifest.txt` plus one output per enabled
case. The repository-provided `mdual.graph` enables eight required cases: k-way
and recursive-bisection partitioning at 10, 50 and 100 parts, plus normal and
connected-component ordering. Supplying `cit-Patents.metis` through
`METIS_GRAPH_DIR` adds three optional k-way cases, for eleven cases total; that
directory must also contain `mdual.graph`. Verification must report one pass per
manifest entry and zero failures. Never compare references made for a different
case set.

The harness copies graph inputs and transient partition/order output below
`<METIS_BUILD_DIR>/perf/`; `bench` appends timing rows to
`<METIS_BUILD_DIR>/perf/RESULTS.tsv`. Set `METIS_BIN_DIR` for a nonstandard
program directory and `METIS_CONFIG` for a multi-config build. Use
`METIS_BASELINE_BIN_DIR` with `perf/compare.sh` or `perf/mem.sh` for A/B runs.
Run timing samples serially on an otherwise idle machine and retain the exact
compiler, configuration, graph set and reference manifest with the results.

[`PERF-REVIEW.md`](../PERF-REVIEW.md) and
[`perf/RESULTS.md`](../perf/RESULTS.md) preserve the June 2026 campaign's old
commands and measurements. They are historical records, not current gates.

## Build-file organization

The root `CMakeLists.txt` declares project options and performs platform
checks before creating targets. Explain each option immediately above its
declaration, including defaults and relevant ABI or performance effects.
Use two-space indentation, blank lines between responsibilities and two
between independent functions or major sections. Keep generated expressions
and embedded probe source intact. Reusable IPO/link-probe helpers and
installation templates remain in `cmake/`; source lists stay with their
owning `src/`, `include/` and `apps/` directories.

## Coding agents

Shared instructions are in [AGENTS.md](../AGENTS.md). See [agent setup](agents.md)
for discovery and [architecture](architecture.md) for code orientation.
