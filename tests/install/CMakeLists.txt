cmake_minimum_required(VERSION 3.24)
project(InstallOwnership LANGUAGES C)

# Enable install rules while disabling unrelated programs, tests and IPO.
set(METIS_INSTALL ON)
set(GKLIB_INSTALL ON)
set(METIS_BUILD_PROGRAMS OFF)
set(GKLIB_BUILD_PROGRAMS OFF)
set(METIS_BUILD_TESTING OFF)
set(GKLIB_BUILD_TESTING OFF)
set(METIS_IPO OFF)
set(GKLIB_IPO OFF)
set(CMAKE_DEBUG_POSTFIX d)

# METIS nests GKlib; the standalone GKlib case uses the same fixture shell.
if(TEST_PROJECT STREQUAL "METIS")
  add_subdirectory("${TEST_SOURCE_DIR}" dependency)
  set_target_properties(metis PROPERTIES OUTPUT_NAME custom-metis)
else()
  add_subdirectory("${TEST_SOURCE_DIR}" dependency)
endif()

set_target_properties(GKlib PROPERTIES OUTPUT_NAME custom-gklib)

# Parent-owned lookalike files verify uninstall ownership boundaries.
foreach(name metis.notes strings.txt GKlib.h)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${name}" "Parent owns ${name}\n")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${name}" DESTINATION share/parent
    COMPONENT Parent)
endforeach()
