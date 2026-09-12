cmake_minimum_required(VERSION 3.24)


# Copy each discovered runtime dependency beside its consuming executable.
foreach(library IN LISTS LIBRARIES)
  get_filename_component(name "${library}" NAME)
  file(COPY_FILE "${library}" "${DESTINATION}/${name}" ONLY_IF_DIFFERENT)
endforeach()
