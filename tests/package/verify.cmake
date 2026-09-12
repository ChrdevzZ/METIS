cmake_minimum_required(VERSION 3.24)

# Refuse cleanup unless the work path and marker identify this fixture.
foreach(required TEST_SOURCE_DIR WORK_ROOT GENERATOR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
cmake_path(ABSOLUTE_PATH WORK_ROOT NORMALIZE OUTPUT_VARIABLE work)
if(NOT work MATCHES "/selfcontained-package$")
  message(FATAL_ERROR "Unsafe package test directory: ${work}")
endif()
if(EXISTS "${work}")
  if(NOT EXISTS "${work}/.package-test")
    message(FATAL_ERROR "Existing package directory is not owned by this test")
  endif()
  foreach(name gklib gklib-sdk metis installed relocated consumer)
    set(target "${work}/${name}")
    cmake_path(IS_PREFIX work "${target}" NORMALIZE inside)
    if(NOT inside)
      message(FATAL_ERROR "Package directory escaped its root")
    endif()
    file(REMOVE_RECURSE "${target}")
  endforeach()
endif()
file(MAKE_DIRECTORY "${work}")
file(WRITE "${work}/.package-test" "Self-contained package regression\n")

function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command failed: ${ARGN}\n${output}\n${error}")
  endif()
endfunction()

# Preserve the caller's generator, compiler and toolchain selections.
set(args -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Release)
foreach(variable CMAKE_MAKE_PROGRAM CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
    list(APPEND args "-D${variable}=${${variable}}")
  endif()
endforeach()
foreach(pair "GENERATOR_PLATFORM;-A" "GENERATOR_TOOLSET;-T")
  list(GET pair 0 variable)
  list(GET pair 1 flag)
  if(DEFINED ${variable} AND NOT "${${variable}}" STREQUAL "")
    list(APPEND args "${flag}" "${${variable}}")
  endif()
endforeach()

# Install static GKlib first, then absorb it into a shared METIS package.
run("${CMAKE_COMMAND}" -S "${TEST_SOURCE_DIR}/ext/GKlib" -B "${work}/gklib" ${args}
  -DGKLIB_BUILD_SHARED_LIBS=OFF -DGKLIB_INSTALL=ON -DGKLIB_BUILD_PROGRAMS=OFF
  -DGKLIB_BUILD_TESTING=OFF -DGKLIB_IPO=OFF)
run("${CMAKE_COMMAND}" --build "${work}/gklib" --config Release --parallel 2)
run("${CMAKE_COMMAND}" --install "${work}/gklib" --config Release --prefix "${work}/gklib-sdk")
run("${CMAKE_COMMAND}" -S "${TEST_SOURCE_DIR}" -B "${work}/metis" ${args}
  -DMETIS_BUILD_SHARED_LIBS=ON -DMETIS_INSTALL=ON -DMETIS_BUILD_PROGRAMS=OFF
  -DMETIS_BUILD_TESTING=OFF -DMETIS_IPO=OFF -DMETIS_GKLIB_PROVIDER=SYSTEM
  "-DCMAKE_PREFIX_PATH=${work}/gklib-sdk")
run("${CMAKE_COMMAND}" --build "${work}/metis" --config Release --parallel 2)
run("${CMAKE_COMMAND}" --install "${work}/metis" --config Release --prefix "${work}/installed")
file(RENAME "${work}/installed" "${work}/relocated")

# The relocated consumer is configured with GKlib discovery disabled.
run("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${work}/consumer" ${args}
  "-DCMAKE_PREFIX_PATH=${work}/relocated" -DCMAKE_DISABLE_FIND_PACKAGE_GKlib=TRUE)
run("${CMAKE_COMMAND}" --build "${work}/consumer" --config Release --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${work}/consumer" -C Release --output-on-failure)
message(STATUS "Self-contained shared METIS C/C++ package consumers passed")
