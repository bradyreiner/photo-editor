function(photo_editor_detect_cargo output_var)
  find_program(_cargo_program cargo)
  set(${output_var} "${_cargo_program}" PARENT_SCOPE)
endfunction()
