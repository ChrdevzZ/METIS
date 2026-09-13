# Exercise the final-link interface of an actually instrumented installation.
set(_metis_sanitizer_runtime_dlls "")
if(TARGET METIS::ASanRuntime)
  set(_metis_sanitizer_runtime_dlls
    "$<GENEX_EVAL:$<TARGET_PROPERTY:METIS::ASanRuntime,_ASAN_DLLS>>")
endif()
if(METIS_BUILD_SHARED_LIBS)
  string(APPEND _metis_sanitizer_runtime_dlls
    ";$<TARGET_RUNTIME_DLLS:metis>")
endif()

# Native MSVC asks link.exe to infer its ASan SDK inputs. A shared producer
# resolves those inputs privately, while a static archive passes them to its
# final consumer. The runtime DLL remains a test-environment dependency in
# either case and is discovered beside the selected compiler.
set(_metis_expect_clean_asan_interface FALSE)
set(_metis_sanitizer_deploy_runtime ${METIS_BUILD_SHARED_LIBS})
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "MSVC" AND
    "address" IN_LIST METIS_SANITIZERS)
  set(_metis_sanitizer_deploy_runtime TRUE)

  get_target_property(_metis_sanitizer_link_options metis LINK_OPTIONS)
  if(NOT _metis_sanitizer_link_options MATCHES "INFERASANLIBS")
    message(FATAL_ERROR
      "The native MSVC producer does not infer its AddressSanitizer libraries")
  endif()

  get_target_property(_metis_sanitizer_interface_options metis
    INTERFACE_LINK_OPTIONS)
  if(METIS_BUILD_SHARED_LIBS)
    set(_metis_expect_clean_asan_interface TRUE)
    if(_metis_sanitizer_interface_options MATCHES "INFERASANLIBS")
      message(FATAL_ERROR
        "The native MSVC shared library exports its private ASan SDK option")
    endif()
  elseif(NOT _metis_sanitizer_interface_options MATCHES "INFERASANLIBS")
    message(FATAL_ERROR
      "The native MSVC static archive omits its final-link ASan requirement")
  endif()

  if(NOT _metis_native_asan_runtime_dll)
    message(FATAL_ERROR
      "The selected native MSVC compiler has no unambiguous adjacent ASan "
      "runtime DLL")
  endif()
  string(APPEND _metis_sanitizer_runtime_dlls
    ";${_metis_native_asan_runtime_dll}")
endif()

add_test(NAME metis.sanitizer-package
  COMMAND "${CMAKE_COMMAND}"
    "-DPROJECT_BINARY=${PROJECT_BINARY_DIR}"
    "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/package"
    "-DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
    "-DINITIAL_CACHE=${_metis_test_initial_cache}"
    "-DCONFIG=$<CONFIG>"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
    "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
    "-DINSTANCE=${CMAKE_GENERATOR_INSTANCE}"
    "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
    "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
    "-DDEPLOY_RUNTIME=${_metis_sanitizer_deploy_runtime}"
    "-DRUNTIME_DLLS=${_metis_sanitizer_runtime_dlls}"
    "-DEXPECT_CLEAN_ASAN_INTERFACE=${_metis_expect_clean_asan_interface}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/verify.cmake")
set_tests_properties(metis.sanitizer-package PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
  SKIP_REGULAR_EXPRESSION "ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")


# A normal static archive may be relinked with another ABI-compatible linker.
# Keep this out of IPO builds because their intermediate objects can impose a
# producer-specific final-linker contract that this regression does not test.
function(_metis_sanitizer_target_uses_ipo target output)
  set(enabled FALSE)
  set(configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE}
    Debug Release RelWithDebInfo MinSizeRel)
  list(REMOVE_DUPLICATES configs)
  set(properties INTERPROCEDURAL_OPTIMIZATION)
  foreach(config IN LISTS configs)
    if(NOT config STREQUAL "")
      string(TOUPPER "${config}" upper)
      list(APPEND properties "INTERPROCEDURAL_OPTIMIZATION_${upper}")
    endif()
  endforeach()
  foreach(property IN LISTS properties)
    get_target_property(value ${target} ${property})
    if(value)
      set(enabled TRUE)
    endif()
  endforeach()
  set(${output} "${enabled}" PARENT_SCOPE)
endfunction()

set(_asan_linker_regression TRUE)
foreach(_asan_test_target IN ITEMS metis GKlib)
  if(NOT TARGET ${_asan_test_target})
    set(_asan_linker_regression FALSE)
    continue()
  endif()
  get_target_property(_asan_test_type ${_asan_test_target} TYPE)
  _metis_sanitizer_target_uses_ipo(${_asan_test_target} _asan_test_uses_ipo)
  if(NOT _asan_test_type STREQUAL "STATIC_LIBRARY" OR _asan_test_uses_ipo)
    set(_asan_linker_regression FALSE)
  endif()
endforeach()

if(TARGET METIS::ASanRuntime AND _asan_linker_regression AND WIN32 AND
    CMAKE_VERSION VERSION_GREATER_EQUAL 3.29 AND
    CMAKE_C_COMPILER_ID STREQUAL "Clang" AND
    CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
  find_program(_asan_test_msvc_linker NAMES link.exe NO_CACHE)
  find_program(_asan_test_lld_linker NAMES lld-link.exe NO_CACHE)

  function(_metis_add_sanitizer_linker_test name type command)
    add_test(NAME "metis.sanitizer-package-linker-${name}"
      COMMAND "${CMAKE_COMMAND}"
        "-DPROJECT_BINARY=${PROJECT_BINARY_DIR}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/package-linker-${name}"
        "-DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer"
        "-DINITIAL_CACHE=${_metis_test_initial_cache}"
        "-DCONFIG=$<CONFIG>"
        "-DGENERATOR=${CMAKE_GENERATOR}"
        "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
        "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
        "-DINSTANCE=${CMAKE_GENERATOR_INSTANCE}"
        "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
        "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
        -DDEPLOY_RUNTIME=OFF
        "-DRUNTIME_DLLS=$<GENEX_EVAL:$<TARGET_PROPERTY:METIS::ASanRuntime,_ASAN_DLLS>>"
        "-DLINKER_TYPE=${type}"
        "-DLINKER_COMMAND=${command}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/verify.cmake")
    set_tests_properties("metis.sanitizer-package-linker-${name}" PROPERTIES
      TIMEOUT 180 RUN_SERIAL TRUE
      SKIP_REGULAR_EXPRESSION
        "ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
  endfunction()

  if(_asan_test_msvc_linker)
    _metis_add_sanitizer_linker_test(msvc MSVC
      "${_asan_test_msvc_linker}")
  endif()
  if(_asan_test_lld_linker)
    _metis_add_sanitizer_linker_test(custom-lld asan_lld
      "${_asan_test_lld_linker}")
  endif()
endif()


# A parent may enable C++ only after this C-only project returns. The deferred
# ASan interface must observe that final language set at the outer directory.
set(_metis_late_gklib_source "")
if(METIS_GKLIB_SOURCE_DIR)
  cmake_path(ABSOLUTE_PATH METIS_GKLIB_SOURCE_DIR
    BASE_DIRECTORY "${PROJECT_SOURCE_DIR}" NORMALIZE
    OUTPUT_VARIABLE _metis_late_gklib_candidate)
  if(EXISTS "${_metis_late_gklib_candidate}/CMakeLists.txt")
    set(_metis_late_gklib_source "${_metis_late_gklib_candidate}")
  endif()
endif()
if(NOT _metis_late_gklib_source AND
    EXISTS "${PROJECT_SOURCE_DIR}/ext/GKlib/CMakeLists.txt")
  set(_metis_late_gklib_source "${PROJECT_SOURCE_DIR}/ext/GKlib")
endif()

if(TARGET METIS::ASanRuntime AND _metis_late_gklib_source AND WIN32 AND
    CMAKE_VERSION VERSION_GREATER_EQUAL 4.2 AND
    CMAKE_C_COMPILER_ID STREQUAL "Clang" AND
    CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" AND
    CMAKE_C_COMPILER_LINKER_ID STREQUAL "LLD")
  add_test(NAME metis.sanitizer-late-cxx
    COMMAND "${CMAKE_COMMAND}"
      "-DFIXTURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/late-language"
      "-DPROJECT_SOURCE=${PROJECT_SOURCE_DIR}"
      "-DDEPENDENCY_SOURCE=${_metis_late_gklib_source}"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/late-cxx"
      "-DINITIAL_CACHE=${_metis_test_initial_cache}"
      "-DCONFIG=$<CONFIG>"
      "-DGENERATOR=${CMAKE_GENERATOR}"
      "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
      "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
      "-DINSTANCE=${CMAKE_GENERATOR_INSTANCE}"
      "-DCROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/late-language/verify.cmake")
  set_tests_properties(metis.sanitizer-late-cxx PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE
    SKIP_REGULAR_EXPRESSION
      "ASAN_RUNTIME_SKIPPED: no cross-compiling emulator")
endif()


# Lookup policy uses language-free nested projects and an explicit runtime SDK.
# Ninja is optional here; ordinary package consumption keeps its own generator.
find_program(_asan_test_ninja NAMES ninja ninja-build)
if(TARGET METIS::ASanRuntime AND _asan_test_ninja)
  set(record_config "$<IF:$<BOOL:$<CONFIG>>,$<UPPER_CASE:$<CONFIG>>,NOCONFIG>")
  add_test(NAME metis.sanitizer-lookup
    COMMAND "${CMAKE_COMMAND}"
      "-DLOOKUP_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/lookup"
      "-DMODULE_FILE=${PROJECT_SOURCE_DIR}/cmake/WindowsASan.cmake"
      "-DNAMESPACE=METIS"
      "-DPRODUCER_RECORD=${PROJECT_BINARY_DIR}/cmake/asan/METISASan-${record_config}.cmake"
      "-DPRODUCER_ARCHIVE=$<TARGET_LINKER_FILE:metis>"
      "-DRUNTIME_ARCHIVES=$<TARGET_PROPERTY:METIS::ASanRuntime,_ASAN_PATHS_${record_config}>"
      "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/lookup-policy"
      "-DGENERATOR=Ninja"
      "-DMULTI_CONFIG_GENERATOR=Ninja Multi-Config"
      "-DMAKE_PROGRAM=${_asan_test_ninja}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/lookup/run.cmake")
  set_tests_properties(metis.sanitizer-lookup PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE)
endif()
