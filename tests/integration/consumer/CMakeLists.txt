cmake_minimum_required(VERSION 3.24)
project(MetisCConsumer LANGUAGES C CXX)

# In source mode, verify METIS does not alter its parent's directory state.
if(METIS_TEST_SOURCE)
  foreach(variable CMAKE_C_FLAGS CMAKE_CXX_FLAGS BUILD_SHARED_LIBS
      CMAKE_RUNTIME_OUTPUT_DIRECTORY CMAKE_LIBRARY_OUTPUT_DIRECTORY
      CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    set(saved_${variable} "${${variable}}")
  endforeach()
  get_directory_property(saved_compile_options COMPILE_OPTIONS)
  set(METIS_BUILD_TESTING OFF)
  set(METIS_BUILD_PROGRAMS OFF)
  set(METIS_INSTALL OFF)
  add_subdirectory("${METIS_TEST_SOURCE}" metis)
  foreach(variable CMAKE_C_FLAGS CMAKE_CXX_FLAGS BUILD_SHARED_LIBS
      CMAKE_RUNTIME_OUTPUT_DIRECTORY CMAKE_LIBRARY_OUTPUT_DIRECTORY
      CMAKE_ARCHIVE_OUTPUT_DIRECTORY)
    if(NOT "${saved_${variable}}" STREQUAL "${${variable}}")
      message(FATAL_ERROR "METIS changed parent variable ${variable}")
    endif()
  endforeach()
  get_directory_property(current_compile_options COMPILE_OPTIONS)
  if(NOT "${saved_compile_options}" STREQUAL "${current_compile_options}")
    message(FATAL_ERROR "METIS changed parent directory compile options")
  endif()
  message(STATUS "Read-only check passed: METIS did not change parent build state")
else()
  find_package(METIS CONFIG REQUIRED)
endif()

# The same executable validates the source and installed-package paths.
add_executable(c_consumer main.c)
target_link_libraries(c_consumer PRIVATE METIS::metis)
enable_testing()
add_test(NAME c_consumer COMMAND c_consumer)

if(WIN32)
  add_custom_command(TARGET c_consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:c_consumer>"
      "-DDESTINATION=$<TARGET_FILE_DIR:c_consumer>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/CopyRuntime.cmake"
    VERBATIM)
endif()
