set(_metis_probe_runner "${CMAKE_CURRENT_SOURCE_DIR}/run_fixture.cmake")
set(_metis_probe_fixture "${CMAKE_CURRENT_SOURCE_DIR}/fixture")

# Each test configures one isolated fixture with the selected probe module.
function(metis_add_probe_test name module function test_mode)
  set(_metis_probe_inherited_target_type STATIC_LIBRARY)
  if(ARGC GREATER 4)
    set(_metis_probe_inherited_target_type "${ARGV4}")
  endif()
  add_test(NAME "metis.probe.${name}"
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXTURE_SOURCE=${_metis_probe_fixture}"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/${name}"
      "-DMODULE_FILE=${module}"
      "-DPROBE_FUNCTION=${function}"
      "-DTEST_MODE=${test_mode}"
      "-DINHERITED_TARGET_TYPE=${_metis_probe_inherited_target_type}"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DGENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DGENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DMAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
      "-DC_COMPILER=${CMAKE_C_COMPILER}"
      "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
      -P "${_metis_probe_runner}")
  set_tests_properties("metis.probe.${name}" PROPERTIES TIMEOUT 120)
endfunction()

# State and reconfigure cases run against both METIS and GKlib implementations.
metis_add_probe_test(metis-state
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link state)
metis_add_probe_test(gklib-state
  "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
  gklib_check_link state)
metis_add_probe_test(gklib-cxx-state
  "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
  gklib_check_link cxx-state)
metis_add_probe_test(metis-executable-state
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link state EXECUTABLE)
metis_add_probe_test(gklib-executable-state
  "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
  gklib_check_link state EXECUTABLE)
metis_add_probe_test(metis-reconfigure
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link reconfigure)
metis_add_probe_test(gklib-reconfigure
  "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
  gklib_check_link reconfigure)
metis_add_probe_test(metis-config-reconfigure
  "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
  metis_check_link config-reconfigure)
metis_add_probe_test(gklib-config-reconfigure
  "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
  gklib_check_link config-reconfigure)

# Instrumentation cases are registered only for their matching compiler family.
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  metis_add_probe_test(metis-gprof-static
    "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
    metis_check_link gprof)
  metis_add_probe_test(gklib-gprof-static
    "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
    gklib_check_link gprof)
endif()

if(MSVC AND CMAKE_C_COMPILER_ID STREQUAL "MSVC")
  metis_add_probe_test(metis-msvc-asan-runtime
    "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake"
    metis_check_link msvc-asan-runtime)
  metis_add_probe_test(gklib-msvc-asan-runtime
    "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake"
    gklib_check_link msvc-asan-runtime)
endif()

# Environment inputs and default Debug flags must invalidate cached probes.
foreach(_project metis gklib)
  if(_project STREQUAL "metis")
    set(_module "${PROJECT_SOURCE_DIR}/cmake/MetisCheckLink.cmake")
  else()
    set(_module "${PROJECT_SOURCE_DIR}/ext/GKlib/cmake/GKlibCheckLink.cmake")
  endif()
  metis_add_probe_test(${_project}-environment
    "${_module}" ${_project}_check_link environment)
  if(MSVC)
    metis_add_probe_test(${_project}-default-debug-reconfigure
      "${_module}" ${_project}_check_link default-debug-reconfigure)
  endif()
endforeach()
