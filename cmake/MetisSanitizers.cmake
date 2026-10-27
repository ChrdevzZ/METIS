# Reuse only the selected sanitizer policy on owned source targets. Other
# production options and test-specific IPO/ownership contracts stay separate.
include_guard(GLOBAL)


include("${CMAKE_CURRENT_LIST_DIR}/MetisCheckLink.cmake")


function(metis_target_sanitizers target)
  cmake_parse_arguments(PARSE_ARGV 1 diagnostics "" "LANGUAGE" "")
  if(diagnostics_UNPARSED_ARGUMENTS OR diagnostics_KEYWORDS_MISSING_VALUES)
    message(FATAL_ERROR "metis_target_sanitizers expects TARGET [LANGUAGE C|CXX]")
  endif()
  set(language C)
  if(diagnostics_LANGUAGE)
    set(language "${diagnostics_LANGUAGE}")
  endif()
  if(NOT language MATCHES "^(C|CXX)$")
    message(FATAL_ERROR "Unsupported sanitizer compile/link language: ${language}")
  endif()

  foreach(sanitizer IN LISTS METIS_SANITIZERS)
    if(NOT sanitizer MATCHES "^(address|undefined)$")
      message(FATAL_ERROR "Unsupported sanitizer: ${sanitizer}")
    endif()
    string(TOUPPER "${sanitizer}" sanitizer_id)
    if(CMAKE_${language}_COMPILER_ID STREQUAL "MSVC")
      if(NOT sanitizer STREQUAL "address")
        message(FATAL_ERROR "MSVC does not support ${sanitizer}")
      endif()
      set(sanitizer_flag /fsanitize=address)
      # Static archives carry instrumented objects, so their final consumers
      # also need the linker to infer the matching AddressSanitizer libraries.
      set(sanitizer_link_flag "LINKER:/INFERASANLIBS")
    else()
      set(sanitizer_flag "-fsanitize=${sanitizer}")
      set(sanitizer_link_flag "${sanitizer_flag}")
    endif()
    set(sanitizer_runtime "")
    if(WIN32 AND CMAKE_${language}_SIMULATE_ID STREQUAL "MSVC" AND
        CMAKE_${language}_COMPILER_ID MATCHES "Clang|IntelLLVM")
      # These CMake toolchains may call link.exe/lld-link directly. Runtime
      # libraries, not a compiler-only -fsanitize switch, form the link interface.
      set(sanitizer_link_flag "")
      if(sanitizer STREQUAL "address")
        include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WindowsASan.cmake")
        windows_asan_attach(${target} METIS metis_check_link)
        set(sanitizer_runtime METIS::ASanRuntime)
      endif()
    endif()
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "${sanitizer_flag}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${sanitizer_link_flag}")
    set(CMAKE_REQUIRED_LIBRARIES "${sanitizer_runtime}")
    # Check the instrumentation macro as well as real memory/arithmetic code.
    # A compiler that merely warns and ignores the switch must not pass.
    set(sanitizer_preamble "")
    if(sanitizer STREQUAL "address")
      set(sanitizer_preamble "#ifndef __has_feature\n#define __has_feature(x) 0\n#endif\n#if !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer)\n#error AddressSanitizer instrumentation is not enabled\n#endif\n")
    elseif(CMAKE_${language}_COMPILER_ID MATCHES "Clang|IntelLLVM")
      set(sanitizer_preamble "#ifndef __has_feature\n#define __has_feature(x) 0\n#endif\n#if !__has_feature(undefined_behavior_sanitizer)\n#error UndefinedBehaviorSanitizer instrumentation is not enabled\n#endif\n")
    endif()
    metis_check_link("${sanitizer_preamble}int main(int argc, char **argv) {
      volatile int values[2] = {1, 2};
      values[argc] = argc * argc;
      return values[argc] + (argv[0] != 0);
    }" METIS_${sanitizer_id}_LINKS LANGUAGE ${language})
    cmake_pop_check_state()
    if(NOT METIS_${sanitizer_id}_LINKS)
      message(FATAL_ERROR "Requested sanitizer is unavailable: ${sanitizer}")
    endif()
    target_compile_options(${target} PRIVATE "$<$<COMPILE_LANGUAGE:${language}>:${sanitizer_flag}>")
    if(sanitizer_link_flag)
      # Native MSVC resolves the ASan imports when linking a DLL. Only static
      # archives leave that work to their consumer's final linker.
      get_target_property(sanitizer_target_type ${target} TYPE)
      if((CMAKE_${language}_COMPILER_ID STREQUAL "MSVC" AND
          NOT sanitizer_target_type STREQUAL "STATIC_LIBRARY") OR
          sanitizer_target_type STREQUAL "EXECUTABLE")
        target_link_options(${target} PRIVATE "${sanitizer_link_flag}")
      else()
        target_link_options(${target} PUBLIC "${sanitizer_link_flag}")
      endif()
    endif()
  endforeach()
endfunction()
