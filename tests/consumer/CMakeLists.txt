cmake_minimum_required(VERSION 3.24)
project(MetisConsumer LANGUAGES C CXX)

# Exercise both subdirectory and installed-package consumption paths.
if(METIS_TEST_SOURCE)
  set(METIS_BUILD_TESTING OFF)
  set(METIS_BUILD_PROGRAMS OFF)
  set(METIS_INSTALL OFF)
  add_library(before INTERFACE)
  set(saved_flags "${CMAKE_C_FLAGS}")
  add_subdirectory("${METIS_TEST_SOURCE}" metis)
  if(NOT "${saved_flags}" STREQUAL "${CMAKE_C_FLAGS}")
    message(FATAL_ERROR "METIS changed parent compiler flags")
  endif()
else()
  find_package(METIS CONFIG REQUIRED)
endif()

add_executable(consumer main.cpp)
target_compile_features(consumer PRIVATE cxx_std_11)
target_link_libraries(consumer PRIVATE METIS::metis GKlib::GKlib)
if(METIS_TEST_LIBCXX)
  target_compile_definitions(consumer PRIVATE METIS_TEST_LIBCXX)
endif()
enable_testing()
add_test(NAME consumer COMMAND consumer)

if(WIN32)
  add_custom_command(TARGET consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:consumer>"
      "-DDESTINATION=$<TARGET_FILE_DIR:consumer>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CopyRuntime.cmake"
    VERBATIM)
endif()
