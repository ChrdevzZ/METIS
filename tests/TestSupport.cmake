include_guard(GLOBAL)


function(metis_test_export_header)
  cmake_parse_arguments(PARSE_ARGV 0 _header "" "OUTPUT;SYMBOLS;SOURCE" "")
  if(NOT _header_OUTPUT OR NOT _header_SYMBOLS OR NOT _header_SOURCE)
    message(FATAL_ERROR
      "metis_test_export_header requires OUTPUT, SYMBOLS and SOURCE")
  endif()

  # The checked-in list remains an independent compatibility oracle. Writing
  # only changed content prevents a no-op reconfigure from rebuilding its
  # consumer, while OBJECT_DEPENDS makes the intentional edge explicit.
  file(STRINGS "${_header_SYMBOLS}" _symbols)
  set(_entries "")
  foreach(_symbol IN LISTS _symbols)
    string(APPEND _entries "  \"${_symbol}\",\n")
  endforeach()
  set(_content "static const char *symbols[] = {\n${_entries}};\n")
  file(CONFIGURE OUTPUT "${_header_OUTPUT}" CONTENT "@_content@"
    @ONLY NEWLINE_STYLE LF)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_header_SYMBOLS}")
  set_property(SOURCE "${_header_SOURCE}" APPEND PROPERTY OBJECT_DEPENDS
    "${_header_OUTPUT}")
endfunction()


function(_metis_test_escape_cache_value output value)
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  string(REPLACE "$" "\\$" value "${value}")
  string(REPLACE "\r" "\\r" value "${value}")
  string(REPLACE "\n" "\\n" value "${value}")
  set(${output} "${value}" PARENT_SCOPE)
endfunction()


# Write only the toolchain, ABI and search inputs needed by nested test builds.
# Loading this file does not enable languages. Fortran-only consumers can reuse
# cached C compiler settings without requiring a C compiler for their build.
function(metis_test_write_initial_cache output)
  set(_variables
    CMAKE_MAKE_PROGRAM
    CMAKE_TOOLCHAIN_FILE
    CMAKE_BUILD_TYPE
    CMAKE_CONFIGURATION_TYPES
    CMAKE_DEFAULT_BUILD_TYPE
    CMAKE_SYSTEM_VERSION
    CMAKE_SYSROOT
    CMAKE_SYSROOT_COMPILE
    CMAKE_SYSROOT_LINK
    CMAKE_FIND_ROOT_PATH
    CMAKE_FIND_ROOT_PATH_MODE_PROGRAM
    CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
    CMAKE_FIND_ROOT_PATH_MODE_INCLUDE
    CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    CMAKE_PREFIX_PATH
    CMAKE_PROGRAM_PATH
    CMAKE_LIBRARY_PATH
    CMAKE_INCLUDE_PATH
    CMAKE_MSVC_RUNTIME_LIBRARY
    CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION
    CMAKE_OSX_ARCHITECTURES
    CMAKE_OSX_DEPLOYMENT_TARGET
    CMAKE_OSX_SYSROOT
    CMAKE_CROSSCOMPILING_EMULATOR
    CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    CMAKE_AR
    CMAKE_RANLIB
    CMAKE_LINKER
    CMAKE_LINKER_TYPE
    CMAKE_MT
    CMAKE_USER_MAKE_RULES_OVERRIDE
    CMAKE_USER_MAKE_RULES_OVERRIDE_C
    CMAKE_USER_MAKE_RULES_OVERRIDE_CXX
    CMAKE_USER_MAKE_RULES_OVERRIDE_Fortran
    CMAKE_C_COMPILER
    CMAKE_C_COMPILER_ARG1
    CMAKE_C_COMPILER_AR
    CMAKE_C_COMPILER_RANLIB
    CMAKE_C_COMPILER_TARGET
    CMAKE_C_COMPILER_EXTERNAL_TOOLCHAIN
    CMAKE_C_COMPILER_LAUNCHER
    CMAKE_CXX_COMPILER
    CMAKE_CXX_COMPILER_ARG1
    CMAKE_CXX_COMPILER_AR
    CMAKE_CXX_COMPILER_RANLIB
    CMAKE_CXX_COMPILER_TARGET
    CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN
    CMAKE_CXX_COMPILER_LAUNCHER
    CMAKE_Fortran_COMPILER
    CMAKE_Fortran_COMPILER_ARG1
    CMAKE_Fortran_COMPILER_AR
    CMAKE_Fortran_COMPILER_RANLIB
    CMAKE_Fortran_COMPILER_TARGET
    CMAKE_Fortran_COMPILER_EXTERNAL_TOOLCHAIN
    CMAKE_Fortran_COMPILER_LAUNCHER
    CMAKE_C_FLAGS
    CMAKE_CXX_FLAGS
    CMAKE_Fortran_FLAGS
    CMAKE_EXE_LINKER_FLAGS
    CMAKE_SHARED_LINKER_FLAGS
    CMAKE_MODULE_LINKER_FLAGS
    CMAKE_STATIC_LINKER_FLAGS
    OpenMP_ROOT
    CompilerRuntime_ROOT
    IntelRuntime_ROOT)

  if(CMAKE_CROSSCOMPILING)
    list(APPEND _variables CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR)
  endif()
  # A custom CMAKE_LINKER_TYPE is meaningful only with its matching language
  # definition. Forward every definition that the parent actually provides;
  # uppercase built-ins and user-defined lowercase types share this interface.
  # CMake 3.29--3.31 also uses USING_LINKER_MODE. CMake 4 derives the mode from
  # its read-only language link mode, which the nested compiler detects itself.
  foreach(_language IN ITEMS C CXX Fortran)
    list(APPEND _variables
      CMAKE_${_language}_USING_LINKER_MODE
      CMAKE_${_language}_LINKER_LAUNCHER)
  endforeach()
  get_cmake_property(_defined_variables VARIABLES)
  foreach(_variable IN LISTS _defined_variables)
    if(_variable MATCHES "^CMAKE_(C|CXX|Fortran)_USING_LINKER_.+")
      list(APPEND _variables "${_variable}")
    endif()
  endforeach()

  set(_configs ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE}
    Debug Release RelWithDebInfo MinSizeRel)
  list(REMOVE_DUPLICATES _configs)
  foreach(_config IN LISTS _configs)
    if(NOT _config STREQUAL "")
      string(TOUPPER "${_config}" _upper)
      list(APPEND _variables
        CMAKE_C_FLAGS_${_upper}
        CMAKE_CXX_FLAGS_${_upper}
        CMAKE_Fortran_FLAGS_${_upper}
        CMAKE_EXE_LINKER_FLAGS_${_upper}
        CMAKE_SHARED_LINKER_FLAGS_${_upper}
        CMAKE_MODULE_LINKER_FLAGS_${_upper}
        CMAKE_STATIC_LINKER_FLAGS_${_upper})
    endif()
  endforeach()
  list(APPEND _variables ${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES})
  if(NOT CMAKE_CROSSCOMPILING)
    list(REMOVE_ITEM _variables CMAKE_SYSTEM_NAME CMAKE_SYSTEM_PROCESSOR)
  endif()
  list(REMOVE_DUPLICATES _variables)

  set(_content "# Generated test-only toolchain and ABI inputs.\n")
  foreach(_variable IN LISTS _variables)
    if(DEFINED ${_variable})
      _metis_test_escape_cache_value(_value "${${_variable}}")
      if(_variable MATCHES "(_COMPILER|_TOOLCHAIN_FILE|_MAKE_PROGRAM)$")
        set(_type FILEPATH)
      else()
        set(_type STRING)
      endif()
      string(APPEND _content
        "set(${_variable} \"${_value}\" CACHE ${_type} \"Forwarded by METIS tests\" FORCE)\n")
    endif()
  endforeach()
  file(CONFIGURE OUTPUT "${output}" CONTENT "@_content@"
    @ONLY NEWLINE_STYLE LF)
endfunction()


# Route executable tests through one cross-compilation boundary. Native tests
# run directly; cross targets use the configured emulator list or report an
# explicit runtime skip after the normal build has already produced the target.
function(metis_test_add_runtime)
  cmake_parse_arguments(PARSE_ARGV 0 _test "" "NAME;TARGET;WORKING_DIRECTORY" "ARGS")
  if(NOT _test_NAME OR NOT _test_TARGET)
    message(FATAL_ERROR "metis_test_add_runtime requires NAME and TARGET")
  endif()

  add_test(NAME "${_test_NAME}"
    COMMAND "${CMAKE_COMMAND}"
      "-DTEST_EXECUTABLE=$<TARGET_FILE:${_test_TARGET}>"
      "-DTEST_ARGUMENTS=${_test_ARGS}"
      "-DTEST_WORKING_DIRECTORY=${_test_WORKING_DIRECTORY}"
      "-DTEST_CROSSCOMPILING=${CMAKE_CROSSCOMPILING}"
      "-DTEST_EMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}"
      -P "${CMAKE_CURRENT_FUNCTION_LIST_FILE}")
  if(CMAKE_CROSSCOMPILING AND NOT CMAKE_CROSSCOMPILING_EMULATOR)
    set_tests_properties("${_test_NAME}" PROPERTIES
      SKIP_REGULAR_EXPRESSION "^METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
  endif()
endfunction()


if(DEFINED CMAKE_SCRIPT_MODE_FILE AND
    "${CMAKE_SCRIPT_MODE_FILE}" STREQUAL "${CMAKE_CURRENT_LIST_FILE}")
  if(NOT DEFINED TEST_EXECUTABLE OR TEST_EXECUTABLE STREQUAL "" OR
      NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "Test executable does not exist: ${TEST_EXECUTABLE}")
  endif()

  if(TEST_CROSSCOMPILING AND NOT TEST_EMULATOR)
    message("METIS_RUNTIME_SKIPPED: no cross-compiling emulator")
    return()
  endif()

  set(_command "${TEST_EXECUTABLE}")
  if(TEST_CROSSCOMPILING)
    list(PREPEND _command ${TEST_EMULATOR})
  endif()
  if(TEST_WORKING_DIRECTORY STREQUAL "")
    get_filename_component(TEST_WORKING_DIRECTORY "${TEST_EXECUTABLE}" DIRECTORY)
  endif()
  execute_process(
    COMMAND ${_command} ${TEST_ARGUMENTS}
    WORKING_DIRECTORY "${TEST_WORKING_DIRECTORY}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE _stderr
    ECHO_OUTPUT_VARIABLE
    ECHO_ERROR_VARIABLE)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR
      "Test executable failed (${_result})\n${_stdout}\n${_stderr}")
  endif()
endif()
