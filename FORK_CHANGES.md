# Changes in the ChrdevzZ METIS fork

## Source and comparison baseline

| Item | Source |
| --- | --- |
| Fork | [ChrdevzZ/METIS](https://github.com/ChrdevzZ/METIS) |
| Official upstream | [KarypisLab/METIS](https://github.com/KarypisLab/METIS) |
| Upstream source branch | `master` |
| Accepted upstream commit | [`272d4a91c5f66c92327493339a476c553ebf1f5d`](https://github.com/KarypisLab/METIS/commit/272d4a91c5f66c92327493339a476c553ebf1f5d) |
| Fork status | Unreleased changes in the current checkout |

The complete commit identifies the comparison baseline; `master` identifies its
source branch and is not a floating replacement for that baseline. The URL and
SHA agree with [upstream/files.json](upstream/files.json). Upstream references
below mean that fixed tree, not a later branch tip or the original 5.2.1 release.

METIS uses [ChrdevzZ/GKlib](https://github.com/ChrdevzZ/GKlib), whose independent
[change document](ext/GKlib/FORK_CHANGES.md) records its official GKlib baseline.
The METIS gitlink and [download revision](ext/CMakeLists.txt) identify the same
GKlib fork commit, distinct from the accepted upstream comparison baseline.
See [dependency preparation](docs/building.md#dependencies) for local sources
and installed packages. Publish GKlib before METIS so remote users can obtain
the referenced dependency; a local commit does not constitute publication.

## Inherited functionality

METIS remains the serial graph/mesh partitioning and sparse-matrix ordering
library, with the existing multilevel algorithms, C API, Fortran wrapper names,
six command-line tools and version 5.2.1. Public inclusion remains `<metis.h>`.
The upstream baseline already includes bit-identical algorithm optimizations;
those are inherited work, not new fork optimizations. IPO default changes below
are build-policy changes and do not establish a universal speedup.

Original attribution, license notices and [Changelog](Changelog) are retained.
The Changelog describes upstream history; fork changes belong here. Source
compatibility depends on using the generated headers and the correct configured
ABI. This fork is not byte-identical to upstream source and does not promise
unconditional binary compatibility.

## Build and directory changes

Upstream combines handwritten Make entry points with older CMake configuration.
The fork uses CMake 3.24 and C99 sources directly, removing the handwritten
Makefiles, legacy parameter module, obsolete TLS probe, old Visual Studio
helper scripts, obsolete source-rewrite helpers and obsolete METIS 4-era tests.
The internal-symbol prefix audit helper remains available for manual use.
CMake-generated Makefiles remain usable; the removed interfaces are not aliases
for current options.

`libmetis/` becomes `src/`, and `programs/` becomes `apps/`. Public header
configuration belongs to `include/`; third-party source orchestration belongs
to `ext/`. Each compiled directory manages explicit source lists and its targets.
Private implementation headers are available to tools but are not installed.
Generated ABI/export headers and test output stay in the binary tree.

The existing `metis` library target gains the public `METIS::metis` alias.
Project-prefixed options and standard CMake variables replace old global build
flags. Top-level program/test/install defaults differ from subdirectory defaults
so embedding METIS does not configure unrelated parent targets. See the
[complete build reference](docs/building.md) for current options and defaults.

## Configuration, optimization and dependencies

The configured public header fixes both integer and real widths at 32 or 64
and rejects conflicting consumer definitions. Standard configurations control
AUTO assertions/debug code. Public template policy uses project-specific macros
rather than exporting a consumer-wide `NDEBUG` policy.

Unlike upstream's automatic optimized IPO path, ordinary static libraries do
not independently enable IPO under AUTO. Explicit parent IPO policy is honored;
shared optimized configurations probe support. ON requires support and OFF
turns it off for project targets. Cached checks avoid repeated probes for the
same toolchain/flag signature. The portable and optimized presets make the
tradeoff explicit; native CPU tuning remains opt-in. Instrumentation and
optional features fail clearly when explicitly requested but unavailable.

GKlib is still an external dependency, now orchestrated through its native fork
CMake target. Parent targets are reused first. AUTO, SYSTEM and SOURCE select
installed or local sources; downloads require opt-in and a fixed revision.
This replaces assumptions based on a library path with an explicit dependency
interface. See [dependencies](docs/building.md#dependencies) for precedence and
current publication limitations.

Static OpenMP-enabled GKlib packages retain the producer runtime by recorded
library name instead of imposing C or C++ OpenMP discovery on the consumer.
This permits pure Fortran package consumption when a compatible producer SDK is
available through `OpenMP_ROOT` or `CMAKE_PREFIX_PATH`. PCRE-enabled packages
also preserve their producer-validated static/shared linkage and use a
compile-and-link-only `BIND(C)` symbol check for pure Fortran consumers rather
than guessing that linkage. Shared GKlib consumers need runtime deployment, not
the producer's OpenMP development SDK.

## Production source and header changes

Most algorithm files are moved without content changes. The following mapped
files have substantive differences; paths describe the fixed upstream tree
and this fork respectively. Build files, new tests and generated headers are
covered separately by the mapping and build documentation.

| Upstream path | Local path | Change and reason |
| --- | --- | --- |
| `include/metis.h` | `include/metis.h.in` | Generate widths/version/export attributes; use standard integer headers; use `llabs` for 64-bit `iabs` on LLP64 Windows. The minimum signed value still has no representable positive absolute value. |
| `libmetis/frename.c` | `src/frename.c` | Apply export and calling-convention declarations to existing Fortran wrapper names. |
| `libmetis/gklib_defs.h`, `libmetis/proto.h` | `src/gklib_defs.h`, `src/proto.h` | Declare METIS-owned template instances and internal entry points with METIS export attributes so DLL tools can link. |
| `libmetis/metislib.h` | `src/metislib.h` | Make METIS template instances follow METIS assertion policy. |
| `programs/metisbin.h` | `apps/metisbin.h` | Use the private library include interface instead of cross-directory legacy paths; remove the dummy function-name override. |
| `programs/io.c` | `apps/io.c` | Allocate output filename buffers from input length and free them, avoiding oversized Windows stack arrays and fixed-size filename storage. |
| `programs/gpmetis.c`, `programs/mpmetis.c`, `programs/ndmetis.c` | Corresponding files in `apps/` | Restrict Linux-specific resource reporting to Linux instead of assuming every non-macOS system supports it. |

## Installation, compatibility and validation

Relocatable Config/Version/Targets files describe installation consumption.
Public headers and enabled tools follow GNUInstallDirs. Static packages retain
needed dependency targets; a shared METIS linked to static GKlib does not require
the consumer to locate the GKlib development package. Each project's uninstall
target uses its own actual installation records rather than filename guesses,
including configuration, component, postfix, prefix and DESTDIR distinctions.

Compatible ordinary C objects may be produced and consumed by different Windows
compiler families. Architecture, width, calling convention, CRT and memory
ownership still matter. Intel-built static archives can need external Intel
runtimes even without IPO; `IntelRuntime_ROOT` or `CMAKE_PREFIX_PATH` locates them
at consumption time. No Intel runtime binaries are bundled or installed.
IPO archives have additional producer/final-linker constraints; common LLVM
ancestry is not an interoperability guarantee.

MSYS2 producer builds remain isolated. UCRT64 and CLANG64 share UCRT and can
consume compatible installed C interfaces when the remaining ABI and runtime
contracts match; their libstdc++ and libc++ object interfaces do not cross that
boundary. MINGW64 uses the different MSVCRT runtime.

Shared GKlib and METIS applications using shared METIS require `/MD[d]` because
CRT-owned objects/private allocations cross DLL boundaries. Fully static builds
and library-only shared METIS backed by static GKlib retain `/MT[d]` support;
callers must release METIS-returned allocations with `METIS_Free`.
See [platform contracts](docs/building.md#platforms-and-mixed-compilers).

Current tests cover the API, CLI output, exports, width choices, dependency
selection, package relocation, uninstall ownership and C/C++/Fortran consumers.
The performance scripts reject failed executions, stale output, missing required
cases and invalid timing samples. These are repeatable checks, not claims that
every platform has run successfully. Cross targets require a runtime or emulator
before execution. Local inventories and timing samples are not source-package
contents.

## Migrating legacy commands

| Legacy entry | Current interface |
| --- | --- |
| `make config`, `make`, `make install` | `cmake -S/-B`, `cmake --build`, `cmake --install` |
| `cc`, `prefix`, `gklib_path` | `CMAKE_C_COMPILER`, `CMAKE_INSTALL_PREFIX`, `GKlib_ROOT` |
| `shared`, `SHARED` | `METIS_BUILD_SHARED_LIBS`; select GKlib's type separately when needed |
| `i64`, `r64` | `METIS_IDXTYPEWIDTH=64`, `METIS_REALTYPEWIDTH=64` |
| `gdb`, `GDB` | Debug or RelWithDebInfo |
| `debug`, `DEBUG`, `assert`, `ASSERT`, `ASSERT2` | `METIS_DEBUG`, `METIS_ASSERTIONS`, `METIS_ASSERTIONS_EXPENSIVE` |
| `gprof`, `GPROF` | `METIS_GPROF` |
| `openmp`, `OPENMP`, `PCRE`, `GKREGEX`, `GKRAND` | Corresponding [GKlib options](ext/GKlib/docs/building.md#build-configuration) |
| `valgrind` | CTest memory checking with a configured checker |
| `make clean`, `make distclean` | Build the `clean` target; remove only the selected binary directory for a full reset |
| `make uninstall` | Build `metis-uninstall`; GKlib has its own uninstall target |
| `make dist` | CPack with the build tree's `CPackSourceConfig.cmake` |

## Maintenance and history

The [file mapping](upstream/files.json) is the machine-readable record of
upstream/local paths, accepted blobs, templates and intentional deletions.
Generated headers are build products, not upstream source baselines. See
[upstream tracking](docs/upstream.md) for read-only comparison and candidate
patch generation. Ordinary builds do not require Python or Git.

When accepting an upstream update, review its source patch and mapping together,
then update the baseline in this document and the mapping. Remove changes from
the current-difference sections when upstream has absorbed them; preserve their
history. A newer upstream branch tip alone does not change the accepted baseline.
Do not automatically restore intentionally removed build files.

### Unreleased

- Keep project options and platform checks in the root CMakeLists.txt, with
  per-option explanations; retain reusable helpers and packaging in cmake/.

- Replace Claude-only guidance with canonical AGENTS.md instructions and thin
  Claude/Gemini import adapters; preserve architecture context in developer docs.

- Replace legacy configuration entry points with target-based CMake builds,
  installation packages and explicit developer checks.
- Remove obsolete in-place source rewrite scripts while retaining and mapping
  the non-mutating internal-symbol prefix audit helper.
- Archive the June 2026 performance review and measurements, and document the
  current CMake-based reference, verification, case-set and binary-tree output
  workflow separately.
- Add portable static/optimized IPO policies, external Intel runtime discovery,
  and compatible mixed-compiler consumption with explicit CRT boundaries.
- Retain the platform and public-header corrections described above and add
  regression coverage for their build and consumer contracts.
- Separate the introductory README, complete build reference and fixed-baseline
  fork comparison; preserve upstream ownership and license notices.

These entries describe the current modified checkout, not a published fork
release. Add actual release identifiers only after a release exists. Public
documentation records reproducible workflows and limitations, not workstation
inventories, absolute paths or unrepeatable performance claims.
