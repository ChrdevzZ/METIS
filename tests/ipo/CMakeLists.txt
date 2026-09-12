cmake_minimum_required(VERSION 3.24)
project(MetisIPOPolicyRegression LANGUAGES C)

if(NOT DEFINED MODULE_FILE)
  set(MODULE_FILE "${CMAKE_CURRENT_LIST_DIR}/../../cmake/MetisIPO.cmake")
endif()
if(NOT DEFINED TARGET_IPO_FUNCTION)
  set(TARGET_IPO_FUNCTION metis_target_ipo)
endif()
if(NOT DEFINED CHECK_IPO_FUNCTION)
  set(CHECK_IPO_FUNCTION metis_check_ipo)
endif()
if(NOT DEFINED IPO_POLICY_VARIABLE)
  set(IPO_POLICY_VARIABLE METIS_IPO)
endif()
if(NOT DEFINED BUILD_PROGRAMS_VARIABLE)
  set(BUILD_PROGRAMS_VARIABLE METIS_BUILD_PROGRAMS)
endif()
if(NOT DEFINED IPO_TEST_MODE)
  set(IPO_TEST_MODE MOCK)
endif()

include("${MODULE_FILE}")

function(fail message_text)
  message(FATAL_ERROR "IPO regression: ${message_text}")
endfunction()

function(assert_equal actual expected context)
  if(NOT "${actual}" STREQUAL "${expected}")
    fail("${context}: expected [${expected}], got [${actual}]")
  endif()
endfunction()

function(assert_variable_state variable expected_defined expected_value context)
  if(DEFINED ${variable})
    set(actual_defined ON)
    set(actual_value "${${variable}}")
  else()
    set(actual_defined OFF)
    set(actual_value "")
  endif()
  assert_equal("${actual_defined}" "${expected_defined}"
    "${context} changed whether ${variable} is defined")
  if(actual_defined)
    assert_equal("${actual_value}" "${expected_value}"
      "${context} changed ${variable}")
  endif()
endfunction()

function(ipo_cache_count output)
  get_cmake_property(cache_variables CACHE_VARIABLES)
  set(count 0)
  foreach(variable IN LISTS cache_variables)
    if(variable MATCHES "^[A-Z]+_IPO_CHECK_[0-9A-Fa-f]+$")
      math(EXPR count "${count} + 1")
    endif()
  endforeach()
  set(${output} "${count}" PARENT_SCOPE)
endfunction()

if(IPO_TEST_MODE STREQUAL "MOCK")
  # Replace only the expensive probe. The production policy function remains
  # under test and reports every branch that actually requests IPO support.
  function(${CHECK_IPO_FUNCTION} result diagnostic)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)
    if(NOT calls)
      set(calls 0)
    endif()
    math(EXPR calls "${calls} + 1")
    set_property(GLOBAL PROPERTY IPO_MOCK_CALLS "${calls}")
    set(${result} ON PARENT_SCOPE)
    set(${diagnostic} "mock-ipo-supported" PARENT_SCOPE)
  endfunction()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/dummy.c" "int ipo_fixture(void) { return 0; }\n")

  macro(capture_policy_scope prefix)
    foreach(variable
        CMAKE_INTERPROCEDURAL_OPTIMIZATION
        CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE
        CMAKE_BUILD_TYPE
        CMAKE_CONFIGURATION_TYPES
        ${IPO_POLICY_VARIABLE}
        ${BUILD_PROGRAMS_VARIABLE})
      string(MAKE_C_IDENTIFIER "${variable}" key)
      if(DEFINED ${variable})
        set(${prefix}_${key}_DEFINED ON)
        set(${prefix}_${key}_VALUE "${${variable}}")
      else()
        set(${prefix}_${key}_DEFINED OFF)
        unset(${prefix}_${key}_VALUE)
      endif()
    endforeach()
  endmacro()

  macro(assert_policy_scope prefix context)
    foreach(variable
        CMAKE_INTERPROCEDURAL_OPTIMIZATION
        CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE
        CMAKE_BUILD_TYPE
        CMAKE_CONFIGURATION_TYPES
        ${IPO_POLICY_VARIABLE}
        ${BUILD_PROGRAMS_VARIABLE})
      string(MAKE_C_IDENTIFIER "${variable}" key)
      if(DEFINED ${variable})
        set(actual_defined ON)
        set(actual_value "${${variable}}")
      else()
        set(actual_defined OFF)
        unset(actual_value)
      endif()
      assert_equal("${actual_defined}" "${${prefix}_${key}_DEFINED}"
        "${context} changed whether ${variable} is defined")
      if(actual_defined)
        assert_equal("${actual_value}" "${${prefix}_${key}_VALUE}"
          "${context} changed ${variable}")
      endif()
    endforeach()
  endmacro()

  function(assert_ipo_property target property expected)
    get_target_property(actual "${target}" "${property}")
    assert_equal("${actual}" "${expected}" "${target} ${property}")
  endfunction()

  function(assert_mock_delta before expected context)
    get_property(after GLOBAL PROPERTY IPO_MOCK_CALLS)
    if(NOT after)
      set(after 0)
    endif()
    math(EXPR delta "${after} - ${before}")
    assert_equal("${delta}" "${expected}" "${context} probe count")
  endfunction()

  function(run_auto_static_case)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    set(${BUILD_PROGRAMS_VARIABLE} OFF)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)
    if(NOT calls)
      set(calls 0)
    endif()

    add_library(auto_static_apps_off STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" auto_static_apps_off)

    assert_ipo_property(auto_static_apps_off INTERPROCEDURAL_OPTIMIZATION OFF)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL PROFILE)
      assert_ipo_property(auto_static_apps_off
        "INTERPROCEDURAL_OPTIMIZATION_${config}" OFF)
    endforeach()
    assert_mock_delta("${calls}" 0 "AUTO static/apps-off")
    assert_policy_scope(before "AUTO static/apps-off")
  endfunction()

  function(run_auto_shared_case)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)
    if(NOT calls)
      set(calls 0)
    endif()

    add_library(auto_shared SHARED "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" auto_shared)

    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION OFF)
    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
    assert_ipo_property(auto_shared INTERPROCEDURAL_OPTIMIZATION_PROFILE OFF)
    assert_mock_delta("${calls}" 3 "AUTO shared optimized configurations")
    assert_policy_scope(before "AUTO shared optimized configurations")
  endfunction()

  function(run_parent_global_case)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(parent_global STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" parent_global)

    assert_ipo_property(parent_global INTERPROCEDURAL_OPTIMIZATION ON)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL PROFILE)
      assert_ipo_property(parent_global
        "INTERPROCEDURAL_OPTIMIZATION_${config}" ON)
    endforeach()
    assert_mock_delta("${calls}" 6 "AUTO parent global")
    assert_policy_scope(before "AUTO parent global")
  endfunction()

  function(run_parent_custom_case)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE ON)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(parent_custom STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" parent_custom)

    assert_ipo_property(parent_custom INTERPROCEDURAL_OPTIMIZATION OFF)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      assert_ipo_property(parent_custom
        "INTERPROCEDURAL_OPTIMIZATION_${config}" OFF)
    endforeach()
    assert_ipo_property(parent_custom INTERPROCEDURAL_OPTIMIZATION_PROFILE ON)
    assert_mock_delta("${calls}" 1 "AUTO parent custom configuration")
    assert_policy_scope(before "AUTO parent custom configuration")
  endfunction()

  function(run_parent_global_off_custom_on_case)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE ON)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(parent_global_off_custom_on SHARED
      "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" parent_global_off_custom_on)

    assert_ipo_property(parent_global_off_custom_on
      INTERPROCEDURAL_OPTIMIZATION OFF)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      assert_ipo_property(parent_global_off_custom_on
        "INTERPROCEDURAL_OPTIMIZATION_${config}" OFF)
    endforeach()
    assert_ipo_property(parent_global_off_custom_on
      INTERPROCEDURAL_OPTIMIZATION_PROFILE ON)
    assert_mock_delta("${calls}" 1 "AUTO parent global OFF/custom ON")
    assert_policy_scope(before "AUTO parent global OFF/custom ON")
  endfunction()

  function(run_parent_global_on_custom_off_case)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE OFF)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} AUTO)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(parent_global_on_custom_off STATIC
      "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" parent_global_on_custom_off)

    assert_ipo_property(parent_global_on_custom_off
      INTERPROCEDURAL_OPTIMIZATION ON)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
      assert_ipo_property(parent_global_on_custom_off
        "INTERPROCEDURAL_OPTIMIZATION_${config}" ON)
    endforeach()
    assert_ipo_property(parent_global_on_custom_off
      INTERPROCEDURAL_OPTIMIZATION_PROFILE OFF)
    assert_mock_delta("${calls}" 5 "AUTO parent global ON/custom OFF")
    assert_policy_scope(before "AUTO parent global ON/custom OFF")
  endfunction()

  function(run_explicit_on_case)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    unset(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} ON)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(explicit_on STATIC "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" explicit_on)

    assert_ipo_property(explicit_on INTERPROCEDURAL_OPTIMIZATION ON)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL PROFILE)
      assert_ipo_property(explicit_on
        "INTERPROCEDURAL_OPTIMIZATION_${config}" ON)
    endforeach()
    assert_mock_delta("${calls}" 6 "explicit ON including empty configuration")
    assert_policy_scope(before "explicit ON")
  endfunction()

  function(run_explicit_off_case)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_PROFILE ON)
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Profile")
    set(CMAKE_BUILD_TYPE "")
    set(${IPO_POLICY_VARIABLE} OFF)
    capture_policy_scope(before)
    get_property(calls GLOBAL PROPERTY IPO_MOCK_CALLS)

    add_library(explicit_off SHARED "${CMAKE_CURRENT_BINARY_DIR}/dummy.c")
    cmake_language(CALL "${TARGET_IPO_FUNCTION}" explicit_off)

    assert_ipo_property(explicit_off INTERPROCEDURAL_OPTIMIZATION OFF)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL PROFILE)
      assert_ipo_property(explicit_off
        "INTERPROCEDURAL_OPTIMIZATION_${config}" OFF)
    endforeach()
    assert_mock_delta("${calls}" 0 "explicit OFF overrides parent")
    assert_policy_scope(before "explicit OFF")
  endfunction()

  capture_policy_scope(outer_before)
  set_property(GLOBAL PROPERTY IPO_MOCK_CALLS 0)
  run_auto_static_case()
  run_auto_shared_case()
  run_parent_global_case()
  run_parent_custom_case()
  run_parent_global_off_custom_on_case()
  run_parent_global_on_custom_off_case()
  run_explicit_on_case()
  run_explicit_off_case()
  assert_policy_scope(outer_before "complete policy fixture")

  # For a single-config generator, generation proves that CONFIG accepts and
  # matches the empty configuration used by the default target property.
  if(NOT CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/empty-config-check-active" "")
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/empty-config-result.txt"
      CONTENT "$<CONFIG:>")
  endif()
elseif(IPO_TEST_MODE STREQUAL "REAL")
  set(state_file "${CMAKE_CURRENT_BINARY_DIR}/ipo-hot-reconfigure.state")
  set(initial_sentinel "IPO_INITIAL_SIGNATURE_REUSED")
  set(variant_sentinel "IPO_VARIANT_SIGNATURE_REUSED")
  set(previous_initial "")
  set(previous_variant "")
  set(previous_state_valid OFF)
  if(EXISTS "${state_file}")
    file(STRINGS "${state_file}" previous_state)
    list(LENGTH previous_state previous_length)
    if(previous_length EQUAL 2)
      set(previous_state_valid ON)
      list(GET previous_state 0 previous_initial)
      list(GET previous_state 1 previous_variant)
    endif()
  endif()

  set(ipo_saved_c_flags "${CMAKE_C_FLAGS}")
  set(ipo_saved_exe_linker_flags "${CMAKE_EXE_LINKER_FLAGS}")
  if(DEFINED CMAKE_TRY_COMPILE_TARGET_TYPE)
    set(ipo_saved_try_type_defined ON)
    set(ipo_saved_try_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  else()
    set(ipo_saved_try_type_defined OFF)
    set(ipo_saved_try_type "")
  endif()
  ipo_cache_count(count_before)
  cmake_language(CALL "${CHECK_IPO_FUNCTION}" initial_supported initial_log)
  if(NOT EXISTS "${initial_log}")
    fail("the real IPO check did not create its diagnostic log")
  endif()
  ipo_cache_count(count_after_initial)
  math(EXPR initial_growth "${count_after_initial} - ${count_before}")
  if(initial_growth LESS 0 OR initial_growth GREATER 1)
    fail("the initial signature created ${initial_growth} cache entries")
  endif()
  assert_equal("${CMAKE_C_FLAGS}" "${ipo_saved_c_flags}"
    "real IPO check changed the caller's CMAKE_C_FLAGS")
  assert_equal("${CMAKE_EXE_LINKER_FLAGS}" "${ipo_saved_exe_linker_flags}"
    "real IPO check changed the caller's CMAKE_EXE_LINKER_FLAGS")
  assert_variable_state(CMAKE_TRY_COMPILE_TARGET_TYPE
    "${ipo_saved_try_type_defined}" "${ipo_saved_try_type}"
    "real IPO check")

  if(previous_state_valid)
    assert_equal("${initial_log}" "${previous_initial}"
      "hot-reconfigure initial signature")
    file(READ "${initial_log}" initial_text)
    if(NOT initial_text MATCHES "${initial_sentinel}")
      fail("hot reconfigure repeated the initial real IPO check")
    endif()
  else()
    file(APPEND "${initial_log}" "\n${initial_sentinel}\n")
  endif()

  cmake_language(CALL "${CHECK_IPO_FUNCTION}" repeated_supported repeated_log)
  ipo_cache_count(count_after_repeat)
  assert_equal("${repeated_log}" "${initial_log}" "same-signature diagnostic path")
  assert_equal("${repeated_supported}" "${initial_supported}" "same-signature result")
  assert_equal("${count_after_repeat}" "${count_after_initial}"
    "same-signature cache count")
  file(READ "${repeated_log}" repeated_text)
  if(NOT repeated_text MATCHES "${initial_sentinel}")
    fail("the same configure repeated the initial real IPO check")
  endif()

  if(MSVC)
    set(ipo_variant_c_flags "${ipo_saved_c_flags} /DIPO_SIGNATURE_VARIANT=1")
    set(CMAKE_C_FLAGS "${ipo_variant_c_flags}"
      CACHE STRING "Flags used by the C compiler" FORCE)
  else()
    set(ipo_variant_c_flags "${ipo_saved_c_flags} -DIPO_SIGNATURE_VARIANT=1")
    set(CMAKE_C_FLAGS "${ipo_variant_c_flags}"
      CACHE STRING "Flags used by the C compiler" FORCE)
  endif()
  cmake_language(CALL "${CHECK_IPO_FUNCTION}" variant_supported variant_log)
  if(variant_log STREQUAL initial_log)
    fail("changing CMAKE_C_FLAGS did not change the IPO cache signature")
  endif()
  if(NOT EXISTS "${variant_log}")
    fail("the changed IPO signature did not create its diagnostic log")
  endif()
  ipo_cache_count(count_after_variant)
  math(EXPR variant_growth "${count_after_variant} - ${count_after_repeat}")
  if(variant_growth LESS 0 OR variant_growth GREATER 1)
    fail("the changed signature created ${variant_growth} cache entries")
  endif()
  assert_equal("${CMAKE_C_FLAGS}" "${ipo_variant_c_flags}"
    "changed-signature IPO check changed the caller's CMAKE_C_FLAGS")
  assert_equal("${CMAKE_EXE_LINKER_FLAGS}" "${ipo_saved_exe_linker_flags}"
    "changed-signature IPO check changed CMAKE_EXE_LINKER_FLAGS")
  assert_variable_state(CMAKE_TRY_COMPILE_TARGET_TYPE
    "${ipo_saved_try_type_defined}" "${ipo_saved_try_type}"
    "changed-signature IPO check")

  if(previous_state_valid)
    assert_equal("${variant_log}" "${previous_variant}"
      "hot-reconfigure changed signature")
    file(READ "${variant_log}" variant_text)
    if(NOT variant_text MATCHES "${variant_sentinel}")
      fail("hot reconfigure repeated the changed-signature real IPO check")
    endif()
  else()
    file(APPEND "${variant_log}" "\n${variant_sentinel}\n")
  endif()

  cmake_language(CALL "${CHECK_IPO_FUNCTION}" variant_repeat_supported variant_repeat_log)
  ipo_cache_count(count_after_variant_repeat)
  assert_equal("${variant_repeat_log}" "${variant_log}"
    "changed same-signature diagnostic path")
  assert_equal("${variant_repeat_supported}" "${variant_supported}"
    "changed same-signature result")
  assert_equal("${count_after_variant_repeat}" "${count_after_variant}"
    "changed same-signature cache count")
  file(READ "${variant_repeat_log}" variant_repeat_text)
  if(NOT variant_repeat_text MATCHES "${variant_sentinel}")
    fail("the same configure repeated the changed-signature real IPO check")
  endif()

  set(CMAKE_C_FLAGS "${ipo_saved_c_flags}"
    CACHE STRING "Flags used by the C compiler" FORCE)
  cmake_language(CALL "${CHECK_IPO_FUNCTION}" restored_supported restored_log)
  ipo_cache_count(count_after_restore)
  assert_equal("${restored_log}" "${initial_log}" "restored signature diagnostic path")
  assert_equal("${restored_supported}" "${initial_supported}" "restored signature result")
  assert_equal("${count_after_restore}" "${count_after_variant}"
    "restored signature cache count")
  assert_equal("${CMAKE_C_FLAGS}" "${ipo_saved_c_flags}"
    "real IPO check changed the caller's CMAKE_C_FLAGS")

  file(WRITE "${state_file}" "${initial_log}\n${variant_log}\n")
else()
  fail("unknown IPO_TEST_MODE: ${IPO_TEST_MODE}")
endif()
