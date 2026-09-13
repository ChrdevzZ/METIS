cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS
    MODULE_FILE FUNCTION_PREFIX SKIP_MARKER WORK_DIR GENERATOR)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE work)
if(NOT work MATCHES "[/\\]test-support$")
  message(FATAL_ERROR "Unsafe test-support work directory: ${work}")
endif()
file(MAKE_DIRECTORY "${work}")


function(run_checked description)
  cmake_parse_arguments(PARSE_ARGV 1 command "" "" "")
  execute_process(
    COMMAND ${command_UNPARSED_ARGUMENTS}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "${description} failed (${result})\n${output}\n${error}")
  endif()
endfunction()


# Load the cache written by the project helper and compare the resulting CMake
# values. This exercises serialization of list, path, generator-expression and
# custom-linker inputs without inspecting the generated file's spelling.
include("${MODULE_FILE}")
set(TEST_PLATFORM_PAYLOAD [=[alpha;C:\SDK Path\$<CONFIG>@TOKEN@]=])
set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  TEST_PLATFORM_PAYLOAD CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR)
if(NOT CMAKE_CROSSCOMPILING)
  set(CMAKE_SYSTEM_NAME "${CMAKE_HOST_SYSTEM_NAME}")
  set(CMAKE_SYSTEM_PROCESSOR test-host)
endif()
set(CMAKE_LINKER_TYPE test_custom)
set(CMAKE_C_USING_LINKER_test_custom "-fuse-ld=lld")
set(CMAKE_C_USING_LINKER_MODE FLAG)
set(CMAKE_C_LINKER_LAUNCHER "launcher;--trace")
set(initial_cache "${work}/initial-cache.cmake")
cmake_language(CALL "${FUNCTION_PREFIX}_test_write_initial_cache"
  "${initial_cache}")

set(loader "${work}/load-cache.cmake")
file(CONFIGURE OUTPUT "${loader}" CONTENT [=[
if(NOT "@CMAKE_CROSSCOMPILING@" AND
    (DEFINED CMAKE_SYSTEM_NAME OR DEFINED CMAKE_SYSTEM_PROCESSOR))
  message(FATAL_ERROR "Native test cache forced a target system identity")
endif()
file(WRITE "${OUTPUT_FILE}"
  "${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES}\n${TEST_PLATFORM_PAYLOAD}\n"
  "${CMAKE_LINKER_TYPE}\n${CMAKE_C_USING_LINKER_test_custom}\n"
  "${CMAKE_C_USING_LINKER_MODE}\n${CMAKE_C_LINKER_LAUNCHER}")
]=] @ONLY NEWLINE_STYLE LF)
set(loaded "${work}/loaded.txt")
run_checked("load generated test cache"
  "${CMAKE_COMMAND}" -C "${initial_cache}"
  "-DOUTPUT_FILE=${loaded}" -P "${loader}")
file(READ "${loaded}" loaded_payload)
string(CONCAT expected_payload
  "TEST_PLATFORM_PAYLOAD;CMAKE_SYSTEM_NAME;CMAKE_SYSTEM_PROCESSOR\n"
  "${TEST_PLATFORM_PAYLOAD}\ntest_custom\n-fuse-ld=lld\nFLAG\nlauncher;--trace")
if(NOT loaded_payload STREQUAL expected_payload)
  message(FATAL_ERROR
    "Test cache changed a forwarded platform or linker value\n"
    "expected: ${expected_payload}\nactual: ${loaded_payload}")
endif()

# Exercise the project runtime wrapper itself. Native failures must not become
# skips, successful output must remain available to CTest diagnostics, and a
# missing cross target must fail before the no-emulator skip boundary.
set(bad_script "${work}/bad-runtime.cmake")
file(WRITE "${bad_script}"
  "message(\"${SKIP_MARKER}\")\nmessage(FATAL_ERROR \"deliberate failure\")\n")
set(output_script "${work}/runtime-output.cmake")
file(WRITE "${output_script}" "message(\"error at index\")\n")
set(recorder_script "${work}/record-emulator.cmake")
file(WRITE "${recorder_script}" [=[
file(WRITE "${RECORDER_FILE}" "invoked")
set(command)
set(index 4)
while(index LESS CMAKE_ARGC)
  list(APPEND command "${CMAKE_ARGV${index}}")
  math(EXPR index "${index} + 1")
endwhile()
execute_process(COMMAND ${command} RESULT_VARIABLE result)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "emulated command failed: ${result}")
endif()
]=])

set(fixture_source "${work}/fixture")
file(MAKE_DIRECTORY "${fixture_source}")
set(BAD_SCRIPT "${bad_script}")
set(OUTPUT_SCRIPT "${output_script}")
file(CONFIGURE OUTPUT "${fixture_source}/CMakeLists.txt" CONTENT [=[
cmake_minimum_required(VERSION 3.24)
project(TestSupportRuntime LANGUAGES NONE)
include("@MODULE_FILE@")
enable_testing()
add_executable(test-command IMPORTED GLOBAL)
if(TEST_MISSING_TARGET)
  set_property(TARGET test-command PROPERTY
    IMPORTED_LOCATION "@fixture_source@/missing-command")
  @FUNCTION_PREFIX@_test_add_runtime(NAME runtime TARGET test-command)
elseif(TEST_OUTPUT_TOKEN)
  set_property(TARGET test-command PROPERTY IMPORTED_LOCATION "@CMAKE_COMMAND@")
  @FUNCTION_PREFIX@_test_add_runtime(
    NAME runtime TARGET test-command ARGS -P "@OUTPUT_SCRIPT@")
  set_tests_properties(runtime PROPERTIES
    FAIL_REGULAR_EXPRESSION "error at index")
elseif(TEST_SUCCESS)
  set_property(TARGET test-command PROPERTY IMPORTED_LOCATION "@CMAKE_COMMAND@")
  @FUNCTION_PREFIX@_test_add_runtime(
    NAME runtime TARGET test-command ARGS -E true)
else()
  set_property(TARGET test-command PROPERTY IMPORTED_LOCATION "@CMAKE_COMMAND@")
  @FUNCTION_PREFIX@_test_add_runtime(
    NAME runtime TARGET test-command ARGS -P "@BAD_SCRIPT@")
endif()
]=] @ONLY NEWLINE_STYLE LF)

set(generator_args -G "${GENERATOR}")
if(MAKE_PROGRAM)
  list(APPEND generator_args "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}")
endif()
if(GENERATOR_INSTANCE)
  list(APPEND generator_args
    "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND generator_args -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND generator_args -T "${GENERATOR_TOOLSET}")
endif()

set(native_build "${work}/native")
run_checked("configure native runtime fixture"
  "${CMAKE_COMMAND}" -S "${fixture_source}" -B "${native_build}"
  ${generator_args})
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${native_build}"
    --output-on-failure
  RESULT_VARIABLE native_result
  OUTPUT_VARIABLE native_output
  ERROR_VARIABLE native_error)
if(native_result EQUAL 0 OR
    "${native_output}\n${native_error}" MATCHES "[ *]Skipped")
  message(FATAL_ERROR
    "Native failure was accepted as a skip\n${native_output}\n${native_error}")
endif()

set(output_build "${work}/runtime-output")
run_checked("configure runtime output fixture"
  "${CMAKE_COMMAND}" -S "${fixture_source}" -B "${output_build}"
  ${generator_args} -DTEST_OUTPUT_TOKEN=ON)
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${output_build}"
    --output-on-failure
  RESULT_VARIABLE output_result
  OUTPUT_VARIABLE output_output
  ERROR_VARIABLE output_error)
if(output_result EQUAL 0 OR
    NOT "${output_output}\n${output_error}" MATCHES "error at index")
  message(FATAL_ERROR
    "Runtime wrapper hid successful process output\n${output_output}\n${output_error}")
endif()

set(no_emulator_toolchain "${work}/no-emulator.cmake")
file(WRITE "${no_emulator_toolchain}" "set(CMAKE_SYSTEM_NAME Generic)\n")
set(no_emulator_build "${work}/cross-no-emulator")
run_checked("configure cross fixture without emulator"
  "${CMAKE_COMMAND}" -S "${fixture_source}" -B "${no_emulator_build}"
  ${generator_args} "-DCMAKE_TOOLCHAIN_FILE=${no_emulator_toolchain}")
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${no_emulator_build}"
    --output-on-failure
  RESULT_VARIABLE skip_result
  OUTPUT_VARIABLE skip_output
  ERROR_VARIABLE skip_error)
if(NOT skip_result EQUAL 0 OR
    NOT "${skip_output}\n${skip_error}" MATCHES "[ *]Skipped")
  message(FATAL_ERROR
    "Cross runtime was not reported as skipped\n${skip_output}\n${skip_error}")
endif()

set(missing_build "${work}/cross-missing-target")
run_checked("configure cross fixture with a missing target"
  "${CMAKE_COMMAND}" -S "${fixture_source}" -B "${missing_build}"
  ${generator_args} "-DCMAKE_TOOLCHAIN_FILE=${no_emulator_toolchain}"
  -DTEST_MISSING_TARGET=ON)
execute_process(
  COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${missing_build}"
    --output-on-failure
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_output
  ERROR_VARIABLE missing_error)
if(missing_result EQUAL 0 OR
    "${missing_output}\n${missing_error}" MATCHES "[ *]Skipped" OR
    NOT "${missing_output}\n${missing_error}" MATCHES "does not exist")
  message(FATAL_ERROR
    "Missing cross target was accepted as a runtime skip\n"
    "${missing_output}\n${missing_error}")
endif()

# A configured emulator is a list-valued command prefix. Prove that the wrapper
# invokes it and that the emulator subsequently runs the requested command.
set(recorder "${work}/emulator-invoked.txt")
file(REMOVE "${recorder}")
set(emulator_toolchain "${work}/emulator.cmake")
file(WRITE "${emulator_toolchain}"
  "set(CMAKE_SYSTEM_NAME Generic)\n"
  "set(CMAKE_CROSSCOMPILING_EMULATOR \"${CMAKE_COMMAND};-DRECORDER_FILE=${recorder};-P;${recorder_script}\")\n")
set(emulator_build "${work}/cross-emulator")
run_checked("configure cross fixture with emulator"
  "${CMAKE_COMMAND}" -S "${fixture_source}" -B "${emulator_build}"
  ${generator_args} "-DCMAKE_TOOLCHAIN_FILE=${emulator_toolchain}"
  -DTEST_SUCCESS=ON)
run_checked("emulated runtime"
  "${CMAKE_CTEST_COMMAND}" --test-dir "${emulator_build}"
  --output-on-failure)
if(NOT EXISTS "${recorder}")
  message(FATAL_ERROR "Configured emulator was not invoked")
endif()
