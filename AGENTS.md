# Repository guidance: METIS

## Scope and sources

This is the shared guidance for coding agents working on ChrdevzZ/METIS. Read
[README](README.md) for purpose, [building](docs/building.md) for current options,
and [fork changes](FORK_CHANGES.md) for the accepted upstream repository, branch
and full commit. The mapping in `upstream/files.json` records file provenance.

`ext/GKlib` is a separate ChrdevzZ/GKlib repository. Before changing files there,
read its [AGENTS.md](ext/GKlib/AGENTS.md); use its commands and project-specific
contracts for that subtree. Do not assume an agent starting at the METIS root
automatically loads nested instructions. GKlib must remain independently usable.

## Project map

METIS 5.2.1 implements serial graph/mesh partitioning and sparse-matrix ordering
through multilevel coarsening, partitioning and refinement. Preserve its C API,
Fortran wrapper names and algorithm behavior unless the task changes them.

- `include/metis.h.in`: public API and configured idx_t/real_t widths.
- `src/`: algorithm implementation and private headers; `metislib.h` is the
  internal include interface, and `struct.h` defines graph/control/mesh state.
- `apps/`: six command-line programs using private library interfaces.
- Root `CMakeLists.txt`: project options, target configuration and platform checks.
- `ext/`: dependency resolution; `cmake/`: target/probe helpers and packaging.
- `tests/`: API/CLI, package, ABI and integration fixtures; `graphs/`: test inputs;
  `perf/`: performance/reference scripts with output in the binary tree.

Use [architecture notes](docs/architecture.md) for algorithm entry points. Public
names use `METIS_`; existing internal renaming headers isolate library symbols.
Initialize options with `METIS_SetDefaultOptions`; free returned allocations
through `METIS_Free`. Follow existing GKlib error-recovery and ownership patterns.

## Build and test

Use CMake 3.24 or newer and a compiler capable of compiling these C99 sources.
Ninja examples require Ninja and an initialized compiler environment. The build
needs the pinned GKlib fork sources or a compatible installed package. Consult
[dependency preparation](docs/building.md#dependencies) before configuring.

```sh
cmake -S . -B build/agent -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/agent --parallel 2
ctest --test-dir build/agent --output-on-failure
```

Use `METIS_GKLIB_SOURCE_DIR` for an explicit compatible local source checkout.
For multi-config generators omit CMAKE_BUILD_TYPE and pass `--config Release`
to build/install and `-C Release` to CTest. METIS's default preset is Release.
The public CMake target is `METIS::metis`; width cache variables are
`METIS_IDXTYPEWIDTH` and `METIS_REALTYPEWIDTH`.

Programs/tests/install default off in subdirectory builds. Enable the explicit
`METIS_BUILD_TESTING`, `METIS_BUILD_INTEGRATION_TESTING` and
`METIS_BUILD_DEVELOPER_TESTING` options for top-level developer coverage; see the
build reference for Bash and other optional test prerequisites. Static AUTO IPO
stays off unless inherited; portable and optimized presets select explicit IPO
policies. Shared GKlib or tools using shared METIS require the DLL CRT on Windows.

For embedded full-suite runs, also enable `METIS_BUILD_PROGRAMS` so
program-dependent tests are registered; test switches alone do not build tools.

## Editing and build contracts

- Preserve existing user changes, public APIs, upstream attribution and licenses.
  Keep production C/header changes narrowly justified; do not bulk-format them.
- Follow neighboring C declarations, indentation, comments and macro continuation
  style. Use two-space CMake indentation, a blank line between responsibilities
  and two between independent functions or large sections. Do not split generator
  expressions or rewrite embedded source strings. Use English comments that
  explain contracts, not line-by-line translations. Write edited Markdown as LF.
- Keep source lists explicit and properties target-local. Do not introduce global
  compiler/CRT/optimization policy, generated files in source directories, or
  automatic network access. Keep standalone and add_subdirectory builds usable.
- Preserve caller-owned template export attributes and library-owned allocation
  boundaries. Check architecture, widths, CRT and external runtimes separately;
  compiler brand alone neither proves nor disproves C ABI compatibility. IPO
  archives require additional final-linker compatibility.
- Keep local tool paths, inventories, logs and one-off scripts in ignored build
  directories. Do not publish those artifacts or infer success from old logs.
  Do not add CI, release actions or dependency updates as an incidental change.

## Validation and reporting

- Run focused tests for the actual change, using separate build/install paths
  for incompatible compilers, CRTs or widths. Default local build parallelism to
  2; do not impose that limit on downstream CMake users.
- For library behavior changes run API/CLI tests; for public headers and package
  interfaces also test installed C/C++ consumers. For build-policy changes use
  the relevant integration fixtures; Fortran changes need actual wrapper and
  interoperable API tests when that compiler is available.
- Audit mappings with the maintenance tool and review changed source style
  as described in [development](docs/development.md). Python/Git are maintenance
  dependencies only;
  ordinary builds must not require them. Documentation-only changes need link,
  option and example checks rather than a complete compiler matrix.
- Cross compilation may compile/link probes but must not run target programs
  without an appropriate runtime or emulator. Distinguish passed, failed,
  unavailable and unexecuted cases. Report commands, meaningful results and
  limitations without turning local observations into platform guarantees.
- Review source and index changes before finishing; do not stage, commit, push,
  change accepted upstream baselines or update dependency revisions unless the
  task authorizes those actions. Explicit task instructions govern the scope.

## Code Review Rules

Flag regressions in ABI width/export/TLS/CRT ownership, static dependency closure,
parent-project isolation, offline configuration, install/uninstall ownership and
source-tree cleanliness. Require evidence appropriate to the changed behavior.
Check that fork-specific corrections are distinguished from inherited upstream
work, and that generated headers are not mistaken for upstream source files.
