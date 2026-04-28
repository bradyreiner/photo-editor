set(photo_engine_sources
  src/catalog/asset_location.cpp
  src/pipeline/color_adjustments.cpp
  src/pipeline/color_pipeline.cpp
  src/pipeline/edit_recipe.cpp
  src/pipeline/render_graph.cpp
)

if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  enable_language(OBJCXX)
  list(APPEND photo_engine_sources
    src/pipeline/metal/metal_pipeline_backend.mm
  )
endif()

add_library(photo_engine ${photo_engine_sources})

target_include_directories(photo_engine PUBLIC include)
target_include_directories(photo_engine PRIVATE src)
target_compile_features(photo_engine PUBLIC cxx_std_20)

if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  target_link_libraries(photo_engine PRIVATE "-framework Metal" "-framework Foundation")
  target_compile_definitions(photo_engine PUBLIC PHOTO_EDITOR_HAS_METAL_BACKEND=1)
  set_source_files_properties(
    src/pipeline/metal/metal_pipeline_backend.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

photo_editor_apply_warnings(photo_engine)
