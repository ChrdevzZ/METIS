cmake_minimum_required(VERSION 3.24)
project(METISSanitizerConsumer LANGUAGES C CXX)

find_package(METIS CONFIG REQUIRED)

# A completed native MSVC DLL owns its AddressSanitizer SDK link step. Keep the
# installed interface free of that development-only linker instruction.
if(EXPECT_CLEAN_ASAN_INTERFACE)
  get_target_property(package_link_options METIS::metis
    INTERFACE_LINK_OPTIONS)
  get_target_property(package_target_type METIS::metis TYPE)
  if(NOT package_target_type STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR
      "The clean sanitizer interface check requires a shared package")
  endif()
  if(package_link_options MATCHES "INFERASANLIBS")
    message(FATAL_ERROR
      "The installed shared package exports its private ASan SDK option")
  endif()
endif()

# Linker-type regressions select a concrete compatible linker for the final
# consumer link. CMake 3.x also needs to know that clang-cl invokes it directly.
if(CONSUMER_LINKER_TYPE)
  if(CMAKE_VERSION VERSION_LESS 3.29)
    message(FATAL_ERROR "CONSUMER_LINKER_TYPE requires CMake 3.29 or newer")
  endif()
  if(CONSUMER_LINKER_COMMAND)
    foreach(language C CXX)
      set(CMAKE_${language}_USING_LINKER_${CONSUMER_LINKER_TYPE}
        "${CONSUMER_LINKER_COMMAND}")
      if(CMAKE_VERSION VERSION_LESS 4.0)
        set(CMAKE_${language}_USING_LINKER_MODE TOOL)
      endif()
    endforeach()
  endif()
endif()

enable_testing()
foreach(language c cpp)
  if(language STREQUAL "c")
    set(consumer_source ../../consumer/main.c)
  else()
    set(consumer_source ../../consumer/smoke.cpp)
  endif()
  add_executable(consumer-${language} "${consumer_source}")
  target_link_libraries(consumer-${language} PRIVATE METIS::metis)
  if(CONSUMER_LINKER_TYPE)
    set_property(TARGET consumer-${language} PROPERTY
      LINKER_TYPE "${CONSUMER_LINKER_TYPE}")
  endif()
  if(language STREQUAL "cpp")
    target_compile_features(consumer-${language} PRIVATE cxx_std_11)
  endif()
  add_test(NAME consumer-${language} COMMAND consumer-${language})
  set_tests_properties(consumer-${language} PROPERTIES TIMEOUT 30)
  if(WIN32)
    add_custom_command(TARGET consumer-${language} POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
        "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:consumer-${language}>;${EXTERNAL_RUNTIME_DLLS}"
        "-DDESTINATION=$<TARGET_FILE_DIR:consumer-${language}>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/CopyRuntime.cmake"
      VERBATIM)
  endif()
endforeach()
