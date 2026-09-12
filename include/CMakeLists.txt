configure_file(metis.h.in "${CMAKE_CURRENT_BINARY_DIR}/metis.h" @ONLY)

# Install the configured API and generated visibility declarations together.
if(METIS_INSTALL)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/metis.h"
    "${CMAKE_CURRENT_BINARY_DIR}/metis_export.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" COMPONENT METIS_Development)
endif()
