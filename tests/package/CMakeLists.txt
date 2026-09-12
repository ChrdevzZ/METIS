cmake_minimum_required(VERSION 3.24)
project(SelfContainedConsumer LANGUAGES C CXX)

# The shared package must load without exposing a separate GKlib target.
find_package(METIS CONFIG REQUIRED)
if(TARGET GKlib::GKlib)
  message(FATAL_ERROR "Self-contained METIS must not require a GKlib target")
endif()
add_executable(package-c main.c)
add_executable(package-cxx main.cpp)

enable_testing()
foreach(target package-c package-cxx)
  target_link_libraries(${target} PRIVATE METIS::metis)
  add_test(NAME ${target} COMMAND ${target})
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_RUNTIME_DLLS:${target}>" "$<TARGET_FILE_DIR:${target}>"
      COMMAND_EXPAND_LISTS VERBATIM)
  endif()
endforeach()
