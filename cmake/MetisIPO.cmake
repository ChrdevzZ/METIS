# Keep IPO policy separate from ordinary target options. A successful probe
# describes this toolchain; it does not certify foreign LTO object formats.
include_guard(GLOBAL)


include(CheckIPOSupported)


function(metis_check_ipo result diagnostic)
  # Cache only inside this build tree. Include compiler/linker identity, all
  # configuration flags and search environment so a changed setup is retested.
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" implementation)
  file(SHA256 "${CMAKE_ROOT}/Modules/CheckIPOSupported.cmake" cmake_check)
  set(signature "${implementation};${cmake_check};${CMAKE_VERSION}")
  get_cmake_property(names VARIABLES)
  list(SORT names)
  foreach(name IN LISTS names)
    # Compiler identification also creates transient CMAKE_C_* variables on
    # the first configure. Hash only stable compiler inputs, not those probes.
    # CMake 3.24 exposes an empty architecture ID and the RC candidate list
    # only during compiler discovery. Normalize the former below and omit the
    # latter: the selected resource compiler is the effective input.
    if(name STREQUAL "CMAKE_C_COMPILER_ARCHITECTURE_ID" OR
        name STREQUAL "CMAKE_RC_COMPILER_LIST")
      continue()
    endif()
    if(name MATCHES "^CMAKE_C_(FLAGS($|_)|COMPILER($|_ID$|_VERSION$|_TARGET$|_FRONTEND_VARIANT$|_SIMULATE_ID$|_SIMULATE_VERSION$|_ARCHITECTURE_ID$|_AR$|_RANLIB$|_EXTERNAL_TOOLCHAIN$|_LINKER|_LAUNCHER$)|STANDARD$|STANDARD_REQUIRED$|EXTENSIONS$|LINKER_LAUNCHER$|LINK_OPTIONS_IPO$|COMPILE_OPTIONS_IPO$|ARCHIVE_)" OR
       name MATCHES "^CMAKE_(BUILD_TYPE|CONFIGURATION_TYPES|EXE_LINKER_|STATIC_LINKER_|SHARED_LINKER_|MSVC_|SYSROOT|OSX_|SYSTEM|GENERATOR|TRY_COMPILE|AR$|RANLIB$|RC_|MT$|LINKER|TOOLCHAIN|MAKE_PROGRAM)" OR
       name MATCHES "^METIS_(SANITIZERS|GPROF|NATIVE_OPTIMIZATION)$")
      string(APPEND signature "\n${name}=[${${name}}]")
    endif()
  endforeach()

  string(APPEND signature
    "\nCMAKE_C_COMPILER_ARCHITECTURE_ID=[${CMAKE_C_COMPILER_ARCHITECTURE_ID}]")

  foreach(name IN LISTS CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
    string(APPEND signature "\nplatform:${name}=[${${name}}]")
  endforeach()
  if(EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    file(SHA256 "${CMAKE_TOOLCHAIN_FILE}" toolchain_hash)
    string(APPEND signature "\ntoolchain=${toolchain_hash}")
  endif()

  foreach(tool CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB CMAKE_LINKER CMAKE_MAKE_PROGRAM
      CMAKE_C_COMPILER_AR CMAKE_C_COMPILER_RANLIB CMAKE_C_COMPILER_LINKER
      CMAKE_RC_COMPILER CMAKE_MT)
    if(EXISTS "${${tool}}")
      file(TIMESTAMP "${${tool}}" stamp UTC)
      file(SIZE "${${tool}}" size)
      string(APPEND signature "\n${tool}=${stamp};${size}")
    endif()
  endforeach()
  foreach(name PATH LIB LIBPATH INCLUDE LIBRARY_PATH COMPILER_PATH CPATH
      C_INCLUDE_PATH CPLUS_INCLUDE_PATH SDKROOT CL _CL_ LINK _LINK_
      LD_LIBRARY_PATH DYLD_LIBRARY_PATH)
    string(APPEND signature "\nenv:${name}=[$ENV{${name}}]")
  endforeach()

  string(SHA256 key "${signature}")
  set(cache "METIS_IPO_CHECK_${key}")
  if(NOT DEFINED ${cache})
    set(CMAKE_TRY_COMPILE_TARGET_TYPE EXECUTABLE)
    if(METIS_GPROF)
      string(APPEND CMAKE_C_FLAGS " -pg")
      string(APPEND CMAKE_EXE_LINKER_FLAGS " -pg")
    endif()
    foreach(sanitizer IN LISTS METIS_SANITIZERS)
      if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
        string(APPEND CMAKE_C_FLAGS " /fsanitize=${sanitizer}")
      else()
        string(APPEND CMAKE_C_FLAGS " -fsanitize=${sanitizer}")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -fsanitize=${sanitizer}")
      endif()
    endforeach()

    check_ipo_supported(RESULT supported OUTPUT reason LANGUAGES C)
    set(${cache} "${supported}" CACHE INTERNAL "METIS IPO toolchain check")
    # Keep multiline diagnostics outside CMakeCache.txt, which truncates them.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${cache}.log" "${reason}")
    set(${cache}_LOG "${CMAKE_CURRENT_BINARY_DIR}/${cache}.log"
      CACHE INTERNAL "METIS IPO check log")
  endif()
  set(${result} "${${cache}}" PARENT_SCOPE)
  set(${diagnostic} "${${cache}_LOG}" PARENT_SCOPE)
endfunction()


function(metis_target_ipo target)
  get_target_property(type ${target} TYPE)
  set(configs __DEFAULT Debug Release RelWithDebInfo MinSizeRel
    ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
  list(REMOVE_DUPLICATES configs)

  # Explicit project policy wins. AUTO preserves the parent's per-config
  # choice before its general choice. Ordinary static archives stay native.
  set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
  foreach(config IN LISTS configs)
    string(TOUPPER "${config}" upper)
    set(property INTERPROCEDURAL_OPTIMIZATION_${upper})
    if(config STREQUAL "__DEFAULT")
      set(property INTERPROCEDURAL_OPTIMIZATION)
      set(config "")
    endif()
    set(required OFF)
    set(enabled OFF)
    if(NOT METIS_IPO STREQUAL "AUTO")
      set(enabled "${METIS_IPO}")
      set(required "${METIS_IPO}")
    elseif(config AND DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_${upper})
      set(enabled "${CMAKE_INTERPROCEDURAL_OPTIMIZATION_${upper}}")
      set(required "${enabled}")
    elseif(DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
      set(enabled "${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
      set(required "${enabled}")
    elseif(type STREQUAL "SHARED_LIBRARY" AND
           upper MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
      set(enabled ON)
    endif()

    if(enabled)
      metis_check_ipo(supported diagnostic)
      if(NOT supported)
        if(required)
          message(FATAL_ERROR "Requested METIS IPO is unavailable; see ${diagnostic}")
        endif()
        set(enabled OFF)
      endif()
    endif()
    set_property(TARGET ${target} PROPERTY ${property} "${enabled}")

    # Intel archives containing IPO need an Intel final driver. Do not pass
    # the producer's C link options to an unrelated C++ or Fortran compiler.
    if(enabled AND WIN32 AND type STREQUAL "STATIC_LIBRARY" AND
       CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
      set(intel_driver "$<OR:$<LINK_LANG_AND_ID:C,IntelLLVM>,$<LINK_LANG_AND_ID:CXX,IntelLLVM>,$<LINK_LANG_AND_ID:Fortran,IntelLLVM>>")
      target_link_options(${target} INTERFACE
        "$<BUILD_INTERFACE:$<$<AND:$<CONFIG:${config}>,${intel_driver}>:-Qipo>>")
    endif()
  endforeach()
endfunction()
