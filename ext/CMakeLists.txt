# Dependency acquisition is confined to this directory.
if(TARGET GKlib::GKlib)
  return()
endif()

# An explicit source path wins unless SYSTEM forbids source fallback.
set(gklib_source "${METIS_GKLIB_SOURCE_DIR}")
if(METIS_GKLIB_PROVIDER STREQUAL "SYSTEM")
  set(gklib_source "")
endif()

if(NOT gklib_source AND NOT METIS_GKLIB_PROVIDER STREQUAL "SOURCE")
  # The library and applications consume this target from sibling directories.
  find_package(GKlib CONFIG QUIET GLOBAL)
endif()

if(TARGET GKlib::GKlib)
  return()
endif()

if(METIS_GKLIB_PROVIDER STREQUAL "SYSTEM")
  message(FATAL_ERROR "GKlib package not found. Set GKlib_ROOT or CMAKE_PREFIX_PATH.")
endif()

# Prefer the checked-out submodule before an explicitly enabled download.
if(NOT gklib_source AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/GKlib/CMakeLists.txt")
  set(gklib_source "${CMAKE_CURRENT_SOURCE_DIR}/GKlib")
endif()

if(NOT gklib_source AND METIS_FETCH_GKLIB)
  include(FetchContent)
  # Keep this pin equal to the gitlink; publish GKlib before publishing METIS.
  set(METIS_GKLIB_REVISION a750db993113bd988b0623d400f33bf457b055e0)
  FetchContent_Declare(gklib
    GIT_REPOSITORY https://github.com/ChrdevzZ/GKlib.git
    GIT_TAG ${METIS_GKLIB_REVISION}
    SOURCE_SUBDIR cmake-populate-only)
  FetchContent_MakeAvailable(gklib)
  if(TARGET GKlib::GKlib)
    return()
  endif()
  set(gklib_source "${gklib_SOURCE_DIR}")
endif()

if(NOT EXISTS "${gklib_source}/CMakeLists.txt")
  message(FATAL_ERROR "GKlib source is unavailable. Initialize ext/GKlib, set METIS_GKLIB_SOURCE_DIR, or explicitly enable METIS_FETCH_GKLIB.")
endif()

# These defaults live only in the dependency directory scope.
if(NOT DEFINED GKLIB_INSTALL)
  set(GKLIB_INSTALL ${METIS_INSTALL})
endif()
if(NOT DEFINED GKLIB_BUILD_SHARED_LIBS)
  set(GKLIB_BUILD_SHARED_LIBS ${METIS_BUILD_SHARED_LIBS})
endif()

add_subdirectory("${gklib_source}" "${CMAKE_CURRENT_BINARY_DIR}/GKlib")
if(NOT TARGET GKlib::GKlib)
  message(FATAL_ERROR "GKlib source must provide GKlib::GKlib")
endif()
