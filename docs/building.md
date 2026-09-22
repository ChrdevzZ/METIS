# Building METIS

This reference describes the current fork. For the fixed upstream comparison
and removed legacy commands, see [fork changes](../FORK_CHANGES.md).

## Requirements

CMake 3.24 or newer, a supported C compiler for the C99 sources, and a CMake
build tool are required. Ninja presets require Ninja. GKlib is a separate
required dependency. Building GKlib's own tests also requires C++11; METIS's
normal C API and program tests do not enable an extra language.

Python 3.9 and Git are needed only for developer checks. Optional Fortran
consumers require a Fortran compiler; integration performance-script checks
also require Bash. MSVC can compile these sources without offering a strict
ISO C99 language mode.

## Source preparation

```sh
git clone --recurse-submodules https://github.com/ChrdevzZ/METIS.git
cd METIS
```

Read [dependencies](#dependencies) before configuring this Unreleased checkout.
All generated headers, build products and test output belong in binary directories.
`include/` holds public header templates, `src/` private implementation, `apps/`
tools, and `ext/` dependency orchestration.

## Build configuration


Select the compiler, generator, toolchain file, installation prefix and MSVC
runtime through standard CMake variables before the first configuration. Use
separate binary and installation directories for incompatible toolchains or
ABIs. Debug, Release, RelWithDebInfo and MinSizeRel are supported.

For a multi-config generator:

```sh
cmake -S . -B build/multi -G "Ninja Multi-Config"
cmake --build build/multi --config Release --parallel 2
ctest --test-dir build/multi -C Release --output-on-failure
cmake --install build/multi --config Release --prefix /path/to/prefix
```

`BUILD_SHARED_LIBS` supplies the initial shared-library default. Project-specific
options take precedence; no option forces unrelated parent targets to use the
same settings. Programs, tests and install rules default on only at top level.


| Option | Default | Purpose |
| --- | --- | --- |
| `METIS_BUILD_SHARED_LIBS` | `BUILD_SHARED_LIBS`, otherwise OFF | Shared instead of static library |
| `METIS_BUILD_PROGRAMS` | Top-level only | Build all six command-line tools |
| `METIS_BUILD_TESTING` | Top-level only | Build and register regression tests |
| `METIS_INSTALL` | Top-level only | Generate installation rules |
| `METIS_BUILD_INTEGRATION_TESTING` | OFF | Isolated build, package and install regressions |
| `METIS_BUILD_DEVELOPER_TESTING` | OFF | Maintenance tool regressions; requires `METIS_BUILD_TESTING` |
| `METIS_IDXTYPEWIDTH` | 32 | Integer width: 32 or 64 |
| `METIS_REALTYPEWIDTH` | 32 | Floating-point width: 32 or 64 |
| `METIS_ASSERTIONS` | AUTO | ON, OFF, or enabled in Debug |
| `METIS_ASSERTIONS_EXPENSIVE` | OFF | Expensive invariant checks |
| `METIS_DEBUG` | AUTO | Debug instrumentation in Debug |
| `METIS_IPO` | AUTO | Follow parent IPO policy; otherwise probe shared Release-like builds |
| `METIS_NATIVE_OPTIMIZATION` | OFF | Explicit native CPU optimization |
| `METIS_GPROF` | OFF | Compiler and linker profiling instrumentation |
| `METIS_SANITIZERS` | Empty | Semicolon list: address;undefined |
| `METIS_WARNINGS_AS_ERRORS` | OFF | Treat project warnings as errors |
| `METIS_GKLIB_PROVIDER` | AUTO | AUTO, SYSTEM, or SOURCE dependency selection |
| `METIS_GKLIB_SOURCE_DIR` | Empty | Explicit local GKlib source directory |
| `METIS_FETCH_GKLIB` | OFF | Permit fetching the fixed GKlib revision |


`METIS_ASSERTIONS=AUTO` and `METIS_DEBUG=AUTO` follow Debug configuration.
Explicitly requested unsupported features fail configuration. OpenMP, regex,
random-number selection and architecture fallbacks belong to GKlib.

`METIS_WARNINGS_AS_ERRORS` promotes the selected compiler's project warnings
to build errors. It is an opt-in diagnostic policy; retained upstream code can
still produce unused-variable and numeric-conversion warnings. Keep it off for
ordinary dependency builds, or review those diagnostics with the selected
compiler. Configuration success does not certify a warning-free build.

`METIS_IPO=AUTO` honors explicit per-configuration parent IPO settings before
its global setting. Without parent policy, static libraries and applications do
not enable IPO; shared Release, RelWithDebInfo and MinSizeRel probe support,
while Debug and custom configurations remain off. `ON` requires a successful
capability check; `OFF` disables IPO for METIS targets.

Only configurations available in the current build are checked. Each check
configures and links a small CMake project with that configuration's compiler,
CRT, compile/link flags and target options. Static checks link an archive into
a caller; shared checks link a shared library. Target programs are never run.
Required IPO failures report a diagnostic log; default shared AUTO disables
only the configuration whose check failed. Results are cached in this build
tree per project, configuration, link type and effective input signature. An
unchanged reconfigure reuses the result and log. Disabled IPO needs no probe.
This checks the selected toolchain and options, not arbitrary dependency graphs
or another compiler's IPO object format.

The Ninja presets are `default` (Release), `debug`, `shared` (Release shared),
`wide` (Release 64/64), `portable` and `optimized`. The latter two are Release
presets: portable explicitly disables IPO and native tuning in both projects;
optimized requires IPO in both while still disabling native tuning. They do
not select a library type. An already installed GKlib is not rebuilt by a
METIS preset. Keep machine paths in untracked `CMakeUserPresets.json`.

Native tuning is opt-in and unavailable for cross-compilation or universal
multi-architecture builds. Quote sanitizer lists such as
`-DMETIS_SANITIZERS="address;undefined"` so the shell passes one CMake argument.

Sanitizer checks require actual instrumentation and its final-link runtime.
An ignored compiler switch is not support. Native MSVC supports AddressSanitizer
only; clang-cl and Intel LLVM are checked separately. MSVC-style frontends do
not implement gprof's `-pg` interface and reject `METIS_GPROF=ON`.

Compile-and-link feature checks use an executable and the active configuration,
including configuration-specific flags and the selected custom linker. Common
diagnostics for ignored or unsupported options make a requested capability fail
even when the compiler exits successfully. Each check restores its temporary
state and caches a result only for the complete effective input signature.

For Windows MSVC-ABI Clang and Intel LLVM ASan, the build records the runtime
libraries selected by the compiler for the effective architecture and CRT.
Static installation consumers recover those libraries through their compiler
SDK, `CompilerRuntime_ROOT` or `CMAKE_PREFIX_PATH`; no producer SDK paths are
exported. Use a compatible producer runtime SDK, including its DLLs. A
completed shared library requires runtime deployment, not the static development
SDK. Sanitized archives have
additional runtime constraints beyond the ordinary C ABI.

ASan final links using LLD disable string tail merging to avoid overlapping
instrumented globals, following the [LLVM workaround](https://github.com/llvm/llvm-project/pull/74207).
The link interface follows the final language and target
linker selection, including explicit `LINKER_TYPE` overrides; ordinary
`link.exe` links do not receive LLD-only switches. Intel Windows IPO links use
the Intel driver and its LLD path. This workaround does not change ordinary
unsanitized builds or make IPO archives portable between compiler toolchains.

Build-tree tools and tests copy the selected ASan DLL beside their executables.
On Windows, a bounded retry handles a short permission or sharing conflict
during that copy; missing inputs and persistent failures remain fatal.
Installed packages do not redistribute compiler runtimes. Deploy compatible
ASan and other required runtime DLLs explicitly; an unrelated SDK on `PATH`
must not be used as a substitute. Debug CRT support is checked for the actual
compiler configuration and is not assumed from Release ASan support.

For MSVC AddressSanitizer builds, prefer `RelWithDebInfo`: without debug
information MSVC emits [C5072](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-c5072?view=msvc-170).
Warnings-as-errors is an opt-in diagnostic policy; inherited source warnings on
some compilers can prevent a build even when the default configuration succeeds.

## Dependencies

`ext/CMakeLists.txt` manages the dependency on
[ChrdevzZ/GKlib](https://github.com/ChrdevzZ/GKlib). An existing `GKlib::GKlib`
target is always reused. `METIS_GKLIB_PROVIDER` accepts `AUTO` (default),
`SYSTEM`, or `SOURCE`.

- `AUTO`: explicit `METIS_GKLIB_SOURCE_DIR`, installed package, initialized
  `ext/GKlib`, then explicitly enabled download.
- `SYSTEM`: installed package selected with `GKlib_ROOT` or `CMAKE_PREFIX_PATH`.
- `SOURCE`: explicit source directory or initialized submodule.

`METIS_FETCH_GKLIB=ON` explicitly permits fetching the pinned revision. CMake
never initializes submodules or downloads dependencies by default. For an
offline build, provide compatible sources or an installed package beforehand.

When METIS manages a source dependency, it enables GKlib installation together
with METIS unless `GKLIB_INSTALL` was explicitly set by the parent.

The gitlink and `ext/CMakeLists.txt` select the same fixed GKlib fork commit,
including the explicit-export `_PROTO_EX` templates and configuration headers
used by METIS. In an existing clean checkout, initialize the dependency with
`git submodule update --init --recursive`. Preserve any local submodule changes
before updating its checkout.

Publish the referenced GKlib commit before publishing a METIS commit that uses
it. Until that commit is available remotely, use the matching local checkout
or its installed package; remote submodule initialization and explicit fetching
cannot obtain a commit that exists only locally.

An explicit local dependency can be selected without modifying the submodule:

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMETIS_GKLIB_PROVIDER=SOURCE -DMETIS_GKLIB_SOURCE_DIR=/path/to/GKlib
```

For an installed dependency, use `METIS_GKLIB_PROVIDER=SYSTEM` and
`GKlib_ROOT=/path/to/prefix`. METIS uses the native fork package rather than
inferring its compile definitions and dependencies from a library filename.
When source is managed by METIS, its shared/static choice also defaults to
`METIS_BUILD_SHARED_LIBS` unless `GKLIB_BUILD_SHARED_LIBS` was explicitly set.
See the [GKlib build reference](../ext/GKlib/docs/building.md) for its own options.

A static OpenMP-enabled GKlib package records the producer runtime library
names. Its package resolves those runtime libraries without enabling C or C++
or adding another compiler's OpenMP compile flags, so a pure Fortran METIS
package consumer remains supported. Set `OpenMP_ROOT` or `CMAKE_PREFIX_PATH` to
a compatible producer SDK when the runtime libraries are outside normal search
paths. A shared GKlib needs its runtime DLL at execution time, but its private
OpenMP link does not require that development SDK from consumers. A PCRE-enabled
GKlib package also records its producer-validated static/shared linkage; in a
pure Fortran consumer it uses a compile-and-link-only `BIND(C)` symbol check and
does not guess the linkage when that record is unavailable.


## Installation and consumption

```sh
cmake --build build/release --parallel 2
cmake --install build/release --prefix /path/to/prefix
```

Install directories follow GNUInstallDirs; Windows DLLs go into the binary
directory. Only public headers are installed. Exported targets carry include
paths and link dependencies and remain relocatable with the install prefix.

```cmake
find_package(METIS 5 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE METIS::metis)
```

For source integration, use `add_subdirectory(ext/METIS)` and the same target.
A parent may first add GKlib to supply `GKlib::GKlib`, which METIS reuses.
Set project-prefixed program/test/install options before adding the directory
when the parent needs those components.

Use the installed `<metis.h>` for the chosen 32/64 integer and real widths.
Conflicting consumer width definitions are rejected. Separate incompatible ABI
variants into separate installation prefixes.

`metis-uninstall` removes only files recorded by METIS during installation.
GKlib has its own `gklib-uninstall` target. Records include actual target names,
Debug postfixes, configuration, component, install prefix, and `DESTDIR`.
Parent-project files are not inferred from their names. Keep the build tree's
`install-manifests/` directory when uninstall support is needed.

```sh
cmake --build build/release --target metis-uninstall
cmake --build build/release --target gklib-uninstall
```

If that build installed into more than one prefix, select one explicitly:

```sh
cmake -DMETIS_UNINSTALL_PREFIX=/your/install/prefix -P build/release/MetisUninstall.cmake
cmake -DGKLIB_UNINSTALL_PREFIX=/your/install/prefix -P build/release/ext/GKlib/cmake/uninstall.cmake
```

Uninstall rejects malformed records and paths outside the recorded prefix.
Use install destinations within that prefix for the dedicated uninstall target.

For staged UNIX installs, supply the same `DESTDIR` for uninstall. METIS install
components are `METIS_Runtime`, `METIS_Development`, and `METIS_Applications`.
A shared METIS library linked with static GKlib does not require the GKlib SDK
when consumed from its installed package; static METIS and dynamic GKlib
combinations retain their exported dependencies.

## Platforms and mixed compilers

On Windows, an IntelLLVM-produced static archive consumed by another compiler
family needs the matching external Intel C runtime development libraries. Set
`IntelRuntime_ROOT` or add that SDK to `CMAKE_PREFIX_PATH`. The helper validates
the target architecture and selects the MD, MDd, MT, or MTd libraries matching
the producer's MSVC runtime policy. It does not bundle or install Intel runtime
files. For an Intel IPO archive, the extra final-link option is emitted only for
IntelLLVM C, C++, or Fortran link languages; this does not make the archive
compatible with a foreign IPO linker.

Cross-compiler consumers must still agree on object architecture, calling ABI,
MSVC runtime policy, and the configured `METIS_IDXTYPEWIDTH` and
`METIS_REALTYPEWIDTH`. Install incompatible width or runtime variants into
separate prefixes.

Windows DLL boundaries also constrain CRT-owned objects. Shared GKlib requires
`/MD` or `/MDd`: its `FILE*` and signal interfaces cannot safely cross separate
static CRT instances. METIS applications likewise require the DLL CRT when
using shared METIS or GKlib, because private graph allocations cross that
boundary. These configurations are checked before building. `/MT` and `/MTd`
remain supported for static libraries and applications. A library-only shared
METIS with static GKlib can use a static CRT; callers must use `METIS_Free` for
memory returned by METIS and must not use its private application interfaces.

A parent project can select compilers separately for languages that CMake's
platform modules allow together. On Windows, CMake 3.24 and 4.3 reject
mixing MSVC with Clang or another CL-compatible compiler ID across C and C++
during language initialization; this is a CMake toolchain restriction, not a
finding that ordinary ABI-compatible objects cannot interoperate. This project
does not bypass that platform check. C and compatible Fortran compilers can
still be selected independently.

Different C compilers for GKlib and METIS require separate producer builds and
installed packages; `add_subdirectory()` does not choose a second C compiler.
Do not assume LLVM-based IPO formats are interchangeable. Fortran callers can
use the existing wrappers or interoperable `BIND(C)` calls; private compiler
module files are not a portable exchange interface. See the corresponding
[CMake 3.24](https://gitlab.kitware.com/cmake/cmake/-/blob/v3.24.0/Modules/Platform/Windows-Clang.cmake#L151-166)
and [CMake 4.3](https://gitlab.kitware.com/cmake/cmake/-/blob/v4.3.0/Modules/Platform/Windows-Clang.cmake#L180-202)
platform checks.

Keep each MSYS2 producer build inside one environment. MINGW64 uses MSVCRT;
UCRT64 and CLANG64 both use UCRT, so compatible installed C-library consumption
between the latter two is possible when architecture, calling ABI, runtime
ownership and external dependencies match. Their C++ standard libraries differ,
and IPO/object compatibility must be established separately.

See [Windows toolchains](../BUILD-Windows.txt) and
[Fortran examples](../tests/interop/README.md). Cross-compilation tests must not
run target executables without a suitable runtime or emulator.

The installed-package fixture uses the package's integer and real widths for
both direct C calls and the original underscore wrappers. It links only the
public METIS target, allowing its package configuration to restore any required
dependencies instead of unconditionally requiring a separate GKlib package.

## Tests and maintenance

```sh
ctest --test-dir build/release --output-on-failure
```

Python 3.9 or newer and Git are needed only for maintenance tools. The optional
integration suite also requires Bash for performance-script regressions; set
`METIS_BASH_EXECUTABLE` if it is not on `PATH`. Enable the checks explicitly:

```sh
cmake -S . -B build/developer -G Ninja -DCMAKE_BUILD_TYPE=Release -DMETIS_BUILD_DEVELOPER_TESTING=ON -DMETIS_BUILD_INTEGRATION_TESTING=ON
cmake --build build/developer --parallel 2
ctest --test-dir build/developer --output-on-failure
python tools/upstream.py check
```

Review changed source style against the recorded upstream files without bulk
reformatting. See [development checks](development.md) for maintenance commands,
test coverage and toolchain limitations.

Nested build tests inherit a bounded set of toolchain, architecture, CRT and
compiler/linker settings through a generated initial cache. They use the active
CTest configuration; scenarios that deliberately select another configuration
state it explicitly. The cache does not enable additional project languages.

Runtime tests execute native programs directly. Cross-compiled programs run
only through `CMAKE_CROSSCOMPILING_EMULATOR`, including its argument list. Without
an emulator, configuration, compilation and linking are still checked, while
runtime checks are reported as skipped. A skipped run is not execution evidence,
and earlier build failures remain failures.

Windows ASan fixtures cover configuration selection, configless records, failed
lookup and retry behavior, external SDK resolution, and LLD versus `link.exe`
final-link selection. A separate fixture enables its C++ final-link language
only after embedding the sanitized C library.

API tests validate graph symmetry, independently recompute partition edge cuts
and compare mesh adjacency sets without requiring one neighbor order. The export
regression checks both unchanged reconfiguration and automatic regeneration when
the independent symbol baseline changes. Program and API regressions reject
overflowing or inverted target-weight ranges, incomplete distributions whose
specified sum already reaches one, non-finite values, nonpositive edge weights
and trailing input. They also cover pure self-loop repair,
constructor/workspace failure, numbering restoration, transactional output and
MOVEINFO diagnostics with one move candidate.

`upstream/files.json` records every source/header relationship to the official
upstream revision, including moved files, generated-header templates, and
intentionally removed build files. Updates produce advisory candidates only:

```sh
python tools/upstream.py check
python tools/upstream.py compare --to <upstream-commit>
python tools/upstream.py prepare --to <upstream-commit> --output build/upstream-review
```

Replace `<upstream-commit>` with an available commit ID. Network access requires
explicit opt-in; neither detection nor candidate generation updates the source,
index, or accepted baseline. Review conflicts, candidate patches and mapping
changes together. See [upstream tracking](upstream.md).

Performance scripts require all repository-provided datasets. With `mdual.graph`
they run eight mandatory cases; an externally supplied `cit-Patents.metis` adds
three optional cases. References include a manifest for that exact case set.
A failed executable, stale output, empty timing, missing manifest entry or
missing required case is a failure, not a passing comparison. Reference data,
copied graphs and `RESULTS.tsv` stay below the selected build directory. See the
[current commands and output contract](development.md#performance-validation).

Use `cmake --build build/release --target clean` to clean selected build output.
Use `cpack --config build/release/CPackSourceConfig.cmake` for a source archive;
CPack exclusions are independent of Git ignore rules. CTest memory checking
requires a configured checker such as Valgrind. See [development checks](development.md)
for suite requirements and [upstream tracking](upstream.md) for accepting changes.
