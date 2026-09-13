# Applications use private METIS structures whose allocation/free operations
# cross the DLL boundary. Shared GKlib also exposes CRT-owned FILE and signal
# state. These paths require a shared CRT; the public METIS C API with static
# GKlib remains usable in a library-only DLL with a static CRT.
include_guard(GLOBAL)


function(metis_check_crt target)
  get_target_property(type ${target} TYPE)
  get_target_property(gklib_type GKlib::GKlib TYPE)
  if(NOT WIN32 OR
      NOT (CMAKE_C_COMPILER_ID STREQUAL "MSVC" OR
           CMAKE_C_SIMULATE_ID STREQUAL "MSVC"))
    return()
  endif()
  if(NOT gklib_type STREQUAL "SHARED_LIBRARY" AND
      NOT (METIS_BUILD_PROGRAMS AND type STREQUAL "SHARED_LIBRARY"))
    return()
  endif()
  if(gklib_type STREQUAL "SHARED_LIBRARY")
    set(reason "Shared GKlib")
  else()
    set(reason "Shared METIS applications")
  endif()

  include(CheckCSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  get_target_property(runtime ${target} MSVC_RUNTIME_LIBRARY)
  if(NOT runtime STREQUAL "runtime-NOTFOUND")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "${runtime}")
  endif()
  if(CMAKE_CONFIGURATION_TYPES)
    set(configs ${CMAKE_CONFIGURATION_TYPES})
    # A multi-config try-compile can build a custom configuration only when its
    # generated project receives the caller's complete configuration set.
    list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
      CMAKE_CONFIGURATION_TYPES)
    list(REMOVE_DUPLICATES CMAKE_TRY_COMPILE_PLATFORM_VARIABLES)
  elseif(CMAKE_BUILD_TYPE)
    set(configs "${CMAKE_BUILD_TYPE}")
  else()
    set(configs __DEFAULT)
  endif()
  list(REMOVE_DUPLICATES configs)
  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" implementation)

  # These are compile-only checks. No target program runs during configuration,
  # and function scope restores the parent's compiler/check settings on return.
  foreach(config IN LISTS configs)
    if(config STREQUAL "__DEFAULT")
      set(config "")
    endif()
    string(TOUPPER "${config}" upper)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${config}")
    set(CMAKE_BUILD_TYPE "${config}")
    string(SHA256 signature
      "${implementation};${CMAKE_VERSION};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_VERSION};${CMAKE_C_COMPILER_TARGET};${CMAKE_C_FLAGS};${CMAKE_C_FLAGS_${upper}};${CMAKE_MSVC_RUNTIME_LIBRARY};${config};${CMAKE_CONFIGURATION_TYPES};${CMAKE_TOOLCHAIN_FILE};${CMAKE_GENERATOR_PLATFORM};${CMAKE_GENERATOR_TOOLSET};$ENV{CL};$ENV{_CL_};$ENV{INCLUDE}")
    set(cache "METIS_SHARED_CRT_${signature}")
    check_c_source_compiles("#if !defined(_DLL)
#error A shared CRT is required for these METIS DLL interfaces
#endif
int main(void) { return 0; }" ${cache})
    if(NOT ${cache})
      set(sanity_cache "METIS_SHARED_CRT_SANITY_${signature}")
      check_c_source_compiles("int main(void) { return 0; }" ${sanity_cache})
      if(${sanity_cache})
        message(FATAL_ERROR
          "${reason} requires the DLL CRT (/MD or /MDd) in configuration '${config}': CRT state and private allocations cross DLL boundaries. Use the DLL MSVC runtime, or use static METIS and GKlib for applications with /MT or /MTd. A library-only shared METIS with static GKlib can use a static CRT. See the shared CRT check in CMake's configure log.")
      else()
        message(FATAL_ERROR
          "Cannot validate the DLL CRT for ${reason} in configuration '${config}'. The compile-only probe failed before evaluating its _DLL requirement. See the shared CRT check in CMake's configure log.")
      endif()
    endif()
  endforeach()
  cmake_pop_check_state()
endfunction()
