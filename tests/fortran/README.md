# Fortran consumer test

This standalone consumer verifies the legacy lowercase-underscore Fortran
wrappers with a real `ISO_C_BINDING` program. It calls
`metis_setdefaultoptions_` and `metis_nodend_`, then validates the returned
permutation. Use a METIS package built with 32-bit indices, matching
`integer(c_int)` in the test.

```sh
cmake -S tests/fortran -B build/fortran \
  -DCMAKE_PREFIX_PATH=/path/to/metis/install \
  -DCMAKE_Fortran_COMPILER=/path/to/fortran/compiler
cmake --build build/fortran --config Release
ctest --test-dir build/fortran -C Release --output-on-failure
```

The selected Fortran installation must provide its intrinsic `iso_c_binding`
module and matching runtime libraries. Keep compiler paths in a toolchain file
or `CMakeUserPresets.json` when this fixture is used repeatedly.
