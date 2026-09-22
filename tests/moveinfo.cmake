cmake_minimum_required(VERSION 3.24)

if(CROSSCOMPILING AND NOT EMULATOR)
  if(NOT EXISTS "${ND}")
    message(FATAL_ERROR "Built ndmetis target is missing: ${ND}")
  endif()
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  return()
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(graph "${WORK_DIR}/mdual.graph")
file(COPY "${GRAPH}" DESTINATION "${WORK_DIR}")

set(command "${ND}" -rtype=2sided -dbglvl=32 -niter=1 -nooutput "${graph}")
if(CROSSCOMPILING)
  list(PREPEND command ${EMULATOR})
endif()

execute_process(
  COMMAND ${command}
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "move-info diagnostics failed (${result}):\n${output}\n${error}")
endif()

if(NOT output MATCHES "Moved[ ]+[-0-9]+ to")
  message(FATAL_ERROR "move-info diagnostics produced no move record:\n${output}")
endif()

if(NOT output MATCHES "Gain:[ ]+[-0-9]+[ ]+\\[[ ]*N/A\\]")
  message(FATAL_ERROR
    "move-info diagnostics did not exercise a single-queue move:\n${output}")
endif()

if(NOT output MATCHES "Operation Count:[ ]+[0-9]+\\.[0-9]+e\\+0*10")
  message(FATAL_ERROR
    "fill-in reporting did not preserve its 64-bit operation count:\n${output}")
endif()
