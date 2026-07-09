add_executable(render_graph_smoke_test render_graph_smoke_test.cpp)
target_link_libraries(render_graph_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_graph_smoke_test)

add_test(NAME render_graph_smoke_test COMMAND render_graph_smoke_test)

add_executable(color_pipeline_test color_pipeline_test.cpp)
target_link_libraries(color_pipeline_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_pipeline_test)

add_test(NAME color_pipeline_test COMMAND color_pipeline_test)

add_executable(cube_lut_test cube_lut_test.cpp)
target_link_libraries(cube_lut_test PRIVATE photo_engine)
photo_editor_apply_warnings(cube_lut_test)

add_test(NAME cube_lut_test COMMAND cube_lut_test)

add_executable(raw_corpus_test raw_corpus_test.cpp)
target_link_libraries(raw_corpus_test PRIVATE photo_engine)
photo_editor_apply_warnings(raw_corpus_test)

add_test(NAME raw_corpus_test COMMAND raw_corpus_test)

add_executable(lens_profile_test lens_profile_test.cpp)
target_link_libraries(lens_profile_test PRIVATE photo_engine)
photo_editor_apply_warnings(lens_profile_test)

add_test(NAME lens_profile_test COMMAND lens_profile_test)

add_executable(color_chart_validation_test color_chart_validation_test.cpp)
target_link_libraries(color_chart_validation_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_chart_validation_test)

add_test(NAME color_chart_validation_test COMMAND color_chart_validation_test)

add_executable(color_tool_invariant_test color_tool_invariant_test.cpp)
target_link_libraries(color_tool_invariant_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_tool_invariant_test)

add_test(NAME color_tool_invariant_test COMMAND color_tool_invariant_test)

add_executable(color_science_sweep_test color_science_sweep_test.cpp)
target_link_libraries(color_science_sweep_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_science_sweep_test)

add_test(NAME color_science_sweep_test COMMAND color_science_sweep_test)

add_executable(catalog_contract_smoke_test catalog_contract_smoke_test.cpp)
target_link_libraries(catalog_contract_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(catalog_contract_smoke_test)

add_test(NAME catalog_contract_smoke_test COMMAND catalog_contract_smoke_test)

if(PHOTO_EDITOR_BUILD_DESKTOP)
  find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Network Qml Quick QuickControls2 Widgets)

  set(photo_editor_desktop_image_decoder_sources
    ../apps/desktop/src/raw_image_decoder_stub.cpp)
  set(photo_editor_desktop_image_decoder_frameworks)
  set(photo_editor_desktop_observability_sources
    ../apps/desktop/src/observability.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp)
  set(photo_editor_desktop_brush_preview_sources
    ../apps/desktop/src/brush_preview_compositor.h
    ../apps/desktop/src/brush_preview_compositor.cpp
    ../apps/desktop/src/editor_preview_texture.h
    ../apps/desktop/src/editor_preview_texture.cpp)
  set(photo_editor_desktop_ai_model_sources
    ../apps/desktop/src/ai_model_runtime.h
    ../apps/desktop/src/ai_model_runtime.cpp)
  set(photo_editor_desktop_mask_alpha_provider_sources
    ../apps/desktop/src/mask_alpha_provider.h
    ../apps/desktop/src/mask_alpha_provider.cpp)
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
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp)
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
    agent_edit_style_test
    agent_edit_style_test.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_edit_style_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_edit_style_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(agent_edit_style_test)

  add_test(NAME agent_edit_style_test COMMAND agent_edit_style_test)

  add_executable(
    agent_edit_recipe_patch_test
    agent_edit_recipe_patch_test.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_edit_recipe_patch_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_edit_recipe_patch_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(agent_edit_recipe_patch_test)

  add_test(NAME agent_edit_recipe_patch_test COMMAND agent_edit_recipe_patch_test)

  add_executable(
    agent_local_model_test
    agent_local_model_test.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_local_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_local_model_test
    PRIVATE
      Qt6::Core
      Qt6::Network)
  photo_editor_apply_warnings(agent_local_model_test)

  add_test(NAME agent_local_model_test COMMAND agent_local_model_test)

  add_executable(
    agent_image_validation_test
    agent_image_validation_test.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp)
  target_include_directories(
    agent_image_validation_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_image_validation_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(agent_image_validation_test)

  add_test(NAME agent_image_validation_test COMMAND agent_image_validation_test)

  add_executable(
    agent_edit_controller_test
    agent_edit_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.h
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(agent_edit_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    agent_edit_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    agent_edit_controller_test
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    agent_edit_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Network
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(agent_edit_controller_test)

  add_test(NAME agent_edit_controller_test COMMAND agent_edit_controller_test)

  add_executable(
    photo_automation_bridge_test
    photo_automation_bridge_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.h
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_automation_bridge.h
    ../apps/desktop/src/photo_automation_bridge.cpp
    ../apps/desktop/src/photo_mcp_server.h
    ../apps/desktop/src/photo_mcp_server.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(photo_automation_bridge_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    photo_automation_bridge_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    photo_automation_bridge_test
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    photo_automation_bridge_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Network
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(photo_automation_bridge_test)

  add_test(NAME photo_automation_bridge_test COMMAND photo_automation_bridge_test)

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
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
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
    thumbnail_benchmark_driver
    thumbnail_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(thumbnail_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    thumbnail_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    thumbnail_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    thumbnail_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(thumbnail_benchmark_driver)

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
    ../apps/desktop/src/tablet_input_area.h
    ../apps/desktop/src/tablet_input_area.cpp
    ../apps/desktop/src/editor_preview_texture_item.h
    ../apps/desktop/src/editor_preview_texture_item.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/editor_preview_image_provider.h
    ../apps/desktop/src/editor_preview_image_provider.cpp
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
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp
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
    thumbnail_state_utils_test
    thumbnail_state_utils_test.cpp)
  target_compile_definitions(
    thumbnail_state_utils_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    thumbnail_state_utils_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  photo_editor_apply_warnings(thumbnail_state_utils_test)

  add_test(NAME thumbnail_state_utils_test COMMAND thumbnail_state_utils_test)

  add_executable(
    canvas_coordinate_mapper_test
    canvas_coordinate_mapper_test.cpp)
  target_compile_definitions(
    canvas_coordinate_mapper_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    canvas_coordinate_mapper_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  photo_editor_apply_warnings(canvas_coordinate_mapper_test)

  add_test(NAME canvas_coordinate_mapper_test COMMAND canvas_coordinate_mapper_test)

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
    mask_lasso_render_math_test
    mask_lasso_render_math_test.cpp)
  target_link_libraries(
    mask_lasso_render_math_test
    PRIVATE
      photo_engine)
  photo_editor_apply_warnings(mask_lasso_render_math_test)

  add_test(NAME mask_lasso_render_math_test COMMAND mask_lasso_render_math_test)

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
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
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
    editor_preview_before_after_recipe_test
    editor_preview_before_after_recipe_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_before_after_recipe_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_before_after_recipe_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_before_after_recipe_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_before_after_recipe_test)

  add_test(
    NAME editor_preview_before_after_recipe_test
    COMMAND editor_preview_before_after_recipe_test)

  add_executable(
    editor_preview_coalesced_undo_test
    editor_preview_coalesced_undo_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_coalesced_undo_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_coalesced_undo_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_coalesced_undo_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_coalesced_undo_test)

  add_test(
    NAME editor_preview_coalesced_undo_test
    COMMAND editor_preview_coalesced_undo_test)

  add_executable(
    editor_preview_local_edit_target_test
    editor_preview_local_edit_target_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_local_edit_target_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_local_edit_target_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_local_edit_target_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_local_edit_target_test)

  add_test(
    NAME editor_preview_local_edit_target_test
    COMMAND editor_preview_local_edit_target_test)

  add_executable(
    editor_preview_pixel_zoom_test
    editor_preview_pixel_zoom_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_pixel_zoom_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_pixel_zoom_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_pixel_zoom_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  target_compile_definitions(
    editor_preview_pixel_zoom_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(editor_preview_pixel_zoom_test)

  add_test(NAME editor_preview_pixel_zoom_test COMMAND editor_preview_pixel_zoom_test)

  add_executable(
    editor_preview_smart_selection_stale_test
    editor_preview_smart_selection_stale_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_smart_selection_stale_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_smart_selection_stale_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_smart_selection_stale_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_smart_selection_stale_test)

  add_test(NAME editor_preview_smart_selection_stale_test COMMAND editor_preview_smart_selection_stale_test)

  add_executable(
    editor_preview_color_cache_test
    editor_preview_color_cache_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_color_cache_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_color_cache_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_color_cache_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_color_cache_test)

  add_test(NAME editor_preview_color_cache_test COMMAND editor_preview_color_cache_test)

  add_executable(
    editor_preview_layer_mask_history_test
    editor_preview_layer_mask_history_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_layer_mask_history_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_layer_mask_history_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_layer_mask_history_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_layer_mask_history_test)

  add_test(NAME editor_preview_layer_mask_history_test COMMAND editor_preview_layer_mask_history_test)

  add_executable(
    editor_preview_outline_trace_test
    editor_preview_outline_trace_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_outline_trace_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_outline_trace_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    editor_preview_outline_trace_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    editor_preview_outline_trace_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_outline_trace_test)

  add_test(NAME editor_preview_outline_trace_test COMMAND editor_preview_outline_trace_test)

  add_executable(
    editor_preview_layer_adjustment_test
    editor_preview_layer_adjustment_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_layer_adjustment_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_layer_adjustment_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_layer_adjustment_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_layer_adjustment_test)

  add_test(NAME editor_preview_layer_adjustment_test COMMAND editor_preview_layer_adjustment_test)

  add_executable(
    preview_benchmark_driver
    preview_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(preview_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    preview_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    preview_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    preview_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(preview_benchmark_driver)

  add_executable(
    interaction_workflow_benchmark_driver
    interaction_workflow_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(interaction_workflow_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    interaction_workflow_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    interaction_workflow_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    interaction_workflow_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(interaction_workflow_benchmark_driver)

  add_executable(
    actual_app_workflow_benchmark_driver
    actual_app_workflow_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ../apps/desktop/src/brush_live_preview_item.h
    ../apps/desktop/src/brush_live_preview_item.cpp
    ../apps/desktop/src/tablet_input_area.h
    ../apps/desktop/src/tablet_input_area.cpp
    ../apps/desktop/src/editor_preview_texture_item.h
    ../apps/desktop/src/editor_preview_texture_item.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/editor_preview_image_provider.h
    ../apps/desktop/src/editor_preview_image_provider.cpp
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
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp)
  set_target_properties(actual_app_workflow_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    actual_app_workflow_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    actual_app_workflow_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    actual_app_workflow_benchmark_driver
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
  photo_editor_apply_warnings(actual_app_workflow_benchmark_driver)

  add_executable(
    color_adjustment_benchmark_driver
    color_adjustment_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(color_adjustment_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    color_adjustment_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    color_adjustment_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    color_adjustment_benchmark_driver
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(color_adjustment_benchmark_driver)

  add_executable(
    render_parity_test
    render_parity_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    render_parity_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_parity_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(render_parity_test)

  add_test(NAME render_parity_test COMMAND render_parity_test)
  set_tests_properties(render_parity_test PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(
    layer_lut_render_parity_test
    layer_lut_render_parity_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    layer_lut_render_parity_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    layer_lut_render_parity_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(layer_lut_render_parity_test)

  add_test(NAME layer_lut_render_parity_test COMMAND layer_lut_render_parity_test)

  add_executable(
    render_golden_test
    render_golden_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ../apps/desktop/src/photo_render_utils.cpp
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    render_golden_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_golden_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(render_golden_test)

  add_test(NAME render_golden_test COMMAND render_golden_test)

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
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
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
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
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
