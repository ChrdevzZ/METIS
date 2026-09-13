cmake_minimum_required(VERSION 3.24)

file(MAKE_DIRECTORY "${WORK_DIR}")

function(run_stage name)
  cmake_parse_arguments(PARSE_ARGV 1 stage "" "" "")
  execute_process(COMMAND ${stage_UNPARSED_ARGUMENTS} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  file(WRITE "${WORK_DIR}/${name}.log" "${output}\n${error}")
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${name} failed (${result}):\n${output}\n${error}")
  endif()
endfunction()

set(generator_args -G "${GENERATOR}")
if(PLATFORM)
  list(APPEND generator_args -A "${PLATFORM}")
endif()
if(TOOLSET)
  list(APPEND generator_args -T "${TOOLSET}")
endif()
if(INSTANCE)
  list(APPEND generator_args "-DCMAKE_GENERATOR_INSTANCE=${INSTANCE}")
endif()

run_stage(configure "${CMAKE_COMMAND}" -S "${FIXTURE_SOURCE}"
  -B "${WORK_DIR}/build" ${generator_args} -C "${INITIAL_CACHE}"
  "-DCMAKE_BUILD_TYPE=${CONFIG}"
  "-DMETIS_SOURCE_DIR=${PROJECT_SOURCE}"
  "-DGKLIB_SOURCE_DIR=${DEPENDENCY_SOURCE}")

# Repeated test runs must exercise a real final link without rebuilding the
# embedded libraries. Only the binary-tree consumer source is made newer.
file(TOUCH_NOCREATE "${WORK_DIR}/build/main.cpp")
run_stage(build "${CMAKE_COMMAND}" --build "${WORK_DIR}/build"
  --config "${CONFIG}" --parallel 2 --verbose)

# The outer C++ language is enabled after the library subdirectory returns.
# Its real final-link command must still receive the LLD ASan workaround.
file(READ "${WORK_DIR}/build.log" link_log)
string(TOLOWER "${link_log}" link_log)
if(NOT link_log MATCHES "lld-link\\.exe" OR
    NOT link_log MATCHES "nolldtailmerge")
  message(FATAL_ERROR
    "The late-enabled C++ final link did not receive the LLD ASan workaround")
endif()

if(CROSSCOMPILING AND NOT EMULATOR)
  message("ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
else()
  if(WIN32 AND NOT CROSSCOMPILING)
    set(ENV{PATH} "$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
  endif()
  run_stage(run "${CMAKE_CTEST_COMMAND}" --test-dir "${WORK_DIR}/build"
    -C "${CONFIG}" --output-on-failure)
endif()
