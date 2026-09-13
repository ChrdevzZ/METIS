# Windows MSVC-ABI Clang and Intel LLVM ASan objects need an explicit runtime
# when CMake invokes the linker directly. Keep that dependency separate from
# compiler switches.
include_guard(GLOBAL)


# LLD's string tail merging can place an ASan-instrumented empty string in
# another global's redzone (LLVM issue 62078). Restrict the workaround to ASan
# final links using LLD; normal builds and Microsoft's linker keep their policy.
function(_windows_asan_linker_options target)
  # Linker-ID expressions describe the detected compiler toolchain. Even with
  # newer CMake, a target's LINKER_TYPE may override that toolchain's default.
  # Resolve explicit types separately instead of passing LLD flags to link.exe.
  get_cmake_property(variables VARIABLES)
  foreach(language C CXX Fortran)
    if(NOT CMAKE_${language}_COMPILER_LOADED)
      continue()
    endif()
    set(default "$<OR:$<NOT:$<BOOL:$<TARGET_PROPERTY:LINKER_TYPE>>>,$<STREQUAL:$<TARGET_PROPERTY:LINKER_TYPE>,DEFAULT>>")
    if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.2)
      target_link_options(${target} INTERFACE
        "$<$<AND:$<LINK_LANGUAGE:${language}>,${default},$<${language}_COMPILER_LINKER_ID:LLD>>:LINKER:/OPT:NOLLDTAILMERGE>")
      set(types "")
    else()
      set(types DEFAULT)
    endif()
    foreach(variable IN LISTS variables)
      if(variable MATCHES "^CMAKE_${language}_USING_LINKER_(.+)$" AND
          NOT CMAKE_MATCH_1 STREQUAL "MODE")
        list(APPEND types "${CMAKE_MATCH_1}")
      endif()
    endforeach()
    list(REMOVE_DUPLICATES types)
    foreach(type IN LISTS types)
      if(type STREQUAL "DEFAULT")
        set(command "${CMAKE_LINKER}")
        set(condition "${default}")
      else()
        set(command "${CMAKE_${language}_USING_LINKER_${type}}")
        set(condition "$<STREQUAL:$<TARGET_PROPERTY:LINKER_TYPE>,${type}>")
      endif()
      string(TOLOWER "${command}" command)
      string(REPLACE "\\" "/" command "${command}")
      if(command MATCHES "(^|[/;=])lld-link(\\.exe)?($|[ ;])" OR
          command MATCHES "(^|[ ;])-fuse-ld=lld($|[ ;])")
        target_link_options(${target} INTERFACE
          "$<$<AND:$<LINK_LANGUAGE:${language}>,${condition}>:LINKER:/OPT:NOLLDTAILMERGE>")
      endif()
    endforeach()
  endforeach()
endfunction()


function(_windows_asan_add namespace config paths whole symbols)
  if(NOT TARGET ${namespace}::ASanRuntime)
    add_library(${namespace}::ASanRuntime INTERFACE IMPORTED GLOBAL)
    # A parent may enable another language after add_subdirectory returns.
    # Delay until the top-level directory ends so its compiler and custom
    # linker definitions are visible; target properties remain expressions.
    cmake_language(EVAL CODE
      "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL _windows_asan_linker_options \"${namespace}::ASanRuntime\")")
  endif()
  foreach(path IN LISTS paths)
    string(SHA256 id "${namespace};${path}")
    set(part "${namespace}::ASan_${id}")
    if(NOT TARGET ${part})
      get_filename_component(directory "${path}" DIRECTORY)
      get_filename_component(name "${path}" NAME_WLE)
      set(dll "${directory}/${name}.dll")
      if(EXISTS "${dll}")
        add_library(${part} SHARED IMPORTED GLOBAL)
        set_target_properties(${part} PROPERTIES
          IMPORTED_IMPLIB "${path}" IMPORTED_LOCATION "${dll}")
      else()
        add_library(${part} STATIC IMPORTED GLOBAL)
        set_property(TARGET ${part} PROPERTY IMPORTED_LOCATION "${path}")
      endif()
    endif()
    get_target_property(kind ${part} TYPE)
    if(kind STREQUAL "SHARED_LIBRARY")
      get_target_property(dll ${part} IMPORTED_LOCATION)
      set_property(TARGET ${namespace}::ASanRuntime APPEND PROPERTY
        _ASAN_DLLS "$<$<CONFIG:${config}>:${dll}>")
    endif()
    get_filename_component(name "${path}" NAME)
    if(name IN_LIST whole)
      set(dependency "$<LINK_LIBRARY:WHOLE_ARCHIVE,${part}>")
    else()
      set(dependency "${part}")
    endif()
    set_property(TARGET ${namespace}::ASanRuntime APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES "$<$<CONFIG:${config}>:${dependency}>")
  endforeach()
  foreach(symbol IN LISTS symbols)
    set_property(TARGET ${namespace}::ASanRuntime APPEND PROPERTY
      INTERFACE_LINK_OPTIONS "$<$<CONFIG:${config}>:LINKER:/INCLUDE:${symbol}>")
  endforeach()
  set_property(TARGET ${namespace}::ASanRuntime APPEND PROPERTY
    INTERFACE_LINK_OPTIONS "$<$<CONFIG:${config}>:LINKER:/INCREMENTAL:NO>")
endfunction()


function(windows_asan_attach target namespace check_function)
  if(NOT TARGET ${namespace}::ASanRuntime)
    if(CMAKE_CONFIGURATION_TYPES)
      set(configs ${CMAKE_CONFIGURATION_TYPES})
    elseif(CMAKE_BUILD_TYPE)
      set(configs "${CMAKE_BUILD_TYPE}")
    else()
      set(configs NOCONFIG)
    endif()
    list(REMOVE_DUPLICATES configs)
    set(directory "${PROJECT_BINARY_DIR}/cmake/asan")
    file(MAKE_DIRECTORY "${directory}")
    file(CONFIGURE OUTPUT "${directory}/runtime.c"
      CONTENT "int main(void) { return 0; }\n" @ONLY NEWLINE_STYLE LF)
    get_target_property(runtime ${target} MSVC_RUNTIME_LIBRARY)
    if(NOT runtime STREQUAL "runtime-NOTFOUND")
      set(CMAKE_MSVC_RUNTIME_LIBRARY "${runtime}")
    endif()

    foreach(config IN LISTS configs)
      string(TOUPPER "${config}" upper)
      if(config STREQUAL "NOCONFIG")
        set(config "")
      endif()
      set(CMAKE_TRY_COMPILE_CONFIGURATION "${config}")
      set(CMAKE_BUILD_TYPE "${config}")
      cmake_push_check_state(RESET)
      cmake_language(CALL ${check_function}
        "#ifndef _DLL\n#error Static CRT\n#endif\nint main(void) { return 0; }" dll_crt)
      cmake_language(CALL ${check_function}
        "#ifndef _DEBUG\n#error Release CRT\n#endif\nint main(void) { return 0; }" debug_crt)
      cmake_pop_check_state()
      set(crt /MT)
      if(dll_crt)
        set(crt /MD)
      endif()
      if(debug_crt)
        string(APPEND crt d)
      endif()

      # The driver's documented dry run selects architecture- and CRT-specific
      # runtime variants, including older static and newer DLL-backed runtimes.
      # It never compiles or executes target code. Final capability checks below
      # still compile and link using these exact libraries.
      separate_arguments(flags NATIVE_COMMAND
        "${CMAKE_C_COMPILER_ARG1} ${CMAKE_C_FLAGS} ${CMAKE_C_FLAGS_${upper}}")
      if(CMAKE_C_COMPILER_TARGET)
        list(APPEND flags "--target=${CMAKE_C_COMPILER_TARGET}")
      endif()
      # The target ABI does not determine the driver's command-line syntax.
      # clang/icx GNU frontends can also produce MSVC-compatible COFF objects.
      get_target_property(type ${target} TYPE)
      if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        list(APPEND flags "${crt}" /fsanitize=address)
        if(type STREQUAL "SHARED_LIBRARY")
          list(APPEND flags /LD)
        endif()
      else()
        set(runtime_name MultiThreaded)
        if(debug_crt)
          string(APPEND runtime_name Debug)
        endif()
        if(dll_crt)
          string(APPEND runtime_name DLL)
        endif()
        # Match CMake's selected CRT flags, including the preprocessor inputs
        # which the GNU frontend also uses when selecting its ASan thunk.
        list(APPEND flags
          ${CMAKE_C_COMPILE_OPTIONS_MSVC_RUNTIME_LIBRARY_${runtime_name}}
          -fsanitize=address)
        if(type STREQUAL "SHARED_LIBRARY")
          list(APPEND flags -shared)
        endif()
      endif()
      execute_process(COMMAND "${CMAKE_COMMAND}" -E env "TEMP=${directory}" "TMP=${directory}"
        "${CMAKE_C_COMPILER}" ${flags} "-###"
        "${directory}/runtime.c"
        WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
      string(REGEX MATCHALL "\"[^\"]*clang_rt\\.asan[^\"]*\\.lib\"" entries "${output}\n${error}")
      set(paths "")
      set(names "")
      set(whole "")
      foreach(entry IN LISTS entries)
        string(REPLACE "\"" "" path "${entry}")
        set(is_whole FALSE)
        if(path MATCHES "^[-/]wholearchive:")
          set(is_whole TRUE)
          string(REGEX REPLACE "^[-/]wholearchive:" "" path "${path}")
        endif()
        string(REPLACE "\\\\" "/" path "${path}")
        cmake_path(NORMAL_PATH path)
        if(NOT EXISTS "${path}")
          message(FATAL_ERROR "${namespace} ASan runtime is missing: ${path}")
        endif()
        get_filename_component(name "${path}" NAME)
        list(APPEND paths "${path}")
        list(APPEND names "${name}")
        if(is_whole)
          list(APPEND whole "${name}")
        endif()
      endforeach()
      if(NOT status STREQUAL "0" OR NOT paths)
        message(FATAL_ERROR
          "Cannot identify ${namespace} AddressSanitizer runtime for ${config}/${crt}:\n${output}\n${error}")
      endif()
      list(REMOVE_DUPLICATES paths)
      list(REMOVE_DUPLICATES names)
      list(REMOVE_DUPLICATES whole)
      string(REGEX MATCHALL "[-/]include:_[A-Za-z0-9_]*asan[A-Za-z0-9_]*" includes "${output}\n${error}")
      set(symbols "")
      foreach(entry IN LISTS includes)
        string(REGEX REPLACE "^[-/]include:" "" entry "${entry}")
        list(APPEND symbols "${entry}")
      endforeach()
      list(REMOVE_DUPLICATES symbols)
      _windows_asan_add("${namespace}" "${config}" "${paths}" "${whole}" "${symbols}")
      set_target_properties(${namespace}::ASanRuntime PROPERTIES
        _ASAN_PATHS_${upper} "${paths}"
        _ASAN_WHOLE_${upper} "${whole}"
        _ASAN_SYMBOLS_${upper} "${symbols}")
      set(record "${directory}/${namespace}ASan-${upper}.cmake")
      file(CONFIGURE OUTPUT "${record}" CONTENT
        "# Producer runtime filenames; SDK paths are resolved by the consumer.\nset(_asan_names \"${names}\")\nset(_asan_whole \"${whole}\")\nset(_asan_symbols \"${symbols}\")\n"
        @ONLY NEWLINE_STYLE LF)
      set_property(TARGET ${namespace}::ASanRuntime APPEND PROPERTY _ASAN_RECORDS "${record}")
    endforeach()
  endif()
  get_target_property(type ${target} TYPE)
  set_property(TARGET ${target} PROPERTY _WINDOWS_ASAN_NAMESPACE "${namespace}")
  if(type STREQUAL "STATIC_LIBRARY")
    target_link_libraries(${target} PUBLIC ${namespace}::ASanRuntime)
  else()
    target_link_libraries(${target} PRIVATE ${namespace}::ASanRuntime)
  endif()
endfunction()


function(windows_asan_install namespace destination component)
  get_target_property(records ${namespace}::ASanRuntime _ASAN_RECORDS)
  foreach(record IN LISTS records)
    get_filename_component(config "${record}" NAME_WLE)
    string(REGEX REPLACE "^${namespace}ASan-" "" config "${config}")
    if(config STREQUAL "NOCONFIG")
      install(FILES "${record}" DESTINATION "${destination}" COMPONENT "${component}")
    else()
      install(FILES "${record}" DESTINATION "${destination}"
        CONFIGURATIONS "${config}" COMPONENT "${component}")
    endif()
  endforeach()
  install(FILES "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
    DESTINATION "${destination}" COMPONENT "${component}")
endfunction()


function(windows_asan_import target namespace directory)
  # Exported targets survive a failed QUIET lookup. A retry may supply a missing
  # SDK, but must not combine that archive with another installation's records.
  get_target_property(previous ${target} _WINDOWS_ASAN_RECORD_DIRECTORY)
  if(previous AND NOT previous STREQUAL directory)
    set(${namespace}_FOUND FALSE PARENT_SCOPE)
    set(${namespace}_NOT_FOUND_MESSAGE
      "${target} already belongs to ${previous}; cannot use ASan records from ${directory}." PARENT_SCOPE)
    return()
  endif()
  set_property(TARGET ${target} PROPERTY _WINDOWS_ASAN_RECORD_DIRECTORY "${directory}")
  if(TARGET ${namespace}::ASanRuntime)
    return()
  endif()
  # SDK roots remain consumer inputs. Fortran-only consumers can supply the
  # producer SDK explicitly; importing the package never enables C or C++.
  set(hints ${CompilerRuntime_ROOT} ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES}
    ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
  foreach(language C CXX)
    if(CMAKE_${language}_COMPILER_ID MATCHES "Clang|IntelLLVM")
      execute_process(COMMAND "${CMAKE_${language}_COMPILER}" -print-resource-dir
        OUTPUT_VARIABLE resource ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
      if(IS_DIRECTORY "${resource}")
        list(APPEND hints "${resource}/lib/windows" "${resource}/lib")
      endif()
    endif()
  endforeach()
  cmake_path(CONVERT "$ENV{LIB}" TO_CMAKE_PATH_LIST environment_libs)
  list(APPEND hints ${environment_libs})
  get_target_property(available ${target} IMPORTED_CONFIGURATIONS)
  if(CMAKE_CONFIGURATION_TYPES)
    set(configs ${CMAKE_CONFIGURATION_TYPES})
  elseif(CMAKE_BUILD_TYPE)
    set(configs "${CMAKE_BUILD_TYPE}")
  else()
    set(configs NOCONFIG)
  endif()
  list(REMOVE_DUPLICATES configs)
  foreach(config IN LISTS configs)
    string(TOUPPER "${config}" upper)
    get_property(mapped TARGET ${target} PROPERTY MAP_IMPORTED_CONFIG_${upper} SET)
    if(mapped)
      get_target_property(candidates ${target} MAP_IMPORTED_CONFIG_${upper})
      if("${candidates}" STREQUAL "")
        set(candidates _ASAN_CONFIGLESS)
      endif()
    else()
      set(candidates "${upper}" _ASAN_CONFIGLESS ${available})
    endif()
    set(selected "")
    foreach(candidate IN LISTS candidates)
      if("${candidate}" STREQUAL "")
        set(candidate _ASAN_CONFIGLESS)
      endif()
      string(TOUPPER "${candidate}" candidate)
      if(candidate STREQUAL "_ASAN_CONFIGLESS")
        get_target_property(location ${target} IMPORTED_LOCATION)
        if(location)
          set(selected NOCONFIG)
          break()
        endif()
        continue()
      endif()
      get_target_property(location ${target} IMPORTED_LOCATION_${candidate})
      if(candidate IN_LIST available AND location)
        set(selected "${candidate}")
        break()
      endif()
    endforeach()
    if(NOT selected OR NOT EXISTS "${directory}/${namespace}ASan-${selected}.cmake")
      set(${namespace}_FOUND FALSE PARENT_SCOPE)
      set(${namespace}_NOT_FOUND_MESSAGE
        "No installed ${namespace} ASan runtime record matches ${config}." PARENT_SCOPE)
      return()
    endif()
    include("${directory}/${namespace}ASan-${selected}.cmake")
    set(paths "")
    foreach(name IN LISTS _asan_names)
      set(_asan_library "_asan_library-NOTFOUND")
      find_library(_asan_library NAMES "${name}" HINTS ${hints}
        PATH_SUFFIXES lib lib/windows NO_CACHE)
      if(NOT _asan_library)
        set(${namespace}_FOUND FALSE PARENT_SCOPE)
        set(${namespace}_NOT_FOUND_MESSAGE
          "${namespace} ${selected} requires AddressSanitizer runtime ${name}; set CompilerRuntime_ROOT or CMAKE_PREFIX_PATH to the producer's compatible compiler runtime SDK." PARENT_SCOPE)
        return()
      endif()
      list(APPEND paths "${_asan_library}")
    endforeach()
    set(paths_${upper} "${paths}")
    set(whole_${upper} "${_asan_whole}")
    set(symbols_${upper} "${_asan_symbols}")
  endforeach()

  # Publish targets only after the complete active runtime closure is found.
  # A failed QUIET package lookup must not leave a partially usable target.
  foreach(config IN LISTS configs)
    string(TOUPPER "${config}" upper)
    if(config STREQUAL "NOCONFIG")
      set(config "")
    endif()
    _windows_asan_add("${namespace}" "${config}"
      "${paths_${upper}}" "${whole_${upper}}" "${symbols_${upper}}")
  endforeach()
endfunction()
