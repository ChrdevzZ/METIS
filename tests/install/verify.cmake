cmake_minimum_required(VERSION 3.24)

# Refuse cleanup unless the path and marker identify this exact fixture.
foreach(required TEST_SOURCE_DIR TEST_PROJECT WORK_ROOT GENERATOR)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REAL_PATH "${TEST_SOURCE_DIR}" source)
cmake_path(ABSOLUTE_PATH WORK_ROOT NORMALIZE OUTPUT_VARIABLE work)
if(work STREQUAL source OR NOT work MATCHES "/install-ownership$")
  message(FATAL_ERROR "Unsafe install test directory: ${work}")
endif()
if(EXISTS "${work}")
  if(NOT EXISTS "${work}/.ownership-test")
    message(FATAL_ERROR "Existing directory is not owned by this test")
  endif()
  foreach(name producer first second)
    set(target "${work}/${name}")
    cmake_path(IS_PREFIX work "${target}" NORMALIZE inside)
    if(NOT inside)
      message(FATAL_ERROR "Test directory escaped its root")
    endif()
    file(REMOVE_RECURSE "${target}")
  endforeach()
endif()
file(MAKE_DIRECTORY "${work}")
file(WRITE "${work}/.ownership-test" "Install ownership regression\n")

# Helpers retain subprocess output for success and expected-failure assertions.
function(run)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "Command failed: ${ARGN}\n${output}\n${error}")
  endif()
endfunction()

function(reject)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result
    OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "Command unexpectedly succeeded: ${ARGN}")
  endif()
endfunction()

# Forward the active generator and toolchain into the nested producer.
set(args -G "${GENERATOR}" "-DBUILD_SHARED_LIBS=${TEST_SHARED}"
  -DCMAKE_INSTALL_LIBDIR=custom/lib "-DTEST_SOURCE_DIR=${source}"
  "-DTEST_PROJECT=${TEST_PROJECT}" -DCMAKE_BUILD_TYPE=Debug)
foreach(variable CMAKE_MAKE_PROGRAM CMAKE_C_COMPILER CMAKE_TOOLCHAIN_FILE)
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

set(build "${work}/producer")
set(projects GKlib)
set(GKlib_script "${build}/dependency/cmake/uninstall.cmake")
if(TEST_PROJECT STREQUAL "METIS")
  list(PREPEND projects METIS)
  set(METIS_script "${build}/dependency/MetisUninstall.cmake")
  set(GKlib_script "${build}/dependency/ext/GKlib/cmake/uninstall.cmake")
endif()

run("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${build}" ${args})

# Merge Debug, Release and component installs into ownership records.
foreach(config Debug Release)
  run("${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${build}" ${args}
    "-DCMAKE_BUILD_TYPE=${config}")
  run("${CMAKE_COMMAND}" --build "${build}" --config ${config} --parallel 2)
  run("${CMAKE_COMMAND}" --install "${build}" --config ${config}
    --prefix "${work}/first")
endforeach()
# Component installs must contribute their actual paths as well.
run("${CMAKE_COMMAND}" --install "${build}" --config Release
  --prefix "${work}/first" --component GKlib_Development)
run("${CMAKE_COMMAND}" --install "${build}" --config Release
  --prefix "${work}/second")

# Uninstall requires a prefix and is idempotent for each selected prefix.
foreach(project IN LISTS projects)
  reject("${CMAKE_COMMAND}" -P "${${project}_script}")
endforeach()
foreach(prefix first second)
  foreach(project IN LISTS projects)
    string(TOUPPER "${project}" option_project)
    run("${CMAKE_COMMAND}" "-D${option_project}_UNINSTALL_PREFIX=${work}/${prefix}"
      -P "${${project}_script}")
    run("${CMAKE_COMMAND}" "-D${option_project}_UNINSTALL_PREFIX=${work}/${prefix}"
      -P "${${project}_script}")
  endforeach()
  foreach(name metis.notes strings.txt GKlib.h)
    if(NOT EXISTS "${work}/${prefix}/share/parent/${name}")
      message(FATAL_ERROR "Uninstall removed parent file ${name}")
    endif()
  endforeach()
  file(GLOB_RECURSE leftovers LIST_DIRECTORIES FALSE "${work}/${prefix}/*")
  foreach(file IN LISTS leftovers)
    if(NOT file MATCHES "/share/parent/(metis[.]notes|strings[.]txt|GKlib[.]h)$")
      message(FATAL_ERROR "Uninstall left a project file: ${file}")
    endif()
  endforeach()
endforeach()

# A syntactically valid record must not authorize out-of-prefix removal.
set(record_dir "${build}/dependency/install-manifests")
file(GLOB records "${record_dir}/*.json")
list(GET records 0 record)
file(READ "${record}" original_record)
string(JSON record_prefix GET "${original_record}" prefix)
set(outside "${work}/outside-sentinel.txt")
file(WRITE "${outside}" "Keep this unrelated file\n")
string(JSON corrupt_record SET "${original_record}" files "[]")
string(JSON corrupt_record SET "${corrupt_record}" files 0 "\"${outside}\"")
file(WRITE "${record}" "${corrupt_record}")
list(GET projects 0 project)
string(TOUPPER "${project}" option_project)
reject("${CMAKE_COMMAND}" "-D${option_project}_UNINSTALL_PREFIX=${record_prefix}"
  -P "${${project}_script}")
if(NOT EXISTS "${outside}")
  message(FATAL_ERROR "Uninstall removed a file outside its recorded prefix")
endif()

# On Unix, directory links must not permit traversal beyond the install root.
if(UNIX)
  file(CREATE_LINK "${work}" "${record_prefix}/escape" SYMBOLIC RESULT link_result)
  if(NOT link_result STREQUAL "0")
    message(FATAL_ERROR "Unable to create directory-link regression: ${link_result}")
  endif()
  string(JSON corrupt_record SET "${original_record}" files "[]")
  string(JSON corrupt_record SET "${corrupt_record}" files 0
    "\"${record_prefix}/escape/outside-sentinel.txt\"")
  file(WRITE "${record}" "${corrupt_record}")
  reject("${CMAKE_COMMAND}" "-D${option_project}_UNINSTALL_PREFIX=${record_prefix}"
    -P "${${project}_script}")
  if(NOT EXISTS "${outside}")
    message(FATAL_ERROR "Uninstall followed a directory link outside its prefix")
  endif()
  file(REMOVE "${record_prefix}/escape")
endif()

file(WRITE "${record}" "${original_record}")

# Invalid records must be rejected, including on an already uninstalled prefix.
set(record_dir "${build}/dependency/install-manifests")
file(WRITE "${record_dir}/invalid.json" "not JSON")
list(GET projects 0 project)
string(TOUPPER "${project}" option_project)
reject("${CMAKE_COMMAND}" "-D${option_project}_UNINSTALL_PREFIX=${work}/first"
  -P "${${project}_script}")
file(REMOVE "${record_dir}/invalid.json")
message(STATUS "Install ownership, postfix, components, prefixes and repeat uninstall passed")
