include("${CMAKE_CURRENT_SOURCE_DIR}/TestSupport.cmake")
set(_metis_test_initial_cache
  "${CMAKE_CURRENT_BINARY_DIR}/test-initial-cache.cmake")
metis_test_write_initial_cache("${_metis_test_initial_cache}")

add_executable(metis_api_test api.c)
target_link_libraries(metis_api_test PRIVATE METIS::metis)
metis_test_add_runtime(NAME metis.api TARGET metis_api_test)
set_tests_properties(metis.api PROPERTIES TIMEOUT 30)

add_executable(metis_api_invalid_dimensions api_invalid_dimensions.c)
target_link_libraries(metis_api_invalid_dimensions PRIVATE METIS::metis)
metis_test_add_runtime(NAME metis.api-invalid-dimensions
  TARGET metis_api_invalid_dimensions)
set_tests_properties(metis.api-invalid-dimensions PROPERTIES TIMEOUT 30)

add_executable(metis_coarsen_numeric coarsen_numeric.c)
target_include_directories(metis_coarsen_numeric PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_coarsen_numeric PRIVATE METIS::metis
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.coarsen-numeric TARGET metis_coarsen_numeric)
set_tests_properties(metis.coarsen-numeric PROPERTIES TIMEOUT 30)

# Compile the numbering helpers locally so shared-library visibility does not
# hide the failure-recovery contract exercised by this fixture.
add_executable(metis_numbering_failure numbering_failure.c
  "${PROJECT_SOURCE_DIR}/src/fortran.c")
target_compile_definitions(metis_numbering_failure PRIVATE METIS_STATIC_DEFINE)
target_include_directories(metis_numbering_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/src" "${PROJECT_BINARY_DIR}/include")
target_link_libraries(metis_numbering_failure PRIVATE GKlib::GKlib)
metis_test_add_runtime(NAME metis.numbering-failure
  TARGET metis_numbering_failure)
set_tests_properties(metis.numbering-failure PROPERTIES TIMEOUT 30)

# Compile against the private graph interface to verify that on-disk spill
# creation cannot clobber an existing filesystem entry.
add_executable(metis_ondisk_file_security ondisk_file_security.c)
target_include_directories(metis_ondisk_file_security PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_ondisk_file_security PRIVATE METIS::metis
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.ondisk-file-security
  TARGET metis_ondisk_file_security)
set_tests_properties(metis.ondisk-file-security PROPERTIES TIMEOUT 30)

# Compile against the private workspace interface to verify that rejected
# growth leaves pool positions, capacities, and tracked pointers unchanged.
add_executable(metis_workspace_failure workspace_failure.c)
target_include_directories(metis_workspace_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_workspace_failure PRIVATE METIS::metis
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.workspace-failure
  TARGET metis_workspace_failure)
set_tests_properties(metis.workspace-failure PROPERTIES TIMEOUT 30)

# GNU's linker wrapping lets this fixture fail each allocator call without
# adding test hooks to the production constructors.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND NOT METIS_BUILD_SHARED_LIBS)
  add_executable(metis_constructor_failure constructor_failure.c)
  target_include_directories(metis_constructor_failure PRIVATE
    "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(metis_constructor_failure PRIVATE METIS::metis
    GKlib::GKlib)
  target_link_options(metis_constructor_failure PRIVATE
    "-Wl,--wrap=gk_malloc"
    "-Wl,--wrap=gk_malloc_nosignal"
    "-Wl,--wrap=gk_mcoreMalloc"
    "-Wl,--wrap=malloc"
    "-Wl,--wrap=free")
  metis_test_add_runtime(NAME metis.constructor-failure
    TARGET metis_constructor_failure)
  set_tests_properties(metis.constructor-failure PROPERTIES TIMEOUT 30)
  metis_copy_runtime(metis_constructor_failure)

  add_executable(metis_stat_failure stat_failure.c
    "${PROJECT_SOURCE_DIR}/apps/stat.c")
  target_include_directories(metis_stat_failure PRIVATE
    "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(metis_stat_failure PRIVATE METIS::metis
    GKlib::GKlib)
  target_link_options(metis_stat_failure PRIVATE "-Wl,--wrap=gk_malloc")
  metis_test_add_runtime(NAME metis.stat-failure TARGET metis_stat_failure)
  set_tests_properties(metis.stat-failure PROPERTIES TIMEOUT 30)
  metis_copy_runtime(metis_stat_failure)
endif()

add_executable(metis_smbfactor_failure smbfactor_failure.c)
target_include_directories(metis_smbfactor_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_smbfactor_failure PRIVATE METIS::metis
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.smbfactor-failure
  TARGET metis_smbfactor_failure)
set_tests_properties(metis.smbfactor-failure PROPERTIES TIMEOUT 30)

# Compile the application I/O unit with private failpoints for stream,
# allocation, close, commit and rollback errors.
add_executable(metis_output_pair_failure output_pair_failure.c
  "${PROJECT_SOURCE_DIR}/apps/io.c")
target_compile_definitions(metis_output_pair_failure PRIVATE
  METIS_IO_TRANSACTION_TEST)
target_include_directories(metis_output_pair_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_output_pair_failure PRIVATE METIS::metis
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.output-pair-failure
  TARGET metis_output_pair_failure)
set_tests_properties(metis.output-pair-failure PROPERTIES TIMEOUT 30)

if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  target_compile_definitions(metis_ondisk_file_security PRIVATE
    _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(metis_output_pair_failure PRIVATE
    _CRT_SECURE_NO_WARNINGS)
endif()

add_executable(metis_fortran_symbols fortran_symbols.c)
target_link_libraries(metis_fortran_symbols PRIVATE METIS::metis)
metis_test_add_runtime(NAME metis.fortran-symbols TARGET metis_fortran_symbols)
set_tests_properties(metis.fortran-symbols PROPERTIES TIMEOUT 30)
metis_copy_runtime(metis_api_test)
metis_copy_runtime(metis_api_invalid_dimensions)
metis_copy_runtime(metis_coarsen_numeric)
metis_copy_runtime(metis_numbering_failure)
metis_copy_runtime(metis_ondisk_file_security)
metis_copy_runtime(metis_workspace_failure)
metis_copy_runtime(metis_smbfactor_failure)
metis_copy_runtime(metis_output_pair_failure)
metis_copy_runtime(metis_fortran_symbols)

# On Windows, verify the shared library exports the compatibility symbol list.
if(WIN32 AND METIS_BUILD_SHARED_LIBS)
  # Keep the compatibility oracle independent of current declarations.
  metis_test_export_header(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/export_symbols.h"
    SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/export-symbols.txt"
    SOURCE exports.c)

  add_executable(metis_export_test exports.c)
  target_include_directories(metis_export_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(metis_export_test PRIVATE METIS::metis)

  metis_copy_runtime(metis_export_test)
  metis_test_add_runtime(NAME metis.exports TARGET metis_export_test
    ARGS "$<TARGET_FILE:metis>")
  set_tests_properties(metis.exports PROPERTIES TIMEOUT 30)
endif()

# Exercise each command-line workflow against known graph fixtures.
if(METIS_BUILD_PROGRAMS)
  foreach(case graph kway rb nd mesh dual nodal fill)
    add_test(NAME metis.${case}
      COMMAND ${CMAKE_COMMAND}
        -DCASE=${case} -DGRAPH_DIR=${PROJECT_SOURCE_DIR}/graphs
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${case}
        -DGP=$<TARGET_FILE:gpmetis> -DND=$<TARGET_FILE:ndmetis>
        -DMP=$<TARGET_FILE:mpmetis> -DMG=$<TARGET_FILE:m2gmetis>
        -DGC=$<TARGET_FILE:graphchk> -DCF=$<TARGET_FILE:cmpfillin>
        "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake)
    set_tests_properties(metis.${case} PROPERTIES TIMEOUT 30)
    if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
      set_tests_properties(metis.${case} PROPERTIES
        SKIP_REGULAR_EXPRESSION
          "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
    endif()
  endforeach()

  # Reject malformed target-weight records before they can update the
  # partition-weight matrix with incomplete or out-of-range values.
  add_test(NAME metis.tpwgts
    COMMAND ${CMAKE_COMMAND}
      -DGP=$<TARGET_FILE:gpmetis>
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/tpwgts
      "-DIDXTYPEWIDTH=${METIS_IDXTYPEWIDTH}"
      "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tpwgts.cmake)
  set_tests_properties(metis.tpwgts PROPERTIES TIMEOUT 30)

  add_test(NAME metis.input-validation
    COMMAND ${CMAKE_COMMAND}
      -DGP=$<TARGET_FILE:gpmetis>
      -DMP=$<TARGET_FILE:mpmetis>
      -DND=$<TARGET_FILE:ndmetis>
      -DMG=$<TARGET_FILE:m2gmetis>
      -DGC=$<TARGET_FILE:graphchk>
      -DCF=$<TARGET_FILE:cmpfillin>
      -DGRAPH_DIR=${PROJECT_SOURCE_DIR}/graphs
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/input-validation
      "-DIDXTYPEWIDTH=${METIS_IDXTYPEWIDTH}"
      "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/input_validation.cmake)
  set_tests_properties(metis.input-validation PROPERTIES TIMEOUT 60)

  # Exercise two-sided refinement diagnostics when only one boundary queue
  # supplies a candidate move.
  add_test(NAME metis.moveinfo
    COMMAND ${CMAKE_COMMAND}
      -DND=$<TARGET_FILE:ndmetis>
      -DGRAPH=${PROJECT_SOURCE_DIR}/graphs/mdual.graph
      -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/moveinfo
      "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P ${CMAKE_CURRENT_SOURCE_DIR}/moveinfo.cmake)
  set_tests_properties(metis.moveinfo PROPERTIES TIMEOUT 60)

  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties(metis.tpwgts metis.input-validation metis.moveinfo
      PROPERTIES
      SKIP_REGULAR_EXPRESSION
        "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  endif()
endif()

# Install ownership covers static and shared dependency layouts separately.
if(METIS_BUILD_INTEGRATION_TESTING)
  if(METIS_INSTALL)
    set(_metis_test_native_msvc_asan FALSE)
    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND
        "address" IN_LIST METIS_SANITIZERS)
      set(_metis_test_native_msvc_asan TRUE)
    endif()

    # Windows MSVC-ABI Clang and Intel LLVM targets expose an imported runtime
    # target. Native MSVC instead asks its linker to infer the SDK libraries,
    # so exercise its installed interface without requiring that target.
    if(TARGET METIS::ASanRuntime OR _metis_test_native_msvc_asan)
      add_subdirectory(sanitizers)
    endif()
  endif()

  # Verify the toolchain-cache serialization and runtime execution boundary
  # used by nested integration and cross-compilation fixtures.
  add_test(NAME metis.test-support
    COMMAND "${CMAKE_COMMAND}"
      "-DMODULE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/TestSupport.cmake"
      -DFUNCTION_PREFIX=metis
      "-DSKIP_MARKER=METIS_RUNTIME_SKIPPED: no cross-compiling emulator"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-support"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/test-support/verify.cmake")
  set_tests_properties(metis.test-support PROPERTIES TIMEOUT 120)

  add_test(NAME metis.install-ownership
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DTEST_PROJECT=METIS"
      "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-ownership"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CONFIG=$<CONFIG>"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/install/verify.cmake")
  set_tests_properties(metis.install-ownership PROPERTIES TIMEOUT 300)
  add_test(NAME metis.install-ownership-shared
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DTEST_PROJECT=METIS"
      "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/shared/install-ownership"
      -DTEST_SHARED=ON
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CONFIG=$<CONFIG>"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/install/verify.cmake")
  set_tests_properties(metis.install-ownership-shared PROPERTIES TIMEOUT 300)

  # Exercise the complete static producer, relocated package, source-tree and
  # parent-project consumer workflow that is not covered by isolated fixtures.
  add_test(NAME metis.integration
    COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}/integration"
      "-DCONFIG=$<CONFIG>"
      -DSHARED=OFF
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/integration.cmake")
  set_tests_properties(metis.integration PROPERTIES TIMEOUT 900)
  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties(metis.integration PROPERTIES
      SKIP_REGULAR_EXPRESSION
        "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  endif()

  # Dependency-provider coverage remains separate from the package workflow:
  # it removes the bundled source before exercising offline parent overrides.
  add_test(NAME metis.dependencies
    COMMAND "${CMAKE_COMMAND}"
      "-DROOT=${PROJECT_SOURCE_DIR}"
      "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}/dependencies"
      "-DCONFIG=$<CONFIG>"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/dependencies.cmake")
  set_tests_properties(metis.dependencies PROPERTIES
    PROCESSORS 2
    RUN_SERIAL TRUE
    TIMEOUT 600)
  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties(metis.dependencies PROPERTIES
      SKIP_REGULAR_EXPRESSION
        "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  endif()
endif()

# The relocated shared package must remain self-contained for C and C++ users.
if(METIS_BUILD_INTEGRATION_TESTING)
  add_test(NAME metis.selfcontained-package
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/fresh/selfcontained-package"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CONFIG=$<CONFIG>"
      "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/package/verify.cmake")
  set_tests_properties(metis.selfcontained-package PROPERTIES TIMEOUT 300)
  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties(metis.selfcontained-package PROPERTIES
      SKIP_REGULAR_EXPRESSION
        "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  endif()
endif()

# A no-op reconfigure must preserve generated headers and all dependent build
# outputs, while a real symbol-list change must rebuild the exact dependency.
if(METIS_BUILD_INTEGRATION_TESTING AND WIN32)
  add_test(NAME metis.incremental-exports
    COMMAND "${CMAKE_COMMAND}"
      "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/incremental"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/incremental-exports"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DMETIS_TEST_SUPPORT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/TestSupport.cmake"
      "-DCONFIG=$<CONFIG>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/incremental/verify.cmake")
  set_tests_properties(metis.incremental-exports PROPERTIES TIMEOUT 300)
endif()

# Probe and shell suites are slower configure-time integration fixtures.
if(METIS_BUILD_INTEGRATION_TESTING)
  add_subdirectory(probes)
  add_subdirectory(perf)
endif()

# Exercise IPO policy and cold/hot capability caches in an isolated build tree.
if(METIS_BUILD_INTEGRATION_TESTING)
  add_test(NAME metis.ipo-policy-cache
    COMMAND "${CMAKE_COMMAND}"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/ipo-policy-cache"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CONFIG=$<CONFIG>"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/ipo/verify.cmake")
  set_tests_properties(metis.ipo-policy-cache PROPERTIES TIMEOUT 300)
endif()

# Check the CRT requirements of shared GKlib and shared METIS applications.
if(METIS_BUILD_INTEGRATION_TESTING AND WIN32 AND
    (MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"))
  add_test(NAME metis.shared-crt-policy
    COMMAND "${CMAKE_COMMAND}"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/shared-crt-policy"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      "-DMODULE_FILE=${PROJECT_SOURCE_DIR}/cmake/MetisCRT.cmake"
      -DCHECK_FUNCTION=metis_check_crt
      -DFIXTURE_PROJECT=METIS
      -P "${CMAKE_CURRENT_SOURCE_DIR}/crt/verify.cmake")
  set_tests_properties(metis.shared-crt-policy PROPERTIES TIMEOUT 300)
endif()


# Maintenance regressions exercise merge behavior, runtime SDK discovery, and
# bounded recovery when Windows temporarily locks a runtime copy destination.
# Repository prose and formatting are reviewed separately from CTest.
if(METIS_BUILD_DEVELOPER_TESTING)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

  add_test(NAME metis.upstream-tool
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/upstream" -p "test*.py")
  add_test(NAME metis.intel-runtime
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/intelruntime" -p "test*.py")
  add_test(NAME metis.copy-runtime
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/copyruntime" -p "test*.py")
  set_tests_properties(metis.upstream-tool metis.intel-runtime metis.copy-runtime
    PROPERTIES
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;CMAKE_COMMAND=${CMAKE_COMMAND};COPY_RUNTIME_MODULE=${PROJECT_SOURCE_DIR}/cmake/CopyRuntime.cmake"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}" TIMEOUT 120)
endif()
