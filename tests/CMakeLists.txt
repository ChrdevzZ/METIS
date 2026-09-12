add_executable(metis_api_test api.c)
target_link_libraries(metis_api_test PRIVATE METIS::metis)
add_test(NAME metis.api COMMAND metis_api_test)
set_tests_properties(metis.api PROPERTIES TIMEOUT 30)

add_executable(metis_fortran_symbols fortran_symbols.c)
target_link_libraries(metis_fortran_symbols PRIVATE METIS::metis)
add_test(NAME metis.fortran-symbols COMMAND metis_fortran_symbols)
set_tests_properties(metis.fortran-symbols PROPERTIES TIMEOUT 30)
metis_copy_runtime(metis_api_test)
metis_copy_runtime(metis_fortran_symbols)

# On Windows, verify the shared library exports the compatibility symbol list.
if(WIN32 AND METIS_BUILD_SHARED_LIBS)
  # Keep the compatibility oracle independent of current declarations.
  file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/export-symbols.txt" exported_symbols)
  set(symbol_entries "")
  foreach(symbol IN LISTS exported_symbols)
    string(APPEND symbol_entries "  \"${symbol}\",\n")
  endforeach()
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/export_symbols.h"
    "static const char *symbols[] = {\n${symbol_entries}};\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/export-symbols.txt")
  add_executable(metis_export_test exports.c)
  target_include_directories(metis_export_test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(metis_export_test PRIVATE METIS::metis)
  metis_copy_runtime(metis_export_test)
  add_test(NAME metis.exports COMMAND metis_export_test $<TARGET_FILE:metis>)
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
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake)
    set_tests_properties(metis.${case} PROPERTIES TIMEOUT 30)
  endforeach()
endif()

# Install ownership covers static and shared dependency layouts separately.
if(METIS_BUILD_INTEGRATION_TESTING)
  add_test(NAME metis.install-ownership
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}" "-DTEST_PROJECT=METIS"
      "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/install-ownership"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
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
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/install/verify.cmake")
  set_tests_properties(metis.install-ownership-shared PROPERTIES TIMEOUT 300)
endif()

# The relocated shared package must remain self-contained for C and C++ users.
if(METIS_BUILD_INTEGRATION_TESTING)
  add_test(NAME metis.selfcontained-package
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
      "-DWORK_ROOT=${CMAKE_CURRENT_BINARY_DIR}/fresh/selfcontained-package"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/package/verify.cmake")
  set_tests_properties(metis.selfcontained-package PROPERTIES TIMEOUT 300)
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
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
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
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
      "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}"
      "-DMODULE_FILE=${PROJECT_SOURCE_DIR}/cmake/MetisCRT.cmake"
      -DCHECK_FUNCTION=metis_check_crt
      -DFIXTURE_PROJECT=METIS
      -P "${CMAKE_CURRENT_SOURCE_DIR}/crt/verify.cmake")
  set_tests_properties(metis.shared-crt-policy PROPERTIES TIMEOUT 300)
endif()


# Maintenance regressions exercise merge behavior and runtime SDK discovery.
# Repository prose and formatting are reviewed separately from CTest.
if(METIS_BUILD_DEVELOPER_TESTING)
  find_package(Python3 3.9 REQUIRED COMPONENTS Interpreter)

  add_test(NAME metis.upstream-tool
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/upstream" -p "test*.py")
  add_test(NAME metis.intel-runtime
    COMMAND "${Python3_EXECUTABLE}" -m unittest discover
      -s "${CMAKE_CURRENT_SOURCE_DIR}/intelruntime" -p "test*.py")
  set_tests_properties(metis.upstream-tool metis.intel-runtime PROPERTIES
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1;CMAKE_COMMAND=${CMAKE_COMMAND}"
    WORKING_DIRECTORY "${PROJECT_BINARY_DIR}" TIMEOUT 120)
endif()
