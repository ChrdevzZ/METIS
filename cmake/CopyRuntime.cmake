cmake_minimum_required(VERSION 3.24)


# Copy each resolved runtime dependency beside its consuming executable. Windows
# may briefly deny access while another post-build step replaces the same DLL.
foreach(library IN LISTS LIBRARIES)
  if("${library}" STREQUAL "")
    continue()
  endif()
  if(NOT EXISTS "${library}")
    message(FATAL_ERROR "Runtime dependency does not exist: ${library}")
  endif()

  get_filename_component(name "${library}" NAME)
  set(destination "${DESTINATION}/${name}")
  set(attempt 1)
  while(TRUE)
    file(COPY_FILE "${library}" "${destination}"
      RESULT result ONLY_IF_DIFFERENT)
    if(result STREQUAL "0")
      break()
    endif()

    set(transient FALSE)
    if(WIN32 AND
        (result MATCHES "^Permission denied( \\((input|output)\\))?$" OR
         result MATCHES "^The process cannot access the file because it is being used by another process( \\((input|output)\\))?$"))
      set(transient TRUE)
    endif()
    if(NOT transient OR attempt GREATER_EQUAL 5)
      message(FATAL_ERROR
        "Failed to copy runtime dependency ${library} to ${destination} after ${attempt} attempts: ${result}")
    endif()

    execute_process(COMMAND "${CMAKE_COMMAND}" -E sleep 0.1)
    math(EXPR attempt "${attempt} + 1")
  endwhile()
endforeach()
