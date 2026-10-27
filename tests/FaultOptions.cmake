include_guard(GLOBAL)


include("${CMAKE_CURRENT_LIST_DIR}/TestSupport.cmake")


# Policy values travel as literal cache data, never as a list expanded through
# add_test, a function's ARGV and execute_process. Empty and OFF are intentional.
function(metis_test_write_fault_options output)
  set(content "# Generated test-only project policy, after toolchain inputs.\n")
  foreach(option IN ITEMS METIS_ASSERTIONS METIS_ASSERTIONS_EXPENSIVE METIS_DEBUG
      METIS_NATIVE_OPTIMIZATION METIS_GPROF METIS_WARNINGS_AS_ERRORS
      METIS_SANITIZERS GKLIB_ASSERTIONS GKLIB_ASSERTIONS_EXPENSIVE GKLIB_DEBUG
      GKLIB_USE_GKRAND GKLIB_NO_X86 GKLIB_NATIVE_OPTIMIZATION GKLIB_GPROF
      GKLIB_WARNINGS_AS_ERRORS GKLIB_THREAD_LOCAL_STORAGE GKLIB_REGEX_BACKEND
      GKLIB_OPENMP GKLIB_SANITIZERS)
    if(DEFINED ${option})
      _metis_test_escape_cache_value(value "${${option}}")
      string(APPEND content
        "set(${option} \"${value}\" CACHE STRING \"Forwarded fault policy\" FORCE)\n")
    endif()
  endforeach()
  file(CONFIGURE OUTPUT "${output}" CONTENT "@content@" @ONLY NEWLINE_STYLE LF)
endfunction()
