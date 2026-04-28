add_executable(render_graph_smoke_test render_graph_smoke_test.cpp)
target_link_libraries(render_graph_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_graph_smoke_test)

add_test(NAME render_graph_smoke_test COMMAND render_graph_smoke_test)

add_executable(color_pipeline_test color_pipeline_test.cpp)
target_link_libraries(color_pipeline_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_pipeline_test)

add_test(NAME color_pipeline_test COMMAND color_pipeline_test)

add_executable(catalog_contract_smoke_test catalog_contract_smoke_test.cpp)
target_link_libraries(catalog_contract_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(catalog_contract_smoke_test)

add_test(NAME catalog_contract_smoke_test COMMAND catalog_contract_smoke_test)

if(PHOTO_EDITOR_BUILD_DESKTOP)
  find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Qml Quick QuickControls2 Widgets)

  set(photo_editor_desktop_image_decoder_sources
    ../apps/desktop/src/raw_image_decoder_stub.cpp)
  set(photo_editor_desktop_image_decoder_frameworks)
  set(photo_editor_desktop_observability_sources
    ../apps/desktop/src/observability.cpp)
  set(photo_editor_desktop_brush_preview_sources
    ../apps/desktop/src/brush_preview_compositor.h
    ../apps/desktop/src/brush_preview_compositor.cpp
    ../apps/desktop/src/editor_preview_texture.h
    ../apps/desktop/src/editor_preview_texture.cpp)
  set(photo_editor_desktop_ai_model_sources
    ../apps/desktop/src/ai_model_runtime.h
    ../apps/desktop/src/ai_model_runtime.cpp)
  set(photo_editor_desktop_brush_preview_frameworks)
  if(APPLE)
    if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
      list(APPEND photo_editor_desktop_brush_preview_sources
        ../apps/desktop/src/brush_preview_compositor_metal.mm
        ../apps/desktop/src/editor_preview_texture_metal.mm)
      set(photo_editor_desktop_brush_preview_frameworks
        "-framework Metal"
        "-framework Foundation")
      set_source_files_properties(
        ../apps/desktop/src/brush_preview_compositor_metal.mm
        ../apps/desktop/src/editor_preview_texture_metal.mm
        PROPERTIES
          COMPILE_OPTIONS "-fobjc-arc")
    endif()
    list(REMOVE_ITEM photo_editor_desktop_image_decoder_sources ../apps/desktop/src/raw_image_decoder_stub.cpp)
    list(APPEND photo_editor_desktop_image_decoder_sources ../apps/desktop/src/raw_image_decoder.mm)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(COREIMAGE_FRAMEWORK CoreImage)
    find_library(IMAGEIO_FRAMEWORK ImageIO)
    if(COREGRAPHICS_FRAMEWORK AND COREIMAGE_FRAMEWORK AND IMAGEIO_FRAMEWORK)
      set(photo_editor_desktop_image_decoder_frameworks
        ${COREGRAPHICS_FRAMEWORK}
        ${COREIMAGE_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK})
    endif()
    set_source_files_properties(
      ../apps/desktop/src/raw_image_decoder.mm
      PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(
      ../apps/desktop/src/brush_live_preview_item.cpp
      ../apps/desktop/src/editor_preview_texture_item.cpp
      PROPERTIES
        LANGUAGE OBJCXX)
  endif()

  add_executable(
    app_shell_controller_test
    app_shell_controller_test.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(app_shell_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    app_shell_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    app_shell_controller_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(app_shell_controller_test)

  add_test(NAME app_shell_controller_test COMMAND app_shell_controller_test)

  add_executable(
    app_bootstrap_test
    app_bootstrap_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(app_bootstrap_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    app_bootstrap_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    app_bootstrap_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(app_bootstrap_test)

  add_test(NAME app_bootstrap_test COMMAND app_bootstrap_test)

  add_executable(
    catalog_asset_model_test
    catalog_asset_model_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(catalog_asset_model_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    catalog_asset_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    catalog_asset_model_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(catalog_asset_model_test)

  add_test(NAME catalog_asset_model_test COMMAND catalog_asset_model_test)

  add_executable(
    desktop_shell_smoke_test
    desktop_shell_smoke_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ../apps/desktop/src/brush_live_preview_item.h
    ../apps/desktop/src/brush_live_preview_item.cpp
    ../apps/desktop/src/editor_preview_texture_item.h
    ../apps/desktop/src/editor_preview_texture_item.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/macos_updater.h
    ../apps/desktop/src/macos_updater_stub.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp)
  set_target_properties(desktop_shell_smoke_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    desktop_shell_smoke_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    desktop_shell_smoke_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    desktop_shell_smoke_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Multimedia
      Qt6::Qml
      Qt6::Quick
      Qt6::QuickControls2
      Qt6::Widgets
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine
      photo_rust_ffi)
  photo_editor_apply_warnings(desktop_shell_smoke_test)

  add_test(NAME desktop_shell_smoke_test COMMAND desktop_shell_smoke_test)

  add_executable(
    job_queue_model_test
    job_queue_model_test.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(job_queue_model_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    job_queue_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    job_queue_model_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(job_queue_model_test)

  add_test(NAME job_queue_model_test COMMAND job_queue_model_test)

  add_executable(
    editor_preview_math_test
    editor_preview_math_test.cpp)
  target_link_libraries(
    editor_preview_math_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  target_link_libraries(editor_preview_math_test PRIVATE photo_engine)
  photo_editor_apply_warnings(editor_preview_math_test)

  add_test(NAME editor_preview_math_test COMMAND editor_preview_math_test)

  add_executable(
    edit_recipe_state_test
    edit_recipe_state_test.cpp)
  target_include_directories(
    edit_recipe_state_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    edit_recipe_state_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(edit_recipe_state_test)

  add_test(NAME edit_recipe_state_test COMMAND edit_recipe_state_test)

  add_executable(
    editor_preview_controller_test
    editor_preview_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_controller_test)

  add_test(NAME editor_preview_controller_test COMMAND editor_preview_controller_test)

  add_executable(
    export_job_controller_test
    export_job_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(export_job_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    export_job_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    export_job_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(export_job_controller_test)

  add_test(NAME export_job_controller_test COMMAND export_job_controller_test)

  add_executable(
    carousel_document_controller_test
    carousel_document_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ${photo_editor_desktop_image_decoder_sources})
  set_target_properties(carousel_document_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    carousel_document_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    carousel_document_controller_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(carousel_document_controller_test)

  add_test(NAME carousel_document_controller_test COMMAND carousel_document_controller_test)

  add_executable(
    timeline_document_controller_test
    timeline_document_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(timeline_document_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    timeline_document_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_document_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(timeline_document_controller_test)

  add_test(NAME timeline_document_controller_test COMMAND timeline_document_controller_test)

  add_executable(
    timeline_playback_controller_test
    timeline_playback_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp)
  set_target_properties(timeline_playback_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    timeline_playback_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_playback_controller_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      Qt6::Multimedia)
  photo_editor_apply_warnings(timeline_playback_controller_test)

  add_test(NAME timeline_playback_controller_test COMMAND timeline_playback_controller_test)
endif()
