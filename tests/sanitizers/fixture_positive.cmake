cmake_minimum_required(VERSION 3.24)

if(CROSSCOMPILING AND NOT EMULATOR)
  message("ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
  return()
endif()

set(command "${PROGRAM}")
if(CROSSCOMPILING)
  list(PREPEND command ${EMULATOR})
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result
  OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 15)
message("${output}${error}")
if("${result}" STREQUAL "0" OR
    NOT "${output}" MATCHES "ASAN_FIXTURE_STARTED" OR
    NOT "${output}${error}" MATCHES "AddressSanitizer: heap-buffer-overflow")
  message(FATAL_ERROR "ASan fixture did not hit the expected diagnostic (${result})")
endif()
