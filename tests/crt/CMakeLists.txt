cmake_minimum_required(VERSION 3.24)
project(MetisCRTPolicyRegression LANGUAGES C)

if(NOT DEFINED MODULE_FILE OR NOT DEFINED CHECK_FUNCTION OR
    NOT DEFINED FIXTURE_PROJECT OR NOT DEFINED TARGET_KIND)
  message(FATAL_ERROR "The CRT fixture is missing its policy inputs")
endif()

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dummy.c"
  "int crt_fixture(void) { return 0; }\n")

# METIS has two unsafe boundaries: a shared GKlib dependency, and its bundled
# applications exchanging private allocations with a shared METIS library.
if(FIXTURE_PROJECT STREQUAL "METIS")
  if(GKLIB_KIND STREQUAL "SHARED")
    add_library(GKlib::GKlib SHARED IMPORTED GLOBAL)
  elseif(GKLIB_KIND STREQUAL "STATIC")
    add_library(GKlib::GKlib STATIC IMPORTED GLOBAL)
  else()
    message(FATAL_ERROR "The METIS CRT fixture requires GKLIB_KIND")
  endif()
  set(METIS_BUILD_PROGRAMS "${BUILD_PROGRAMS}")
elseif(NOT FIXTURE_PROJECT STREQUAL "GKLIB")
  message(FATAL_ERROR "Unknown CRT fixture project: ${FIXTURE_PROJECT}")
endif()

if(TARGET_KIND STREQUAL "SHARED")
  add_library(crt_guard_target SHARED "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
elseif(TARGET_KIND STREQUAL "STATIC")
  add_library(crt_guard_target STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
else()
  message(FATAL_ERROR "Unknown CRT fixture target kind: ${TARGET_KIND}")
endif()

# An explicitly empty property exercises detection from the compiler flags.
if(DEFINED RUNTIME)
  set_property(TARGET crt_guard_target PROPERTY MSVC_RUNTIME_LIBRARY "${RUNTIME}")
endif()

include("${MODULE_FILE}")
cmake_language(CALL "${CHECK_FUNCTION}" crt_guard_target)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/configured.txt" "accepted\n")
