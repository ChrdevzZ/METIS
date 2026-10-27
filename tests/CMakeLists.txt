include("${CMAKE_CURRENT_SOURCE_DIR}/TestSupport.cmake")
set(_metis_test_initial_cache
  "${CMAKE_CURRENT_BINARY_DIR}/test-initial-cache.cmake")
metis_test_write_initial_cache("${_metis_test_initial_cache}")
include("${CMAKE_CURRENT_SOURCE_DIR}/FaultOptions.cmake")
set(_metis_test_fault_options "${CMAKE_CURRENT_BINARY_DIR}/fault-options.cmake")
metis_test_write_fault_options("${_metis_test_fault_options}")

add_executable(metis_api_test api.c)
target_link_libraries(metis_api_test PRIVATE METIS::metis)
if(metis_gklib_package_dir)
  # Package selection records complement the consumer's actual link evidence.
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/api-gklib-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:GKlib::GKlib>")
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/api-gklib-link-$<CONFIG>.txt"
    CONTENT "$<TARGET_LINKER_FILE:GKlib::GKlib>")
endif()
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
target_link_libraries(metis_coarsen_numeric PRIVATE ${metis_private_library}
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
target_link_libraries(metis_ondisk_file_security PRIVATE ${metis_private_library}
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.ondisk-file-security
  TARGET metis_ondisk_file_security)
set_tests_properties(metis.ondisk-file-security PROPERTIES TIMEOUT 30)

# Compile against the private workspace interface to verify that rejected
# growth leaves pool positions, capacities, and tracked pointers unchanged.
add_executable(metis_workspace_failure workspace_failure.c)
target_include_directories(metis_workspace_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_workspace_failure PRIVATE ${metis_private_library}
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.workspace-failure
  TARGET metis_workspace_failure)
set_tests_properties(metis.workspace-failure PROPERTIES TIMEOUT 30)

# Wrapping crosses object-file references in static libraries, not calls bound
# inside a shared library. Probe the selected final linker independently of
# compiler brand; the fixtures also check that injection reaches real calls.
get_target_property(_metis_test_gklib_type GKlib::GKlib TYPE)
set(_metis_test_link_wrap FALSE)
if(NOT METIS_BUILD_SHARED_LIBS AND
    _metis_test_gklib_type STREQUAL "STATIC_LIBRARY")
  metis_test_check_link_wrap(_metis_test_link_wrap)
endif()
if(_metis_test_link_wrap)
  file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/fault-gklib-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:GKlib::GKlib>")
endif()

# IPO may bind calls before symbol wrapping. Test the same sources in a
# separate non-IPO producer while retaining the production target policy.
set(_metis_test_ipo FALSE)
foreach(_target IN ITEMS metis GKlib::GKlib)
  foreach(_config IN ITEMS "" ${CMAKE_BUILD_TYPE} ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER "${_config}" _upper)
    set(_property INTERPROCEDURAL_OPTIMIZATION)
    if(NOT _config STREQUAL "")
      string(APPEND _property "_${_upper}")
    endif()
    get_target_property(_enabled ${_target} ${_property})
    if(_enabled)
      set(_metis_test_ipo TRUE)
    endif()
  endforeach()
endforeach()
get_target_property(_metis_test_gklib_imported GKlib::GKlib IMPORTED)
if(_metis_test_link_wrap AND _metis_test_ipo AND
    (NOT _metis_test_gklib_imported OR metis_gklib_package_dir))
  set(_gklib_source "")
  set(_gklib_package "")
  if(_metis_test_gklib_imported)
    set(_gklib_package "${metis_gklib_package_dir}")
    message(STATUS "METIS wrapping tests rebuild METIS without IPO; the installed GKlib archive is unchanged")
  else()
    get_target_property(_gklib_source GKlib::GKlib SOURCE_DIR)
    get_filename_component(_gklib_source "${_gklib_source}" DIRECTORY)
    message(STATUS "METIS wrapping tests use a separate non-IPO source producer")
  endif()
  set(_metis_fault_cases constructor-failure stat-failure frame-setup-cleanup
    release-failure cli-linked-frame-failure cache-frame-cleanup)
  if(NOT WIN32)
    list(APPEND _metis_fault_cases posix-query-failure)
  endif()
  foreach(_case IN LISTS _metis_fault_cases)
    add_test(NAME metis.${_case}
      COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DGKLIB_SOURCE_DIR=${_gklib_source}"
        "-DGKLIB_PACKAGE_DIR=${_gklib_package}"
        "-DGKLIB_EXPECTED_ARCHIVE=$<TARGET_FILE:GKlib::GKlib>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fault-producer/$<CONFIG>"
        "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
        "-DTEST_OPTIONS_CACHE=${_metis_test_fault_options}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
        "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DTEST_CONFIG=$<CONFIG>" "-DCASE=${_case}"
        "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
        "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        "-DIDXTYPEWIDTH=${METIS_IDXTYPEWIDTH}"
        "-DREALTYPEWIDTH=${METIS_REALTYPEWIDTH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fault_producer.cmake")
    set_tests_properties(metis.${_case} PROPERTIES
      TIMEOUT 300 RESOURCE_LOCK metis_fault_producer)
    if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
      set_tests_properties(metis.${_case} PROPERTIES
        SKIP_REGULAR_EXPRESSION "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
    endif()
  endforeach()
elseif(_metis_test_link_wrap)
  if(_metis_test_ipo)
    message(STATUS "METIS wrapping tests cannot recreate this imported GKlib target; direct injection assertions remain active")
  endif()
  add_executable(metis_constructor_failure constructor_failure.c
    "${PROJECT_SOURCE_DIR}/apps/io.c")
  target_include_directories(metis_constructor_failure PRIVATE
    "${PROJECT_SOURCE_DIR}/src" "${PROJECT_SOURCE_DIR}/apps")
  target_link_libraries(metis_constructor_failure PRIVATE METIS::metis
    GKlib::GKlib)
  target_link_options(metis_constructor_failure PRIVATE
    "LINKER:--wrap=gk_malloc"
    "LINKER:--wrap=gk_malloc_nosignal"
    "LINKER:--wrap=gk_mcoreMalloc"
    "LINKER:--wrap=gk_mcoreCreate"
    "LINKER:--wrap=gk_mcorePop"
    "LINKER:--wrap=METIS_PartGraphRecursive"
    "LINKER:--wrap=gk_sigtrap"
    "LINKER:--wrap=gk_siguntrap"
    "LINKER:--wrap=malloc"
    "LINKER:--wrap=calloc"
    "LINKER:--wrap=free")
  if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_compile_definitions(metis_constructor_failure PRIVATE
      METIS_TEST_POSIX_QUERY _POSIX_C_SOURCE=200809L)
    target_link_libraries(metis_constructor_failure PRIVATE Threads::Threads)
    target_link_options(metis_constructor_failure PRIVATE
      "LINKER:--wrap=pthread_sigmask")
  endif()
  metis_test_add_runtime(NAME metis.constructor-failure
    TARGET metis_constructor_failure)
  set_tests_properties(metis.constructor-failure PROPERTIES TIMEOUT 30)
  metis_test_add_runtime(NAME metis.frame-setup-cleanup
    TARGET metis_constructor_failure ARGS frame-cleanup)
  set_tests_properties(metis.frame-setup-cleanup PROPERTIES TIMEOUT 30)
  metis_test_add_runtime(NAME metis.cli-linked-frame-failure
    TARGET metis_constructor_failure ARGS cli-frame-failure)
  set_tests_properties(metis.cli-linked-frame-failure PROPERTIES TIMEOUT 30)
  metis_test_add_runtime(NAME metis.cache-frame-cleanup
    TARGET metis_constructor_failure ARGS cache-frame-cleanup)
  set_tests_properties(metis.cache-frame-cleanup PROPERTIES TIMEOUT 30)
  foreach(_mode IN ITEMS 0 1)
    metis_test_add_runtime(NAME metis.release-failure.${_mode}
      TARGET metis_constructor_failure ARGS release-failure ${_mode}
      EXPECT_FAILURE REQUIRE_FAILURE_HIT)
    set_tests_properties(metis.release-failure.${_mode} PROPERTIES TIMEOUT 30)
  endforeach()
  if(NOT WIN32)
    metis_test_add_runtime(NAME metis.posix-query-failure
      TARGET metis_constructor_failure ARGS posix-query)
    set_tests_properties(metis.posix-query-failure PROPERTIES TIMEOUT 30)
  endif()
  metis_target_sanitizers(metis_constructor_failure)
  metis_copy_runtime(metis_constructor_failure)

  add_executable(metis_stat_failure stat_failure.c
    "${PROJECT_SOURCE_DIR}/apps/stat.c")
  target_include_directories(metis_stat_failure PRIVATE
    "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(metis_stat_failure PRIVATE METIS::metis
    GKlib::GKlib)
  target_link_options(metis_stat_failure PRIVATE
    "LINKER:--wrap=gk_malloc" "LINKER:--wrap=free")
  metis_test_add_runtime(NAME metis.stat-failure TARGET metis_stat_failure)
  set_tests_properties(metis.stat-failure PROPERTIES TIMEOUT 30)
  metis_target_sanitizers(metis_stat_failure)
  metis_copy_runtime(metis_stat_failure)
else()
  message(STATUS "METIS allocator wrapping tests unavailable: require static METIS/GKlib and a verified wrapping linker")
endif()

add_executable(metis_smbfactor_failure smbfactor_failure.c)
target_include_directories(metis_smbfactor_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_smbfactor_failure PRIVATE ${metis_private_library}
  GKlib::GKlib)
metis_test_add_runtime(NAME metis.smbfactor-failure
  TARGET metis_smbfactor_failure)
set_tests_properties(metis.smbfactor-failure PROPERTIES TIMEOUT 30)

# Existing stream and symbolic-factorization fixtures inject allocation errors;
# this fixture targets failed frame establishment in all private CLI owners.
set_property(SOURCE cli_frame_failure.c PROPERTY OBJECT_DEPENDS
  "${PROJECT_SOURCE_DIR}/apps/io.c"
  "${PROJECT_SOURCE_DIR}/apps/smbfactor.c"
  "${PROJECT_SOURCE_DIR}/apps/cmpfillin.c")
foreach(_owner IN ITEMS io factor fillin)
  set(_target "metis_cli_frame_${_owner}_failure")
  add_executable(${_target} cli_frame_failure.c)
  target_include_directories(${_target} PRIVATE
    "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
  target_link_libraries(${_target} PRIVATE ${metis_private_library} GKlib::GKlib)
  set_target_properties(${_target} PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
  string(TOUPPER "${_owner}" _upper)
  target_compile_definitions(${_target} PRIVATE "METIS_CLI_FRAME_${_upper}")
  if(_owner STREQUAL "fillin")
    target_sources(${_target} PRIVATE
      "${PROJECT_SOURCE_DIR}/apps/io.c" "${PROJECT_SOURCE_DIR}/apps/smbfactor.c")
  endif()
  if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  metis_test_add_runtime(NAME "metis.cli-frame-failure.${_owner}"
    TARGET ${_target} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
  set_tests_properties("metis.cli-frame-failure.${_owner}" PROPERTIES TIMEOUT 30)
  metis_target_sanitizers(${_target})
  metis_copy_runtime(${_target})
endforeach()

# Compile the application I/O unit with private failpoints for stream,
# allocation, close, commit and rollback errors.
add_executable(metis_output_pair_failure output_pair_failure.c
  "${PROJECT_SOURCE_DIR}/apps/io.c")
target_compile_definitions(metis_output_pair_failure PRIVATE
  METIS_IO_TRANSACTION_TEST)
target_include_directories(metis_output_pair_failure PRIVATE
  "${PROJECT_SOURCE_DIR}/apps" "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(metis_output_pair_failure PRIVATE ${metis_private_library}
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
# Instrument owned fixture TUs without applying the full production policy.
foreach(test IN ITEMS
    metis_api_test
    metis_api_invalid_dimensions
    metis_coarsen_numeric
    metis_numbering_failure
    metis_ondisk_file_security
    metis_workspace_failure
    metis_smbfactor_failure
    metis_output_pair_failure
    metis_fortran_symbols)
  metis_target_sanitizers(${test})
endforeach()
metis_copy_runtime(metis_api_test)
metis_copy_runtime(metis_api_invalid_dimensions)
metis_copy_runtime(metis_coarsen_numeric)
metis_copy_runtime(metis_numbering_failure)
metis_copy_runtime(metis_ondisk_file_security)
metis_copy_runtime(metis_workspace_failure)
metis_copy_runtime(metis_smbfactor_failure)
metis_copy_runtime(metis_output_pair_failure)
metis_copy_runtime(metis_fortran_symbols)

# A standalone positive control verifies compilation, final link and deployment
# without a METIS/GKlib dependency masking the fixture's diagnostic closure.
if("address" IN_LIST METIS_SANITIZERS)
  add_executable(metis_asan_fixture_positive sanitizers/fixture_positive.c)
  metis_target_sanitizers(metis_asan_fixture_positive)
  metis_copy_runtime(metis_asan_fixture_positive)
  add_test(NAME metis.asan-fixture-positive
    COMMAND "${CMAKE_COMMAND}"
      "-DPROGRAM=$<TARGET_FILE:metis_asan_fixture_positive>"
      "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/sanitizers/fixture_positive.cmake")
  set_tests_properties(metis.asan-fixture-positive PROPERTIES TIMEOUT 30
    SKIP_REGULAR_EXPRESSION "ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
endif()

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

  metis_target_sanitizers(metis_export_test)
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
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_CONFIG=$<CONFIG>"
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

  set(_fault_source "")
  if(NOT _metis_test_gklib_imported)
    get_target_property(_fault_source GKlib::GKlib SOURCE_DIR)
    get_filename_component(_fault_source "${_fault_source}" DIRECTORY)
  endif()
  set(_fault_cases literal)
  if(EXISTS "${_fault_source}/CMakeLists.txt" OR metis_gklib_package_dir)
    list(APPEND _fault_cases address multiple empty)
  endif()
  foreach(_case IN LISTS _fault_cases)
    add_test(NAME "metis.fault-cache.${_case}"
      COMMAND "${CMAKE_COMMAND}"
        "-DMETIS_SOURCE=${PROJECT_SOURCE_DIR}"
        "-DGKLIB_SOURCE_DIR=${_fault_source}"
        "-DGKLIB_PACKAGE_DIR=${metis_gklib_package_dir}"
        "-DGKLIB_EXPECTED_ARCHIVE=$<TARGET_FILE:GKlib::GKlib>"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/fault-cache/${_case}"
        "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
        "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DTEST_CONFIG=$<CONFIG>" "-DCASE=${_case}"
        "-DTEST_MSVC_ABI=${MSVC}"
        "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
        "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        "-DIDXTYPEWIDTH=${METIS_IDXTYPEWIDTH}"
        "-DREALTYPEWIDTH=${METIS_REALTYPEWIDTH}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/fault-cache/verify.cmake")
    set_tests_properties("metis.fault-cache.${_case}" PROPERTIES
      TIMEOUT 240 SKIP_REGULAR_EXPRESSION "METIS_RUNTIME_SKIPPED")
  endforeach()

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
