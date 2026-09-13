cmake_minimum_required(VERSION 3.24)
project(METISLinkProbeFixture LANGUAGES C CXX)

if(NOT DEFINED MODULE_FILE OR NOT DEFINED PROBE_FUNCTION OR
    NOT DEFINED PROBE_MODE)
  message(FATAL_ERROR "The link probe fixture is missing required arguments")
endif()

# Preserve the caller's try-compile target type across the probe function.
if(NOT DEFINED INHERITED_TARGET_TYPE)
  set(INHERITED_TARGET_TYPE STATIC_LIBRARY)
endif()
if(NOT INHERITED_TARGET_TYPE MATCHES "^(STATIC_LIBRARY|EXECUTABLE)$")
  message(FATAL_ERROR
    "INHERITED_TARGET_TYPE must be STATIC_LIBRARY or EXECUTABLE")
endif()
set(CMAKE_TRY_COMPILE_TARGET_TYPE "${INHERITED_TARGET_TYPE}")
include("${MODULE_FILE}")

# Select custom and multi-config probe configurations only after compiler
# identification, whose bootstrap project may not know a user configuration.
if(DEFINED PROBE_ACTIVE_CONFIG)
  set(CMAKE_TRY_COMPILE_CONFIGURATION "${PROBE_ACTIVE_CONFIG}")
endif()

# Seed every check-state variable with a value that the fixture can compare.
set(CMAKE_REQUIRED_FLAGS "")
set(CMAKE_REQUIRED_DEFINITIONS -DPROBE_STATE=1)
set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}")
set(CMAKE_REQUIRED_LINK_OPTIONS "")
set(CMAKE_REQUIRED_LIBRARIES "")
set(CMAKE_REQUIRED_LINK_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}")

# Select the expected result and any scenario-specific check inputs.
if(PROBE_MODE STREQUAL "gprof")
  set(CMAKE_REQUIRED_FLAGS -pg)
  set(CMAKE_REQUIRED_LINK_OPTIONS -pg)
  set(_probe_expected TRUE)
elseif(PROBE_MODE STREQUAL "missing-library")
  set(CMAKE_REQUIRED_LIBRARIES metis_probe_library_that_does_not_exist)
  set(_probe_expected FALSE)
elseif(PROBE_MODE STREQUAL "valid" OR PROBE_MODE STREQUAL "environment")
  set(_probe_expected TRUE)
elseif(PROBE_MODE STREQUAL "config-switch")
  if(NOT DEFINED PROBE_EXPECT_RESULT)
    message(FATAL_ERROR "config-switch requires PROBE_EXPECT_RESULT")
  endif()
  set(_probe_expected "${PROBE_EXPECT_RESULT}")
elseif(PROBE_MODE MATCHES "^config-link-(valid|missing)$")
  if(NOT DEFINED PROBE_ACTIVE_CONFIG)
    message(FATAL_ERROR "config-link modes require PROBE_ACTIVE_CONFIG")
  endif()
  set(_probe_expected TRUE)
  if(PROBE_MODE STREQUAL "config-link-missing")
    string(TOUPPER "${PROBE_ACTIVE_CONFIG}" _probe_config_upper)
    if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
      # The Intel LLVM MSVC-style driver forwards raw linker options through
      # /Qoption,link,. A bare /DEFAULTLIB is otherwise consumed by the driver.
      set(CMAKE_EXE_LINKER_FLAGS_${_probe_config_upper}
        "/Qoption,link,/DEFAULTLIB:metis_probe_library_that_does_not_exist.lib")
    elseif(MSVC)
      set(CMAKE_EXE_LINKER_FLAGS_${_probe_config_upper}
        "/DEFAULTLIB:metis_probe_library_that_does_not_exist.lib")
    else()
      set(CMAKE_EXE_LINKER_FLAGS_${_probe_config_upper}
        "-lmetis_probe_library_that_does_not_exist")
    endif()
    set(_probe_expected FALSE)
  endif()
elseif(PROBE_MODE STREQUAL "custom-linker-cache")
  if(CMAKE_VERSION VERSION_LESS 3.29 OR
      NOT CMAKE_C_COMPILER_ID STREQUAL "Clang" OR
      NOT CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "GNU" OR
      NOT DEFINED CMAKE_C_USING_LINKER_SYSTEM)
    message(FATAL_ERROR
      "custom-linker-cache requires CMake 3.29 and a GNU-style Clang driver")
  endif()
  set(CMAKE_LINKER_TYPE metis_probe_system)
  set(CMAKE_C_USING_LINKER_metis_probe_system
    "${CMAKE_C_USING_LINKER_SYSTEM}")
  set(_probe_expected FALSE)
elseif(PROBE_MODE STREQUAL "ignored-option")
  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "/metis-probe-ignored-option")
  elseif(CMAKE_C_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
    set(CMAKE_REQUIRED_FLAGS "-Wmetis-probe-ignored-option")
  else()
    message(FATAL_ERROR
      "ignored-option requires an MSVC-style or Clang compiler driver")
  endif()
  set(_probe_expected FALSE)
elseif(PROBE_MODE STREQUAL "msvc-asan")
  if(NOT DEFINED PROBE_EXPECT_RESULT)
    message(FATAL_ERROR "msvc-asan requires PROBE_EXPECT_RESULT")
  endif()
  set(CMAKE_REQUIRED_FLAGS /fsanitize=address)
  set(CMAKE_REQUIRED_LINK_OPTIONS LINKER:/INFERASANLIBS)
  set(_probe_expected "${PROBE_EXPECT_RESULT}")
else()
  message(FATAL_ERROR "Unknown PROBE_MODE: ${PROBE_MODE}")
endif()

set(_probe_state_variables
  CMAKE_REQUIRED_FLAGS
  CMAKE_REQUIRED_DEFINITIONS
  CMAKE_REQUIRED_INCLUDES
  CMAKE_REQUIRED_LINK_OPTIONS
  CMAKE_REQUIRED_LIBRARIES
  CMAKE_REQUIRED_LINK_DIRECTORIES
  CMAKE_TRY_COMPILE_CONFIGURATION
  CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
foreach(_probe_state_variable IN LISTS _probe_state_variables)
  set("_probe_before_${_probe_state_variable}"
    "${${_probe_state_variable}}")
endforeach()

# Embedded probe programs stay byte-for-byte stable as cache-signature inputs.
set(_probe_source "#ifndef PROBE_STATE
#error PROBE_STATE was not passed to the probe
#endif
#ifdef PROBE_CONFIG_BREAK
#error The custom configuration must invalidate the cached probe
#endif
int main(void) { return 0; }")
if(PROBE_MODE STREQUAL "msvc-asan")
  set(_probe_source "#ifndef __SANITIZE_ADDRESS__
#error AddressSanitizer instrumentation is not enabled
#endif
int main(int argc, char **argv) {
  volatile int values[2] = {1, 2};
  values[argc] = argc * argc;
  return values[argc] + (argv[0] != 0);
}")
endif()

# A custom linker type must invalidate a result when its implementation changes.
if(PROBE_MODE STREQUAL "custom-linker-cache")
  cmake_language(CALL "${PROBE_FUNCTION}" "${_probe_source}" _probe_valid)
  set(CMAKE_C_USING_LINKER_metis_probe_system
    "--ld-path=metis-probe-linker-that-does-not-exist")
  cmake_language(CALL "${PROBE_FUNCTION}" "${_probe_source}" _probe_invalid)
  if(NOT _probe_valid OR _probe_invalid)
    message(FATAL_ERROR
      "Changing a custom linker implementation did not invalidate the probe")
  endif()
endif()

# Environment mode proves that compiler search paths participate in the cache.
if(PROBE_MODE STREQUAL "environment")
  if(MSVC)
    set(_probe_env INCLUDE)
    set(_probe_separator ";")
  else()
    set(_probe_env C_INCLUDE_PATH)
    if(WIN32)
      set(_probe_separator ";")
    else()
      set(_probe_separator ":")
    endif()
  endif()
  set(_probe_env_before "$ENV{${_probe_env}}")
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/valid" "${CMAKE_CURRENT_BINARY_DIR}/invalid")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/valid/probe_env.h" "/* valid */\n")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/invalid/probe_env.h" "#error Invalid environment\n")
  set(_probe_source "#include <probe_env.h>\nint main(void) { return 0; }")
  set(ENV{${_probe_env}}
    "${CMAKE_CURRENT_BINARY_DIR}/valid${_probe_separator}${_probe_env_before}")
  cmake_language(CALL "${PROBE_FUNCTION}" "${_probe_source}" _probe_valid)
  set(ENV{${_probe_env}}
    "${CMAKE_CURRENT_BINARY_DIR}/invalid${_probe_separator}${_probe_env_before}")
  cmake_language(CALL "${PROBE_FUNCTION}" "${_probe_source}" _probe_invalid)
  set(ENV{${_probe_env}} "${_probe_env_before}")
  if(NOT _probe_valid OR _probe_invalid)
    message(FATAL_ERROR "Environment changes did not invalidate the link check")
  endif()
  set(_probe_source "int main(void) { return 0; }")
endif()

if(PROBE_LANGUAGE STREQUAL "CXX")
  cmake_language(CALL "${PROBE_FUNCTION}"
    "${_probe_source}" _probe_result LANGUAGE CXX)
else()
  cmake_language(CALL "${PROBE_FUNCTION}"
    "${_probe_source}" _probe_result)
endif()

# Validate both the result and restoration of all inherited check state.
if(_probe_expected AND NOT _probe_result)
  message(FATAL_ERROR
    "Expected the ${PROBE_MODE} link probe to succeed")
elseif(NOT _probe_expected AND _probe_result)
  message(FATAL_ERROR
    "Expected the ${PROBE_MODE} link probe to fail")
endif()
if(NOT CMAKE_TRY_COMPILE_TARGET_TYPE STREQUAL
    "${INHERITED_TARGET_TYPE}")
  message(FATAL_ERROR "The link probe changed the parent try-compile target type")
endif()
foreach(_probe_state_variable IN LISTS _probe_state_variables)
  if(NOT "${${_probe_state_variable}}" STREQUAL
      "${_probe_before_${_probe_state_variable}}")
    message(FATAL_ERROR
      "The link probe changed parent check state ${_probe_state_variable}")
  endif()
endforeach()

if(PROBE_MODE STREQUAL "gprof")
  add_executable(probe_gprof main.c)
  target_compile_options(probe_gprof PRIVATE -pg)
  target_link_options(probe_gprof PRIVATE -pg)
endif()
