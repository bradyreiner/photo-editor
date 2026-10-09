function(photo_editor_add_creative_fonts target)
  set(font_root "${PROJECT_SOURCE_DIR}/apps/desktop/assets/fonts")
  file(GLOB_RECURSE creative_font_files CONFIGURE_DEPENDS
    "${font_root}/*.ttf" "${font_root}/OFL.txt")
  # License files remain beside each unmodified font inside the executable.
  list(APPEND creative_font_files "${font_root}/manifest.json")
  qt_add_resources(${target} "looksy_creative_fonts"
    PREFIX "/photo-editor/fonts"
    BASE "${font_root}"
    FILES ${creative_font_files})
endfunction()
