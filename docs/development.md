# Development checks

For current build instructions see [building](building.md). For the fixed
upstream source, compatibility changes and Unreleased history see
[fork changes](../FORK_CHANGES.md).


Ordinary builds require CMake 3.24 and a supported C compiler. GKlib tests also
use C++11. Python 3.9 and Git are required only when developer testing is enabled.
Fortran is needed only for the optional installed-package consumer fixture.
The same width-adaptive fixture exercises direct C entry points and the
original underscore wrappers. The C symbol test invokes all four NodeND
spellings; the export inventory covers every wrapper symbol.
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
the installation and consumer tests directly through CTest. The dependency-provider
fixture also runs through the opt-in integration suite, covering missing
sources, offline overrides and parent FetchContent declarations.

Program-specific regressions cover strict target-weight parsing and the
MOVEINFO-only single-candidate diagnostic paths. The ordering fixture uses a
fill-in operation count above the 32-bit range so reporting remains independent
of host pointer width. Input fixtures also cover
nonpositive edge weights, pure self-loop repair, single-stream graph/mesh reads,
read/write failure status and output replacement/rollback. They require the
corresponding METIS programs; a library-only suite does not register those
command tests.

The performance-script fixture owns a marked directory below the test binary
tree. Direct invocations must use a new directory or one already marked by the
fixture; existing unmarked directories, symlinks and source ancestors are
rejected before cleanup. POSIX systems do not require the MSYS2 `cygpath` tool.

The API regression also checks node-refinement balance decisions with unchanged
separator weight. Its 64-bit fixture uses partition-weight differences beyond
the C `int` range so that accidental narrowing cannot silently reject a valid
balance improvement. Legal aggregate vertex weights at `IDX_MAX` and maximum
`NITER`, `NIPARTS` and `UFACTOR` options exercise exact balance thresholds,
bounded iteration and count arithmetic, and saturated nonnegative real-to-index
thresholds without changing representable-range decisions.
The API fixture also enables the private block-partitioning debug path on a
low-degree graph and verifies that multi-constraint input continues through
the supported partitioner.

Allocation-failure fixtures exercise control/graph/workspace construction,
neighbor-pool exhaustion, derived size overflow, both numbering modes and
caller-owned graph/mesh outputs. Each covered public failure must return the
documented status, restore temporary input numbering and leave result pointers
null or unchanged as required by that API.
Mesh conversion checks both returned-array `malloc` failures and rejects
aliased output slots before changing input numbering.
They also cover failed workspace marker insertion, markerless pop and the
post-partition node-element and row-induction allocation points in mesh
partitioning; both caller partition arrays and the objective remain unchanged
on failure.
Minimum-cover failure injection covers every matching and
decomposition allocation and requires the caller's cover to remain unchanged.
Application fault injection checks that connectivity and post-partition
statistics return an allocation error after releasing every partially
allocated work array. A failed statistics report must produce no partial
success output; reporting begins only after every fallible calculation and
allocation succeeds. Empty induced vertex sets must return zero components
without indexing beyond the graph.

Enable `GKLIB_BUILD_TESTING=ON` when a source-provider METIS build should also
register GKlib's deterministic allocation-failure checks. They require mcore
capacity growth to commit transactionally, failed tracked reallocations to
retain the original record, partial constructors to clean up, and marker-rejected
frees or mcore cleanup to retain ownership records and caller handles. These
invariants preserve the state needed by METIS when a `SIGMEM` recovery point
converts allocation failure into `METIS_ERROR_MEMORY`. The constructor fixture
also checks every initial node-bisection and K-way refinement allocation for
both cut and volume objectives. Failed replacement must retain the previous
graph refinement state, including the volume-mode cut-info alias. Coarse-graph
construction rejects invalid dimensions and unrepresentable counts before any
allocation or finer/coarser link is changed. The three retained matching
strategies also stop when their shared bucket-sort workspace cannot be
allocated, before reading its uninitialized permutation. Minimum degree
ordering checks its derived workspace length before changing graph numbering;
the public failure fixture verifies the error status, output preservation and
restoration of one-based input.

The Python developer checks also exercise runtime copying, unchanged-file
timestamps, missing and empty inputs, and bounded recovery from a transient
Windows exclusive lock. A lock held beyond the retry budget must still fail.

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
CTest runtime checks use the configured emulator or explicitly report a skip
when it is absent. Build and link checks must still succeed before a composite
scenario may report that runtime skip. See the [test execution contract](building.md#tests-and-maintenance).

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
