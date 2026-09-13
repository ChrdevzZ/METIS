cmake_minimum_required(VERSION 3.24)

# The fixture owns only a binary-tree install prefix and nested consumer build.
file(MAKE_DIRECTORY "${WORK_DIR}")
set(prefix "${WORK_DIR}/prefix")
function(run_stage name)
  cmake_parse_arguments(PARSE_ARGV 1 stage "" "" "")
  execute_process(COMMAND ${stage_UNPARSED_ARGUMENTS} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  file(WRITE "${WORK_DIR}/${name}.log" "${output}\n${error}")
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR "${name} failed (${result}):\n${output}\n${error}")
  endif()
endfunction()

run_stage(install "${CMAKE_COMMAND}" --install "${PROJECT_BINARY}"
  --config "${CONFIG}" --prefix "${prefix}")
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
# Completed shared libraries require deployment of their external runtime DLLs,
# but not their development SDKs. Use the producer's known runtime set in this
# fixture; static consumers must discover link inputs through exported targets.
set(runtime_dlls "")
if(DEPLOY_RUNTIME)
  foreach(path IN LISTS RUNTIME_DLLS)
    list(APPEND runtime_dlls "${path}")
  endforeach()
  list(REMOVE_DUPLICATES runtime_dlls)
endif()
if(LINKER_TYPE)
  run_stage(configure "${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE}"
    -B "${WORK_DIR}/consumer" ${generator_args} -C "${INITIAL_CACHE}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DEXTERNAL_RUNTIME_DLLS=${runtime_dlls}"
    "-DCONSUMER_LINKER_TYPE=${LINKER_TYPE}"
    "-DCONSUMER_LINKER_COMMAND=${LINKER_COMMAND}")
else()
  run_stage(configure "${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE}"
    -B "${WORK_DIR}/consumer" ${generator_args} -C "${INITIAL_CACHE}"
    "-DCMAKE_BUILD_TYPE=${CONFIG}" "-DCMAKE_PREFIX_PATH=${prefix}"
    "-DEXTERNAL_RUNTIME_DLLS=${runtime_dlls}")
endif()
set(build_command "${CMAKE_COMMAND}" --build "${WORK_DIR}/consumer"
  --config "${CONFIG}" --parallel 2)
if(LINKER_TYPE)
  # These cases inspect the final link command. Rebuild the small consumer so
  # repeated CTest runs cannot pass or fail based on stale build output.
  list(APPEND build_command --clean-first --verbose)
endif()
run_stage(build ${build_command})

# A real link command guards both sides of the target-level linker contract.
# link.exe must never receive LLD-only options, while a custom lld-link mapping
# must retain the option that prevents incompatible ASan tail merging.
if(LINKER_TYPE)
  file(READ "${WORK_DIR}/build.log" link_log)
  string(TOLOWER "${link_log}" link_log)
  if(LINKER_TYPE STREQUAL "MSVC")
    string(REPLACE "lld-link.exe" "" msvc_log "${link_log}")
    if(NOT msvc_log MATCHES "link\\.exe" OR
        link_log MATCHES "nolldtailmerge")
      message(FATAL_ERROR
        "The MSVC linker regression did not use clean link.exe commands")
    endif()
  elseif(LINKER_TYPE STREQUAL "asan_lld")
    if(NOT link_log MATCHES "lld-link\\.exe" OR
        NOT link_log MATCHES "nolldtailmerge")
      message(FATAL_ERROR
        "The custom LLD linker regression did not use its ASan link options")
    endif()
  endif()
endif()

if(CROSSCOMPILING AND NOT EMULATOR)
  message("ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
else()
  # Do not borrow another compiler's runtime from the initialized SDK PATH.
  # Cross emulators retain their explicitly configured execution environment.
  if(WIN32 AND NOT CROSSCOMPILING)
    set(ENV{PATH} "$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
  endif()
  run_stage(run "${CMAKE_CTEST_COMMAND}" --test-dir "${WORK_DIR}/consumer"
    -C "${CONFIG}" --output-on-failure)
endif()
