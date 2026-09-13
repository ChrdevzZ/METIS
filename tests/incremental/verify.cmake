cmake_minimum_required(VERSION 3.24)

foreach(required IN ITEMS SOURCE_DIR WORK_DIR GENERATOR METIS_TEST_SUPPORT_FILE)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()

cmake_path(ABSOLUTE_PATH SOURCE_DIR NORMALIZE OUTPUT_VARIABLE source_dir)
cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE work_dir)
cmake_path(IS_PREFIX source_dir "${work_dir}" NORMALIZE work_is_in_source)
cmake_path(IS_PREFIX work_dir "${source_dir}" NORMALIZE source_is_in_work)
if(work_is_in_source OR source_is_in_work OR source_dir STREQUAL work_dir)
  message(FATAL_ERROR "WORK_DIR must be outside the fixture source directory")
endif()

set(work_marker "${work_dir}/.metis-incremental-owned")
if(EXISTS "${work_dir}" AND NOT EXISTS "${work_marker}")
  message(FATAL_ERROR
    "Refusing to clean an incremental work directory without its ownership marker")
endif()
file(MAKE_DIRECTORY "${work_dir}")
file(WRITE "${work_marker}" "Fixture-owned METIS incremental regression directory.\n")


function(run_checked description)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR
      "${description} failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
  endif()
endfunction()


function(read_state prefix)
  set(header "${work_dir}/build/export_symbols.h")
  if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
    set(target_file "${work_dir}/build/target-${CONFIG}.txt")
  else()
    set(target_file "${work_dir}/build/target-.txt")
  endif()

  file(GLOB_RECURSE objects
    "${work_dir}/build/CMakeFiles/export_header_consumer.dir/*.o"
    "${work_dir}/build/CMakeFiles/export_header_consumer.dir/*.obj"
    "${work_dir}/build/export_header_consumer.dir/*.o"
    "${work_dir}/build/export_header_consumer.dir/*.obj")
  list(LENGTH objects object_count)
  if(NOT object_count EQUAL 1 OR NOT EXISTS "${header}" OR
      NOT EXISTS "${target_file}")
    message(FATAL_ERROR
      "incremental fixture did not produce one object and its tracked outputs")
  endif()

  list(GET objects 0 object)
  file(READ "${target_file}" executable)
  if(NOT EXISTS "${executable}")
    message(FATAL_ERROR "incremental fixture executable is missing: ${executable}")
  endif()

  foreach(item IN ITEMS header object executable)
    file(TIMESTAMP "${${item}}" ${prefix}_${item}_time "%s")
    set(${prefix}_${item}_time "${${prefix}_${item}_time}" PARENT_SCOPE)
  endforeach()
  file(SHA256 "${header}" header_hash)
  set(${prefix}_header_hash "${header_hash}" PARENT_SCOPE)
endfunction()


function(require_same before after description)
  if(NOT "${before}" STREQUAL "${after}")
    message(FATAL_ERROR "${description} changed during a no-op rebuild")
  endif()
endfunction()


function(require_newer before after description)
  if(NOT "${after}" GREATER "${before}")
    message(FATAL_ERROR "${description} was not rebuilt after its input changed")
  endif()
endfunction()


foreach(name IN ITEMS source build)
  set(owned_path "${work_dir}/${name}")
  cmake_path(IS_PREFIX work_dir "${owned_path}" NORMALIZE owned)
  if(NOT owned)
    message(FATAL_ERROR "Incremental test directory escaped its owned root")
  endif()
  file(REMOVE_RECURSE "${owned_path}")
endforeach()
file(COPY "${source_dir}/" DESTINATION "${work_dir}/source")

set(configure_command
  "${CMAKE_COMMAND}" -S "${work_dir}/source" -B "${work_dir}/build"
  -G "${GENERATOR}"
  "-DMETIS_TEST_SUPPORT_FILE=${METIS_TEST_SUPPORT_FILE}")
if(DEFINED TEST_INITIAL_CACHE AND NOT TEST_INITIAL_CACHE STREQUAL "")
  if(NOT EXISTS "${TEST_INITIAL_CACHE}")
    message(FATAL_ERROR "TEST_INITIAL_CACHE does not exist: ${TEST_INITIAL_CACHE}")
  endif()
  list(APPEND configure_command -C "${TEST_INITIAL_CACHE}")
endif()
if(DEFINED GENERATOR_INSTANCE AND NOT GENERATOR_INSTANCE STREQUAL "")
  list(APPEND configure_command
    "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
foreach(pair IN ITEMS
    GENERATOR_PLATFORM:-A
    GENERATOR_TOOLSET:-T)
  string(REPLACE ":" ";" fields "${pair}")
  list(GET fields 0 variable)
  list(GET fields 1 option)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
    list(APPEND configure_command "${option}" "${${variable}}")
  endif()
endforeach()
foreach(variable IN ITEMS
    CMAKE_MAKE_PROGRAM
    CMAKE_TOOLCHAIN_FILE
    CMAKE_C_COMPILER
    CMAKE_C_COMPILER_TARGET
    CMAKE_SYSROOT
    CMAKE_MSVC_RUNTIME_LIBRARY
    CMAKE_BUILD_TYPE)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
    list(APPEND configure_command "-D${variable}=${${variable}}")
  endif()
endforeach()

set(build_command "${CMAKE_COMMAND}" --build "${work_dir}/build" --parallel 2)
if(DEFINED CONFIG AND NOT CONFIG STREQUAL "")
  # Single-config generators select their configuration during generation;
  # multi-config generators select the same configuration at build time.
  list(APPEND configure_command "-DCMAKE_BUILD_TYPE=${CONFIG}")
  list(APPEND build_command --config "${CONFIG}")
endif()

run_checked("initial configure" ${configure_command})
run_checked("initial build" ${build_command})
read_state(initial)

execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
run_checked("no-op reconfigure" ${configure_command})
run_checked("no-op build" ${build_command})
read_state(noop)

foreach(item IN ITEMS header object executable)
  require_same("${initial_${item}_time}" "${noop_${item}_time}" "${item}")
endforeach()
require_same("${initial_header_hash}" "${noop_header_hash}" "generated header")

file(APPEND "${work_dir}/source/export-symbols.txt" "METIS_NodeND\n")
execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 1.1)
# Let the build tool observe CMAKE_CONFIGURE_DEPENDS. An explicit configure
# here would conceal a missing automatic regeneration dependency.
run_checked("changed-symbol build" ${build_command})
read_state(changed)

if(initial_header_hash STREQUAL changed_header_hash)
  message(FATAL_ERROR "the generated header ignored a changed symbol list")
endif()
foreach(item IN ITEMS header object executable)
  require_newer("${noop_${item}_time}" "${changed_${item}_time}" "${item}")
endforeach()
