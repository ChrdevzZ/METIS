include_guard(GLOBAL)


include(CheckCSourceCompiles)
include(CMakePushCheckState)


function(metis_check_link source result)
  cmake_push_check_state()
  set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)

  # Cache by every compiler, configuration and check input that can affect link.
  set(_metis_link_check_signature "source=[${source}]\nlanguage=[C]\n")
  foreach(_metis_link_check_name IN ITEMS
      CMAKE_C_COMPILER
      CMAKE_C_COMPILER_ARG1
      CMAKE_C_COMPILER_ID
      CMAKE_C_COMPILER_VERSION
      CMAKE_C_COMPILER_FRONTEND_VARIANT
      CMAKE_C_SIMULATE_ID
      CMAKE_C_SIMULATE_VERSION
      CMAKE_C_COMPILER_TARGET
      CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN
      CMAKE_C_COMPILER_LAUNCHER
      CMAKE_C_LINKER_LAUNCHER
      CMAKE_SYSROOT
      CMAKE_SYSROOT_COMPILE
      CMAKE_SYSROOT_LINK
      CMAKE_OSX_ARCHITECTURES
      CMAKE_GENERATOR
      CMAKE_GENERATOR_PLATFORM
      CMAKE_GENERATOR_TOOLSET
      CMAKE_BUILD_TYPE
      CMAKE_CONFIGURATION_TYPES
      CMAKE_TRY_COMPILE_CONFIGURATION
      CMAKE_TRY_COMPILE_NO_PLATFORM_VARIABLES
      CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
      CMAKE_MSVC_RUNTIME_LIBRARY
      CMAKE_LINKER_TYPE
      CMAKE_C_LINKER_TYPE
      CMAKE_C_STANDARD_LIBRARIES
      CMAKE_C_FLAGS
      CMAKE_EXE_LINKER_FLAGS
      CMAKE_REQUIRED_FLAGS
      CMAKE_REQUIRED_DEFINITIONS
      CMAKE_REQUIRED_INCLUDES
      CMAKE_REQUIRED_LINK_OPTIONS
      CMAKE_REQUIRED_LIBRARIES
      CMAKE_REQUIRED_LINK_DIRECTORIES)
    string(APPEND _metis_link_check_signature
      "${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
  endforeach()

  set(_metis_link_check_configs
    Debug Release RelWithDebInfo MinSizeRel
    ${CMAKE_CONFIGURATION_TYPES}
    "${CMAKE_BUILD_TYPE}"
    "${CMAKE_TRY_COMPILE_CONFIGURATION}")
  list(REMOVE_DUPLICATES _metis_link_check_configs)
  foreach(_metis_link_check_config IN LISTS _metis_link_check_configs)
    if(_metis_link_check_config)
      string(TOUPPER "${_metis_link_check_config}"
        _metis_link_check_config_upper)
      foreach(_metis_link_check_prefix IN ITEMS
          CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS)
        set(_metis_link_check_name
          "${_metis_link_check_prefix}_${_metis_link_check_config_upper}")
        string(APPEND _metis_link_check_signature
          "${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
      endforeach()
    endif()
  endforeach()

  foreach(_metis_link_check_name IN LISTS
      CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
    string(APPEND _metis_link_check_signature
      "platform:${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
  endforeach()

  # Compiler search paths and implicit driver options affect link capability.
  foreach(_metis_link_check_name IN ITEMS
      PATH LIB LIBPATH INCLUDE LIBRARY_PATH COMPILER_PATH CPATH
      C_INCLUDE_PATH CPLUS_INCLUDE_PATH SDKROOT CL _CL_ LINK _LINK_)
    string(APPEND _metis_link_check_signature
      "env:${_metis_link_check_name}=[$ENV{${_metis_link_check_name}}]\n")
  endforeach()
  string(SHA256 _metis_link_check_id "${_metis_link_check_signature}")
  set(_metis_link_check_variable
    "METIS_LINK_CHECK_${_metis_link_check_id}")

  # Return the cached result without leaking the temporary check state.
  check_c_source_compiles("${source}" ${_metis_link_check_variable})
  set(${result} "${${_metis_link_check_variable}}" PARENT_SCOPE)
  cmake_pop_check_state()
endfunction()
