function(photo_editor_enforce_qt_free_target target_name)
  if(NOT TARGET "${target_name}")
    message(FATAL_ERROR "Architecture boundary target does not exist: ${target_name}")
  endif()

  get_target_property(target_sources "${target_name}" SOURCES)
  foreach(source_path IN LISTS target_sources)
    if(IS_ABSOLUTE "${source_path}")
      set(resolved_source "${source_path}")
    else()
      get_filename_component(
        resolved_source
        "${source_path}"
        ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    if(NOT EXISTS "${resolved_source}" OR
       NOT resolved_source MATCHES "\\.(c|cc|cpp|cxx|h|hh|hpp|hxx|m|mm)$")
      continue()
    endif()
    file(READ "${resolved_source}" source_contents)
    if(source_contents MATCHES "#[ \t]*include[ \t]*[<\"](Q[A-Z]|Qt[/A-Z])")
      message(FATAL_ERROR
        "${target_name} is Qt-independent but ${resolved_source} imports a Qt header")
    endif()
  endforeach()

  get_target_property(target_links "${target_name}" LINK_LIBRARIES)
  if(target_links)
    foreach(link_item IN LISTS target_links)
      if(link_item MATCHES "(^|;)Qt[0-9]*::")
        message(FATAL_ERROR
          "${target_name} is Qt-independent but links ${link_item}")
      endif()
    endforeach()
  endif()
endfunction()
