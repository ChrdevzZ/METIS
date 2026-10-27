set(_metis_probe_runner "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.cmake")
set(_metis_probe_fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixture")

# A parent may supply GKlib before dependency resolution. Follow that target's
# actual source directory; installed/imported targets need not ship private helpers.
get_target_property(_metis_probe_gklib_target GKlib::GKlib ALIASED_TARGET)
if(NOT _metis_probe_gklib_target)
  set(_metis_probe_gklib_target GKlib::GKlib)
endif()
get_target_property(_metis_probe_gklib_imported ${_metis_probe_gklib_target} IMPORTED)
set(_metis_probe_gklib_module "")
set(_metis_probe_gklib_status NOT_APPLICABLE)
set(_metis_probe_gklib_reason "imported GKlib target has no paired private source helper")
if(NOT _metis_probe_gklib_imported)
  get_target_property(_metis_probe_gklib_source ${_metis_probe_gklib_target} SOURCE_DIR)
  foreach(_source IN ITEMS "${_metis_probe_gklib_source}" "${_metis_probe_gklib_source}/..")
    if(EXISTS "${_source}/cmake/GKlibCheckLink.cmake")
      file(REAL_PATH "${_source}/cmake/GKlibCheckLink.cmake" _metis_probe_gklib_module)
      break()
    endif()
  endforeach()
  set(_metis_probe_gklib_status UNAVAILABLE)
  set(_metis_probe_gklib_reason "resolved source target does not expose its private check helper")
endif()
if(NOT _metis_probe_gklib_module AND _metis_probe_gklib_imported AND
    METIS_GKLIB_PROVIDER STREQUAL "SOURCE" AND
    METIS_GKLIB_SOURCE_DIR AND
    EXISTS "${METIS_GKLIB_SOURCE_DIR}/cmake/GKlibCheckLink.cmake")
  file(REAL_PATH "${METIS_GKLIB_SOURCE_DIR}/cmake/GKlibCheckLink.cmake"
    _metis_probe_gklib_module)
endif()
if(_metis_probe_gklib_module)
  set(_metis_probe_gklib_status AVAILABLE)
  set(_metis_probe_gklib_reason "helper from the resolved source target or explicitly paired SOURCE directory")
endif()
set(_metis_probe_availability "${CMAKE_CURRENT_BINARY_DIR}/probe-availability.json")
file(WRITE "${_metis_probe_availability}" "[\n")
set(_metis_probe_availability_count 0)


function(_metis_probe_json_string output value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  string(REPLACE "\r" "\\r" value "${value}")
  string(REPLACE "\t" "\\t" value "${value}")
  set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()


# Each test configures one isolated fixture with the selected probe module.
function(metis_add_probe_test name module function test_mode)
  set(_status AVAILABLE)
  set(_reason "METIS-owned helper")
  set(_registered true)
  if(function STREQUAL "gklib_check_link")
    set(_status "${_metis_probe_gklib_status}")
    set(_reason "${_metis_probe_gklib_reason}")
  endif()
  if(NOT module)
    set(_registered false)
  endif()
  set(_separator "")
  if(_metis_probe_availability_count GREATER 0)
    set(_separator ",\n")
  endif()
  _metis_probe_json_string(_module_json "${module}")
  _metis_probe_json_string(_reason_json "${_reason}")
  file(APPEND "${_metis_probe_availability}"
    "${_separator}  {\"name\":\"metis.probe.${name}\",\"status\":\"${_status}\",\"registered\":${_registered},\"module\":${_module_json},\"reason\":${_reason_json}}")
  math(EXPR _count "${_metis_probe_availability_count} + 1")
  set(_metis_probe_availability_count "${_count}" PARENT_SCOPE)
  if(NOT _registered)
    message(STATUS "metis.probe.${name}: ${_status}: ${_reason}")
    return()
  endif()
  set(_runner "${_metis_probe_runner}")
  set(_fixture "${_metis_probe_fixture}")
  set(_work "${CMAKE_CURRENT_BINARY_DIR}/${name}")
  if(test_mode STREQUAL "standard-cache")
    set(_runner "${CMAKE_CURRENT_SOURCE_DIR}/run_standards.cmake")
    set(_fixture "${CMAKE_CURRENT_SOURCE_DIR}/standards")
    # Keep native Windows object paths below the compiler's path limit.
    set(_work "${CMAKE_BINARY_DIR}/CMakeFiles/${function}-std")
  endif()
  set(_metis_probe_inherited_target_type STATIC_LIBRARY)
  if(ARGC GREATER 4)
    set(_metis_probe_inherited_target_type "${ARGV4}")
  endif()
  add_test(NAME "metis.probe.${name}"
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXTURE_SOURCE=${_fixture}"
      "-DWORK_DIR=${_work}"
      "-DMODULE_FILE=${module}"
      "-DPROBE_FUNCTION=${function}"
      "-DTEST_MODE=${test_mode}"
      "-DINHERITED_TARGET_TYPE=${_metis_probe_inherited_target_type}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DTEST_INITIAL_CACHE=${_metis_test_initial_cache}"
      "-DTEST_CONFIG=$<CONFIG>"
      "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DC_COMPILER=${CMAKE_C_COMPILER}"
      "-DC_COMPILER_ID=${CMAKE_C_COMPILER_ID}"
      "-DC_COMPILER_VERSION=${CMAKE_C_COMPILER_VERSION}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      -P "${_runner}")
  set_tests_properties("metis.probe.${name}" PROPERTIES TIMEOUT 180)
endfunction()

# State and reconfigure cases run against both METIS and GKlib implementations.
metis_add_probe_test(metis-state
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link state)
metis_add_probe_test(gklib-state
  "${_metis_probe_gklib_module}"
  gklib_check_link state)
metis_add_probe_test(gklib-cxx-state
  "${_metis_probe_gklib_module}"
  gklib_check_link cxx-state)
metis_add_probe_test(metis-executable-state
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link state EXECUTABLE)
metis_add_probe_test(gklib-executable-state
  "${_metis_probe_gklib_module}"
  gklib_check_link state EXECUTABLE)
metis_add_probe_test(metis-reconfigure
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link reconfigure)
metis_add_probe_test(gklib-reconfigure
  "${_metis_probe_gklib_module}"
  gklib_check_link reconfigure)
metis_add_probe_test(metis-config-reconfigure
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link config-reconfigure)
metis_add_probe_test(gklib-config-reconfigure
  "${_metis_probe_gklib_module}"
  gklib_check_link config-reconfigure)

# Instrumentation cases are registered only for their matching compiler family.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  metis_add_probe_test(metis-gprof-static
    "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
    metis_check_link gprof)
  metis_add_probe_test(gklib-gprof-static
    "${_metis_probe_gklib_module}"
    gklib_check_link gprof)
endif()

if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  metis_add_probe_test(metis-msvc-asan-runtime
    "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
    metis_check_link msvc-asan-runtime)
  metis_add_probe_test(gklib-msvc-asan-runtime
    "${_metis_probe_gklib_module}"
    gklib_check_link msvc-asan-runtime)
endif()

# Environment inputs and effective configuration flags control cached probes.
foreach(_project metis gklib)
  if(_project STREQUAL "metis")
    set(_module "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake")
  else()
    set(_module "${_metis_probe_gklib_module}")
  endif()
  metis_add_probe_test(${_project}-environment
    "${_module}" ${_project}_check_link environment)
  if(MSVC)
    metis_add_probe_test(${_project}-inactive-debug-reconfigure
      "${_module}" ${_project}_check_link inactive-debug-reconfigure)
  endif()
  metis_add_probe_test(${_project}-active-link-flags
    "${_module}" ${_project}_check_link active-link-flags)

  if(MSVC OR CMAKE_C_COMPILER_ID MATCHES "^(AppleClang|Clang)$")
    metis_add_probe_test(${_project}-ignored-option
      "${_module}" ${_project}_check_link ignored-option)
  endif()

  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.29 AND
      CMAKE_C_COMPILER_ID STREQUAL "Clang" AND
      CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "GNU" AND
      DEFINED CMAKE_C_USING_LINKER_SYSTEM)
    metis_add_probe_test(${_project}-custom-linker-cache
      "${_module}" ${_project}_check_link custom-linker-cache)
  endif()
endforeach()

metis_add_probe_test(metis-standard-cache
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link standard-cache)
metis_add_probe_test(gklib-standard-cache
  "${_metis_probe_gklib_module}"
  gklib_check_link standard-cache)
file(APPEND "${_metis_probe_availability}" "\n]\n")
