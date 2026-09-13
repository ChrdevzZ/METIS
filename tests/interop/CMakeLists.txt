cmake_minimum_required(VERSION 3.24)
project(MetisInterop LANGUAGES NONE)

set(INTEROP_MAIN_LANGUAGE C CACHE STRING
  "Language used by the interop test executable: C, CXX, or Fortran")
set_property(CACHE INTEROP_MAIN_LANGUAGE PROPERTY STRINGS C CXX Fortran)

# Add interoperable Fortran calls with the caller's selected Fortran compiler.
# OFF keeps C/C++-only consumer checks independent of a Fortran installation.
option(INTEROP_FORTRAN "Enable the Fortran BIND(C) interop checks" OFF)

set(interop_main_languages C CXX Fortran)
if(NOT INTEROP_MAIN_LANGUAGE IN_LIST interop_main_languages)
  message(FATAL_ERROR
    "INTEROP_MAIN_LANGUAGE must be one of: C, CXX, Fortran")
endif()
if(INTEROP_MAIN_LANGUAGE STREQUAL "Fortran" AND NOT INTEROP_FORTRAN)
  message(FATAL_ERROR
    "INTEROP_MAIN_LANGUAGE=Fortran requires INTEROP_FORTRAN=ON")
endif()

if(INTEROP_MAIN_LANGUAGE STREQUAL "Fortran")
  enable_language(Fortran)
else()
  enable_language(${INTEROP_MAIN_LANGUAGE})
  if(INTEROP_FORTRAN)
    enable_language(Fortran)
  endif()
endif()

find_package(METIS CONFIG REQUIRED)

foreach(width_variable METIS_IDXTYPEWIDTH METIS_REALTYPEWIDTH)
  if(NOT DEFINED ${width_variable} OR
      NOT "${${width_variable}}" MATCHES "^(32|64)$")
    message(FATAL_ERROR
      "The installed METIS package must define ${width_variable} as 32 or 64")
  endif()
endforeach()

if(INTEROP_MAIN_LANGUAGE STREQUAL "C")
  set(interop_main_source main.c)
elseif(INTEROP_MAIN_LANGUAGE STREQUAL "CXX")
  set(interop_main_source main.cpp)
else()
  set(interop_main_source main.f90)
endif()

add_executable(metis_interop ${interop_main_source})
# Let the installed target restore its actual dependency closure. A completed
# self-contained shared METIS package must not require a separate GKlib SDK.
target_link_libraries(metis_interop PRIVATE METIS::metis)

if(INTEROP_MAIN_LANGUAGE STREQUAL "C")
  target_compile_features(metis_interop PRIVATE c_std_99)
elseif(INTEROP_MAIN_LANGUAGE STREQUAL "CXX")
  target_compile_features(metis_interop PRIVATE cxx_std_11)
endif()

if(INTEROP_FORTRAN)
  if(METIS_IDXTYPEWIDTH EQUAL 32)
    set(METIS_FORTRAN_IDX_KIND c_int32_t)
  else()
    set(METIS_FORTRAN_IDX_KIND c_int64_t)
  endif()
  if(METIS_REALTYPEWIDTH EQUAL 32)
    set(METIS_FORTRAN_REAL_KIND c_float)
  else()
    set(METIS_FORTRAN_REAL_KIND c_double)
  endif()

  configure_file(metis_kinds.f90.in
    "${CMAKE_CURRENT_BINARY_DIR}/metis_kinds.f90" @ONLY)
  target_sources(metis_interop PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/metis_kinds.f90"
    metis_fortran.f90)
  set_target_properties(metis_interop PROPERTIES
    Fortran_MODULE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")
  if(NOT INTEROP_MAIN_LANGUAGE STREQUAL "Fortran")
    target_compile_definitions(metis_interop PRIVATE INTEROP_WITH_FORTRAN)
  endif()
endif()

enable_testing()
add_test(NAME metis.interop COMMAND metis_interop)

if(WIN32)
  add_custom_command(TARGET metis_interop POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:metis_interop>"
      "-DDESTINATION=$<TARGET_FILE_DIR:metis_interop>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CopyRuntime.cmake"
    VERBATIM)
endif()
