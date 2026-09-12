set(metis_sources
  auxapi.c balance.c bucketsort.c checkgraph.c coarsen.c compress.c contig.c
  debug.c fm.c fortran.c frename.c gklib.c graph.c initpart.c kmetis.c
  kwayfm.c kwayrefine.c mcutil.c mesh.c meshpart.c minconn.c mincover.c mmd.c
  ometis.c options.c parmetis.c pmetis.c refine.c separator.c sfm.c srefine.c
  stat.c timing.c util.c wspace.c)

# Keep one canonical target name across static and shared builds.
if(METIS_BUILD_SHARED_LIBS)
  add_library(metis SHARED ${metis_sources})
else()
  add_library(metis STATIC ${metis_sources})
endif()
add_library(METIS::metis ALIAS metis)

# Preserve ownership across the private application and GKlib DLL interfaces.
include("${PROJECT_SOURCE_DIR}/cmake/MetisCRT.cmake")
metis_check_crt(metis)

if(WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
  intel_runtime_link(metis)
endif()

# Only the generated public headers are visible to downstream consumers.
target_include_directories(metis
  PUBLIC $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/include>
         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(metis PRIVATE GKlib::GKlib)
if(METIS_MATH_LIBRARY)
  target_link_libraries(metis PRIVATE "${METIS_MATH_LIBRARY}")
endif()

set_target_properties(metis PROPERTIES VERSION ${PROJECT_VERSION} SOVERSION 5
  WINDOWS_EXPORT_ALL_SYMBOLS OFF)
if(MINGW AND METIS_BUILD_SHARED_LIBS)
  # The CLI and Fortran wrappers also use symbols outside the public C API.
  target_link_options(metis PRIVATE -Wl,--export-all-symbols)
endif()
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
  set_property(TARGET metis PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

include(GenerateExportHeader)
generate_export_header(metis EXPORT_FILE_NAME "${PROJECT_BINARY_DIR}/include/metis_export.h")
if(NOT METIS_BUILD_SHARED_LIBS)
  target_compile_definitions(metis PUBLIC METIS_STATIC_DEFINE)
endif()

metis_target_options(metis)

if(METIS_INSTALL)
  install(TARGETS metis EXPORT METISTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT METIS_Runtime
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT METIS_Runtime
      NAMELINK_COMPONENT METIS_Development
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT METIS_Development)
endif()
