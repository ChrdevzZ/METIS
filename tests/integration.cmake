cmake_minimum_required(VERSION 3.24)

# Normalize fixture defaults before validating the owned work directory.
foreach(required ROOT BUILD)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
if(NOT DEFINED CONFIG)
  set(CONFIG Release)
endif()
if(NOT DEFINED SHARED)
  set(SHARED OFF)
endif()
if(NOT DEFINED GENERATOR)
  set(GENERATOR Ninja)
endif()

# The marker prevents recursive cleanup outside this fixture's work tree.
file(REAL_PATH "${ROOT}" root)
get_filename_component(build "${BUILD}" ABSOLUTE
  BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}")
if(root STREQUAL build)
  message(FATAL_ERROR "BUILD must not be the source directory")
endif()
set(marker "${build}/.metis-integration")
if(EXISTS "${build}" AND NOT EXISTS "${marker}")
  message(FATAL_ERROR "BUILD exists without the METIS integration marker: ${build}")
endif()
file(MAKE_DIRECTORY "${build}")
file(WRITE "${marker}" "Owned by tests/integration.cmake\n")
foreach(name producer install relocated package-c package-cxx source-c source-cxx system)
  file(REMOVE_RECURSE "${build}/${name}")
endforeach()

# Preserve subprocess output for a single actionable failure diagnostic.
function(run)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command failed (${result}): ${ARGN}\n${output}\n${error}")
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

# Build, test, install and relocate the producer package first.
set(configure_args -G "${GENERATOR}")
if(TEST_INITIAL_CACHE)
  list(APPEND configure_args -C "${TEST_INITIAL_CACHE}")
endif()
if(GENERATOR_INSTANCE)
  list(APPEND configure_args
    "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND configure_args -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND configure_args -T "${GENERATOR_TOOLSET}")
endif()
run("${CMAKE_COMMAND}" -S "${root}" -B "${build}/producer" ${configure_args}
  -DCMAKE_BUILD_TYPE=${CONFIG} -DMETIS_BUILD_SHARED_LIBS=${SHARED}
  -DMETIS_BUILD_TESTING=ON -DMETIS_BUILD_PROGRAMS=ON -DMETIS_INSTALL=ON
  -DMETIS_FETCH_GKLIB=OFF -DMETIS_GKLIB_PROVIDER=SOURCE -DMETIS_IPO=OFF)
run("${CMAKE_COMMAND}" --build "${build}/producer" --config "${CONFIG}" --parallel 2)
run_tests("${build}/producer")
run("${CMAKE_COMMAND}" --install "${build}/producer" --config "${CONFIG}"
  --prefix "${build}/install")
file(RENAME "${build}/install" "${build}/relocated")

# Installation must contain public package artifacts only.
foreach(private_header metislib.h defs.h proto.h rename.h struct.h
    gklib_defs.h gklib_rename.h)
  if(EXISTS "${build}/relocated/include/${private_header}")
    message(FATAL_ERROR "Private header was installed: ${private_header}")
  endif()
endforeach()
message(STATUS "Read-only check passed: no METIS private headers are installed")

# Reconsume GKlib and METIS through the relocated package prefix.
run("${CMAKE_COMMAND}" -S "${root}" -B "${build}/system" ${configure_args}
  -DCMAKE_BUILD_TYPE=${CONFIG} -DCMAKE_PREFIX_PATH=${build}/relocated
  -DMETIS_GKLIB_PROVIDER=SYSTEM -DMETIS_BUILD_SHARED_LIBS=${SHARED}
  -DMETIS_BUILD_PROGRAMS=OFF -DMETIS_BUILD_TESTING=ON -DMETIS_INSTALL=OFF
  -DMETIS_IPO=OFF)
run("${CMAKE_COMMAND}" --build "${build}/system" --config "${CONFIG}" --parallel 2)
run_tests("${build}/system")

# Validate installed and source-tree consumers in both C and C++.
run("${CMAKE_COMMAND}" -S "${root}/tests/integration/consumer"
  -B "${build}/package-c" ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DCMAKE_PREFIX_PATH=${build}/relocated)
run("${CMAKE_COMMAND}" --build "${build}/package-c" --config "${CONFIG}" --parallel 2)
run_tests("${build}/package-c")

run("${CMAKE_COMMAND}" -S "${root}/tests/consumer" -B "${build}/package-cxx"
  ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DCMAKE_PREFIX_PATH=${build}/relocated)
run("${CMAKE_COMMAND}" --build "${build}/package-cxx" --config "${CONFIG}" --parallel 2)
run_tests("${build}/package-cxx")

run("${CMAKE_COMMAND}" -S "${root}/tests/integration/consumer"
  -B "${build}/source-c" ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DMETIS_TEST_SOURCE=${root} -DMETIS_BUILD_SHARED_LIBS=${SHARED})
run("${CMAKE_COMMAND}" --build "${build}/source-c" --config "${CONFIG}" --parallel 2)
run_tests("${build}/source-c")

run("${CMAKE_COMMAND}" -S "${root}/tests/consumer" -B "${build}/source-cxx"
  ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG} -DMETIS_TEST_SOURCE=${root}
  -DMETIS_BUILD_SHARED_LIBS=${SHARED})
run("${CMAKE_COMMAND}" --build "${build}/source-cxx" --config "${CONFIG}" --parallel 2)
run_tests("${build}/source-cxx")

get_property(runtime_was_skipped GLOBAL PROPERTY METIS_RUNTIME_WAS_SKIPPED)
if(runtime_was_skipped)
  message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
else()
  message(STATUS "METIS install, relocation, package and subdirectory integration passed")
endif()
