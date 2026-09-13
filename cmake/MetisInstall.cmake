include(CMakePackageConfigHelpers)


# A shared METIS library hides a static GKlib dependency from package consumers.
set(METIS_CONFIG_NEEDS_GKLIB ON)
get_target_property(gklib_type GKlib::GKlib TYPE)
if(METIS_BUILD_SHARED_LIBS AND gklib_type STREQUAL "STATIC_LIBRARY")
  # The static dependency is absorbed into the shared library.
  set(METIS_CONFIG_NEEDS_GKLIB OFF)
endif()

# Export the public target and its package metadata as development artifacts.
set(metis_cmakedir "${CMAKE_INSTALL_LIBDIR}/cmake/METIS")
set(METIS_CONFIG_NEEDS_ASAN OFF)
if(TARGET METIS::ASanRuntime AND NOT METIS_BUILD_SHARED_LIBS)
  set(METIS_CONFIG_NEEDS_ASAN ON)
  windows_asan_install(METIS "${metis_cmakedir}" METIS_Development)
endif()

configure_package_config_file(cmake/METISConfig.cmake.in
  "${PROJECT_BINARY_DIR}/METISConfig.cmake" INSTALL_DESTINATION "${metis_cmakedir}")
write_basic_package_version_file("${PROJECT_BINARY_DIR}/METISConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(EXPORT METISTargets NAMESPACE METIS:: DESTINATION "${metis_cmakedir}" COMPONENT METIS_Development)
install(FILES "${PROJECT_BINARY_DIR}/METISConfig.cmake"
  "${PROJECT_BINARY_DIR}/METISConfigVersion.cmake"
  DESTINATION "${metis_cmakedir}" COMPONENT METIS_Development)
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/METIS" COMPONENT METIS_Development)

# The generated script removes files recorded by this project's install bracket.
set(manifest_project METIS)
set(manifest_directory "${PROJECT_BINARY_DIR}/install-manifests")
configure_file(cmake/Uninstall.cmake.in "${PROJECT_BINARY_DIR}/MetisUninstall.cmake" @ONLY)
add_custom_target(metis-uninstall
  COMMAND ${CMAKE_COMMAND} -P "${PROJECT_BINARY_DIR}/MetisUninstall.cmake")

# Install only the portable discovery module, never producer machine paths.
install(FILES "${PROJECT_SOURCE_DIR}/cmake/FindIntelRuntime.cmake"
  DESTINATION "${metis_cmakedir}" COMPONENT METIS_Development)

if(METIS_CONFIG_NEEDS_INTEL_RUNTIME)
  intel_runtime_install(metis METIS "${metis_cmakedir}" METIS_Development)
endif()
