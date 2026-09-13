cmake_minimum_required(VERSION 3.24)

project(WindowsASanLookup NONE)


foreach(required MODULE_FILE NAMESPACE PRODUCER_RECORD PRODUCER_ARCHIVE
    COMPILER_RUNTIME_ROOT SCENARIO)
  if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
    message(FATAL_ERROR "The lookup fixture requires ${required}")
  endif()
endforeach()

foreach(path MODULE_FILE PRODUCER_RECORD PRODUCER_ARCHIVE
    COMPILER_RUNTIME_ROOT)
  if(NOT EXISTS "${${path}}")
    message(FATAL_ERROR "${path} does not exist: ${${path}}")
  endif()
endforeach()

include("${MODULE_FILE}")

# Lookup tests supply one explicit compiler SDK. Disable ambient SDK discovery
# so a negative case cannot pass because of the developer machine's PATH, LIB,
# compiler installation, system prefix, or previous find result.
set(CMAKE_C_IMPLICIT_LINK_DIRECTORIES "")
set(CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES "")
set(CMAKE_PREFIX_PATH "")
set(CMAKE_LIBRARY_PATH "")
set(CMAKE_SYSTEM_LIBRARY_PATH "")
set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
set(CMAKE_FIND_USE_INSTALL_PREFIX FALSE)
set(CMAKE_FIND_USE_PACKAGE_ROOT_PATH FALSE)
set(ENV{LIB} "")

file(REAL_PATH "${PRODUCER_ARCHIVE}" _lookup_archive)
file(REAL_PATH "${COMPILER_RUNTIME_ROOT}" _lookup_runtime_root)
set(CompilerRuntime_ROOT "${_lookup_runtime_root}")
include("${PRODUCER_RECORD}")
set(_lookup_runtime_names "${_asan_names}")
set(_lookup_runtime_whole "${_asan_whole}")
set(_lookup_runtime_symbols "${_asan_symbols}")
if(NOT _lookup_runtime_names)
  message(FATAL_ERROR "The producer ASan record contains no runtime names")
endif()

set(_lookup_target "${NAMESPACE}::lookup_archive")
add_library("${_lookup_target}" STATIC IMPORTED GLOBAL)

# A caller cache entry with the same generic name must neither redirect the
# ASan lookup nor be overwritten by its temporary find_library variable.
set(_asan_library "${_lookup_archive}" CACHE FILEPATH
  "Unrelated parent cache entry used by the lookup regression" FORCE)
set(_lookup_parent_cache "${_asan_library}")


function(_lookup_write_record directory suffix)
  file(MAKE_DIRECTORY "${directory}")
  configure_file("${PRODUCER_RECORD}"
    "${directory}/${NAMESPACE}ASan-${suffix}.cmake" COPYONLY)
endfunction()


function(_lookup_assert_runtime active_config prohibited_config)
  if(NOT TARGET "${NAMESPACE}::ASanRuntime")
    message(FATAL_ERROR "A successful lookup did not publish ${NAMESPACE}::ASanRuntime")
  endif()

  get_target_property(links "${NAMESPACE}::ASanRuntime"
    INTERFACE_LINK_LIBRARIES)
  get_target_property(options "${NAMESPACE}::ASanRuntime"
    INTERFACE_LINK_OPTIONS)
  string(FIND "${links}" "$<CONFIG:${active_config}>" active_index)
  string(FIND "${options}" "$<CONFIG:${active_config}>" option_index)
  if(active_index EQUAL -1 OR option_index EQUAL -1)
    message(FATAL_ERROR
      "ASanRuntime has incomplete policy for active configuration ${active_config}:\nlinks=${links}\noptions=${options}")
  endif()
  if(prohibited_config)
    string(FIND "${links}" "$<CONFIG:${prohibited_config}>"
      prohibited_index)
    if(NOT prohibited_index EQUAL -1)
      message(FATAL_ERROR
        "Inactive configuration ${prohibited_config} entered ASanRuntime: ${links}")
    endif()
    string(FIND "${options}" "$<CONFIG:${prohibited_config}>"
      prohibited_index)
    if(NOT prohibited_index EQUAL -1)
      message(FATAL_ERROR
        "Inactive configuration ${prohibited_config} entered ASanRuntime options: ${options}")
    endif()
  endif()

  foreach(name IN LISTS _lookup_runtime_names)
    set(expected "${_lookup_runtime_root}/${name}")
    if(NOT EXISTS "${expected}")
      set(expected "${_lookup_runtime_root}/lib/windows/${name}")
    endif()
    cmake_path(NORMAL_PATH expected)
    if(NOT EXISTS "${expected}")
      message(FATAL_ERROR "The selected SDK lacks the expected runtime: ${expected}")
    endif()

    string(SHA256 id "${NAMESPACE};${expected}")
    set(part "${NAMESPACE}::ASan_${id}")
    if(NOT TARGET "${part}")
      message(FATAL_ERROR "The runtime target was not created for ${expected}")
    endif()
    if(name IN_LIST _lookup_runtime_whole)
      string(FIND "${links}" "$<LINK_LIBRARY:WHOLE_ARCHIVE,${part}>"
        dependency_index)
    else()
      string(FIND "${links}" "${part}" dependency_index)
    endif()
    if(dependency_index EQUAL -1)
      message(FATAL_ERROR
        "ASanRuntime did not preserve the producer link policy for ${name}: ${links}")
    endif()
    get_target_property(type "${part}" TYPE)
    if(type STREQUAL "SHARED_LIBRARY")
      get_target_property(actual "${part}" IMPORTED_IMPLIB)
    else()
      get_target_property(actual "${part}" IMPORTED_LOCATION)
    endif()
    cmake_path(NORMAL_PATH actual)
    if(NOT actual STREQUAL expected)
      message(FATAL_ERROR
        "The runtime target resolved outside the selected SDK:\nexpected=${expected}\nactual=${actual}")
    endif()
  endforeach()

  foreach(symbol IN LISTS _lookup_runtime_symbols)
    string(FIND "${options}" "LINKER:/INCLUDE:${symbol}" symbol_index)
    if(symbol_index EQUAL -1)
      message(FATAL_ERROR
        "ASanRuntime did not preserve required symbol ${symbol}: ${options}")
    endif()
  endforeach()
  string(FIND "${options}" "LINKER:/INCREMENTAL:NO" incremental_index)
  if(incremental_index EQUAL -1)
    message(FATAL_ERROR
      "ASanRuntime omitted the non-incremental link requirement: ${options}")
  endif()

  if(NOT _asan_library STREQUAL _lookup_parent_cache)
    message(FATAL_ERROR
      "ASan lookup changed the parent _asan_library cache entry")
  endif()
endfunction()


set(record_directory "${CMAKE_CURRENT_BINARY_DIR}/records")
set(${NAMESPACE}_FOUND TRUE)

if(SCENARIO STREQUAL "multi-release")
  if(NOT CMAKE_CONFIGURATION_TYPES STREQUAL "Release" OR
      NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR
      "multi-release requires Release as the only generated configuration and Debug as the inactive build type")
  endif()

  set_property(TARGET "${_lookup_target}" PROPERTY
    IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
  set_property(TARGET "${_lookup_target}" PROPERTY
    IMPORTED_LOCATION_RELWITHDEBINFO "${_lookup_archive}")
  set_property(TARGET "${_lookup_target}" PROPERTY
    MAP_IMPORTED_CONFIG_RELEASE RelWithDebInfo)
  _lookup_write_record("${record_directory}" RELWITHDEBINFO)

  windows_asan_import("${_lookup_target}" "${NAMESPACE}"
    "${record_directory}")
  if(NOT ${NAMESPACE}_FOUND)
    message(FATAL_ERROR
      "Release-only multi-config lookup failed: ${${NAMESPACE}_NOT_FOUND_MESSAGE}")
  endif()
  _lookup_assert_runtime(Release Debug)

elseif(SCENARIO STREQUAL "configless")
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(FATAL_ERROR "configless requires the Debug consumer configuration")
  endif()

  set_property(TARGET "${_lookup_target}" PROPERTY
    IMPORTED_LOCATION "${_lookup_archive}")
  set_property(TARGET "${_lookup_target}" PROPERTY
    MAP_IMPORTED_CONFIG_DEBUG "")
  _lookup_write_record("${record_directory}" NOCONFIG)

  windows_asan_import("${_lookup_target}" "${NAMESPACE}"
    "${record_directory}")
  if(NOT ${NAMESPACE}_FOUND)
    message(FATAL_ERROR
      "Configless lookup failed: ${${NAMESPACE}_NOT_FOUND_MESSAGE}")
  endif()
  _lookup_assert_runtime(Debug "")

elseif(SCENARIO STREQUAL "retry")
  if(NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    message(FATAL_ERROR "retry requires the RelWithDebInfo configuration")
  endif()

  set_property(TARGET "${_lookup_target}" PROPERTY
    IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
  set_property(TARGET "${_lookup_target}" PROPERTY
    IMPORTED_LOCATION_RELWITHDEBINFO "${_lookup_archive}")
  _lookup_write_record("${record_directory}" RELWITHDEBINFO)

  set(CompilerRuntime_ROOT "${CMAKE_CURRENT_BINARY_DIR}/missing-sdk")
  file(MAKE_DIRECTORY "${CompilerRuntime_ROOT}")
  windows_asan_import("${_lookup_target}" "${NAMESPACE}"
    "${record_directory}")

  list(GET _lookup_runtime_names 0 first_runtime)
  set(expected_message
    "${NAMESPACE} RELWITHDEBINFO requires AddressSanitizer runtime ${first_runtime}; set CompilerRuntime_ROOT or CMAKE_PREFIX_PATH to the producer's compatible compiler runtime SDK.")
  if(${NAMESPACE}_FOUND OR TARGET "${NAMESPACE}::ASanRuntime" OR
      NOT "${${NAMESPACE}_NOT_FOUND_MESSAGE}" STREQUAL "${expected_message}")
    message(FATAL_ERROR
      "Missing-SDK lookup did not fail atomically with the expected diagnostic:\n${${NAMESPACE}_NOT_FOUND_MESSAGE}")
  endif()

  get_target_property(owner "${_lookup_target}"
    _WINDOWS_ASAN_RECORD_DIRECTORY)
  if(NOT owner STREQUAL record_directory)
    message(FATAL_ERROR
      "A failed lookup did not retain its package record directory")
  endif()
  if(NOT _asan_library STREQUAL _lookup_parent_cache)
    message(FATAL_ERROR
      "A failed lookup changed the parent _asan_library cache entry")
  endif()

  set(CompilerRuntime_ROOT "${_lookup_runtime_root}")
  set(${NAMESPACE}_FOUND TRUE)
  unset(${NAMESPACE}_NOT_FOUND_MESSAGE)
  windows_asan_import("${_lookup_target}" "${NAMESPACE}"
    "${record_directory}")
  if(NOT ${NAMESPACE}_FOUND)
    message(FATAL_ERROR
      "A same-prefix retry did not recover: ${${NAMESPACE}_NOT_FOUND_MESSAGE}")
  endif()
  _lookup_assert_runtime(RelWithDebInfo "")
  get_target_property(original_links "${NAMESPACE}::ASanRuntime"
    INTERFACE_LINK_LIBRARIES)

  set(alternate_directory "${CMAKE_CURRENT_BINARY_DIR}/alternate-records")
  _lookup_write_record("${alternate_directory}" RELWITHDEBINFO)
  set(${NAMESPACE}_FOUND TRUE)
  unset(${NAMESPACE}_NOT_FOUND_MESSAGE)
  windows_asan_import("${_lookup_target}" "${NAMESPACE}"
    "${alternate_directory}")
  set(expected_message
    "${_lookup_target} already belongs to ${record_directory}; cannot use ASan records from ${alternate_directory}.")
  if(${NAMESPACE}_FOUND OR
      NOT "${${NAMESPACE}_NOT_FOUND_MESSAGE}" STREQUAL "${expected_message}")
    message(FATAL_ERROR
      "An alternate-prefix retry did not fail with the expected diagnostic:\n${${NAMESPACE}_NOT_FOUND_MESSAGE}")
  endif()
  get_target_property(final_links "${NAMESPACE}::ASanRuntime"
    INTERFACE_LINK_LIBRARIES)
  if(NOT final_links STREQUAL original_links)
    message(FATAL_ERROR
      "An alternate-prefix retry changed the existing runtime target")
  endif()

else()
  message(FATAL_ERROR "Unknown lookup fixture scenario: ${SCENARIO}")
endif()

message(STATUS "Windows ASan lookup policy passed for ${NAMESPACE}/${SCENARIO}")
