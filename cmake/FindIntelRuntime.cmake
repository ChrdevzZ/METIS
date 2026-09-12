# Find the Windows Intel C runtime used by ordinary Intel-produced objects.
#
# IntelRuntime::C carries the required IRC, math and vector math libraries.
# IntelRuntime_ROOT and CMAKE_PREFIX_PATH may select an external installation.
# IntelRuntime_RUNTIME_LIBRARY selects a nonempty MSVC runtime expression for
# direct users of the aggregate. Installed archives use their own CRT records.
# Existing parent targets take precedence. No runtime binaries are installed.


# An explicitly empty standard runtime policy delegates to compiler flags.
# Identify that choice through preprocessing/compilation only, per configuration;
# cross builds do not execute probes and the parent check state stays untouched.
function(_intelruntime_detect_crt result)
  include(CheckCSourceCompiles)
  include(CMakePushCheckState)
  cmake_push_check_state(RESET)
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "")
  set(configs __DEFAULT Debug Release RelWithDebInfo MinSizeRel
    ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
  list(REMOVE_DUPLICATES configs)
  set(expression "")
  foreach(config IN LISTS configs)
    if(config STREQUAL "__DEFAULT")
      set(config "")
    endif()
    string(TOUPPER "${config}" upper)
    set(CMAKE_TRY_COMPILE_CONFIGURATION "${config}")
    set(CMAKE_BUILD_TYPE "${config}")
    string(SHA256 signature
      "${CMAKE_VERSION};${CMAKE_C_COMPILER};${CMAKE_C_COMPILER_VERSION};${CMAKE_C_FLAGS};${CMAKE_C_FLAGS_${upper}};${config};${CMAKE_TOOLCHAIN_FILE};$ENV{CL};$ENV{_CL_};$ENV{INCLUDE}")
    set(selected "")
    foreach(mode MD MDd MT MTd)
      if(mode MATCHES "^MD")
        set(dll "defined(_DLL)")
      else()
        set(dll "!defined(_DLL)")
      endif()
      if(mode MATCHES "d$")
        set(debug "defined(_DEBUG)")
      else()
        set(debug "!defined(_DEBUG)")
      endif()
      set(cache "IntelRuntime_CRT_${signature}_${mode}")
      check_c_source_compiles("#if !defined(_MT) || !(${dll}) || !(${debug})
#error CRT variant does not match
#endif
int main(void) { return 0; }" ${cache})
      if(${cache})
        set(selected MultiThreaded)
        if(mode MATCHES "d$")
          string(APPEND selected Debug)
        endif()
        if(mode MATCHES "^MD")
          string(APPEND selected DLL)
        endif()
        break()
      endif()
    endforeach()
    if(NOT selected)
      message(FATAL_ERROR
        "Cannot identify Intel CRT for configuration '${config}'. Set CMAKE_MSVC_RUNTIME_LIBRARY explicitly or correct the compiler flags.")
    endif()
    if(config STREQUAL "")
      set(expression "${selected}")
    else()
      set(expression "$<IF:$<CONFIG:${config}>,${selected},${expression}>")
    endif()
  endforeach()
  cmake_pop_check_state()
  set(${result} "${expression}" PARENT_SCOPE)
endfunction()


# Freeze the producer's CRT expression in its link interface, so relocating an
# archive never changes its runtime family to match a consumer's default flags.
# The per-CRT targets are implementation details of the aggregate C runtime.
function(intel_runtime_link target)
  get_target_property(runtime ${target} MSVC_RUNTIME_LIBRARY)
  if(runtime STREQUAL "runtime-NOTFOUND")
    set(runtime "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  elseif(runtime STREQUAL "")
    _intelruntime_detect_crt(runtime)
  endif()
  set_property(TARGET ${target} PROPERTY _INTEL_RUNTIME_CRT "${runtime}")
  foreach(mode MD MDd MT MTd)
    if(mode STREQUAL "MD")
      set(value MultiThreadedDLL)
    elseif(mode STREQUAL "MDd")
      set(value MultiThreadedDebugDLL)
    elseif(mode STREQUAL "MT")
      set(value MultiThreaded)
    else()
      set(value MultiThreadedDebug)
    endif()
    target_link_libraries(${target} PRIVATE
      "$<BUILD_INTERFACE:$<$<STREQUAL:${runtime},${value}>:IntelRuntime::C_${mode}>>")
  endforeach()
endfunction()


# Resolve generator expressions while the producer target still exists. Each
# installed archive configuration receives its own concrete CRT record.
function(intel_runtime_install target package destination component)
  get_target_property(runtime ${target} _INTEL_RUNTIME_CRT)
  set(record "${PROJECT_BINARY_DIR}/cmake/intel-runtime/$<CONFIG>/${package}IntelRuntime-$<CONFIG>.cmake")
  set(config "$<IF:$<BOOL:$<CONFIG>>,$<UPPER_CASE:$<CONFIG>>,NOCONFIG>")
  set(ipo "$<IF:$<BOOL:$<CONFIG>>,$<TARGET_PROPERTY:${target},INTERPROCEDURAL_OPTIMIZATION_$<UPPER_CASE:$<CONFIG>>>,$<TARGET_PROPERTY:${target},INTERPROCEDURAL_OPTIMIZATION>>")
  file(GENERATE OUTPUT "${record}" CONTENT
    "# Runtime and IPO policy of the installed ${package} archive.\nset(${package}_INTEL_RUNTIME_${config} \"$<TARGET_GENEX_EVAL:${target},${runtime}>\")\nset(${package}_INTEL_IPO_${config} \"$<BOOL:${ipo}>\")\n")
  install(FILES "${record}" DESTINATION "${destination}" COMPONENT "${component}")
endfunction()


# Imported configuration fallback selects an archive before runtime selection.
# Read the matching producer record instead of reevaluating producer expressions
# in a different consumer configuration (for example Release mapped from Debug).
function(intel_runtime_import target package directory)
  set(links "")
  set(options "")
  file(GLOB records "${directory}/${package}IntelRuntime-*.cmake")
  foreach(record IN LISTS records)
    include("${record}")
  endforeach()
  get_target_property(available ${target} IMPORTED_CONFIGURATIONS)
  set(configs ${CMAKE_CONFIGURATION_TYPES})
  if(NOT configs)
    set(configs "${CMAKE_BUILD_TYPE}")
  endif()
  if(NOT configs)
    set(configs __DEFAULT)
  endif()
  foreach(config IN LISTS configs)
    if(config STREQUAL "__DEFAULT")
      set(config "")
    endif()
    string(TOUPPER "${config}" upper)
    get_property(mapped TARGET ${target} PROPERTY MAP_IMPORTED_CONFIG_${upper} SET)
    if(mapped)
      get_target_property(candidates ${target} MAP_IMPORTED_CONFIG_${upper})
      if(candidates STREQUAL "")
        set(candidates NOCONFIG)
      endif()
    elseif(upper IN_LIST available)
      set(candidates "${upper}")
    else()
      set(candidates ${available})
    endif()
    set(selected "")
    foreach(candidate IN LISTS candidates)
      if(candidate STREQUAL "")
        set(candidate NOCONFIG)
      endif()
      string(TOUPPER "${candidate}" candidate)
      if(candidate IN_LIST available)
        set(selected "${candidate}")
        break()
      endif()
    endforeach()
    if(NOT selected OR NOT DEFINED ${package}_INTEL_RUNTIME_${selected} OR
        NOT DEFINED ${package}_INTEL_IPO_${selected})
      message(FATAL_ERROR
        "${package} has no Intel runtime record for imported configuration '${config}'. Install a matching archive and its development metadata.")
    endif()
    if(NOT "${${package}_INTEL_IPO_${selected}}" MATCHES "^[01]$")
      message(FATAL_ERROR "Invalid ${package} Intel IPO record for ${selected}")
    endif()
    set(runtime "${${package}_INTEL_RUNTIME_${selected}}")
    if(runtime STREQUAL "MultiThreadedDLL")
      set(mode MD)
    elseif(runtime STREQUAL "MultiThreadedDebugDLL")
      set(mode MDd)
    elseif(runtime STREQUAL "MultiThreaded")
      set(mode MT)
    elseif(runtime STREQUAL "MultiThreadedDebug")
      set(mode MTd)
    else()
      message(FATAL_ERROR "Invalid ${package} Intel CRT record: ${runtime}")
    endif()
    # STREQUAL compares the actual consumer name, unlike CONFIG:name which can
    # also match imported mappings and activate two mutually exclusive modes.
    list(APPEND links
      "$<$<STREQUAL:$<UPPER_CASE:$<CONFIG>>,${upper}>:IntelRuntime::C_${mode}>")
    if(${package}_INTEL_IPO_${selected})
      set(intel_driver "$<OR:$<LINK_LANG_AND_ID:C,IntelLLVM>,$<LINK_LANG_AND_ID:CXX,IntelLLVM>,$<LINK_LANG_AND_ID:Fortran,IntelLLVM>>")
      list(APPEND options
        "$<$<AND:$<STREQUAL:$<UPPER_CASE:$<CONFIG>>,${upper}>,${intel_driver}>:-Qipo>")
    endif()
  endforeach()

  # A repeated find_package may visit another prefix while its targets still
  # refer to the first one. Never append a conflicting runtime to that archive.
  string(SHA256 signature "${links};${options}")
  get_property(initialized TARGET ${target} PROPERTY _INTEL_RUNTIME_IMPORT_SIGNATURE SET)
  if(initialized)
    get_target_property(previous ${target} _INTEL_RUNTIME_IMPORT_SIGNATURE)
    if(NOT previous STREQUAL signature)
      message(FATAL_ERROR
        "Conflicting ${package} Intel runtime records for an already imported target. Use one consistent package prefix and configuration mapping.")
    endif()
    return()
  endif()
  set_property(TARGET ${target} PROPERTY _INTEL_RUNTIME_IMPORT_SIGNATURE "${signature}")
  set_property(TARGET ${target} APPEND PROPERTY INTERFACE_LINK_LIBRARIES "${links}")
  set_property(TARGET ${target} APPEND PROPERTY INTERFACE_LINK_OPTIONS "${options}")
endfunction()


if(TARGET IntelRuntime::C)
  # A parent-provided aggregate is an explicit dependency override. Give every
  # producer variant the same override without inspecting compiler brands.
  foreach(mode MD MDd MT MTd)
    if(NOT TARGET IntelRuntime::C_${mode})
      add_library(IntelRuntime::C_${mode} INTERFACE IMPORTED GLOBAL)
      set_property(TARGET IntelRuntime::C_${mode} PROPERTY
        INTERFACE_LINK_LIBRARIES IntelRuntime::C)
    endif()
  endforeach()
  set(IntelRuntime_FOUND TRUE)
  return()
endif()

include(FindPackageHandleStandardArgs)


# COFF archives begin with linker index members. Read only member headers and
# the first object machine field; no external inspection tool is required.
function(_intelruntime_machine path result)
  set(${result} "" PARENT_SCOPE)
  file(READ "${path}" magic LIMIT 8)
  if(NOT magic STREQUAL "!<arch>\n")
    return()
  endif()
  file(SIZE "${path}" length)
  set(offset 8)
  while(offset LESS length)
    math(EXPR payload "${offset} + 60")
    if(payload GREATER length)
      return()
    endif()
    file(READ "${path}" name OFFSET ${offset} LIMIT 16)
    string(STRIP "${name}" name)
    math(EXPR size_offset "${offset} + 48")
    file(READ "${path}" size OFFSET ${size_offset} LIMIT 10)
    string(STRIP "${size}" size)
    if(NOT size MATCHES "^[0-9]+$")
      return()
    endif()
    if(NOT name STREQUAL "/" AND NOT name STREQUAL "//" AND size GREATER_EQUAL 8)
      file(READ "${path}" header OFFSET ${payload} LIMIT 8 HEX)
      if(header MATCHES "^0000ffff")
        string(SUBSTRING "${header}" 12 4 machine)
      else()
        string(SUBSTRING "${header}" 0 4 machine)
      endif()
      set(${result} "${machine}" PARENT_SCOPE)
      return()
    endif()
    math(EXPR offset "${payload} + ${size} + (${size} % 2)")
  endwhile()
endfunction()


function(_intelruntime_find key name)
  set(candidate "${${key}}")
  while(TRUE)
    if(candidate AND EXISTS "${candidate}" AND NOT IS_DIRECTORY "${candidate}")
      _intelruntime_machine("${candidate}" machine)
      if(NOT _intelruntime_expected OR machine STREQUAL _intelruntime_expected)
        break()
      endif()
      # Ignore only this directory for this search. A multi-architecture SDK
      # may place a usable archive in a later suffix under the same root.
      get_filename_component(rejected_directory "${candidate}" DIRECTORY)
      list(APPEND CMAKE_IGNORE_PATH "${rejected_directory}")
    endif()
    set(candidate "${key}-NOTFOUND")
    find_library(candidate NAMES "${name}"
      HINTS ${IntelRuntime_LIBRARY_DIRS} ${IntelRuntime_ROOT}
      PATH_SUFFIXES lib lib/intel64 lib/ia32
      NO_CMAKE_SYSTEM_PATH NO_CACHE)
    if(NOT candidate)
      set(candidate "${key}-NOTFOUND")
      break()
    endif()
  endwhile()
  set(${key} "${candidate}" CACHE FILEPATH
    "Intel runtime library (required machine ${_intelruntime_expected})" FORCE)
  set(${key} "${candidate}" PARENT_SCOPE)
endfunction()


# Prefer target/compiler architecture over the host processor when a cross
# toolchain or a native Visual Studio platform selects a different machine.
set(_intelruntime_arch "${CMAKE_C_COMPILER_ARCHITECTURE_ID}")
if(NOT _intelruntime_arch)
  set(_intelruntime_arch "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
endif()
if(NOT _intelruntime_arch)
  set(_intelruntime_arch "${CMAKE_Fortran_COMPILER_ARCHITECTURE_ID}")
endif()
if(NOT _intelruntime_arch)
  set(_intelruntime_arch "${CMAKE_GENERATOR_PLATFORM}")
endif()
if(NOT _intelruntime_arch)
  set(_intelruntime_arch "${CMAKE_SYSTEM_PROCESSOR}")
endif()
string(TOLOWER "${_intelruntime_arch}" _intelruntime_arch)
set(_intelruntime_expected "")
if(_intelruntime_arch MATCHES "^arm64ec")
  set(_intelruntime_expected "41a6")
elseif(_intelruntime_arch MATCHES "^(arm64|aarch64)")
  set(_intelruntime_expected "64aa")
elseif(_intelruntime_arch MATCHES "^arm")
  set(_intelruntime_expected "c401")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(_intelruntime_expected "4c01")
elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_intelruntime_expected "6486")
endif()

_intelruntime_find(IntelRuntime_IRC_LIBRARY libircmt)
# Static Intel math objects also name libirc through COFF default-library
# directives. Supplying libircmt alone does not satisfy that filename lookup.
_intelruntime_find(IntelRuntime_STATIC_IRC_LIBRARY libirc)
foreach(mode MD MDd MT MTd)
  if(mode STREQUAL "MD")
    set(math libmmd)
    set(vector svml_dispmd)
  elseif(mode STREQUAL "MDd")
    set(math libmmdd)
    set(vector svml_dispmd)
  else()
    set(math libmmt)
    set(vector svml_dispmt)
  endif()
  _intelruntime_find(IntelRuntime_${mode}_MATH_LIBRARY "${math}")
  _intelruntime_find(IntelRuntime_${mode}_SVML_LIBRARY "${vector}")
endforeach()

# A partial external SDK is usable when it contains the selected CRT variant.
# Missing variants remain explicit missing targets, diagnosed at generation
# only if that configuration is actually linked by the caller.
set(_intelruntime_any_mode FALSE)
foreach(mode MD MDd MT MTd)
  if(IntelRuntime_${mode}_MATH_LIBRARY AND IntelRuntime_${mode}_SVML_LIBRARY)
    set(_intelruntime_any_mode TRUE)
  endif()
endforeach()
find_package_handle_standard_args(IntelRuntime
  REQUIRED_VARS IntelRuntime_IRC_LIBRARY _intelruntime_any_mode
  REASON_FAILURE_MESSAGE
    "Intel C runtime libraries for ${_intelruntime_arch} are required. Set IntelRuntime_ROOT or CMAKE_PREFIX_PATH to an external runtime development installation. No Intel binaries are bundled.")
if(NOT IntelRuntime_FOUND)
  return()
endif()

add_library(IntelRuntime::IRC STATIC IMPORTED GLOBAL)
set_target_properties(IntelRuntime::IRC PROPERTIES
  IMPORTED_LOCATION "${IntelRuntime_IRC_LIBRARY}")
if(IntelRuntime_STATIC_IRC_LIBRARY)
  add_library(IntelRuntime::STATIC_IRC STATIC IMPORTED GLOBAL)
  set_property(TARGET IntelRuntime::STATIC_IRC PROPERTY
    IMPORTED_LOCATION "${IntelRuntime_STATIC_IRC_LIBRARY}")
  # lld-link resolves COFF /DEFAULTLIB:libirc directives by filename even when
  # the same archive appears explicitly by absolute path. Recover this search
  # directory from the consumer's SDK; never export a producer SDK location.
  get_filename_component(_intelruntime_static_directory
    "${IntelRuntime_STATIC_IRC_LIBRARY}" DIRECTORY)
  set_property(TARGET IntelRuntime::STATIC_IRC PROPERTY
    INTERFACE_LINK_DIRECTORIES "${_intelruntime_static_directory}")
endif()
foreach(mode MD MDd MT MTd)
  foreach(part MATH SVML)
    set(library "${IntelRuntime_${mode}_${part}_LIBRARY}")
    if(library)
      if(mode MATCHES "^MD")
        get_filename_component(directory "${library}" DIRECTORY)
        get_filename_component(name "${library}" NAME_WE)
        find_file(IntelRuntime_${mode}_${part}_DLL NAMES "${name}.dll"
          HINTS ${IntelRuntime_RUNTIME_DIRS} "${directory}/../bin"
                "${directory}/../../bin" ${IntelRuntime_ROOT}
          PATH_SUFFIXES bin bin/intel64 bin/ia32
          NO_CMAKE_SYSTEM_PATH)
        if(IntelRuntime_${mode}_${part}_DLL)
          add_library(IntelRuntime::${mode}_${part} SHARED IMPORTED GLOBAL)
          set_target_properties(IntelRuntime::${mode}_${part} PROPERTIES
            IMPORTED_IMPLIB "${library}"
            IMPORTED_LOCATION "${IntelRuntime_${mode}_${part}_DLL}")
        else()
          # An import library is sufficient for linking. Without a known DLL,
          # deployment remains the caller's responsibility, not a guessed path.
          add_library(IntelRuntime::${mode}_${part} UNKNOWN IMPORTED GLOBAL)
          set_property(TARGET IntelRuntime::${mode}_${part} PROPERTY
            IMPORTED_LOCATION "${library}")
        endif()
      else()
        add_library(IntelRuntime::${mode}_${part} STATIC IMPORTED GLOBAL)
        set_property(TARGET IntelRuntime::${mode}_${part} PROPERTY
          IMPORTED_LOCATION "${library}")
      endif()
    endif()
  endforeach()
endforeach()

# These targets keep archive requirements independent of the first package
# discovered in a parent build. Missing selected libraries remain explicit.
foreach(mode MD MDd MT MTd)
  add_library(IntelRuntime::C_${mode} INTERFACE IMPORTED GLOBAL)
  set_property(TARGET IntelRuntime::C_${mode} PROPERTY INTERFACE_LINK_LIBRARIES
    "IntelRuntime::IRC;IntelRuntime::${mode}_MATH;IntelRuntime::${mode}_SVML")
  if(mode MATCHES "^MT")
    set_property(TARGET IntelRuntime::C_${mode} APPEND PROPERTY
      INTERFACE_LINK_LIBRARIES IntelRuntime::STATIC_IRC)
  endif()
endforeach()

if(DEFINED IntelRuntime_RUNTIME_LIBRARY AND IntelRuntime_RUNTIME_LIBRARY STREQUAL "")
  message(FATAL_ERROR
    "IntelRuntime_RUNTIME_LIBRARY must be a nonempty CRT expression. Set an explicit MultiThreaded[Debug][DLL] policy for direct IntelRuntime::C use.")
endif()
if(NOT DEFINED IntelRuntime_RUNTIME_LIBRARY)
  if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "")
    set(IntelRuntime_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
  elseif(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND CMAKE_C_COMPILER_LOADED)
    _intelruntime_detect_crt(IntelRuntime_RUNTIME_LIBRARY)
  else()
    # Without C, there is no C compiler default to probe. Direct aggregate
    # callers using flag-selected CRTs must provide the nonempty policy above;
    # package imports link their recorded per-CRT targets instead.
    set(IntelRuntime_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  endif()
endif()
add_library(IntelRuntime::C INTERFACE IMPORTED GLOBAL)
set_property(TARGET IntelRuntime::C PROPERTY
  INTERFACE_LINK_LIBRARIES IntelRuntime::IRC)
foreach(mode MD MDd MT MTd)
  if(mode STREQUAL "MD")
    set(runtime MultiThreadedDLL)
  elseif(mode STREQUAL "MDd")
    set(runtime MultiThreadedDebugDLL)
  elseif(mode STREQUAL "MT")
    set(runtime MultiThreaded)
  else()
    set(runtime MultiThreadedDebug)
  endif()
  set_property(TARGET IntelRuntime::C APPEND PROPERTY INTERFACE_LINK_LIBRARIES
    "$<$<STREQUAL:${IntelRuntime_RUNTIME_LIBRARY},${runtime}>:IntelRuntime::C_${mode}>")
endforeach()
