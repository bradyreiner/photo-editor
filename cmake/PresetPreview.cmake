function(photo_editor_add_preset_preview target)
  qt_add_resources(${target} "looksy_preset_preview"
    PREFIX "/photo-editor/presets"
    BASE "${PROJECT_SOURCE_DIR}/apps/desktop/assets/presets"
    FILES
      "${PROJECT_SOURCE_DIR}/apps/desktop/assets/presets/mountain-preview.jpg"
      "${PROJECT_SOURCE_DIR}/apps/desktop/assets/presets/README.md")
endfunction()
