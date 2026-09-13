cmake_minimum_required(VERSION 3.24)

# The marker limits cleanup to a directory created by this fixture.
if(NOT DEFINED ROOT OR NOT DEFINED BUILD)
  message(FATAL_ERROR "Set ROOT to METIS sources and BUILD to a dedicated test directory")
endif()

file(REAL_PATH "${ROOT}" root)
get_filename_component(build "${BUILD}" ABSOLUTE)
if(build STREQUAL root)
  message(FATAL_ERROR "The dependency test directory must differ from the source directory")
endif()
if(NOT DEFINED GENERATOR)
  set(GENERATOR Ninja)
endif()
if(NOT DEFINED CONFIG)
  set(CONFIG Debug)
endif()
cmake_path(IS_PREFIX build "${root}" NORMALIZE contains_source)
if(contains_source)
  message(FATAL_ERROR "The dependency test directory must not contain the source tree")
endif()
if(EXISTS "${build}")
  if(NOT EXISTS "${build}/.dependency-test")
    message(FATAL_ERROR "Existing directory is not owned by this dependency test")
  endif()
  foreach(name source missing override parent parent-build)
    set(target "${build}/${name}")
    cmake_path(IS_PREFIX build "${target}" NORMALIZE inside)
    if(NOT inside)
      message(FATAL_ERROR "Dependency test directory escaped its root")
    endif()
    file(REMOVE_RECURSE "${target}")
  endforeach()
endif()
file(MAKE_DIRECTORY "${build}")
file(WRITE "${build}/.dependency-test" "Dependency integration regression\n")

# Copy a source snapshot without bundled GKlib to exercise fallback behavior.
set(snapshot "${build}/source")
if(EXISTS "${snapshot}/ext/GKlib")
  message(FATAL_ERROR "The dependency test snapshot must not contain bundled GKlib")
endif()
file(MAKE_DIRECTORY "${snapshot}/ext" "${build}/parent")
file(COPY "${root}/CMakeLists.txt" "${root}/cmake" "${root}/src"
  "${root}/include" "${root}/LICENSE" DESTINATION "${snapshot}")
file(COPY "${root}/ext/CMakeLists.txt" DESTINATION "${snapshot}/ext")

# Fail each external command with its captured configure or build output.
function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Dependency test failed: ${ARGN}\n${output}\n${error}")
  endif()
endfunction()

function(run_tests binary_dir)
  if(TEST_CROSSCOMPILING AND NOT TEST_EMULATOR)
    set_property(GLOBAL PROPERTY METIS_RUNTIME_WAS_SKIPPED TRUE)
    return()
  endif()
  set(command "${CMAKE_CTEST_COMMAND}" --test-dir "${binary_dir}")
  if(NOT CONFIG STREQUAL "")
    list(APPEND command -C "${CONFIG}")
  endif()
  list(APPEND command --output-on-failure)
  run(${command})
endfunction()

# SOURCE mode without a source must fail before an accidental network request.
set(options -G "${GENERATOR}" -DMETIS_IPO=OFF
  -DMETIS_BUILD_PROGRAMS=OFF -DMETIS_BUILD_TESTING=OFF -DMETIS_INSTALL=OFF
  -DMETIS_GKLIB_PROVIDER=SOURCE)
if(TEST_INITIAL_CACHE)
  list(APPEND options -C "${TEST_INITIAL_CACHE}")
endif()
list(APPEND options "-DCMAKE_BUILD_TYPE=${CONFIG}")
if(GENERATOR_INSTANCE)
  list(APPEND options "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND options -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND options -T "${GENERATOR_TOOLSET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${snapshot}"
  -B "${build}/missing" ${options} -DMETIS_FETCH_GKLIB=OFF
  RESULT_VARIABLE missing_result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(missing_result EQUAL 0 OR NOT error MATCHES "GKlib source is unavailable")
  message(FATAL_ERROR "Missing offline dependency did not produce the expected diagnostic")
endif()

# An explicit disconnected FetchContent source remains a supported override.
run("${CMAKE_COMMAND}" -S "${snapshot}" -B "${build}/override" ${options}
  -DMETIS_FETCH_GKLIB=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON
  -DFETCHCONTENT_SOURCE_DIR_GKLIB=${root}/ext/GKlib)
run("${CMAKE_COMMAND}" --build "${build}/override" --config "${CONFIG}"
  --parallel 2)

# A parent FetchContent declaration must also satisfy METIS as a subproject.
file(WRITE "${build}/parent/CMakeLists.txt" "cmake_minimum_required(VERSION 3.24)
project(DependencyParent LANGUAGES C)
include(FetchContent)
FetchContent_Declare(gklib SOURCE_DIR \"${root}/ext/GKlib\")
add_subdirectory(\"${snapshot}\" metis)
add_executable(consumer main.c)
target_link_libraries(consumer PRIVATE METIS::metis)
enable_testing()
add_test(NAME parent.consumer COMMAND consumer)
")
file(WRITE "${build}/parent/main.c" "#include <metis.h>
int main(void) { idx_t options[METIS_NOPTIONS];
  return METIS_SetDefaultOptions(options) != METIS_OK; }
")
run("${CMAKE_COMMAND}" -S "${build}/parent" -B "${build}/parent-build" ${options}
  -DMETIS_FETCH_GKLIB=ON -DFETCHCONTENT_FULLY_DISCONNECTED=ON)
run("${CMAKE_COMMAND}" --build "${build}/parent-build" --config "${CONFIG}"
  --parallel 2)
run_tests("${build}/parent-build")
get_property(runtime_was_skipped GLOBAL PROPERTY METIS_RUNTIME_WAS_SKIPPED)
if(runtime_was_skipped)
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
else()
  message(STATUS
    "Missing dependency, offline source override, and parent FetchContent override passed")
endif()
