add_library(photo_engine
  src/catalog/asset_location.cpp
  src/pipeline/color_adjustments.cpp
  src/pipeline/edit_recipe.cpp
  src/pipeline/render_graph.cpp
)

target_include_directories(photo_engine PUBLIC include)
target_compile_features(photo_engine PUBLIC cxx_std_20)

photo_editor_apply_warnings(photo_engine)
