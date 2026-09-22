cmake_minimum_required(VERSION 3.24)

file(MAKE_DIRECTORY "${WORK_DIR}")

if(CROSSCOMPILING AND NOT EMULATOR)
  foreach(_program IN ITEMS GP MP MG GC CF)
    if(NOT EXISTS "${${_program}}")
      message(FATAL_ERROR "Built CLI target is missing: ${${_program}}")
    endif()
  endforeach()
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  return()
endif()

function(run_case name expect_success)
  set(_command ${ARGN})
  if(CROSSCOMPILING)
    list(PREPEND _command ${EMULATOR})
  endif()
  execute_process(COMMAND ${_command}
    WORKING_DIRECTORY "${WORK_DIR}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
  if(expect_success AND NOT _result EQUAL 0)
    message(FATAL_ERROR
      "${name} unexpectedly failed (${_result}):\n${_output}\n${_error}")
  elseif(NOT expect_success AND _result EQUAL 0)
    message(FATAL_ERROR "${name} unexpectedly succeeded:\n${_output}\n${_error}")
  endif()
endfunction()

set(_graph "${GRAPH_DIR}/4elt.graph")
set(_bad_fmt "${WORK_DIR}/bad-fmt.graph")
set(_trailing "${WORK_DIR}/trailing-token.graph")
set(_wide "${WORK_DIR}/out-of-range.graph")
set(_mesh "${WORK_DIR}/empty-element.mesh")
set(_mesh_output "${WORK_DIR}/empty-element.graph")
set(_multicon_graph "${WORK_DIR}/multicon.graph")
set(_multicon_mesh "${WORK_DIR}/multicon.mesh")
set(_duplicate "${WORK_DIR}/duplicate.iperm")
set(_edgeless "${WORK_DIR}/edgeless.graph")
set(_weighted_edgeless "${WORK_DIR}/weighted-edgeless.graph")
set(_edgeless_partition "${_edgeless}.part.2")
set(_packed_graph "${WORK_DIR}/packed.graph")
set(_packed_permutation "${WORK_DIR}/packed.iperm")
set(_pair_mesh "${WORK_DIR}/pair.mesh")
set(_element_partition "${_pair_mesh}.epart.2")
set(_node_partition "${_pair_mesh}.npart.2")
set(_self_loop "${WORK_DIR}/self-loop.graph")
set(_fixed_self_loop "${WORK_DIR}/fixed-self-loop.graph")

file(WRITE "${_bad_fmt}" "2 1 12\n2\n1\n")
file(WRITE "${_trailing}" "2 1\n2x\n1\n")
if(IDXTYPEWIDTH EQUAL 32)
  file(WRITE "${_wide}" "2147483648 1\n")
else()
  file(WRITE "${_wide}" "9223372036854775808 1\n")
endif()
file(WRITE "${_mesh}" "1\n\n")
file(WRITE "${_multicon_graph}"
  "2 1 10 2\n1 1 2\n1 1 1\n")
file(WRITE "${_multicon_mesh}" "1 2\n1 1 1\n")
file(WRITE "${_duplicate}" "0\n0\n")
file(WRITE "${_edgeless}" "3 0\n\n\n\n")
file(WRITE "${_weighted_edgeless}" "3 0 1\n\n\n\n")
file(WRITE "${_packed_graph}" "2 1\n2\n1\n")
file(WRITE "${_packed_permutation}" "0 1\n")
file(WRITE "${_pair_mesh}" "2\n1 2 3\n2 3 4\n")
file(WRITE "${_self_loop}" "1 1 1\n1 1 1 1\n")
file(REMOVE "${_mesh_output}")

run_case("explicit help" TRUE "${GP}" -help)
run_case("trailing CLI integer" FALSE "${GP}" "${_graph}" 2junk)
run_case("missing CLI arguments" FALSE "${MG}")
run_case("invalid fmt digit" FALSE "${GC}" "${_bad_fmt}")
run_case("trailing graph token" FALSE "${GC}" "${_trailing}")
run_case("out-of-range graph integer" FALSE "${GC}" "${_wide}")
run_case("empty mesh element" FALSE "${MG}" "${_mesh}" "${_mesh_output}")
execute_process(
  COMMAND ${EMULATOR} "${MG}" "${_multicon_mesh}" "${_mesh_output}"
  WORKING_DIRECTORY "${WORK_DIR}"
  RESULT_VARIABLE _multicon_result
  OUTPUT_VARIABLE _multicon_output
  ERROR_VARIABLE _multicon_error)
if(_multicon_result EQUAL 0 OR
    NOT "${_multicon_output}${_multicon_error}" MATCHES
      "supports at most one balancing constraint")
  message(FATAL_ERROR
    "mesh ncon was not rejected before allocation:\n${_multicon_output}${_multicon_error}")
endif()
run_case("unwritable graph output" FALSE "${MG}"
  "${GRAPH_DIR}/metis.mesh" "${WORK_DIR}")
run_case("duplicate permutation" FALSE "${CF}" "${_graph}" "${_duplicate}")
run_case("packed permutation" TRUE "${CF}" "${_packed_graph}"
  "${_packed_permutation}")
run_case("edgeless graph ordering" TRUE "${ND}" "${_edgeless}")
run_case("edgeless graph" TRUE "${GP}" "${_edgeless}" 2)
file(STRINGS "${_edgeless_partition}" _edgeless_parts)
list(LENGTH _edgeless_parts _edgeless_part_count)
if(NOT _edgeless_part_count EQUAL 3)
  message(FATAL_ERROR
    "edgeless graph produced ${_edgeless_part_count} partition entries")
endif()
foreach(_part IN LISTS _edgeless_parts)
  if(NOT _part MATCHES "^[01]$")
    message(FATAL_ERROR "edgeless graph produced invalid partition ${_part}")
  endif()
endforeach()
run_case("weighted edgeless graph" TRUE "${GC}" "${_weighted_edgeless}")
run_case("fix self-loop-only graph" FALSE "${GC}" "${_self_loop}"
  "${_fixed_self_loop}")
run_case("validate fixed self-loop-only graph" TRUE "${GC}"
  "${_fixed_self_loop}")
run_case("non-finite ubvec" FALSE "${GP}" -ubvec=nan "${_graph}" 2)
run_case("trailing ubvec data" FALSE "${GP}" -ubvec=1.05junk "${_graph}" 2)
run_case("unseparated ubvec values" FALSE "${GP}" "-ubvec=1.1+1.2"
  "${_multicon_graph}" 2)

# A failure validating the second mesh output must leave the first output
# unchanged and remove every uncommitted temporary file.
file(WRITE "${_element_partition}" "preserved-element-partition")
file(MAKE_DIRECTORY "${_node_partition}")
run_case("mesh output pair rollback" FALSE "${MP}" "${_pair_mesh}" 2)
file(READ "${_element_partition}" _preserved_partition)
file(GLOB _mesh_temporaries "${_pair_mesh}.?part.2.tmp.*")
if(NOT _preserved_partition STREQUAL "preserved-element-partition" OR
    NOT IS_DIRECTORY "${_node_partition}" OR _mesh_temporaries)
  message(FATAL_ERROR "Failed mesh output transaction changed prior outputs")
endif()

if(EXISTS "${_mesh_output}")
  message(FATAL_ERROR "Malformed mesh left an output file: ${_mesh_output}")
endif()
