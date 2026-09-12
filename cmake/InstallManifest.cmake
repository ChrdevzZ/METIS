# Capture only this project's install rules, after external dependencies.
function(metis_install_manifest_begin)
  set(manifest_project METIS)
  set(manifest_directory "${PROJECT_BINARY_DIR}/install-manifests")
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RecordInstall.cmake.in"
    "${PROJECT_BINARY_DIR}/RecordInstall.cmake" @ONLY)
  install(CODE "list(LENGTH CMAKE_INSTALL_MANIFEST_FILES _METIS_install_start)"
    ALL_COMPONENTS)
endfunction()

# Record only files added since the matching begin marker.
function(metis_install_manifest_end)
  install(SCRIPT "${PROJECT_BINARY_DIR}/RecordInstall.cmake" ALL_COMPONENTS)
endfunction()
