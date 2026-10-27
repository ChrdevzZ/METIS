cmake_minimum_required(VERSION 3.24)
project(FaultPolicyParent NONE)
enable_testing()
include("${METIS_SOURCE}/tests/FaultOptions.cmake")

set(METIS_IPO ON CACHE STRING "Parent policy control" FORCE)
set(GKLIB_IPO ON CACHE STRING "Parent policy control" FORCE)
metis_test_write_fault_options("${CMAKE_CURRENT_BINARY_DIR}/project-policy.cmake")
configure_file("${TEST_INITIAL_CACHE}" "${CMAKE_CURRENT_BINARY_DIR}/toolchain.cmake" COPYONLY)
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/toolchain.cmake"
  "\nset(METIS_SANITIZERS stale CACHE STRING \"override control\" FORCE)\nset(GKLIB_SANITIZERS stale CACHE STRING \"override control\" FORCE)\nset(METIS_IPO ON CACHE STRING \"override control\" FORCE)\nset(GKLIB_IPO ON CACHE STRING \"override control\" FORCE)\n")

add_test(NAME fault.policy-configuration
  COMMAND "${CMAKE_COMMAND}"
    "-DSOURCE_DIR=${CHILD_SOURCE}"
    "-DGKLIB_SOURCE_DIR=${GKLIB_SOURCE_DIR}"
    "-DGKLIB_PACKAGE_DIR=${GKLIB_PACKAGE_DIR}"
    "-DGKLIB_EXPECTED_ARCHIVE=${GKLIB_EXPECTED_ARCHIVE}"
    "-DWORK_DIR=${CHILD_BINARY}"
    "-DTEST_INITIAL_CACHE=${CMAKE_CURRENT_BINARY_DIR}/toolchain.cmake"
    "-DTEST_OPTIONS_CACHE=${CMAKE_CURRENT_BINARY_DIR}/project-policy.cmake"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
    "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DTEST_CONFIG=${TEST_CONFIG}" -DCASE=constructor-failure
    "-DIDXTYPEWIDTH=${IDXTYPEWIDTH}" "-DREALTYPEWIDTH=${REALTYPEWIDTH}"
    -DTEST_CONFIGURE_ONLY=ON
    -P "${METIS_SOURCE}/tests/fault_producer.cmake")
set_tests_properties(fault.policy-configuration PROPERTIES TIMEOUT 120)
