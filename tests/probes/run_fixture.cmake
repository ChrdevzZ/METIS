if(NOT DEFINED FIXTURE_SOURCE OR NOT DEFINED WORK_DIR OR
    NOT DEFINED MODULE_FILE OR NOT DEFINED PROBE_FUNCTION OR
    NOT DEFINED TEST_MODE OR NOT DEFINED GENERATOR)
  message(FATAL_ERROR "The probe fixture runner is missing required arguments")
endif()

# Normalize paths before creating any fixture-owned directory.
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE WORK_DIR)
file(REAL_PATH "${FIXTURE_SOURCE}" _probe_source_root)
cmake_path(IS_PREFIX WORK_DIR "${_probe_source_root}" NORMALIZE _contains_source)
if(_contains_source)
  message(FATAL_ERROR "The probe work directory must not contain source files")
endif()

# Reconfigure this scenario's own binary directory without deleting directories.
file(MAKE_DIRECTORY "${WORK_DIR}")
if(NOT DEFINED INHERITED_TARGET_TYPE)
  set(INHERITED_TARGET_TYPE STATIC_LIBRARY)
endif()

# Forward the parent generator and compiler choices into each probe configure.
set(_probe_configure_command
  "${CMAKE_COMMAND}"
  -S "${FIXTURE_SOURCE}"
  -B "${WORK_DIR}"
  -G "${GENERATOR}")
if(TEST_INITIAL_CACHE)
  list(APPEND _probe_configure_command -C "${TEST_INITIAL_CACHE}")
endif()
if(GENERATOR_INSTANCE)
  list(APPEND _probe_configure_command
    "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND _probe_configure_command -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND _probe_configure_command -T "${GENERATOR_TOOLSET}")
endif()
if(MAKE_PROGRAM)
  list(APPEND _probe_configure_command
    "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(C_COMPILER)
  list(APPEND _probe_configure_command
    "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(CXX_COMPILER)
  list(APPEND _probe_configure_command
    "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()
list(APPEND _probe_configure_command
  "-DMODULE_FILE=${MODULE_FILE}"
  "-DPROBE_FUNCTION=${PROBE_FUNCTION}"
  "-DINHERITED_TARGET_TYPE=${INHERITED_TARGET_TYPE}")

# Keep configure output available for a single scenario-specific diagnostic.
function(run_probe_configure mode)
  execute_process(
    COMMAND ${_probe_configure_command} "-DPROBE_MODE=${mode}" ${ARGN}
    RESULT_VARIABLE _probe_result
    OUTPUT_VARIABLE _probe_stdout
    ERROR_VARIABLE _probe_stderr)
  if(NOT _probe_result EQUAL 0)
    message(FATAL_ERROR
      "Probe fixture configuration failed for ${mode}:\n${_probe_stdout}\n${_probe_stderr}")
  endif()
  set(_probe_configure_stdout "${_probe_stdout}" PARENT_SCOPE)
endfunction()

# Reuse the binary directory where the scenario tests cache invalidation.
if(TEST_MODE STREQUAL "state")
  run_probe_configure(valid)
elseif(TEST_MODE STREQUAL "cxx-state")
  list(APPEND _probe_configure_command -DPROBE_LANGUAGE=CXX)
  run_probe_configure(valid)
elseif(TEST_MODE STREQUAL "reconfigure")
  run_probe_configure(valid)
  run_probe_configure(missing-library)
elseif(TEST_MODE STREQUAL "environment")
  run_probe_configure(environment)
elseif(TEST_MODE STREQUAL "inactive-debug-reconfigure")
  if(GENERATOR MATCHES "Multi-Config|Visual Studio|Xcode")
    set(_probe_active_config "${TEST_CONFIG}")
    if(NOT _probe_active_config OR _probe_active_config STREQUAL "Debug")
      set(_probe_active_config Release)
    endif()
  else()
    set(_probe_active_config Release)
  endif()
  run_probe_configure(config-switch
    -DPROBE_ACTIVE_CONFIG=${_probe_active_config}
    -DPROBE_EXPECT_RESULT=TRUE
    -DCMAKE_BUILD_TYPE=${_probe_active_config}
    -DCMAKE_C_FLAGS_DEBUG=-DPROBE_CONFIG_VALID=1)
  run_probe_configure(config-switch
    -DPROBE_ACTIVE_CONFIG=${_probe_active_config}
    -DPROBE_EXPECT_RESULT=TRUE
    -DCMAKE_BUILD_TYPE=${_probe_active_config}
    -DCMAKE_C_FLAGS_DEBUG=-DPROBE_CONFIG_BREAK=1)
  if(_probe_configure_stdout MATCHES "Performing Test")
    message(FATAL_ERROR
      "Changing inactive Debug flags unnecessarily reran the link probe")
  endif()
elseif(TEST_MODE STREQUAL "active-link-flags")
  if(GENERATOR MATCHES "Multi-Config|Visual Studio|Xcode")
    set(_probe_active_config "${TEST_CONFIG}")
    if(NOT _probe_active_config)
      message(FATAL_ERROR
        "A multi-config active-link-flags test requires TEST_CONFIG")
    endif()
  else()
    set(_probe_active_config ProbeConfig)
  endif()
  run_probe_configure(config-link-valid
    -DPROBE_ACTIVE_CONFIG=${_probe_active_config}
    -DCMAKE_BUILD_TYPE=${_probe_active_config})
  run_probe_configure(config-link-missing
    -DPROBE_ACTIVE_CONFIG=${_probe_active_config}
    -DCMAKE_BUILD_TYPE=${_probe_active_config})
elseif(TEST_MODE STREQUAL "custom-linker-cache")
  run_probe_configure(custom-linker-cache)
elseif(TEST_MODE STREQUAL "ignored-option")
  run_probe_configure(ignored-option)
elseif(TEST_MODE STREQUAL "config-reconfigure")
  run_probe_configure(config-switch
    -DPROBE_EXPECT_RESULT=TRUE
    -DCMAKE_BUILD_TYPE=ProbeConfig
    -DCMAKE_TRY_COMPILE_CONFIGURATION=ProbeConfig
    -DCMAKE_C_FLAGS_PROBECONFIG=-DPROBE_CONFIG_VALID=1)
  run_probe_configure(config-switch
    -DPROBE_EXPECT_RESULT=FALSE
    -DCMAKE_BUILD_TYPE=ProbeConfig
    -DCMAKE_TRY_COMPILE_CONFIGURATION=ProbeConfig
    -DCMAKE_C_FLAGS_PROBECONFIG=-DPROBE_CONFIG_BREAK=1)
elseif(TEST_MODE STREQUAL "gprof")
  run_probe_configure(gprof)
  execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}" --parallel 2
    RESULT_VARIABLE _probe_result
    OUTPUT_VARIABLE _probe_stdout
    ERROR_VARIABLE _probe_stderr)
  if(NOT _probe_result EQUAL 0)
    message(FATAL_ERROR
      "The gprof probe passed, but the executable build failed:\n${_probe_stdout}\n${_probe_stderr}")
  endif()
elseif(TEST_MODE STREQUAL "msvc-asan-runtime")
  if(NOT DEFINED C_COMPILER_VERSION)
    message(FATAL_ERROR "The MSVC ASan probe requires C_COMPILER_VERSION")
  endif()
  if(C_COMPILER_VERSION VERSION_GREATER_EQUAL 19.28)
    set(_probe_asan_supported TRUE)
  else()
    set(_probe_asan_supported FALSE)
  endif()
  run_probe_configure(msvc-asan
    -DPROBE_EXPECT_RESULT=${_probe_asan_supported}
    "-DCMAKE_EXE_LINKER_FLAGS=")
  set(_probe_asan_block_flags
    "/NODEFAULTLIB:clang_rt.asan_dynamic-x86_64.lib /NODEFAULTLIB:clang_rt.asan_dynamic_runtime_thunk-x86_64.lib /NODEFAULTLIB:clang_rt.asan_dbg_dynamic-x86_64.lib /NODEFAULTLIB:clang_rt.asan_dbg_dynamic_runtime_thunk-x86_64.lib /NODEFAULTLIB:libvcasan.lib /NODEFAULTLIB:libvcasand.lib /NODEFAULTLIB:vcasan.lib /NODEFAULTLIB:vcasand.lib")
  run_probe_configure(msvc-asan
    -DPROBE_EXPECT_RESULT=FALSE
    "-DCMAKE_EXE_LINKER_FLAGS=${_probe_asan_block_flags}")
else()
  message(FATAL_ERROR "Unknown TEST_MODE: ${TEST_MODE}")
endif()
