include_guard(GLOBAL)


include(CheckCSourceCompiles)
include(CMakePushCheckState)


function(metis_check_link source result)
  cmake_parse_arguments(PARSE_ARGV 2 _metis_link_check "" "LANGUAGE" "")
  if(_metis_link_check_UNPARSED_ARGUMENTS OR
      _metis_link_check_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "metis_check_link expects SOURCE RESULT [LANGUAGE C|CXX]")
  endif()
  set(_metis_link_check_language C)
  if(_metis_link_check_LANGUAGE)
    set(_metis_link_check_language "${_metis_link_check_LANGUAGE}")
  endif()
  if(NOT _metis_link_check_language MATCHES "^(C|CXX)$")
    message(FATAL_ERROR "Unsupported metis_check_link language")
  endif()
  if(_metis_link_check_language STREQUAL "CXX")
    include(CheckCXXSourceCompiles)
  endif()

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
      "CMAKE_${_metis_link_check_language}_FLAGS_${_metis_link_check_config_upper}"
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
    "implementation=[${_metis_link_check_implementation}]\nsource=[${source}]\nlanguage=[${_metis_link_check_language}]\n")

  # Function policies are captured when the helper is defined, not when called.
  # Keep language mode inputs distinct from an unset compiler default, including
  # explicit OFF and empty values. The final link below uses this same scope.
  foreach(_metis_link_check_policy IN ITEMS CMP0067 CMP0128)
    cmake_policy(GET ${_metis_link_check_policy} _metis_link_check_policy_value)
    string(APPEND _metis_link_check_signature
      "helper-policy:${_metis_link_check_policy}=[${_metis_link_check_policy_value}]\n")
  endforeach()
  foreach(_metis_link_check_suffix IN ITEMS STANDARD STANDARD_REQUIRED EXTENSIONS
      STANDARD_DEFAULT EXTENSIONS_DEFAULT)
    set(_metis_link_check_name
      "CMAKE_${_metis_link_check_language}_${_metis_link_check_suffix}")
    if(DEFINED ${_metis_link_check_name})
      string(APPEND _metis_link_check_signature
        "${_metis_link_check_name}=defined:[${${_metis_link_check_name}}]\n")
    else()
      string(APPEND _metis_link_check_signature
        "${_metis_link_check_name}=unset\n")
    endif()
  endforeach()

  foreach(_metis_link_check_name IN ITEMS
      CMAKE_COMMAND
      CMAKE_VERSION
      CMAKE_MAKE_PROGRAM
      CMAKE_${_metis_link_check_language}_COMPILER
      CMAKE_${_metis_link_check_language}_COMPILER_ARG1
      CMAKE_${_metis_link_check_language}_COMPILER_ID
      CMAKE_${_metis_link_check_language}_COMPILER_VERSION
      CMAKE_${_metis_link_check_language}_COMPILER_FRONTEND_VARIANT
      CMAKE_${_metis_link_check_language}_SIMULATE_ID
      CMAKE_${_metis_link_check_language}_SIMULATE_VERSION
      CMAKE_${_metis_link_check_language}_COMPILER_TARGET
      CMAKE_${_metis_link_check_language}_COMPILER_ARCHITECTURE_ID
      CMAKE_${_metis_link_check_language}_COMPILER_EXTERNAL_TOOLCHAIN
      CMAKE_${_metis_link_check_language}_COMPILER_LAUNCHER
      CMAKE_${_metis_link_check_language}_LINKER_LAUNCHER
      CMAKE_${_metis_link_check_language}_COMPILER_LINKER
      CMAKE_${_metis_link_check_language}_COMPILER_LINKER_ID
      CMAKE_${_metis_link_check_language}_COMPILER_LINKER_VERSION
      CMAKE_${_metis_link_check_language}_COMPILER_LINKER_FRONTEND_VARIANT
      CMAKE_${_metis_link_check_language}_LINK_MODE
      CMAKE_${_metis_link_check_language}_LINK_EXECUTABLE
      CMAKE_${_metis_link_check_language}_LINKER_WRAPPER_FLAG
      CMAKE_${_metis_link_check_language}_LINKER_WRAPPER_FLAG_SEP
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
      CMAKE_${_metis_link_check_language}_LINKER_TYPE
      CMAKE_${_metis_link_check_language}_STANDARD_LIBRARIES
      CMAKE_${_metis_link_check_language}_FLAGS
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
    if(_metis_link_check_name MATCHES "^CMAKE_${_metis_link_check_language}_USING_LINKER_")
      string(APPEND _metis_link_check_signature
        "${_metis_link_check_name}=[${${_metis_link_check_name}}]\n")
    endif()
  endforeach()

  if(_metis_link_check_config)
    foreach(_metis_link_check_prefix IN ITEMS
        CMAKE_${_metis_link_check_language}_FLAGS CMAKE_EXE_LINKER_FLAGS)
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

  # Check modules record the policies of their first inclusion. Run the private
  # cache miss here so the signature and actual try_compile have one policy scope.
  if(NOT DEFINED ${_metis_link_check_variable})
    set(_metis_link_check_extension c)
    if(_metis_link_check_language STREQUAL "CXX")
      set(_metis_link_check_extension cxx)
    endif()
    set(_metis_link_check_work
      "${CMAKE_BINARY_DIR}/CMakeFiles/metis-link-checks/${_metis_link_check_id}")
    file(MAKE_DIRECTORY "${_metis_link_check_work}")
    file(WRITE "${_metis_link_check_work}/source.${_metis_link_check_extension}"
      "${source}\n")
    set(_metis_link_check_options "")
    if(CMAKE_REQUIRED_LINK_OPTIONS)
      list(APPEND _metis_link_check_options
        LINK_OPTIONS ${CMAKE_REQUIRED_LINK_OPTIONS})
    endif()
    if(CMAKE_REQUIRED_LIBRARIES)
      list(APPEND _metis_link_check_options
        LINK_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
    endif()
    set(_metis_link_check_directories "")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.31 AND CMAKE_REQUIRED_LINK_DIRECTORIES)
      set(_metis_link_check_directories
        "-DLINK_DIRECTORIES:STRING=${CMAKE_REQUIRED_LINK_DIRECTORIES}")
    endif()
    if(NOT CMAKE_REQUIRED_QUIET)
      message(CHECK_START "Performing Test ${_metis_link_check_variable}")
    endif()
    try_compile(${_metis_link_check_variable} "${_metis_link_check_work}/build"
      SOURCES "${_metis_link_check_work}/source.${_metis_link_check_extension}"
      COMPILE_DEFINITIONS -D${_metis_link_check_variable} ${CMAKE_REQUIRED_DEFINITIONS}
      ${_metis_link_check_options}
      CMAKE_FLAGS "-DCOMPILE_DEFINITIONS:STRING=${CMAKE_REQUIRED_FLAGS}"
        "-DINCLUDE_DIRECTORIES:STRING=${CMAKE_REQUIRED_INCLUDES}"
        "${_metis_link_check_directories}"
      OUTPUT_VARIABLE _metis_link_check_output)
    foreach(_metis_link_check_pattern IN LISTS _metis_link_check_fail_patterns)
      if(NOT _metis_link_check_pattern STREQUAL "FAIL_REGEX" AND
          "${_metis_link_check_output}" MATCHES "${_metis_link_check_pattern}")
        set(${_metis_link_check_variable} FALSE)
      endif()
    endforeach()
    if(${_metis_link_check_variable})
      set(_metis_link_check_result 1)
      set(_metis_link_check_log CMakeOutput.log)
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_PASS "Success")
      endif()
    else()
      set(_metis_link_check_result "")
      set(_metis_link_check_log CMakeError.log)
      if(NOT CMAKE_REQUIRED_QUIET)
        message(CHECK_FAIL "Failed")
      endif()
    endif()
    file(WRITE "${_metis_link_check_work}/output.log" "${_metis_link_check_output}")
    file(APPEND "${CMAKE_BINARY_DIR}/CMakeFiles/${_metis_link_check_log}"
      "Link check ${_metis_link_check_variable}:\n${_metis_link_check_output}\nSource:\n${source}\n")
    set(${_metis_link_check_variable} "${_metis_link_check_result}"
      CACHE INTERNAL "Test ${_metis_link_check_variable}" FORCE)
    set(${_metis_link_check_variable} "${_metis_link_check_result}")
  endif()
  set(${result} "${${_metis_link_check_variable}}" PARENT_SCOPE)
  cmake_pop_check_state()
endfunction()
