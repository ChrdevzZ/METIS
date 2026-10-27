cmake_minimum_required(VERSION 3.24)

if(NOT CASE MATCHES "^(constructor-failure|stat-failure|frame-setup-cleanup|posix-query-failure|release-failure|cli-linked-frame-failure|cache-frame-cleanup)$")
  message(FATAL_ERROR "Unknown fault producer case: ${CASE}")
endif()

function(run_checked)
  file(MAKE_DIRECTORY "${WORK_DIR}")
  string(RANDOM LENGTH 16 ALPHABET 0123456789abcdef id)
  set(log "${WORK_DIR}/fault-command-${id}")
  string(TIMESTAMP start "%Y-%m-%dT%H:%M:%SZ" UTC)
  execute_process(COMMAND ${ARGV}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  string(TIMESTAMP end "%Y-%m-%dT%H:%M:%SZ" UTC)
  file(WRITE "${log}.stdout.log" "${output}")
  file(WRITE "${log}.stderr.log" "${error}")
  file(SHA256 "${log}.stdout.log" stdout_hash)
  file(SHA256 "${log}.stderr.log" stderr_hash)
  set(args "")
  set(separator "")
  math(EXPR last "${ARGC} - 1")
  foreach(index RANGE ${last})
    set(value "${ARGV${index}}")
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    string(APPEND args "${separator}\"${value}\"")
    set(separator ",")
  endforeach()
  string(REPLACE "\\" "\\\\" cwd "${CMAKE_CURRENT_BINARY_DIR}")
  file(WRITE "${log}.json"
    "{\"argv\":[${args}],\"cwd\":\"${cwd}\",\"start_utc\":\"${start}\",\"end_utc\":\"${end}\",\"exit\":\"${result}\",\"stdout_sha256\":\"${stdout_hash}\",\"stderr_sha256\":\"${stderr_hash}\",\"environment\":\"inherited unchanged from recorded parent CTest\"}\n")
  if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Fault producer failed (${result})\n${output}\n${error}")
  endif()
endfunction()

set(arguments -S "${SOURCE_DIR}" -B "${WORK_DIR}" -G "${GENERATOR}"
  -C "${TEST_INITIAL_CACHE}")
# Toolchain/ABI inputs load first, literal project policy second, and the
# producer-specific -D overrides below keep IPO and fixture ownership local.
if(NOT TEST_OPTIONS_CACHE OR NOT EXISTS "${TEST_OPTIONS_CACHE}")
  message(FATAL_ERROR "Fault producer needs its generated project-policy cache")
endif()
list(APPEND arguments -C "${TEST_OPTIONS_CACHE}")
foreach(field INSTANCE PLATFORM TOOLSET)
  if(NOT "${GENERATOR_${field}}" STREQUAL "")
    list(APPEND arguments "-DCMAKE_GENERATOR_${field}=${GENERATOR_${field}}")
  endif()
endforeach()
if(GKLIB_PACKAGE_DIR)
  list(APPEND arguments -DMETIS_GKLIB_PROVIDER=SYSTEM
    "-DGKlib_DIR=${GKLIB_PACKAGE_DIR}")
else()
  list(APPEND arguments -DMETIS_GKLIB_PROVIDER=SOURCE
    "-DMETIS_GKLIB_SOURCE_DIR=${GKLIB_SOURCE_DIR}")
endif()
list(APPEND arguments
  "-DMETIS_IDXTYPEWIDTH=${IDXTYPEWIDTH}"
  "-DMETIS_REALTYPEWIDTH=${REALTYPEWIDTH}"
  -DMETIS_BUILD_SHARED_LIBS=OFF -DGKLIB_BUILD_SHARED_LIBS=OFF
  -DMETIS_IPO=OFF -DGKLIB_IPO=OFF -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
  -DMETIS_BUILD_TESTING=ON -DMETIS_BUILD_PROGRAMS=OFF -DMETIS_INSTALL=OFF
  -DMETIS_BUILD_INTEGRATION_TESTING=OFF -DMETIS_BUILD_DEVELOPER_TESTING=OFF
  -DGKLIB_BUILD_TESTING=OFF -DGKLIB_BUILD_PROGRAMS=OFF -DGKLIB_INSTALL=OFF)
run_checked("${CMAKE_COMMAND}" ${arguments})
if(TEST_CONFIGURE_ONLY)
  message(STATUS "FAULT_CONFIGURE_ONLY: policy transport; injection not executed")
  return()
endif()
if(GKLIB_PACKAGE_DIR)
  file(READ "${WORK_DIR}/tests/fault-gklib-${TEST_CONFIG}.txt" archive)
  file(SHA256 "${archive}" actual_hash)
  file(SHA256 "${GKLIB_EXPECTED_ARCHIVE}" expected_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(FATAL_ERROR "Fault producer resolved a different GKlib archive")
  endif()
endif()
set(target metis_constructor_failure)
if(CASE STREQUAL "stat-failure")
  set(target metis_stat_failure)
endif()
run_checked("${CMAKE_COMMAND}" --build "${WORK_DIR}" --config "${TEST_CONFIG}"
  --target "${target}" --parallel 2)

if(TEST_CROSSCOMPILING AND NOT TEST_EMULATOR)
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  return()
endif()

set(pattern "^metis\\.${CASE}$")
set(expected_count 1)
if(CASE STREQUAL "release-failure")
  set(pattern "^metis\\.release-failure\\.[01]$")
  set(expected_count 2)
endif()
set(ctest_arguments --test-dir "${WORK_DIR}" -C "${TEST_CONFIG}"
  -R "${pattern}" --output-on-failure
  --output-junit "${WORK_DIR}/${CASE}.xml")
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" ${ctest_arguments}
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
message("${output}${error}")
if(NOT "${result}" STREQUAL "0")
  message(FATAL_ERROR "Non-IPO fault fixture failed (${result})")
endif()
file(READ "${WORK_DIR}/${CASE}.xml" results)
string(REGEX MATCHALL "<testcase[ \t\r\n>]" cases "${results}")
list(LENGTH cases count)
if(NOT count EQUAL expected_count OR results MATCHES "<skipped")
  message(FATAL_ERROR "Fault fixture did not execute the expected number of tests")
endif()
