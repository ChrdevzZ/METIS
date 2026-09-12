# Source packages omit repository state, build trees and local tool caches.
set(CPACK_PACKAGE_NAME metis)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_SOURCE_GENERATOR TGZ)
# Preserve regex escapes when CPack writes and reloads its configuration.
set(CPACK_VERBATIM_VARIABLES TRUE)
set(CPACK_SOURCE_IGNORE_FILES "/\\.git(/|$);/build[^/]*/;/out/;/\\.aijournal/;/\\.aisources/;CMakeUserPresets.json;/__pycache__/;[.]pyc$")

# CPack does not read .gitignore. Exclude this binary tree even when the caller
# chooses an arbitrary directory name, and omit loose local compiler outputs.
set(_metis_binary_pattern "${PROJECT_BINARY_DIR}")
foreach(character "." "+" "*" "?" "^" "$" "(" ")" "[" "]" "|")
  string(REPLACE "${character}" "\\${character}"
    _metis_binary_pattern "${_metis_binary_pattern}")
endforeach()
list(APPEND CPACK_SOURCE_IGNORE_FILES
  "${_metis_binary_pattern}/"
  "[.](obj|o|a|lib|dll|exe|pdb|ilk|exp|pyc)$")
include(CPack)
