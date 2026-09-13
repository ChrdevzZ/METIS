cmake_minimum_required(VERSION 3.18)

project(IntelRuntimeFixture LANGUAGES NONE)

if(NOT DEFINED MODULE_DIR)
  get_filename_component(MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../cmake" ABSOLUTE)
endif()
list(PREPEND CMAKE_MODULE_PATH "${MODULE_DIR}")

# The Python harness disables environment-derived search paths so these
# closed-world tests cannot borrow a developer's installed Intel libraries.
# Make the synthetic Windows archives discoverable on every host.
set(CMAKE_FIND_LIBRARY_PREFIXES "")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".lib")
set(CMAKE_C_COMPILER_ARCHITECTURE_ID "${TARGET_ARCH}")
set(CMAKE_SIZEOF_VOID_P "${TARGET_POINTER_SIZE}")

if(CASE STREQUAL "parent")
  add_library(IntelRuntime::C INTERFACE IMPORTED GLOBAL)
  set_property(TARGET IntelRuntime::C PROPERTY
    INTERFACE_COMPILE_DEFINITIONS parent_runtime_marker)
  find_package(IntelRuntime REQUIRED MODULE)
  foreach(mode MD MDd MT MTd)
    if(NOT TARGET IntelRuntime::C_${mode})
      message(FATAL_ERROR "Parent override did not provide C_${mode}")
    endif()
    get_target_property(links IntelRuntime::C_${mode} INTERFACE_LINK_LIBRARIES)
    if(NOT links STREQUAL "IntelRuntime::C")
      message(FATAL_ERROR "C_${mode} does not reuse the parent aggregate: ${links}")
    endif()
  endforeach()
  file(WRITE "${RESULT_FILE}" "parent-reused\n")

elseif(CASE STREQUAL "discover")
  find_package(IntelRuntime REQUIRED MODULE)
  foreach(target IRC C C_MD C_MDd C_MT C_MTd)
    if(NOT TARGET IntelRuntime::${target})
      message(FATAL_ERROR "Missing IntelRuntime::${target}")
    endif()
  endforeach()
  if(EXPECT_STATIC_IRC AND NOT TARGET IntelRuntime::STATIC_IRC)
    message(FATAL_ERROR "Missing IntelRuntime::STATIC_IRC")
  elseif(NOT EXPECT_STATIC_IRC AND TARGET IntelRuntime::STATIC_IRC)
    message(FATAL_ERROR "Unexpected IntelRuntime::STATIC_IRC")
  endif()
  if(EXPECT_STATIC_IRC)
    get_filename_component(expected_directory
      "${IntelRuntime_STATIC_IRC_LIBRARY}" DIRECTORY)
    get_target_property(search_directories IntelRuntime::STATIC_IRC
      INTERFACE_LINK_DIRECTORIES)
    if(NOT search_directories STREQUAL expected_directory)
      message(FATAL_ERROR
        "The static IRC target must resolve COFF default-library names in its external SDK directory")
    endif()
  endif()
  file(WRITE "${RESULT_FILE}"
    "irc=${IntelRuntime_IRC_LIBRARY}\nstatic_irc=${IntelRuntime_STATIC_IRC_LIBRARY}\n")

elseif(CASE STREQUAL "select")
  set(IntelRuntime_RUNTIME_LIBRARY "${RUNTIME}")
  find_package(IntelRuntime REQUIRED MODULE)
  if(REQUIRE_STATIC_IRC AND NOT TARGET IntelRuntime::STATIC_IRC)
    message(FATAL_ERROR "Selected MT mode requires IntelRuntime::STATIC_IRC")
  endif()
  file(GENERATE OUTPUT "${RESULT_FILE}" CONTENT
    "$<TARGET_GENEX_EVAL:IntelRuntime::C,$<TARGET_PROPERTY:IntelRuntime::C,INTERFACE_LINK_LIBRARIES>>\n")

elseif(CASE STREQUAL "producer")
  set(IntelRuntime_RUNTIME_LIBRARY "${RUNTIME}")
  find_package(IntelRuntime REQUIRED MODULE)
  add_library(producer INTERFACE)
  # Capture the link expression without requiring a compiler or linking the
  # synthetic archives. The production helper still supplies the exact call.
  function(target_link_libraries target visibility item)
    set_property(TARGET ${target} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${item}")
  endfunction()
  set_property(TARGET producer PROPERTY MSVC_RUNTIME_LIBRARY "${RUNTIME}")
  if(DEFINED IPO)
    set_property(TARGET producer PROPERTY
      INTERPROCEDURAL_OPTIMIZATION_RELEASE "${IPO}")
  endif()
  intel_runtime_link(producer)
  intel_runtime_install(producer Pkg "lib/cmake/Pkg" Development)
  file(GENERATE OUTPUT "${RESULT_FILE}" CONTENT
    "$<TARGET_GENEX_EVAL:producer,$<TARGET_PROPERTY:producer,INTERFACE_LINK_LIBRARIES>>\n")

elseif(CASE STREQUAL "import")
  if(CMAKE_BUILD_TYPE)
    set(CMAKE_CONFIGURATION_TYPES "${CMAKE_BUILD_TYPE}" CACHE STRING "" FORCE)
  endif()
  add_library(IntelRuntime::C INTERFACE IMPORTED GLOBAL)
  find_package(IntelRuntime REQUIRED MODULE)
  add_library(Pkg::archive STATIC IMPORTED GLOBAL)
  set(imported_configs "")
  foreach(config IN LISTS IMPORTED_CONFIGS)
    string(TOUPPER "${config}" upper)
    list(APPEND imported_configs "${upper}")
    set_property(TARGET Pkg::archive PROPERTY IMPORTED_LOCATION_${upper}
      "${CMAKE_CURRENT_BINARY_DIR}/archive-${config}.lib")
  endforeach()
  set_property(TARGET Pkg::archive PROPERTY IMPORTED_CONFIGURATIONS "${imported_configs}")
  if(DEFINED USER_MAP)
    set_property(TARGET Pkg::archive PROPERTY MAP_IMPORTED_CONFIG_DEBUG "${USER_MAP}")
  endif()
  intel_runtime_import(Pkg::archive Pkg "${RECORD_DIR}")
  if(IMPORT_AGAIN)
    if(DEFINED SECOND_RECORD_DIR)
      intel_runtime_import(Pkg::archive Pkg "${SECOND_RECORD_DIR}")
    else()
      intel_runtime_import(Pkg::archive Pkg "${RECORD_DIR}")
    endif()
  endif()
  get_target_property(map Pkg::archive MAP_IMPORTED_CONFIG_DEBUG)
  get_target_property(links Pkg::archive INTERFACE_LINK_LIBRARIES)
  get_target_property(options Pkg::archive INTERFACE_LINK_OPTIONS)
  file(WRITE "${RESULT_FILE}.raw" "links=${links}\noptions=${options}\n")
  file(GENERATE OUTPUT "${RESULT_FILE}" CONTENT
    "links=$<TARGET_GENEX_EVAL:Pkg::archive,$<TARGET_PROPERTY:Pkg::archive,INTERFACE_LINK_LIBRARIES>>\nmap=${map}\n")

else()
  message(FATAL_ERROR "Unknown fixture CASE: ${CASE}")
endif()
