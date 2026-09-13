# Resolve IPO for the configurations this build can actually produce. The
# probe compiles and links targets; it never executes target programs or certifies
# compatibility with another compiler's intermediate object format.
include_guard(GLOBAL)


function(metis_ipo_cache_entry output name value)
  # Bracket arguments preserve semicolons, backslashes and empty cache values.
  set(delimiter "=")
  while("${value}" MATCHES "\\]${delimiter}\\]")
    string(APPEND delimiter "=")
  endwhile()
  set(${output}
    "${${output}}set(${name} [${delimiter}[${value}]${delimiter}] CACHE STRING \"IPO probe input\" FORCE)\n"
    PARENT_SCOPE)
endfunction()


function(metis_check_ipo result diagnostic)
  # Optional arguments describe the final link being tested. Keeping the
  # result/log pair first also permits direct, target-free regression probes.
  set(config "${CMAKE_BUILD_TYPE}")
  if(CMAKE_CONFIGURATION_TYPES AND CMAKE_BUILD_TYPE STREQUAL "")
    list(GET CMAKE_CONFIGURATION_TYPES 0 config)
  endif()
  set(type STATIC_LIBRARY)
  if(ARGC GREATER 2)
    set(config "${ARGV2}")
    set(type "${ARGV3}")
  endif()
  string(TOUPPER "${config}" upper)

  set(link_flags EXE_LINKER_FLAGS)
  if(type STREQUAL "STATIC_LIBRARY")
    list(APPEND link_flags STATIC_LINKER_FLAGS)
  elseif(type STREQUAL "SHARED_LIBRARY")
    set(link_flags SHARED_LINKER_FLAGS)
  endif()
  set(flag_names CMAKE_C_FLAGS)
  foreach(flag IN LISTS link_flags)
    list(APPEND flag_names "CMAKE_${flag}")
  endforeach()
  if(NOT config STREQUAL "")
    set(base_flags ${flag_names})
    foreach(flag IN LISTS base_flags)
      list(APPEND flag_names "${flag}_${upper}")
    endforeach()
  endif()

  set(inputs "")
  metis_ipo_cache_entry(inputs CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    "${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES}")
  get_cmake_property(names VARIABLES)
  list(APPEND names ${CMAKE_TRY_COMPILE_PLATFORM_VARIABLES})
  list(REMOVE_DUPLICATES names)
  list(SORT names)
  foreach(name IN LISTS names)
    # Host-derived system names must not turn a native child into a cross
    # build, even when a toolchain lists them as custom platform inputs.
    if(NOT CMAKE_CROSSCOMPILING AND
        name MATCHES "^CMAKE_SYSTEM_(NAME|PROCESSOR)$")
      continue()
    endif()
    # Forward effective inputs, not compiler-discovery outputs or the parent
    # cache wholesale. Only the selected configuration enters this probe.
    if(name MATCHES "^CMAKE_(C_COMPILER($|_ARG1$|_TARGET$|_EXTERNAL_TOOLCHAIN$|_AR$|_RANLIB$|_LAUNCHER$)|C_LINKER_LAUNCHER$|C_USING_LINKER_|C_LINK_MODE$|LINKER_TYPE$|C_STANDARD$|C_STANDARD_REQUIRED$|C_EXTENSIONS$|MSVC_RUNTIME_LIBRARY$|MSVC_DEBUG_INFORMATION_FORMAT$|SYSTEM_VERSION$|USER_MAKE_RULES_OVERRIDE($|_C$)|SYSROOT($|_)|OSX_|AR$|RANLIB$|RC_COMPILER$|RC_FLAGS$|MT$|LINKER$|TOOLCHAIN_FILE$|MAKE_PROGRAM$|FIND_ROOT_PATH($|_)|PREFIX_PATH$|LIBRARY_PATH$|INCLUDE_PATH$|C_STANDARD_LIBRARIES$)" OR
       name IN_LIST CMAKE_TRY_COMPILE_PLATFORM_VARIABLES OR
       name IN_LIST flag_names)
      metis_ipo_cache_entry(inputs "${name}" "${${name}}")
    endif()
  endforeach()
  if(CMAKE_CROSSCOMPILING)
    foreach(name CMAKE_SYSTEM_NAME CMAKE_SYSTEM_VERSION CMAKE_SYSTEM_PROCESSOR)
      if(DEFINED ${name})
        metis_ipo_cache_entry(inputs "${name}" "${${name}}")
      endif()
    endforeach()
  endif()
  metis_ipo_cache_entry(inputs CMAKE_BUILD_TYPE "${config}")
  if(CMAKE_CONFIGURATION_TYPES)
    metis_ipo_cache_entry(inputs CMAKE_CONFIGURATION_TYPES "${config}")
  endif()
  # Compiler discovery may compile archives, but the probe below always has
  # a real final link. Invalid executable flags must fail that link, not abort
  # the parent configure through a nested try_compile generation error.
  metis_ipo_cache_entry(inputs CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  metis_ipo_cache_entry(inputs CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
  metis_ipo_cache_entry(inputs CMAKE_INTERPROCEDURAL_OPTIMIZATION_${upper} OFF)

  set(properties "")
  set(asan_namespace "")
  if(ARGC GREATER 4 AND TARGET "${ARGV4}")
    foreach(property COMPILE_OPTIONS LINK_OPTIONS POSITION_INDEPENDENT_CODE
        MSVC_RUNTIME_LIBRARY LINKER_TYPE C_STANDARD C_STANDARD_REQUIRED C_EXTENSIONS)
      get_target_property(value "${ARGV4}" ${property})
      if(NOT value STREQUAL "value-NOTFOUND")
        metis_ipo_cache_entry(inputs "PROBE_${property}" "${value}")
        string(APPEND properties
          "set_property(TARGET probe PROPERTY ${property} \"\${PROBE_${property}}\")\n")
      endif()
    endforeach()
    # Windows ASan uses imported runtime libraries instead of compiler-driver
    # link flags. Preserve this project-owned dependency in the isolated probe.
    get_target_property(asan_namespace "${ARGV4}" _WINDOWS_ASAN_NAMESPACE)
    if(NOT asan_namespace STREQUAL "asan_namespace-NOTFOUND")
      set(asan_config "${upper}")
      if(asan_config STREQUAL "")
        set(asan_config NOCONFIG)
      endif()
      foreach(part PATHS WHOLE SYMBOLS)
        get_target_property(value ${asan_namespace}::ASanRuntime _ASAN_${part}_${asan_config})
        metis_ipo_cache_entry(inputs "PROBE_ASAN_${part}" "${value}")
      endforeach()
      metis_ipo_cache_entry(inputs PROBE_ASAN_MODULE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WindowsASan.cmake")
      file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/WindowsASan.cmake" asan_implementation)
      metis_ipo_cache_entry(inputs PROBE_ASAN_IMPLEMENTATION "${asan_implementation}")
    else()
      set(asan_namespace "")
    endif()
    # An archive has no ordinary link step. Its caller consumes only the
    # public final-link requirements, never the archive's private options.
    get_target_property(value "${ARGV4}" INTERFACE_LINK_OPTIONS)
    if(NOT value STREQUAL "value-NOTFOUND")
      metis_ipo_cache_entry(inputs PROBE_INTERFACE_LINK_OPTIONS "${value}")
    endif()
  endif()

  set(project "cmake_minimum_required(VERSION 3.24)\nproject(IPOProbe LANGUAGES C)\n")
  if(type STREQUAL "STATIC_LIBRARY")
    string(APPEND project "add_library(probe STATIC probe.c)\nadd_executable(caller main.c)\ntarget_link_libraries(caller PRIVATE probe)\n")
    # Sanitizer/gprof final-link requirements apply to the archive caller too.
    string(APPEND project "set_property(TARGET caller PROPERTY LINK_OPTIONS \"\${PROBE_INTERFACE_LINK_OPTIONS}\")\nset_property(TARGET caller PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)\nset_property(TARGET caller PROPERTY INTERPROCEDURAL_OPTIMIZATION_${upper} ON)\n")
    string(APPEND project "if(DEFINED PROBE_MSVC_RUNTIME_LIBRARY)\n  set_property(TARGET caller PROPERTY MSVC_RUNTIME_LIBRARY \"\${PROBE_MSVC_RUNTIME_LIBRARY}\")\nendif()\n")
  elseif(type STREQUAL "SHARED_LIBRARY")
    # Explicit DLL export avoids scanning intermediate /GL objects for a .def
    # file; that scan is not part of either project's real shared-library build.
    string(APPEND project "add_library(probe SHARED probe.c)\ntarget_compile_definitions(probe PRIVATE IPO_PROBE_SHARED=1)\n")
  else()
    string(APPEND project "add_executable(probe main.c probe.c)\n")
  endif()
  string(APPEND project "${properties}set_property(TARGET probe PROPERTY INTERPROCEDURAL_OPTIMIZATION ON)\nset_property(TARGET probe PROPERTY INTERPROCEDURAL_OPTIMIZATION_${upper} ON)\n")

  if(asan_namespace)
    string(APPEND project
      "include(\"\${PROBE_ASAN_MODULE}\")\n_windows_asan_add(IPOProbe \"${config}\" \"\${PROBE_ASAN_PATHS}\" \"\${PROBE_ASAN_WHOLE}\" \"\${PROBE_ASAN_SYMBOLS}\")\ntarget_link_libraries(probe PUBLIC IPOProbe::ASanRuntime)\n")
    if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
      string(APPEND project
        "target_link_options(probe PUBLIC \"LINKER:/OPT:NOLLDTAILMERGE\")\n")
    endif()
  endif()

  file(SHA256 "${CMAKE_CURRENT_FUNCTION_LIST_FILE}" implementation)
  set(signature "${implementation};${CMAKE_VERSION};${CMAKE_ROOT};${config};${type};${inputs};${project}")
  foreach(name CMAKE_GENERATOR CMAKE_GENERATOR_INSTANCE CMAKE_GENERATOR_PLATFORM
      CMAKE_GENERATOR_TOOLSET CMAKE_C_COMPILER_ID CMAKE_C_COMPILER_VERSION)
    string(APPEND signature "\n${name}=[${${name}}]")
  endforeach()
  foreach(file_variable CMAKE_TOOLCHAIN_FILE CMAKE_USER_MAKE_RULES_OVERRIDE
      CMAKE_USER_MAKE_RULES_OVERRIDE_C)
    if(EXISTS "${${file_variable}}")
      file(SHA256 "${${file_variable}}" input_hash)
      string(APPEND signature "\n${file_variable}=${input_hash}")
    endif()
  endforeach()
  foreach(tool CMAKE_C_COMPILER CMAKE_AR CMAKE_RANLIB CMAKE_LINKER CMAKE_MAKE_PROGRAM
      CMAKE_C_COMPILER_AR CMAKE_C_COMPILER_RANLIB CMAKE_RC_COMPILER CMAKE_MT)
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
    string(SUBSTRING "${key}" 0 20 short_key)
    set(directory "${CMAKE_BINARY_DIR}/CMakeFiles/METIS-ipo/${short_key}")
    file(MAKE_DIRECTORY "${directory}/source")
    file(WRITE "${directory}/inputs.cmake" "${inputs}")
    file(WRITE "${directory}/source/CMakeLists.txt" "${project}")
    file(WRITE "${directory}/source/probe.c" "#if defined(_WIN32) && defined(IPO_PROBE_SHARED)\n#define IPO_EXPORT __declspec(dllexport)\n#else\n#define IPO_EXPORT\n#endif\nIPO_EXPORT int ipo_probe(void);\nIPO_EXPORT int ipo_probe(void) { return 42; }\n")
    file(WRITE "${directory}/source/main.c" "int ipo_probe(void);\nint main(void) { return ipo_probe() != 42; }\n")
    set(command "${CMAKE_COMMAND}" -S "${directory}/source" -B "${directory}/build"
      -G "${CMAKE_GENERATOR}" -C "${directory}/inputs.cmake")
    if(CMAKE_GENERATOR_PLATFORM)
      list(APPEND command -A "${CMAKE_GENERATOR_PLATFORM}")
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
      list(APPEND command -T "${CMAKE_GENERATOR_TOOLSET}")
    endif()
    if(CMAKE_GENERATOR_INSTANCE)
      list(APPEND command "-DCMAKE_GENERATOR_INSTANCE=${CMAKE_GENERATOR_INSTANCE}")
    endif()
    execute_process(COMMAND ${command} RESULT_VARIABLE status
      OUTPUT_VARIABLE output ERROR_VARIABLE error)
    set(reason "Configuration [${config}], ${type}\nConfigure (${status}):\n${output}\n${error}\n")
    set(supported OFF)
    if(status STREQUAL "0")
      execute_process(COMMAND "${CMAKE_COMMAND}" --build "${directory}/build"
          --config "${config}" --parallel 2
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
      string(APPEND reason "Build (${status}):\n${output}\n${error}\n")
      if(status STREQUAL "0")
        set(supported ON)
      endif()
    endif()
    set(${cache} "${supported}" CACHE INTERNAL "METIS configured IPO link check")
    file(WRITE "${directory}/probe.log" "${reason}")
    set(${cache}_LOG "${directory}/probe.log" CACHE INTERNAL "METIS IPO check log")
  endif()
  set(${result} "${${cache}}" PARENT_SCOPE)
  set(${diagnostic} "${${cache}_LOG}" PARENT_SCOPE)
endfunction()


function(metis_target_ipo target)
  get_target_property(type ${target} TYPE)
  if(CMAKE_CONFIGURATION_TYPES)
    set(configs ${CMAKE_CONFIGURATION_TYPES})
  elseif(NOT CMAKE_BUILD_TYPE STREQUAL "")
    set(configs "${CMAKE_BUILD_TYPE}")
  else()
    set(configs __EMPTY)
  endif()
  list(REMOVE_DUPLICATES configs)

  # Clear inherited properties before applying the resolved policy. Inactive
  # configurations do not need probes and must not inherit unchecked IPO.
  set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION OFF)
  foreach(config Debug Release RelWithDebInfo MinSizeRel ${configs})
    string(TOUPPER "${config}" upper)
    set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_${upper} OFF)
  endforeach()
  foreach(config IN LISTS configs)
    if(config STREQUAL "__EMPTY")
      set(config "")
    endif()
    string(TOUPPER "${config}" upper)
    set(property INTERPROCEDURAL_OPTIMIZATION_${upper})
    if(config STREQUAL "")
      set(property INTERPROCEDURAL_OPTIMIZATION)
    endif()
    set(required OFF)
    set(enabled OFF)
    if(NOT METIS_IPO STREQUAL "AUTO")
      set(enabled "${METIS_IPO}")
      set(required "${METIS_IPO}")
    elseif(NOT config STREQUAL "" AND DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION_${upper})
      set(enabled "${CMAKE_INTERPROCEDURAL_OPTIMIZATION_${upper}}")
      set(required "${enabled}")
    elseif(DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
      set(enabled "${CMAKE_INTERPROCEDURAL_OPTIMIZATION}")
      set(required "${enabled}")
    elseif(type STREQUAL "SHARED_LIBRARY" AND upper MATCHES "^(RELEASE|RELWITHDEBINFO|MINSIZEREL)$")
      set(enabled ON)
    endif()

    if(enabled)
      metis_check_ipo(supported diagnostic "${config}" "${type}" "${target}")
      if(NOT supported)
        if(required)
          message(FATAL_ERROR "Requested METIS IPO for configuration [${config}] is unavailable; see ${diagnostic}")
        endif()
        message(STATUS "METIS AUTO IPO disabled for configuration [${config}]; see ${diagnostic}")
        set(enabled OFF)
      endif()
    endif()
    set_property(TARGET ${target} PROPERTY ${property} "${enabled}")

    # Intel's Windows IPO driver switches to LLD even when CMake detected
    # link.exe for ordinary links. Keep ASan's tail-merge workaround paired
    # with that verified IPO mode, including an archive's final consumer.
    get_target_property(asan_namespace ${target} _WINDOWS_ASAN_NAMESPACE)
    if(enabled AND WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM" AND
        asan_namespace AND TARGET ${asan_namespace}::ASanRuntime)
      set(intel_driver "$<OR:$<LINK_LANG_AND_ID:C,IntelLLVM>,$<LINK_LANG_AND_ID:CXX,IntelLLVM>,$<LINK_LANG_AND_ID:Fortran,IntelLLVM>>")
      if(type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target} INTERFACE
          "$<BUILD_INTERFACE:$<$<AND:$<CONFIG:${config}>,${intel_driver}>:LINKER:/OPT:NOLLDTAILMERGE>>")
      else()
        target_link_options(${target} PRIVATE
          "$<$<AND:$<CONFIG:${config}>,${intel_driver}>:LINKER:/OPT:NOLLDTAILMERGE>")
      endif()
    endif()

    # Intel IPO archives still require a compatible final driver. Ordinary
    # machine-code archives never propagate these intermediate-code options.
    if(enabled AND WIN32 AND type STREQUAL "STATIC_LIBRARY" AND
       CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
      set(intel_driver "$<OR:$<LINK_LANG_AND_ID:C,IntelLLVM>,$<LINK_LANG_AND_ID:CXX,IntelLLVM>,$<LINK_LANG_AND_ID:Fortran,IntelLLVM>>")
      target_link_options(${target} INTERFACE
        "$<BUILD_INTERFACE:$<$<AND:$<CONFIG:${config}>,${intel_driver}>:-Qipo>>")
    endif()
  endforeach()
endfunction()
