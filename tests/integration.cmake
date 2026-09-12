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

# Build, test, install and relocate the producer package first.
set(configure_args -G "${GENERATOR}")
run("${CMAKE_COMMAND}" -S "${root}" -B "${build}/producer" ${configure_args}
  -DCMAKE_BUILD_TYPE=${CONFIG} -DMETIS_BUILD_SHARED_LIBS=${SHARED}
  -DMETIS_BUILD_TESTING=ON -DMETIS_BUILD_PROGRAMS=ON -DMETIS_INSTALL=ON
  -DMETIS_FETCH_GKLIB=OFF -DMETIS_GKLIB_PROVIDER=SOURCE -DMETIS_IPO=OFF)
run("${CMAKE_COMMAND}" --build "${build}/producer" --config "${CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/producer" -C "${CONFIG}"
  --output-on-failure)
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
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/system" -C "${CONFIG}"
  --output-on-failure)

# Validate installed and source-tree consumers in both C and C++.
run("${CMAKE_COMMAND}" -S "${root}/tests/integration/consumer"
  -B "${build}/package-c" ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DCMAKE_PREFIX_PATH=${build}/relocated)
run("${CMAKE_COMMAND}" --build "${build}/package-c" --config "${CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/package-c" -C "${CONFIG}"
  --output-on-failure)

run("${CMAKE_COMMAND}" -S "${root}/tests/consumer" -B "${build}/package-cxx"
  ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DCMAKE_PREFIX_PATH=${build}/relocated)
run("${CMAKE_COMMAND}" --build "${build}/package-cxx" --config "${CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/package-cxx" -C "${CONFIG}"
  --output-on-failure)

run("${CMAKE_COMMAND}" -S "${root}/tests/integration/consumer"
  -B "${build}/source-c" ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG}
  -DMETIS_TEST_SOURCE=${root} -DMETIS_BUILD_SHARED_LIBS=${SHARED})
run("${CMAKE_COMMAND}" --build "${build}/source-c" --config "${CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/source-c" -C "${CONFIG}"
  --output-on-failure)

run("${CMAKE_COMMAND}" -S "${root}/tests/consumer" -B "${build}/source-cxx"
  ${configure_args} -DCMAKE_BUILD_TYPE=${CONFIG} -DMETIS_TEST_SOURCE=${root}
  -DMETIS_BUILD_SHARED_LIBS=${SHARED})
run("${CMAKE_COMMAND}" --build "${build}/source-cxx" --config "${CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${build}/source-cxx" -C "${CONFIG}"
  --output-on-failure)

message(STATUS "METIS install, relocation, package and subdirectory integration passed")
