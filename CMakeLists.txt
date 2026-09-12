cmake_minimum_required(VERSION 3.24)

#-------------------------------------------------------------------------------
# PROJECT SETUP
#-------------------------------------------------------------------------------
project(METIS VERSION 5.2.1 LANGUAGES C)


include(GNUInstallDirs)


#-------------------------------------------------------------------------------
# PROJECT OPTIONS
#-------------------------------------------------------------------------------
# Select one library type per build; inherit BUILD_SHARED_LIBS when set.
# Static libraries are the default; external runtimes may still be required.
option(METIS_BUILD_SHARED_LIBS "Build a shared METIS library" "${BUILD_SHARED_LIBS}")

# Build the command-line applications. Standalone builds enable them;
# subdirectory builds leave executable selection to the parent project.
option(METIS_BUILD_PROGRAMS "Build METIS applications" ${PROJECT_IS_TOP_LEVEL})

# Register API and consumer tests with CTest. Standalone builds enable
# them by default; embedded builds opt in without changing parent test policy.
option(METIS_BUILD_TESTING "Build METIS tests" ${PROJECT_IS_TOP_LEVEL})

# Generate install and package-export rules. Default to the standalone
# project setting so embedding does not unexpectedly install this project.
option(METIS_INSTALL "Install METIS" ${PROJECT_IS_TOP_LEVEL})

# Permit fetching the pinned GKlib source only when local/package resolution
# cannot satisfy the dependency. OFF keeps ordinary configuration offline.
option(METIS_FETCH_GKLIB "Allow downloading the pinned GKlib source" OFF)

# Enable costly internal consistency checks in addition to ordinary
# assertions. Keep OFF for normal builds because these checks add work.
option(METIS_ASSERTIONS_EXPENSIVE "Enable expensive assertions" OFF)

# Tune generated code for the build CPU only when explicitly requested.
# Keep OFF for portable binaries; cross and multi-architecture builds reject it.
option(METIS_NATIVE_OPTIMIZATION "Optimize for the build machine" OFF)

# Request gprof instrumentation after checking compiler and linker support.
# Keep OFF for production binaries and fail if explicitly requested but unusable.
option(METIS_GPROF "Enable gprof instrumentation" OFF)

# Treat warnings from this project as errors without changing parent targets.
# Keep OFF for consumers using compilers with different warning diagnostics.
option(METIS_WARNINGS_AS_ERRORS "Treat METIS warnings as errors" OFF)

# Choose the installed idx_t ABI width. Only 32 and 64 are supported;
# all callers and dependent METIS interfaces must use the same width.
set(METIS_IDXTYPEWIDTH 32 CACHE STRING "Index width: 32 or 64")

# Choose the installed real_t ABI width independently of idx_t.
# The generated public header rejects conflicting consumer definitions.
set(METIS_REALTYPEWIDTH 32 CACHE STRING "Real width: 32 or 64")

# AUTO enables ordinary assertions in Debug and disables them otherwise.
# ON/OFF override this project only, without exporting NDEBUG to consumers.
set(METIS_ASSERTIONS AUTO CACHE STRING "Assertions: AUTO, ON or OFF")

# AUTO enables internal debug instrumentation in Debug configurations.
# ON/OFF provide an explicit policy for this project across configurations.
set(METIS_DEBUG AUTO CACHE STRING "Debug code: AUTO, ON or OFF")

# AUTO inherits explicit parent IPO policy; otherwise static targets stay
# portable and optimized shared configurations probe IPO support. ON requires
# support; OFF disables IPO on this project without changing parent targets.
set(METIS_IPO AUTO CACHE STRING "IPO: AUTO, ON or OFF")

# Request a semicolon-separated list of supported sanitizers. Empty means
# none; explicit requests require usable compile and final-link runtimes.
set(METIS_SANITIZERS "" CACHE STRING "Sanitizers: address;undefined")

# Select AUTO, SYSTEM or SOURCE dependency resolution. Existing parent
# targets take precedence; SOURCE and AUTO can use an explicit source path.
set(METIS_GKLIB_PROVIDER AUTO CACHE STRING "GKlib provider: AUTO, SYSTEM or SOURCE")

# Use an existing compatible GKlib checkout without downloading it.
# An empty path leaves discovery to the selected dependency provider.
set(METIS_GKLIB_SOURCE_DIR "" CACHE PATH "Existing GKlib source directory")

# Expose valid cache choices and reject unsupported command-line values early.
foreach(width METIS_IDXTYPEWIDTH METIS_REALTYPEWIDTH)
  set_property(CACHE ${width} PROPERTY STRINGS 32 64)
  if(NOT "${${width}}" MATCHES "^(32|64)$")
    message(FATAL_ERROR "${width} must be 32 or 64")
  endif()
endforeach()

foreach(mode METIS_ASSERTIONS METIS_DEBUG METIS_IPO)
  set_property(CACHE ${mode} PROPERTY STRINGS AUTO ON OFF)
  if(NOT "${${mode}}" MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "${mode} must be AUTO, ON or OFF")
  endif()
endforeach()
set_property(CACHE METIS_GKLIB_PROVIDER PROPERTY STRINGS AUTO SYSTEM SOURCE)
if(NOT METIS_GKLIB_PROVIDER MATCHES "^(AUTO|SYSTEM|SOURCE)$")
  message(FATAL_ERROR "METIS_GKLIB_PROVIDER must be AUTO, SYSTEM or SOURCE")
endif()


# Add Python maintenance-tool regressions when METIS_BUILD_TESTING is enabled.
# Keep OFF so ordinary library builds need neither Python nor Git.
option(METIS_BUILD_DEVELOPER_TESTING "Run Python maintenance tool regressions" OFF)

# Add nested configure/build/install consumer tests when METIS_BUILD_TESTING is
# enabled. Keep OFF for fast ordinary builds; fixtures own their binary trees.
option(METIS_BUILD_INTEGRATION_TESTING "Run build and install integration tests" OFF)


#-------------------------------------------------------------------------------
# TARGET CONFIGURATION
#-------------------------------------------------------------------------------
# Apply the same private policy to the library and its command-line programs.
# These functions do not change directory-wide flags or parent targets.
include(CheckCCompilerFlag)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MetisIPO.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/MetisCheckLink.cmake")


function(metis_copy_runtime target)
  # Runtime DLL discovery is meaningful only for Windows executable targets.
  if(WIN32)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND}
        "-DLIBRARIES=$<TARGET_RUNTIME_DLLS:${target}>"
        "-DDESTINATION=$<TARGET_FILE_DIR:${target}>"
        -P "${PROJECT_SOURCE_DIR}/cmake/CopyRuntime.cmake"
      VERBATIM)
  endif()
endfunction()


function(metis_target_options target)
  # Keep language and warnings target-local for clean subproject integration.
  target_compile_features(${target} PRIVATE c_std_99)
  set_target_properties(${target} PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS OFF)
  if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(${target} PRIVATE /W3)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(METIS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wno-unused-parameter)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|IntelLLVM")
      target_compile_options(${target} PRIVATE -fno-strict-aliasing)
    endif()
    if(METIS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()

  # AUTO follows the active configuration; explicit modes override NDEBUG.
  if(METIS_ASSERTIONS STREQUAL "ON")
    target_compile_definitions(${target} PRIVATE METIS_ASSERTIONS_ENABLED=1)
    if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      target_compile_options(${target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${target} PRIVATE -UNDEBUG)
    endif()
  elseif(METIS_ASSERTIONS STREQUAL "OFF")
    target_compile_definitions(${target} PRIVATE NDEBUG METIS_ASSERTIONS_ENABLED=0)
  else()
    target_compile_definitions(${target} PRIVATE $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
      METIS_ASSERTIONS_ENABLED=$<IF:$<CONFIG:Debug>,1,0>)
  endif()
  target_compile_definitions(${target} PRIVATE
    METIS_ASSERTIONS_EXPENSIVE_ENABLED=$<BOOL:${METIS_ASSERTIONS_EXPENSIVE}>)
  if(NOT METIS_ASSERTIONS_EXPENSIVE)
    target_compile_definitions(${target} PRIVATE NDEBUG2)
  endif()
  if(METIS_DEBUG STREQUAL "ON")
    target_compile_definitions(${target} PRIVATE DEBUG)
  elseif(METIS_DEBUG STREQUAL "AUTO")
    target_compile_definitions(${target} PRIVATE $<$<CONFIG:Debug>:DEBUG>)
  endif()

  # Native tuning is valid only for a single native compiler architecture.
  if(METIS_NATIVE_OPTIMIZATION)
    if(MSVC OR CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
      message(FATAL_ERROR "Native optimization is unavailable with the MSVC frontend; set explicit architecture flags instead")
    endif()
    list(LENGTH CMAKE_OSX_ARCHITECTURES arch_count)
    if(CMAKE_CROSSCOMPILING OR arch_count GREATER 1)
      message(FATAL_ERROR "Native optimization requires a single native architecture")
    endif()
    check_c_compiler_flag("-march=native" METIS_HAVE_MARCH_NATIVE)
    if(NOT METIS_HAVE_MARCH_NATIVE)
      message(FATAL_ERROR "Native optimization is unsupported by this compiler")
    endif()
    target_compile_options(${target} PRIVATE -march=native)
  endif()

  # Link probes verify that instrumentation runtimes are usable, not just parsed.
  if(METIS_GPROF)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-pg")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-pg")
    metis_check_link("int main(void) { return 0; }" METIS_HAVE_GPROF_RUNTIME)
    cmake_pop_check_state()
    if(NOT METIS_HAVE_GPROF_RUNTIME)
      message(FATAL_ERROR "gprof instrumentation is unsupported by this compiler")
    endif()
    target_compile_options(${target} PRIVATE -pg)
    target_link_options(${target} PUBLIC -pg)
  endif()

  foreach(sanitizer IN LISTS METIS_SANITIZERS)
    if(NOT sanitizer MATCHES "^(address|undefined)$")
      message(FATAL_ERROR "Unsupported sanitizer: ${sanitizer}")
    endif()
    string(TOUPPER "${sanitizer}" sanitizer_id)
    if(MSVC AND NOT CMAKE_C_COMPILER_ID MATCHES "Clang|IntelLLVM")
      if(NOT sanitizer STREQUAL "address")
        message(FATAL_ERROR "MSVC does not support ${sanitizer}")
      endif()
      set(sanitizer_flag /fsanitize=address)
      set(sanitizer_link_flag "")
    else()
      set(sanitizer_flag "-fsanitize=${sanitizer}")
      set(sanitizer_link_flag "${sanitizer_flag}")
    endif()
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "${sanitizer_flag}")
    set(CMAKE_REQUIRED_LINK_OPTIONS "${sanitizer_link_flag}")
    # A trivial main() may link without the sanitizer runtime. Instrument real
    # memory and arithmetic operations so missing runtimes fail configuration.
    metis_check_link("int main(int argc, char **argv) {
      volatile int values[2] = {1, 2};
      values[argc] = argc * argc;
      return values[argc] + (argv[0] != 0);
    }" METIS_${sanitizer_id}_LINKS)
    cmake_pop_check_state()
    if(NOT METIS_${sanitizer_id}_LINKS)
      message(FATAL_ERROR "Requested sanitizer is unavailable: ${sanitizer}")
    endif()
    target_compile_options(${target} PRIVATE "${sanitizer_flag}")
    if(sanitizer_link_flag)
      target_link_options(${target} PUBLIC "${sanitizer_link_flag}")
    endif()
  endforeach()

  metis_target_ipo(${target})
endfunction()


#-------------------------------------------------------------------------------
# PLATFORM CAPABILITIES
#-------------------------------------------------------------------------------
# Compile/link probes describe the target platform without executing target
# programs. Reusable probe helpers isolate check state from parent projects.
include(CMakePushCheckState)

# Probe libc first and fall back to the platform math library when required.
cmake_push_check_state(RESET)
set(_math_source "#include <math.h>
  int main(void) {
    double (*volatile operation)(double) = sqrt;
    return operation(4.0) != 2.0;
  }")
metis_check_link("${_math_source}" METIS_MATH_IN_LIBC)
set(METIS_MATH_LIBRARY "")
if(NOT METIS_MATH_IN_LIBC)
  set(CMAKE_REQUIRED_LIBRARIES m)
  metis_check_link("${_math_source}" METIS_MATH_IN_LIBM)
  if(NOT METIS_MATH_IN_LIBM)
    message(FATAL_ERROR "A linkable C math library is required")
  endif()
  set(METIS_MATH_LIBRARY m)
endif()
cmake_pop_check_state()
unset(_math_source)


# Intel C objects may require Intel math/vector runtimes even without IPO.
# Keep discovery local to the build; exported packages rediscover their SDK.
set(METIS_CONFIG_NEEDS_INTEL_RUNTIME OFF)
if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
  function(_metis_find_intel_runtime)
    list(PREPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
    set(IntelRuntime_LIBRARY_DIRS ${CMAKE_C_IMPLICIT_LINK_DIRECTORIES})
    get_filename_component(compiler_directory "${CMAKE_C_COMPILER}" DIRECTORY)
    list(APPEND IntelRuntime_LIBRARY_DIRS "${compiler_directory}/../lib")
    find_package(IntelRuntime REQUIRED MODULE)
  endfunction()
  _metis_find_intel_runtime()
  if(NOT METIS_BUILD_SHARED_LIBS)
    set(METIS_CONFIG_NEEDS_INTEL_RUNTIME ON)
  endif()
endif()


# Register tests before descending so subprojects share the top-level CTest setup.
if(METIS_BUILD_TESTING)
  if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
  endif()
  enable_testing()
endif()


# Resolve GKlib before defining targets that link to its imported target.
add_subdirectory(ext)


# Bracket METIS install rules so the ownership manifest excludes dependencies.
if(METIS_INSTALL)
  include(cmake/InstallManifest.cmake)
  metis_install_manifest_begin()
endif()

# Generate the public headers before targets consume their build-tree paths.
add_subdirectory(include)

# The implementation directory owns source lists and target-local properties.
add_subdirectory(src)

# Programs consume private implementation interfaces without installing them.
if(METIS_BUILD_PROGRAMS)
  add_subdirectory(apps)
endif()

# Tests own their fixtures and generated outputs in the binary tree.
if(METIS_BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(METIS_INSTALL)
  include(cmake/MetisInstall.cmake)
  metis_install_manifest_end()
endif()

# Packaging is an entry-point concern and stays disabled for subproject use.
if(PROJECT_IS_TOP_LEVEL)
  include(cmake/MetisPackaging.cmake)
endif()
