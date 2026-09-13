# Keep shell-script integration optional but fail clearly when requested.
find_program(METIS_BASH_EXECUTABLE NAMES bash)
if(NOT METIS_BASH_EXECUTABLE OR
    NOT EXISTS "${METIS_BASH_EXECUTABLE}")
  message(FATAL_ERROR
    "Performance integration tests require bash; set METIS_BASH_EXECUTABLE to its executable path")
endif()

# The runner creates an ownership marker before reusing this binary directory.
add_test(NAME metis.perf.scripts
  COMMAND "${METIS_BASH_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/run-tests.sh"
    "${PROJECT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/workspace")
set_tests_properties(metis.perf.scripts PROPERTIES TIMEOUT 120)
