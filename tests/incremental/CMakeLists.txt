cmake_minimum_required(VERSION 3.24)

project(METISIncrementalExports LANGUAGES C)

if(NOT DEFINED METIS_TEST_SUPPORT_FILE OR
    NOT EXISTS "${METIS_TEST_SUPPORT_FILE}")
  message(FATAL_ERROR "METIS_TEST_SUPPORT_FILE must name the test helper")
endif()
include("${METIS_TEST_SUPPORT_FILE}")

metis_test_export_header(
  OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/export_symbols.h"
  SYMBOLS "${CMAKE_CURRENT_SOURCE_DIR}/export-symbols.txt"
  SOURCE exports.c)

add_executable(export_header_consumer exports.c)
target_include_directories(export_header_consumer
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Stable, generator-independent paths let the driver observe compilation and
# linking without parsing build-tool diagnostics.
set_target_properties(export_header_consumer PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin/$<CONFIG>")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/target-$<CONFIG>.txt"
  CONTENT "$<TARGET_FILE:export_header_consumer>")
