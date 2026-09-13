include_guard(GLOBAL)


include(CheckCSourceCompiles)
include(CMakePushCheckState)


function(metis_check_link source result)
  cmake_push_check_state()
  set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)

  # A source check must use the same effective configuration as the target that
  # will consume its result. Multi-config generators retain their first configured
  # entry unless the caller selected a configuration explicitly.
  set(_metis_link_check_config "${CMAKE_TRY_COMPILE_CONFIGURATION}")
  if(NOT _metis_link_check_config)
    if(CMAKE_CONFIGURATION_TYPES)
      list(GET CMAKE_CONFIGURATION_TYPES 0 _metis_link_check_config)
    else()
      set(_metis_link_check_config "${CMAKE_BUILD_TYPE}")
    endif()
  endif()

  if(_metis_link_check_config)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${_metis_link_check_config}")
    string(TOUPPER "${_metis_link_check_config}"
      _metis_link_check_config_upper)

    # try_compile selects the requested configuration, but it does not forward
    # caller-defined configuration linker flags automatically. Pass both flag
    # families as platform inputs so the check matches the eventual link.
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
      "CMAKE_C_FLAGS_${_metis_link_check_config_upper}"
      "CMAKE_EXE_LINKER_FLAGS_${_metis_link_check_config_upper}")
    if(CMAKE_CONFIGURATION_TYPES)
      list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
        CMAKE_CONFIGURATION_TYPES)
    endif()
    list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
  endif()

  # Cache by every compiler, configuration and check input that can affect link.
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
    _metis_link_check_implementation)
  set(_metis_link_check_signature
    "implementation=[${_metis_link_check_implementation}]\nsource=[${source}]\nlanguage=[C]\n")
  foreach(_metis_link_check_name IN ITEMS
      CMAKE_COMMAND
      CMAKE_VERSION
      CMAKE_MAKE_PROGRAM
      CMAKE_C_COMPILER
      CMAKE_C_COMPILER_ARG1
      CMAKE_C_COMPILER_ID
      CMAKE_C_COMPILER_VERSION
      CMAKE_C_COMPILER_FRONTEND_VARIANT
      CMAKE_C_SIMULATE_ID
      CMAKE_C_SIMULATE_VERSION
      CMAKE_C_COMPILER_TARGET
      CMAKE_C_COMPILER_ARCHITECTURE_ID
      CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN
      CMAKE_C_COMPILER_LAUNCHER
      CMAKE_C_LINKER_LAUNCHER
      CMAKE_C_COMPILER_LINKER
      CMAKE_C_COMPILER_LINKER_ID
      CMAKE_C_COMPILER_LINKER_VERSION
      CMAKE_C_COMPILER_LINKER_FRONTEND_VARIANT
      CMAKE_C_LINK_MODE
      CMAKE_C_LINK_EXECUTABLE
      CMAKE_C_LINKER_WRAPPER_FLAG
      CMAKE_C_LINKER_WRAPPER_FLAG_SEP
      CMAKE_LINKER
      CMAKE_LINKER_LINK
      CMAKE_LINKER_LLD
      CMAKE_SYSROOT
      CMAKE_SYSROOT_COMPILE
      CMAKE_SYSROOT_LINK
      CMAKE_OSX_ARCHITECTURES
      CMAKE_GENERATOR
      CMAKE_GENERATOR_INSTANCE
      CMAKE_GENERATOR_PLATFORM
      CMAKE_GENERATOR_TOOLSET
      CMAKE_TOOLCHAIN_FILE
      CMAKE_SYSTEM_NAME
      CMAKE_SYSTEM_VERSION
      CMAKE_SYSTEM_PROCESSOR
      CMAKE_SIZEOF_VOID_P
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

  # CMake 3.29 and newer allow projects and toolchains to define custom linker
  # types. Hash every implementation variable, including the 3.29--3.31 MODE
  # input, so changing the command behind an unchanged type reruns the probe.
  get_cmake_property(_metis_link_check_variables VARIABLES)
  list(SORT _metis_link_check_variables)
  foreach(_metis_link_check_name IN LISTS _metis_link_check_variables)
    if(_metis_link_check_name MATCHES "^CMAKE_C_USING_LINKER_")
      string(APPEND _metis_link_check_signature
        "${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
    endif()
  endforeach()

  if(_metis_link_check_config)
    foreach(_metis_link_check_prefix IN ITEMS
        CMAKE_C_FLAGS CMAKE_EXE_LINKER_FLAGS)
      set(_metis_link_check_name
        "${_metis_link_check_prefix}_${_metis_link_check_config_upper}")
      string(APPEND _metis_link_check_signature
        "${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
    endforeach()
  endif()

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

  # A compiler that ignores a requested option has not proved the requested
  # link capability. Treat the common driver diagnostics as failed probes even
  # when the compiler exits successfully.
  set(_metis_link_check_fail_patterns
    FAIL_REGEX "[Uu]nrecogni[sz]ed [^\n]*option"
    FAIL_REGEX "[Uu]nknown [^\n]*option"
    FAIL_REGEX "unknown argument ignored"
    FAIL_REGEX "argument unused"
    FAIL_REGEX "[Ii]gnoring unknown option"
    FAIL_REGEX "warning D9002"
    FAIL_REGEX "option[^\n]*not supported"
    FAIL_REGEX "invalid argument [^\n]*option")

  # Return the cached result without leaking the temporary check state.
  check_c_source_compiles("${source}" ${_metis_link_check_variable}
    ${_metis_link_check_fail_patterns})
  set(${result} "${${_metis_link_check_variable}}" PARENT_SCOPE)
  cmake_pop_check_state()
endfunction()
