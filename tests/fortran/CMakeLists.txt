cmake_minimum_required(VERSION 3.24)
project(MetisFortranConsumer LANGUAGES Fortran)

find_package(METIS CONFIG REQUIRED)

# This fixture binds the 32-bit idx_t ABI declared by its Fortran interface.
if(NOT METIS_IDXTYPEWIDTH EQUAL 32)
  message(FATAL_ERROR "This Fortran fixture requires METIS_IDXTYPEWIDTH=32")
endif()

add_executable(fortran_consumer main.f90)
target_link_libraries(fortran_consumer PRIVATE METIS::metis)

enable_testing()
add_test(NAME fortran_consumer COMMAND fortran_consumer)

if(WIN32)
  add_custom_command(TARGET fortran_consumer POST_BUILD
    COMMAND ${CMAKE_COMMAND}
      "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:fortran_consumer>"
      "-DDESTINATION=$<TARGET_FILE_DIR:fortran_consumer>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/CopyRuntime.cmake"
    VERBATIM)
endif()
