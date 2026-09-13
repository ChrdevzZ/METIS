# METIS

> **Note:** this is the [ChrdevzZ/METIS](https://github.com/ChrdevzZ/METIS) fork
> of [KarypisLab/METIS](https://github.com/KarypisLab/METIS), with a modernized
> CMake build and integration support. See [fork changes](FORK_CHANGES.md) for
> the fixed upstream baseline, differences, and Unreleased history.

METIS is a set of serial programs for partitioning graphs, partitioning finite element meshes,
and producing fill reducing orderings for sparse matrices. The algorithms implemented in
METIS are based on the multilevel recursive-bisection, multilevel k-way, and multi-constraint
partitioning schemes developed in our lab.

## Requirements

CMake 3.24 or newer, a C compiler capable of compiling the C99 sources, and
a build tool supported by CMake are required. The Ninja examples below require
Ninja on PATH. Python and Fortran are optional maintenance/consumer tools.

METIS requires the modern CMake interface of [ChrdevzZ/GKlib](https://github.com/ChrdevzZ/GKlib).
The submodule in `ext/GKlib` and the optional download select the same fixed
fork revision. Use the initialized submodule, an explicit compatible checkout
with `-DMETIS_GKLIB_SOURCE_DIR=/path/to/GKlib`, or its installed package.
See [dependency preparation](docs/building.md#dependencies) before configuring.

## Download

```sh
git clone --recurse-submodules https://github.com/ChrdevzZ/METIS.git
cd METIS
```

When publishing local changes, make the referenced GKlib commit available in
its fork before publishing the METIS commit. Local commits alone do not make
either repository available to remote clones.

## Build and install

```sh
cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 2
ctest --test-dir build/release --output-on-failure
cmake --install build/release --prefix /path/to/prefix
```

These commands build the static library and standalone tools and tests. Select
an initialized compiler environment first. For a multi-config generator, omit
`CMAKE_BUILD_TYPE` and pass `--config Release` to build and install, and
`-C Release` to CTest. See the [build reference](docs/building.md).

Set `METIS_BUILD_SHARED_LIBS=ON` for a shared library. The `portable` and
`optimized` presets select conservative or required IPO policies; they do not
change the library type. IPO checks compile and link for the active configuration
without running target programs. See [configuration](docs/building.md#build-configuration)
for all options and preset differences.

## Use in another project

Link an existing application target to an installed package:

```cmake
find_package(METIS 5 CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE METIS::metis)
```

Set `CMAKE_PREFIX_PATH` to its installation prefix when configuring the parent
project. Alternatively, include a source checkout:

```cmake
add_subdirectory(ext/METIS)
target_link_libraries(my_application PRIVATE METIS::metis)
```

Include `<metis.h>` in application code. Subdirectory builds leave programs,
tests, and installation disabled unless requested by the parent. See
[installation and consumption](docs/building.md#installation-and-consumption)
for dependency, ABI, and static/shared combinations.

Windows consumers must respect CRT ownership, external compiler runtimes for
Intel-built archives, and ASan SDK and deployment requirements for
sanitizer-instrumented libraries. See
[configuration](docs/building.md#build-configuration) and
[platforms and mixed compilers](docs/building.md#platforms-and-mixed-compilers).

## Development

Coding agents should read [AGENTS.md](AGENTS.md); see
[agent setup](docs/agents.md) for client discovery and adapters.

See [development checks](docs/development.md), [upstream tracking](docs/upstream.md),
and [fork changes](FORK_CHANGES.md). Local toolchain paths and validation results
belong in ignored build directories.

## Copyright & License Notice

Copyright 1998-2020, Regents of the University of Minnesota

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
