cmake_minimum_required(VERSION 3.24)


foreach(required LOOKUP_SOURCE MODULE_FILE NAMESPACE PRODUCER_RECORD
    PRODUCER_ARCHIVE WORK_DIR GENERATOR MULTI_CONFIG_GENERATOR MAKE_PROGRAM)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "The ASan lookup runner requires ${required}")
  endif()
endforeach()

if(NOT COMPILER_RUNTIME_ROOT)
  if(NOT RUNTIME_ARCHIVES)
    message(FATAL_ERROR
      "The ASan lookup runner requires COMPILER_RUNTIME_ROOT or RUNTIME_ARCHIVES")
  endif()
  list(GET RUNTIME_ARCHIVES 0 first_runtime_archive)
  file(REAL_PATH "${first_runtime_archive}" first_runtime_archive)
  get_filename_component(COMPILER_RUNTIME_ROOT "${first_runtime_archive}" DIRECTORY)
endif()

file(MAKE_DIRECTORY "${WORK_DIR}")


function(run_lookup name generator config)
  set(binary "${WORK_DIR}/${name}")
  file(REMOVE_RECURSE "${binary}")

  set(command "${CMAKE_COMMAND}"
    -S "${LOOKUP_SOURCE}"
    -B "${binary}"
    -G "${generator}"
    "-DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM}"
    "-DCMAKE_BUILD_TYPE=${config}"
    "-DMODULE_FILE=${MODULE_FILE}"
    "-DNAMESPACE=${NAMESPACE}"
    "-DPRODUCER_RECORD=${PRODUCER_RECORD}"
    "-DPRODUCER_ARCHIVE=${PRODUCER_ARCHIVE}"
    "-DCOMPILER_RUNTIME_ROOT=${COMPILER_RUNTIME_ROOT}"
    "-DSCENARIO=${name}")
  if(name STREQUAL "multi-release")
    list(APPEND command "-DCMAKE_CONFIGURATION_TYPES=Release")
  endif()

  execute_process(COMMAND ${command}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  file(WRITE "${WORK_DIR}/${name}.log" "${output}\n${error}")
  if(NOT result STREQUAL "0")
    message(FATAL_ERROR
      "ASan lookup scenario ${name} failed (${result}):\n${output}\n${error}")
  endif()
  message(STATUS "ASan lookup scenario ${name} passed for ${NAMESPACE}")
endfunction()


run_lookup(multi-release "${MULTI_CONFIG_GENERATOR}" Debug)
run_lookup(configless "${GENERATOR}" Debug)
run_lookup(retry "${GENERATOR}" RelWithDebInfo)
