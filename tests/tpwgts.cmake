cmake_minimum_required(VERSION 3.24)

if(CROSSCOMPILING AND NOT EMULATOR)
  if(NOT EXISTS "${GP}")
    message(FATAL_ERROR "Built gpmetis target is missing: ${GP}")
  endif()
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  return()
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")
set(graph "${WORK_DIR}/ring.graph")
file(WRITE "${graph}"
  "4 4 10 2\n1 1 2 4\n1 1 1 3\n1 1 2 4\n1 1 1 3\n")

function(run_tpwgts_case name contents expected)
  set(weights "${WORK_DIR}/${name}.tpwgts")
  file(WRITE "${weights}" "${contents}")

  set(nparts 2)
  if(ARGC GREATER 3)
    set(nparts "${ARGV3}")
  endif()
  set(command "${GP}" -seed=42 -nooutput "-tpwgts=${weights}" "${graph}" "${nparts}")
  if(CROSSCOMPILING)
    list(PREPEND command ${EMULATOR})
  endif()

  execute_process(
    COMMAND ${command}
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  set(log "${output}${error}")

  if(expected STREQUAL "SUCCESS")
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "${name} failed (${result}):\n${log}")
    endif()
  else()
    if(result EQUAL 0)
      message(FATAL_ERROR "${name} unexpectedly accepted <${contents}>:\n${log}")
    endif()
    if(NOT log MATCHES "${expected}")
      message(FATAL_ERROR
        "${name} failed for an unexpected reason (${result}):\n${log}")
    endif()
  endif()
endfunction()

run_tpwgts_case(valid "0=0.5\n" SUCCESS)
run_tpwgts_case(valid-range "0-1:0=0.5\n" SUCCESS)
run_tpwgts_case(valid-spaces " 0 - 1 : 0 = 0.5 \n" SUCCESS)
run_tpwgts_case(split-token "0 1=0.5\n" "wgt.*missing")
run_tpwgts_case(inverted-partition-range "1-0=0.5\n"
  "Invalid partition range")
run_tpwgts_case(inverted-constraint-range "0:1-0=0.5\n"
  "Invalid constraint number range")
run_tpwgts_case(missing-constraint "0:=0.5\n" "fromcnum.*incorrect")
run_tpwgts_case(missing-range-end "0-=0.5\n" "to.*incorrect")
run_tpwgts_case(nonfinite "0=nan\n" "Invalid partition weight")
run_tpwgts_case(infinite "0=inf\n" "Invalid partition weight")
run_tpwgts_case(index-overflow "9223372036854775808=0.5\n" "from.*incorrect")
if(IDXTYPEWIDTH EQUAL 32)
  run_tpwgts_case(idx32-overflow "4294967296=0.5\n" "from.*incorrect")
endif()
run_tpwgts_case(trailing-text "0=0.5junk\n" "trailing characters")
run_tpwgts_case(missing-weight "0=\n" "wgt.*incorrect")
run_tpwgts_case(exhausted-with-remainder "0=0.5\n1=0.5\n"
  "meet or exceed" 3)
