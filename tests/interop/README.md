# Installed-package interoperability fixture

This standalone CMake project verifies the installed `METIS::metis` target
and its transitive dependencies. A self-contained shared METIS package needs
no separate GKlib development package. The default configuration needs only
a C compiler. It
partitions a graph with asymmetric `real_t` target weights and an imbalance
value, verifies an inverse ordering from `METIS_NodeND`, then checks and frees
adjacency storage allocated by `METIS_MeshToNodal`.

Configure, build, and run the default C executable with installed package
prefixes appropriate for your system:

```sh
cmake -S tests/interop -B build/interop-c \
  -DCMAKE_PREFIX_PATH=/path/to/install
cmake --build build/interop-c --parallel 2
ctest --test-dir build/interop-c --output-on-failure
```

Select the C++ consumer with `-DINTEROP_MAIN_LANGUAGE=CXX`.

Fortran checks are opt-in. They use `ISO_C_BINDING` interfaces to call the C
API directly and exercise the original underscore wrappers for default
options and ordering. They derive the Fortran integer and real kinds from the installed
package's `METIS_IDXTYPEWIDTH` and `METIS_REALTYPEWIDTH` values. A C or C++
main calls the Fortran `BIND(C)` test routine when `INTEROP_FORTRAN=ON`:

```sh
cmake -S tests/interop -B build/interop-c-fortran \
  -DCMAKE_PREFIX_PATH=/path/to/install \
  -DINTEROP_FORTRAN=ON -DINTEROP_MAIN_LANGUAGE=C
cmake --build build/interop-c-fortran --parallel 2
ctest --test-dir build/interop-c-fortran --output-on-failure
```

Use `-DINTEROP_MAIN_LANGUAGE=Fortran -DINTEROP_FORTRAN=ON` to make the
Fortran program the executable entry point. That configuration enables only
the Fortran language. The supported METIS width pairs are 32 or 64 bits
independently for `idx_t` and `real_t`; no width flags need to be supplied to
the fixture.
