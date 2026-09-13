# CMake migration notes

For the complete fixed-baseline comparison and legacy-command migration table,
see [fork changes](../FORK_CHANGES.md). Current commands and defaults are in the
[build reference](building.md). This page retains the established architecture
and reference links.


The migration separates public headers, private library implementation,
applications, and external dependencies. Each directory owns its targets and
source lists. Upstream algorithms and source formatting are retained except for
the documented correctness and platform fixes in [fork changes](../FORK_CHANGES.md).

## Build contracts

- CMake 3.24 and C99 are the baseline. Build policy is target-local.
- `METIS::metis` and `GKlib::GKlib` are the consumer interfaces.
- Installed GKlib dependencies use their native Config package, which carries
  static dependencies and platform definitions. Guessing these from a library
  filename is insufficient for Windows shared libraries.
- Generated ABI and export headers belong to the binary tree and are installed
  with public headers. METIS implementation headers are never installed.
- Dependencies are reused before discovery or acquisition. Fetching requires
  explicit opt-in and uses an immutable revision.

## Platform corrections

Windows DLL exports use declarations annotated with generated export macros.
METIS internal declarations and all four Fortran wrapper naming conventions are
exported as well as the public C API. Template instantiations use the export
attribute of the library that owns them. This avoids scanning MSVC LTCG object
files to generate exports, which fails for IPO builds.

Three CLI output routines allocate filename buffers from the heap. Their former
large local arrays overflowed the default Windows stack before writing output.
The allocation is sized from the input filename and freed after use. Resource
usage reporting remains available on supported non-Apple Unix systems; reading
peak virtual memory from `/proc` is guarded separately for Linux.

Regex selection checks both the header and linkable functions. A header alone
is insufficient on MINGW64. Math and instrumentation probes also check the final
link. `AUTO` leaves ordinary static archives without IPO unless a parent policy
requests it; shared Release-like configurations probe the active toolchain.
IntelLLVM static IPO libraries export the required final-link option only to a
matching Intel final link language.

Foreign Windows compilers consuming ordinary IntelLLVM static objects use an
external `IntelRuntime::C` target. `IntelRuntime_ROOT` or `CMAKE_PREFIX_PATH`
selects the SDK. The lookup checks target architecture and chooses the runtime
variant that matches the producer's MSVC CRT policy. Intel libraries and DLLs
are neither bundled nor installed. Object architecture, ABI, CRT, and METIS
width settings remain explicit compatibility boundaries.

Public template assertions use GKlib-specific policy macros independently of a
consumer's `NDEBUG`. Expensive assertions have their own switch. Static Windows
headers identify their configured library variant without depending on CMake
usage definitions for TLS selection.

## Reference material

- [CMake 3.24 GenerateExportHeader](https://cmake.org/cmake/help/v3.24/module/GenerateExportHeader.html)
  defines shared/static export macros and their use in declarations.
- [CMake 3.24 WINDOWS_EXPORT_ALL_SYMBOLS](https://cmake.org/cmake/help/v3.24/prop_tgt/WINDOWS_EXPORT_ALL_SYMBOLS.html)
  describes object scanning and the separate requirement for imported data.
- [CMake 3.24 FetchContent](https://cmake.org/cmake/help/v3.24/module/FetchContent.html)
  documents dependency overrides and disconnected operation.
- [MSYS2 environments](https://www.msys2.org/docs/environments/)
  distinguishes UCRT64, CLANG64, and MINGW64 runtime and standard-library choices.

## Verification workflow

Keep machine-specific toolchain paths, inventories, logs, and result summaries
below an ignored build directory. Record compiler identity, configuration,
architecture, CRT policy, and width settings for each invocation. Do not treat
compiler discovery, successful configuration, successful linking, and runtime
execution as the same result.

The `portable` preset disables IPO and native CPU tuning in METIS and GKlib.
The `optimized` preset requires IPO in both projects and keeps native CPU tuning
disabled. Neither changes the library type. Existing preset names remain
available for their established configurations.

The Windows DLL regression checks a fixed manifest of expected public C and
Fortran names with `GetProcAddress`; it also checks the
internal entry points needed by applications. `tests/dependencies.cmake` checks
missing dependencies, explicit offline source overrides, and parent declarations.

Building the project's C sources with MSVC does not certify complete ISO C99
conformance. Microsoft provides no strict C99 mode, and older toolsets predate
the C11/C17 switches introduced in VS 2019 16.8. See the official
[/std documentation](https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version?view=msvc-170).
