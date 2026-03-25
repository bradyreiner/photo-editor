photo_editor_detect_cargo(PHOTO_EDITOR_CARGO_EXECUTABLE)

if(PHOTO_EDITOR_ENABLE_RUST AND PHOTO_EDITOR_CARGO_EXECUTABLE)
  add_library(photo_rust_ffi INTERFACE)
  target_compile_definitions(photo_rust_ffi INTERFACE PHOTO_EDITOR_RUST_ENABLED=1)
else()
  add_library(photo_rust_ffi INTERFACE)
  target_compile_definitions(photo_rust_ffi INTERFACE PHOTO_EDITOR_RUST_ENABLED=0)
endif()
