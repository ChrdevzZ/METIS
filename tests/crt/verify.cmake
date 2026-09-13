if(NOT DEFINED WORK_DIR OR NOT DEFINED GENERATOR OR
    NOT DEFINED MODULE_FILE OR NOT DEFINED CHECK_FUNCTION OR
    NOT DEFINED FIXTURE_PROJECT)
  message(FATAL_ERROR "The CRT fixture runner is missing required inputs")
endif()

cmake_path(ABSOLUTE_PATH WORK_DIR NORMALIZE OUTPUT_VARIABLE WORK_DIR)
file(REAL_PATH "${CMAKE_CURRENT_LIST_DIR}" fixture_source)
cmake_path(IS_PREFIX WORK_DIR "${fixture_source}" NORMALIZE work_contains_source)
if(work_contains_source)
  message(FATAL_ERROR "The CRT work directory must not contain source files")
endif()
set(work_marker "${WORK_DIR}/.crt-fixture-owned")
if(EXISTS "${WORK_DIR}" AND NOT EXISTS "${work_marker}")
  message(FATAL_ERROR
    "Refusing to clean a CRT work directory without its ownership marker")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}")
file(WRITE "${work_marker}" "Fixture-owned CRT regression directory.\n")

set(configure_command
  "${CMAKE_COMMAND}"
  -S "${CMAKE_CURRENT_LIST_DIR}"
  -G "${GENERATOR}")
if(DEFINED TEST_INITIAL_CACHE AND NOT TEST_INITIAL_CACHE STREQUAL "")
  list(APPEND configure_command -C "${TEST_INITIAL_CACHE}")
endif()
if(GENERATOR_INSTANCE)
  list(APPEND configure_command
    "-DCMAKE_GENERATOR_INSTANCE=${GENERATOR_INSTANCE}")
endif()
if(GENERATOR_PLATFORM)
  list(APPEND configure_command -A "${GENERATOR_PLATFORM}")
endif()
if(GENERATOR_TOOLSET)
  list(APPEND configure_command -T "${GENERATOR_TOOLSET}")
endif()
if(CMAKE_MAKE_PROGRAM)
  list(APPEND configure_command "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
if(CMAKE_C_COMPILER)
  list(APPEND configure_command "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
endif()
if(CMAKE_TOOLCHAIN_FILE)
  list(APPEND configure_command "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
endif()
list(APPEND configure_command
  "-DMODULE_FILE=${MODULE_FILE}"
  "-DCHECK_FUNCTION=${CHECK_FUNCTION}"
  "-DFIXTURE_PROJECT=${FIXTURE_PROJECT}")

function(run_case name expectation target_kind gklib_kind programs runtime flags
    reason_prefix)
  set(build_dir "${WORK_DIR}/${name}")
  if(name MATCHES "-release$")
    set(config Release)
  else()
    set(config Debug)
  endif()
  file(REMOVE_RECURSE "${build_dir}")
  execute_process(
    COMMAND ${configure_command}
      -B "${build_dir}"
      "-DTARGET_KIND=${target_kind}"
      "-DGKLIB_KIND=${gklib_kind}"
      "-DBUILD_PROGRAMS=${programs}"
      "-DRUNTIME=${runtime}"
      "-DCMAKE_C_FLAGS=${flags}"
      "-DCMAKE_BUILD_TYPE=${config}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr)
  set(output "${stdout}\n${stderr}")

  if(expectation STREQUAL "PASS")
    if(NOT result EQUAL 0 OR NOT EXISTS "${build_dir}/configured.txt")
      message(FATAL_ERROR
        "CRT positive case '${name}' failed:\n${output}")
    endif()
  elseif(expectation STREQUAL "FAIL")
    if(result EQUAL 0)
      message(FATAL_ERROR
        "CRT negative case '${name}' unexpectedly configured")
    endif()
    string(TOLOWER "${output}" output_lower)
    if(NOT output_lower MATCHES "dll crt")
      message(FATAL_ERROR
        "CRT negative case '${name}' lacked the exact 'DLL CRT' diagnosis:\n${output}")
    endif()
    if(NOT output MATCHES "${reason_prefix}")
      message(FATAL_ERROR
        "CRT negative case '${name}' lacked reason prefix '${reason_prefix}':\n${output}")
    endif()
  else()
    message(FATAL_ERROR "Unknown CRT fixture expectation: ${expectation}")
  endif()
endfunction()

set(dynamic_expression "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
set(static_expression "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(FIXTURE_PROJECT STREQUAL "GKLIB")
  set(reason "Shared GKlib")
  run_case(md PASS SHARED STATIC OFF MultiThreadedDLL "" "${reason}")
  run_case(mdd PASS SHARED STATIC OFF MultiThreadedDebugDLL "" "${reason}")
  run_case(dynamic-expression PASS SHARED STATIC OFF
    "${dynamic_expression}" "" "${reason}")
  run_case(dynamic-expression-release PASS SHARED STATIC OFF
    "${dynamic_expression}" "" "${reason}")
  run_case(static-target-mt PASS STATIC STATIC ON MultiThreaded "" "${reason}")

  run_case(mt FAIL SHARED STATIC OFF MultiThreaded "" "${reason}")
  run_case(mtd FAIL SHARED STATIC OFF MultiThreadedDebug "" "${reason}")
  run_case(static-expression FAIL SHARED STATIC OFF
    "${static_expression}" "" "${reason}")
  run_case(static-expression-release FAIL SHARED STATIC OFF
    "${static_expression}" "" "${reason}")
  run_case(empty-property-flags-mt FAIL SHARED STATIC OFF "" "/MT"
    "${reason}")
elseif(FIXTURE_PROJECT STREQUAL "METIS")
  # Exercise the shared-GKlib and shared-METIS-application reasons separately.
  set(gklib_reason "Shared GKlib")
  set(app_reason "Shared METIS applications")
  run_case(shared-gklib-md PASS STATIC SHARED OFF MultiThreadedDLL ""
    "${gklib_reason}")
  run_case(shared-gklib-mtd FAIL STATIC SHARED OFF MultiThreadedDebug ""
    "${gklib_reason}")

  run_case(apps-md PASS SHARED STATIC ON MultiThreadedDLL "" "${app_reason}")
  run_case(apps-mdd PASS SHARED STATIC ON MultiThreadedDebugDLL ""
    "${app_reason}")
  run_case(apps-dynamic-expression PASS SHARED STATIC ON
    "${dynamic_expression}" "" "${app_reason}")
  run_case(apps-dynamic-expression-release PASS SHARED STATIC ON
    "${dynamic_expression}" "" "${app_reason}")
  run_case(static-both-mt PASS STATIC STATIC ON MultiThreaded "" "${app_reason}")
  run_case(shared-library-only-mt PASS SHARED STATIC OFF MultiThreaded ""
    "${app_reason}")

  run_case(apps-mt FAIL SHARED STATIC ON MultiThreaded "" "${app_reason}")
  run_case(apps-mtd FAIL SHARED STATIC ON MultiThreadedDebug "" "${app_reason}")
  run_case(apps-static-expression FAIL SHARED STATIC ON
    "${static_expression}" "" "${app_reason}")
  run_case(apps-static-expression-release FAIL SHARED STATIC ON
    "${static_expression}" "" "${app_reason}")
  run_case(apps-empty-property-flags-mt FAIL SHARED STATIC ON "" "/MT"
    "${app_reason}")
else()
  message(FATAL_ERROR "Unknown CRT fixture project: ${FIXTURE_PROJECT}")
endif()
