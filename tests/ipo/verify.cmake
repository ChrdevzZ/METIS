if(NOT DEFINED WORK_DIR OR NOT DEFINED GENERATOR)
  message(FATAL_ERROR "The IPO fixture runner requires WORK_DIR and GENERATOR")
endif()

set(MODULE_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/MetisIPO.cmake")
set(TARGET_IPO_FUNCTION metis_target_ipo)
set(CHECK_IPO_FUNCTION metis_check_ipo)
set(IPO_POLICY_VARIABLE METIS_IPO)
set(BUILD_PROGRAMS_VARIABLE METIS_BUILD_PROGRAMS)

cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE WORK_DIR)
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}" fixture_source)
cmake_path(IS_PREFIX WORK_DIR "${fixture_source}" NORMALIZE work_contains_source)
if(work_contains_source)
  message(FATAL_ERROR "The IPO work directory must not contain source files")
endif()
set(work_marker "${WORK_DIR}/.ipo-fixture-owned")
if(EXISTS "${WORK_DIR}" AND NOT EXISTS "${work_marker}")
  message(FATAL_ERROR
    "Refusing to clean an IPO work directory without its ownership marker")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${work_marker}" "Fixture-owned IPO regression directory.\n")

# A cold first configure is part of the contract: its cache key must also be
# reusable by the immediately following hot configure.
file(REMOVE_RECURSE "${WORK_DIR}/policy" "${WORK_DIR}/cache")

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${CMAKE_CURRENT_LIST_DIR}"
  -G "${GENERATOR}")
if(GENERATOR_PLATFORM)
  list(APPEND configure_command -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND configure_command -T "${GENERATOR_TOOLSET}")
endif()
if(CMAKE_MAKE_PROGRAM)
  list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
if(CMAKE_C_COMPILER)
  list(APPEND configure_command "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND configure_command "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
list(APPEND configure_command
  "-DMODULE_FILE=${MODULE_FILE}"
  "-DTARGET_IPO_FUNCTION=${TARGET_IPO_FUNCTION}"
  "-DCHECK_IPO_FUNCTION=${CHECK_IPO_FUNCTION}"
  "-DIPO_POLICY_VARIABLE=${IPO_POLICY_VARIABLE}"
  "-DBUILD_PROGRAMS_VARIABLE=${BUILD_PROGRAMS_VARIABLE}"
  "-DCMAKE_BUILD_TYPE=")

function(run_configure mode build_dir)
  execute_process(
    COMMAND ${configure_command} -B "${build_dir}" "-DIPO_TEST_MODE=${mode}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "IPO ${mode} fixture failed:\n${stdout}\n${stderr}")
  endif()
endfunction()

set(policy_build "${WORK_DIR}/policy")
set(cache_build "${WORK_DIR}/cache")
run_configure(MOCK "${policy_build}")

if(EXISTS "${policy_build}/empty-config-check-active")
  if(NOT EXISTS "${policy_build}/empty-config-result.txt")
    message(FATAL_ERROR "The empty CONFIG generator expression produced no result")
  endif()
  file(READ "${policy_build}/empty-config-result.txt" empty_config_result)
  if(NOT empty_config_result STREQUAL "1")
    message(FATAL_ERROR
      "The empty CONFIG generator expression evaluated to [${empty_config_result}]")
  endif()
endif()

# The second configure must reuse both real-probe signatures and preserve the
# sentinels written into their first diagnostics.
run_configure(REAL "${cache_build}")
run_configure(REAL "${cache_build}")
