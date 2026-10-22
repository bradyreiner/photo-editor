find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Network Quick Qml QuickControls2 ShaderTools Widgets)
find_package(SQLite3 REQUIRED)
if(NOT TARGET SQLite3::SQLite3)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

qt_standard_project_setup(REQUIRES 6.5)
include(${PROJECT_SOURCE_DIR}/cmake/CustomerPlatform.cmake)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
  qt_policy(SET QTP0004 NEW)
endif()

set(photo_editor_desktop_sources
  src/agent_edit_controller.cpp
  src/agent_edit_recipe_patch.cpp
  src/agent_edit_style.cpp
  src/agent_image_validation.cpp
  src/agent_local_model.cpp
  src/agent_session_controller.h
  src/agent_session_controller.cpp
  src/managed_agent_runtime.h
  src/managed_agent_runtime.cpp
  src/gaussian_scene_controller.h
  src/gaussian_scene_controller.cpp
  src/gaussian_capture_preparation.cpp
  src/gaussian_video_capture.cpp
  src/gaussian_scene_data.cpp
  src/gaussian_spacetime_artifact.cpp
  src/gaussian_saved_views.h
  src/gaussian_saved_views.cpp
  src/gaussian_camera_path.h
  src/gaussian_camera_path.cpp
  src/gaussian_film_export.h
  src/gaussian_film_export.cpp
  src/gaussian_scene_viewport.h
  src/gaussian_scene_viewport.cpp
  src/gaussian_scene_renderer_stub.cpp
  src/agent_memory.cpp
  src/agent_mcp_client.cpp
  src/ai_model_runtime.cpp
  src/app_shell_controller.cpp
  src/macos_updater.h
  src/brush_live_preview_item.cpp
  src/photo_text_document_style.h
  src/brush_preview_compositor.cpp
  src/carousel_document_controller.cpp
  src/catalog_asset_model.cpp
  src/library_workflow_controller.h
  src/library_workflow_controller.cpp
  src/library_original_image_provider.cpp
  src/catalog_duplicate_analysis.cpp
  src/visual_search_controller.h
  src/visual_search_controller.cpp
  src/visual_search_index.cpp
  src/visual_embedding_index.cpp
  src/local_image_text_encoder.h
  src/local_image_text_encoder.cpp
  src/local_similarity_search.h
  src/local_similarity_search.cpp
  src/catalog_similarity_search.h
  src/catalog_similarity_search.cpp
  src/search_runtime_bundle.cpp
  src/search_edit_workflow.h
  src/search_edit_workflow.cpp
  src/catalog_recipe_projection.cpp
  src/decoded_image_cache.cpp
  src/display_profile_discovery.h
  src/display_profile_service.h
  src/edit_adjustment_patch.h
  src/edit_adjustment_patch.cpp
  src/edit_history_snapshot_store.h
  src/edit_history_snapshot_store.cpp
  src/edit_recipe_content_validation.cpp
  src/edit_recipe_content_validation.h
  src/edit_recipe_curve_codec.cpp
  src/edit_recipe_diff.cpp
  src/edit_recipe_document_codec.cpp
  src/edit_recipe_group_normalization.cpp
  src/edit_recipe_history.cpp
  src/edit_recipe_layer_codec.cpp
  src/edit_recipe_mask_schema.h
  src/edit_recipe_mask_state_queries.h
  src/edit_recipe_mask_types.h
  src/edit_recipe_json_readers.cpp
  src/edit_recipe_migration.cpp
  src/edit_recipe_model_normalization.cpp
  src/edit_recipe_profile_codec.cpp
  src/edit_recipe_profile_validation.cpp
  src/edit_recipe_profile_validation.h
  src/edit_recipe_sidecar.cpp
  src/edit_recipe_validation.cpp
  src/edit_recipe_v13_adapter.cpp
  src/edit_recipe_v13_persistence.h
  src/edit_recipe_v13_persistence.cpp
  src/desktop_recipe_persistence_service.cpp
  src/desktop_pipeline_state_compiler.h
  src/desktop_pipeline_state_compiler.cpp
  src/timeline_video_grade_runtime.h
  src/timeline_video_grade_runtime.cpp
  src/timeline_motion_runtime.h
  src/timeline_scene_runtime.h
  src/timeline_scene_runtime.cpp
  src/timeline_surface_runtime.h
  src/timeline_surface_runtime.cpp
  src/timeline_motion_runtime_common.cpp
  src/timeline_motion_runtime_stub.cpp
  src/timeline_foreground_matte.h
  src/timeline_foreground_matte.cpp
  src/timeline_sam2_runtime.h
  src/timeline_sam2_runtime.cpp
  src/editor_rendering_migration.inc
  src/editor_preview_controller.cpp
  src/editor_recipe_save_journal.cpp
  src/editor_close_coordinator.h
  src/editor_close_coordinator.cpp
  src/editor_preview_histogram_controller.cpp
  src/preset_registry.h
  src/preset_registry.cpp
  src/preview_color_pipeline_config.h
  src/preview_color_pipeline_config.cpp
  src/preview_display_publication.h
  src/preview_display_publication.cpp
  src/preview_output_color_space.h
  src/editor_preview_image_provider.cpp
  src/editor_preview_texture.cpp
  src/editor_preview_texture_item.cpp
  src/export_job_controller.cpp
  src/export_image_encoder.h
  src/brush_input_schema.h
  src/brush_input_schema.cpp
  src/brush_stroke_sampling.h
  src/brush_stroke_sampling.cpp
  src/brush_rasterizer.h
  src/brush_rasterizer.cpp
  src/brush_matte_rebuild.h
  src/brush_matte_rebuild.cpp
  src/brush_outline_geometry.h
  src/brush_outline_geometry.cpp
  src/generated_mask_cleanup.h
  src/generated_mask_cleanup.cpp
  src/generated_mask_metadata.h
  src/generated_mask_metadata.cpp
  src/generated_mask_object_remove_assist.h
  src/generated_mask_object_remove_assist.cpp
  src/generated_mask_quality.h
  src/generated_mask_quality.cpp
  src/segmentation_mask_ops.h
  src/segmentation_mask_ops.cpp
  src/segmentation_result_schema.h
  src/segmentation_result_schema.cpp
  src/segmentation_request_policy.h
  src/segmentation_request_policy.cpp
  src/sensor_dust_detection.h
  src/sensor_dust_detection.cpp
  src/sensor_dust_map_store.h
  src/sensor_dust_map_store.cpp
  src/smart_selection_geometry.h
  src/smart_selection_geometry.cpp
  src/smart_selection_instance_policy.h
  src/smart_selection_instance_policy.cpp
  src/smart_selection_polygon_geometry.h
  src/smart_selection_polygon_geometry.cpp
  src/smart_selection_prompt_schema.h
  src/smart_selection_prompt_schema.cpp
  src/job_queue_model.cpp
  src/job_runner.cpp
  src/job_store.cpp
  src/localized_object_refinement.h
  src/localized_object_refinement.cpp
  src/history_service.h
  src/history_revision.cpp
  src/history_payload_store.cpp
  src/history_state_record.cpp
  src/histogram_scope_service.h
  src/histogram_scope_service.cpp
  src/layer_session.h
  src/mask_session.h
  src/mask_worker_coordinator.h
  src/mask_alpha_provider.cpp
  src/mask_source_revision.h
  src/scaled_mask_matte_service.h
  src/scaled_mask_matte_service.cpp
  src/canonical_composite_mask_alpha.h
  src/canonical_composite_mask_alpha.cpp
  src/mask_thumbnail_payload_service.h
  src/mask_thumbnail_payload_service.cpp
  src/mask_outline_simplification.h
  src/mask_outline_simplification.cpp
  src/mask_outline_presentation.h
  src/mask_outline_presentation.cpp
  src/mask_outline_tracing.h
  src/mask_outline_tracing.cpp
  src/mask_presentation_schema.h
  src/mask_range_preview_math.h
  src/mask_range_preview_math.cpp
  src/local_mask_compiler.h
  src/observability.cpp
  src/object_pick_policy.h
  src/object_pick_policy.cpp
  src/output_profile_names.h
  src/photo_render_crop_utils.cpp
  src/photo_render_utils.cpp
  src/photo_layer_compiler.h
  src/source_image_decode.h
  src/source_image_decode_cache.h
  src/thumbnail_canonical_renderer.h
  src/thumbnail_shared_preview_adapter.h
  src/thumbnail_render_job.h
  src/photo_layer_adjustment_recipe.h
  src/photo_layer_adjustment_recipe.cpp
  src/preview_publisher.h
  src/preview_session.h
  src/preset_service.h
  src/photo_automation_bridge.cpp
  src/photo_mcp_server.cpp
  src/qimage_color_conversion.h
  src/bounded_qimage_reader.h
  src/render_executor_service.h
  src/render_executor_service.cpp
  src/retouch_mask_detection.h
  src/retouch_mask_detection.cpp
  src/render_plan_store.h
  src/render_plan_store.cpp
  src/render_scheduler.h
  src/recipe_asset_store.h
  src/recipe_asset_store.cpp
  src/rendering_migration_evidence.h
  src/rendering_migration_evidence.cpp
  src/rendering_migration_batch_report.h
  src/rendering_migration_batch_report.cpp
  src/rendering_migration_batch_runner.h
  src/rendering_migration_batch_runner.cpp
  src/rendering_migration_chunk_receipt.h
  src/rendering_migration_chunk_receipt.cpp
  src/rendering_migration_batch_command.h
  src/rendering_migration_batch_command.cpp
  src/rendering_migration_asset_assessor.h
  src/rendering_migration_asset_assessor.cpp
  src/qml_performance_monitor.cpp
  src/raw_decode_policy.h
  src/raw_image_decoder_libraw.cpp
  src/raw_image_decoder_stub.cpp
  src/main.cpp
  src/preview_backend_controller.cpp
  src/app_bootstrap.cpp
  src/tablet_input_area.cpp
  src/thumbnail_refresh_queue_service.cpp
  src/thumbnail_worker_renderer_factory.h
  src/thumbnail_worker_renderer_factory.cpp
  src/thumbnail_refresh_coordinator.h
  src/thumbnail_cache_session.h
  src/thumbnail_presentation_service.h
  src/thumbnail_presentation_service.cpp
  src/timeline_asset_store.cpp
  src/timeline_audio_output.h
  src/timeline_audio_output.cpp
  src/timeline_document_controller.cpp
  src/timeline_playback_controller.cpp
  src/worker_queue_registry.cpp
  src/macos_updater_stub.cpp
  src/vision_segmentation_stub.cpp
)

if(APPLE)
  if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    list(REMOVE_ITEM photo_editor_desktop_sources src/gaussian_scene_renderer_stub.cpp)
    list(APPEND photo_editor_desktop_sources src/gaussian_scene_renderer_metal.mm)
    set_source_files_properties(src/gaussian_scene_viewport.cpp PROPERTIES LANGUAGE OBJCXX)
    set_source_files_properties(src/gaussian_scene_renderer_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    list(APPEND photo_editor_desktop_sources
      src/brush_preview_compositor_metal.mm
      src/editor_preview_texture_metal.mm)
    set_source_files_properties(src/editor_preview_texture_item.cpp PROPERTIES LANGUAGE OBJCXX)
  endif()
  list(REMOVE_ITEM photo_editor_desktop_sources src/raw_image_decoder_stub.cpp)
  list(APPEND photo_editor_desktop_sources src/raw_image_decoder.mm)
  list(REMOVE_ITEM photo_editor_desktop_sources src/vision_segmentation_stub.cpp)
  list(APPEND photo_editor_desktop_sources src/vision_segmentation.mm)
  list(REMOVE_ITEM photo_editor_desktop_sources src/timeline_motion_runtime_stub.cpp)
  list(APPEND photo_editor_desktop_sources src/timeline_motion_runtime.mm)

  set(PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER "${FRAMEHOUSE_MACOS_BUNDLE_IDENTIFIER}")
  set(PHOTO_EDITOR_MACOS_SPARKLE_PLIST_KEYS "")

  if(PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
    if(PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR AND NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR)
      set(PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR
        "${PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR}/Sparkle.framework")
    endif()

    if(NOT PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR OR NOT EXISTS "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}")
      message(FATAL_ERROR "PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON requires PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR or PHOTO_EDITOR_SPARKLE_DISTRIBUTION_DIR to point to Sparkle.framework.")
    endif()

    if(NOT PHOTO_EDITOR_MACOS_APPCAST_URL)
      message(FATAL_ERROR "PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON requires PHOTO_EDITOR_MACOS_APPCAST_URL.")
    endif()

    if(NOT PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY)
      message(FATAL_ERROR "PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER=ON requires PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY.")
    endif()

    list(REMOVE_ITEM photo_editor_desktop_sources src/macos_updater_stub.cpp)
    list(APPEND photo_editor_desktop_sources src/macos_updater_sparkle.mm)

    set(PHOTO_EDITOR_MACOS_SPARKLE_PLIST_KEYS "
  <key>SUFeedURL</key>
  <string>${PHOTO_EDITOR_MACOS_APPCAST_URL}</string>
  <key>SUPublicEDKey</key>
  <string>${PHOTO_EDITOR_MACOS_SPARKLE_PUBLIC_KEY}</string>
  <key>SUEnableAutomaticChecks</key>
  <${PHOTO_EDITOR_MACOS_SPARKLE_ENABLE_AUTOMATIC_CHECKS}/>
  <key>SUAutomaticallyUpdate</key>
  <${PHOTO_EDITOR_MACOS_SPARKLE_AUTOMATIC_DOWNLOADS}/>
")
  endif()

endif()

set(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR "" CACHE PATH
  "Optional directory containing bundled AI model manifest.json plus selection/ and inpaint/ model artifacts.")
option(PHOTO_EDITOR_ENABLE_AI_INFERENCE "Enable ONNX Runtime-backed AI inference." OFF)
set(PHOTO_EDITOR_ONNXRUNTIME_ROOT "" CACHE PATH
  "Path to an ONNX Runtime SDK root containing include/ and lib/.")
option(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML "Enable ONNX Runtime CoreML execution provider registration." OFF)
option(PHOTO_EDITOR_REQUIRE_PINNED_AI_RUNTIME
  "Require the verified production lock installation when AI inference is enabled." ON)

if(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
  find_program(PHOTO_EDITOR_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
  execute_process(
    COMMAND
      "${PHOTO_EDITOR_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/verify-ai-model-bundle.py"
      "${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}"
      --require-inpaint
    RESULT_VARIABLE PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_RESULT
    OUTPUT_VARIABLE PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_OUTPUT
    ERROR_VARIABLE PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE)
  if(NOT PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Bundled AI model verification failed: ${PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_ERROR}")
  endif()
  message(STATUS "${PHOTO_EDITOR_AI_MODEL_BUNDLE_VERIFY_OUTPUT}")
endif()

qt_add_executable(photo_editor_app
  ${photo_editor_desktop_sources}
)

set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/timeline-sam2-worker.py"
  PROPERTIES QT_RESOURCE_ALIAS "timeline-sam2-worker.py")
qt_add_resources(
  photo_editor_app
  "timeline_sam2_worker"
  PREFIX "/photo-editor/runtime"
  FILES "${PROJECT_SOURCE_DIR}/scripts/timeline-sam2-worker.py")

set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/timeline_surface_worker.py"
  PROPERTIES QT_RESOURCE_ALIAS "timeline-surface-worker.py")
qt_add_resources(photo_editor_app "timeline_surface_worker" PREFIX "/photo-editor/runtime"
  FILES "${PROJECT_SOURCE_DIR}/scripts/timeline_surface_worker.py")

include(${PROJECT_SOURCE_DIR}/cmake/CreativeFonts.cmake)
photo_editor_add_creative_fonts(photo_editor_app)
include(${PROJECT_SOURCE_DIR}/cmake/PresetPreview.cmake)
photo_editor_add_preset_preview(photo_editor_app)

if(APPLE)
  set_source_files_properties(
    assets/macos/Looksy.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(photo_editor_app PRIVATE assets/macos/Looksy.icns)
elseif(WIN32)
  target_sources(photo_editor_app PRIVATE assets/windows/Looksy.rc)
endif()

set(photo_editor_default_agent_model_manifest
  "${CMAKE_CURRENT_SOURCE_DIR}/resources/agent-models/models.json")
set_source_files_properties(
  "${photo_editor_default_agent_model_manifest}"
  PROPERTIES QT_RESOURCE_ALIAS models.json)
qt_add_resources(
  photo_editor_app
  "agent_model_defaults"
  PREFIX "/photo-editor/agent-models"
  FILES "${photo_editor_default_agent_model_manifest}")

set(photo_editor_agent_sprite_sheet "${CMAKE_CURRENT_SOURCE_DIR}/assets/looksy-agent-sprite-sheet.png")
set_source_files_properties(
  "${photo_editor_agent_sprite_sheet}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-agent-sprite-sheet.png)
qt_add_resources(
  photo_editor_app
  "looksy_agent_sprite"
  PREFIX "/photo-editor/agent"
  FILES "${photo_editor_agent_sprite_sheet}")

set(photo_editor_agent_screen "${CMAKE_CURRENT_SOURCE_DIR}/assets/looksy-agent-screen-v2.png")
set_source_files_properties(
  "${photo_editor_agent_screen}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-agent-screen-v2.png)
qt_add_resources(
  photo_editor_app
  "looksy_agent_screen"
  PREFIX "/photo-editor/agent"
  FILES "${photo_editor_agent_screen}")

set(photo_editor_character_art
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/agent-phosphor/compact-character-parts-v1.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/agent-phosphor/compact-crt-bezel-v1.png")
foreach(character_asset IN LISTS photo_editor_character_art)
  get_filename_component(character_filename "${character_asset}" NAME)
  set_source_files_properties("${character_asset}" PROPERTIES QT_RESOURCE_ALIAS "${character_filename}")
endforeach()
qt_add_resources(photo_editor_app "looksy_compact_character"
  PREFIX "/photo-editor/agent" FILES ${photo_editor_character_art})

set(photo_editor_rgb_orb "${CMAKE_CURRENT_SOURCE_DIR}/assets/looksy-rgb-orb.png")
set(photo_editor_loading_scenes
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/loading-scenes/library-scene-v2.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/loading-scenes/timeline-scene-v2.png"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/loading-scenes/startup-scene-v2.png")
foreach(scene_asset IN LISTS photo_editor_loading_scenes)
  get_filename_component(scene_filename "${scene_asset}" NAME)
  set_source_files_properties("${scene_asset}" PROPERTIES QT_RESOURCE_ALIAS "${scene_filename}")
endforeach()
qt_add_resources(photo_editor_app "looksy_loading_scenes"
  PREFIX "/photo-editor/loading" FILES ${photo_editor_loading_scenes})
set(photo_editor_brand_logo_dark "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/looksy-logo-dark.png")
set(photo_editor_brand_logo_light "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/looksy-logo-light.png")
set(photo_editor_brand_wordmark_dark "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/looksy-wordmark-dark.png")
set(photo_editor_brand_wordmark_light "${CMAKE_CURRENT_SOURCE_DIR}/assets/brand/looksy-wordmark-light.png")
set_source_files_properties(
  "${photo_editor_rgb_orb}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-rgb-orb.png)
set_source_files_properties(
  "${photo_editor_brand_logo_dark}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-logo-dark.png)
set_source_files_properties(
  "${photo_editor_brand_logo_light}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-logo-light.png)
set_source_files_properties(
  "${photo_editor_brand_wordmark_dark}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-wordmark-dark.png)
set_source_files_properties(
  "${photo_editor_brand_wordmark_light}"
  PROPERTIES QT_RESOURCE_ALIAS looksy-wordmark-light.png)
qt_add_resources(
  photo_editor_app
  "looksy_brand_orb"
  PREFIX "/photo-editor/brand"
  FILES
    "${photo_editor_rgb_orb}"
    "${photo_editor_brand_logo_dark}"
    "${photo_editor_brand_logo_light}"
    "${photo_editor_brand_wordmark_dark}"
    "${photo_editor_brand_wordmark_light}")

set_target_properties(
  photo_editor_app
  PROPERTIES
    OUTPUT_NAME "${FRAMEHOUSE_PRODUCT_SLUG}"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE)

if(APPLE)
  set_target_properties(
    photo_editor_app
    PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME "${FRAMEHOUSE_PRODUCT_NAME}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

target_compile_definitions(photo_editor_app PRIVATE
  FRAMEHOUSE_PRODUCT_NAME="${FRAMEHOUSE_PRODUCT_NAME}"
  FRAMEHOUSE_SUPPORT_URL="${FRAMEHOUSE_SUPPORT_URL}"
  FRAMEHOUSE_PRIVACY_URL="${FRAMEHOUSE_PRIVACY_URL}"
  FRAMEHOUSE_TERMS_URL="${FRAMEHOUSE_TERMS_URL}")

if(PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS)
  target_compile_definitions(photo_editor_app PRIVATE PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS=1)
else()
  target_compile_definitions(photo_editor_app PRIVATE PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS=0)
endif()

qt_add_qml_module(photo_editor_app
  URI PhotoEditor
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/ActionButton.qml
    qml/components/ActivityBanner.qml
    qml/components/AgentBatchStatusRow.qml
    qml/components/AgentModelRow.qml
    qml/components/AgentPermissionModeButton.qml
    qml/components/AgentProofCard.qml
    qml/components/AgentRecipeDiffRow.qml
    qml/components/AgentRunHistoryRow.qml
    qml/components/AgentRunStageRow.qml
    qml/components/AgentStyleTile.qml
    qml/components/AgentToolPermissionToggle.qml
    qml/components/AgentToolCallRow.qml
  qml/components/AgentWalkthroughDialog.qml
  qml/components/LooksyAgentCharacter.qml
  qml/components/CrtSignalSurface.qml
    qml/components/LooksyLight.qml
    qml/components/AgentConversationView.qml
    qml/components/AgentSessionTerminal.qml
    qml/components/AgentTerminalScreen.qml
    qml/components/AgentFailureDetails.qml
    qml/components/AgentWorkspaceOverlay.qml
    qml/components/AgentWorkspaceTouchResolver.qml
    qml/components/ActivityStatusBar.qml
    qml/components/AppToolTip.qml
    qml/components/AppIcon.qml
    qml/components/MediaTypeIcon.qml
    qml/components/AppMenu.qml
    qml/components/AppMenuItem.qml
    qml/components/AppPreferencesDialog.qml
    qml/components/CustomerAccountPanel.qml
    qml/components/BrandPanel.qml
    qml/components/BrandWordmark.qml
    qml/components/BrushContextPanel.qml
    qml/components/BrushLayerInspectorHeader.qml
    qml/components/BrushToolHud.qml
    qml/components/CanvasCoordinateMapper.qml
    qml/components/CarouselBoardThumb.qml
    qml/components/CarouselColorVisionEffect.qml
    qml/components/CarouselLayerRow.qml
    qml/components/CarouselMediaThumb.qml
    qml/components/CarouselToolRailButton.qml
    qml/components/ChannelMixerButton.qml
    qml/components/ColorSwatchButton.qml
    qml/components/ColorChannelButton.qml
    qml/components/ColorWheelControl.qml
    qml/components/CommandButtonSurface.qml
    qml/components/CommandPaletteRow.qml
    qml/components/CommandPaletteDialog.qml
    qml/components/ConfirmActionButton.qml
    qml/components/CropOverlay.qml
    qml/components/CutCornerSurface.qml
    qml/components/EditFilmstripThumb.qml
    qml/components/EditInspectorTabButton.qml
    qml/components/EditLayoutToggleRow.qml
    qml/components/EditRailButton.qml
    qml/components/ExportPreflightIssueRow.qml
    qml/components/ExportPresetSummary.qml
    qml/components/ExportQueueRow.qml
    qml/components/FilterChip.qml
    qml/components/FirstRunGuideDialog.qml
    qml/components/GalleryAssetCard.qml
    qml/components/GalleryPickerDialog.qml
    qml/components/CanvasPresets.js
    qml/components/NewCanvasDialog.qml
    qml/components/SavedBoardsLibrary.qml
    qml/components/SavedTimelinesLibrary.qml
    qml/components/LibraryNavigationPanel.qml
    qml/components/LibraryMetadataPanel.qml
    qml/components/LibraryDeliveryPanel.qml
    qml/components/GlassSurface.qml
    qml/components/HistogramDisplay.qml
    qml/components/HorizonStraightenOverlay.qml
    qml/components/ImportFolderChip.qml
    qml/components/ImportFolderRow.qml
    qml/components/ImportImageTile.qml
    qml/components/ImportSourceRow.qml
    qml/components/InfoPill.qml
    qml/components/InspectorSection.qml
    qml/components/InspectorSlider.qml
    qml/components/InlineActivityIndicator.qml
    qml/components/OpticalActivityIndicator.qml
    qml/components/KeyboardShortcutsDialog.qml
    qml/components/UiPrimitivesPage.qml
    qml/components/UiPrimitivesCommands.qml
    qml/components/UiPrimitivesInputs.qml
    qml/components/UiPrimitivesFeedback.qml
    qml/components/UiPrimitivesOpticalDetails.qml
    qml/components/UiPrimitiveCatalog.js
    qml/components/LayerActionsMenu.qml
    qml/components/LayerRowActionZone.qml
    qml/components/LayerRow.qml
    qml/components/LayerRowChrome.qml
    qml/components/LayerTransformOverlay.qml
    qml/components/MenuButton.qml
    qml/components/MenuItemMetrics.qml
    qml/components/MenuPopover.qml
    qml/components/MaskSearchModel.qml
    qml/components/MaskRowActions.qml
    qml/components/MiniButton.qml
    qml/components/MetricCard.qml
    qml/components/RecoveryStatusPanel.qml
    qml/components/ProgressStrip.qml
    qml/components/OpticalLightField.qml
    qml/components/AdditiveLightWaves.qml
    qml/components/LooksyLoadingScene.qml
    qml/components/PopupSurface.qml
    qml/components/AppPopup.qml
    qml/components/AppDialog.qml
    qml/components/AppDrawer.qml
    qml/components/PopupEnterTransition.qml
    qml/components/PopupExitTransition.qml
    qml/components/PopupMenuRow.qml
    qml/components/PreferenceNavRow.qml
    qml/components/ProjectMembershipRow.qml
    qml/components/RecentDocumentRow.qml
    qml/components/RetouchBeforeAfterControls.qml
    qml/components/ReviewPreview.qml
    qml/components/SectionHeader.qml
    qml/components/SidebarButton.qml
    qml/components/SplitButton.qml
    qml/components/StatusChip.qml
    qml/components/StudioAdjustmentSlider.qml
    qml/components/StudioAdjustmentPad2D.qml
    qml/components/StudioColorManagementPanel.qml
    qml/components/StudioChannelPadControl.qml
    qml/components/StudioCollapsibleGroup.qml
    qml/components/StudioEmptyStateCard.qml
    qml/components/StartupLoadingPopup.qml
    qml/components/StudioDetailTextureControl.qml
    qml/components/StudioDiagnosticOverlayStack.qml
    qml/components/StudioGradingMacroControl.qml
    qml/components/StudioHealthStatusStrip.qml
    qml/components/StudioHueRangeControl.qml
    qml/components/StudioImageViewport.qml
    qml/components/StudioInspectorGroup.qml
    qml/components/StudioLensCorrectionControl.qml
    qml/components/StudioOfflineRecoveryBanner.qml
    qml/components/StudioRgbMixerMatrix.qml
    qml/components/StudioScopeDockContent.qml
    qml/components/StudioScopeGrid.qml
    qml/components/StudioScopeTile.qml
    qml/components/StudioThumbnailCard.qml
    qml/components/StudioToolTransitionOverlay.qml
    qml/components/StudioRowRule.qml
    qml/components/StudioMetadataRow.qml
    qml/components/StudioSidebarRow.qml
    qml/components/StudioSectionTitle.qml
    qml/components/StudioPanel.qml
    qml/components/ModularToolPanel.qml
    qml/components/StudioPixelGridOverlay.qml
    qml/components/StudioTinyInput.qml
    qml/components/VisualSearchPanel.qml
    qml/components/SearchEditDialog.qml
    qml/components/AgentModelSetupDialog.qml
    qml/workspaces/SceneWorkspace.qml
    qml/components/StudioViewportStatusStack.qml
    qml/components/RecipeSaveStatusChip.qml
    qml/components/CopyPhotoEditsDialog.qml
    qml/components/EditorCloseDialog.qml
    qml/components/StudioViewportLoupe.qml
    qml/components/StudioViewportBrushCursors.qml
    qml/components/StudioRetouchOverlayCommandSurface.qml
    qml/components/StudioViewportInputCommandSurface.qml
    qml/components/SkeletonBlock.qml
    qml/components/LooksyAssistantDock.qml
    qml/components/LooksyLetterhead.qml
    qml/components/LooksyMark.qml
    qml/components/Theme.qml
    qml/components/CompletionCaustic.qml
    qml/components/TimelineEffectsPanel.qml
    qml/components/TimelineTrackingPanel.qml
    qml/components/TimelineScenePointOverlay.qml
    qml/components/TimelineSurfaceCorrectionOverlay.qml
    qml/components/TimelineRotoscopeCorrectionOverlay.qml
    qml/components/TimelineSceneProjectionEffect.qml
    qml/components/TimelineKeyframeParameterPicker.qml
    qml/components/TimelineTransformOverlay.qml
    qml/components/TimelineEffectMaskOverlay.qml
    qml/components/TimelineColorMaskEffect.qml
    qml/components/TimelinePerspectiveEffect.qml
    qml/components/TimelineDifferenceEffect.qml
    qml/components/TimelineClipContent.qml
    qml/components/TimelineEffectLibraryRow.qml
    qml/components/TimelineMediaAssetRow.qml
    qml/components/TimelineAudioMeter.qml
    qml/components/TimelineAudioMixer.qml
    qml/components/TimelineMarkerCard.qml
    qml/components/TimelineMarkerPin.qml
    qml/components/TimelineMulticamAngleMonitor.qml
    qml/components/TimelinePlatformOverlay.qml
    qml/components/TimelineViewerGuideOverlay.qml
    qml/components/TimelineScopesPanel.qml
    qml/components/TimelinePancakeEditor.qml
    qml/components/TimelineWaveform.qml
    qml/components/TimelineSourcePanel.qml
    qml/components/TimelineTransitionLibraryRow.qml
    qml/components/TimelineTrackHeader.qml
    qml/components/TimelineTrackToggleButton.qml
    qml/components/ThumbnailStateUtils.qml
    qml/components/PhotoLayerSnapshot.qml
    qml/components/PhotoEffectStack.qml
    qml/components/PhotoSpatialEffectControls.qml
    qml/components/PhotoTypographyPanel.qml
    qml/components/PhotoTextContentEditor.qml
    qml/components/PhotoFontPicker.qml
    qml/components/PhotoTextPerspectivePanel.qml
    qml/components/PhotoTextOverlay.qml
    qml/components/PhotoPaintOverlay.qml
    qml/components/PhotoShapeCreateOverlay.qml
    qml/components/PhotoShapePanel.qml
    qml/components/ShapeFillControls.qml
    qml/components/PhotoTextCreateOverlay.qml
    qml/components/ScenePhotoEditHandoff.qml
    qml/components/ToneCurveChannelButton.qml
    qml/components/ToneComboBox.qml
    qml/components/ToneCurveEditor.qml
    qml/components/ToneCurvePointHandle.qml
    qml/components/ToneCurvePresetButton.qml
    qml/components/ToneTextArea.qml
    qml/components/ToneTextField.qml
    qml/components/ToneSpinBox.qml
    qml/components/ToneCheckBox.qml
    qml/components/ToolOptionToggle.qml
    qml/components/ToolCommandBar.qml
    qml/components/ToolValueSlider.qml
    qml/components/ToolVerticalSlider.qml
    qml/components/ToolHud.qml
    qml/components/ToolRail.qml
    qml/components/ToolButton.qml
    qml/components/TransformOverlay.qml
    qml/workspaces/CarouselView.qml
    qml/workspaces/carousel/CarouselVariantComparisonDialogContent.qml
    qml/workspaces/carousel/CarouselLayoutRecipeDialogContent.qml
    qml/workspaces/carousel/CarouselClipboardPasteDialogContent.qml
    qml/workspaces/carousel/CarouselRepeatedElementLinkDialogContent.qml
    qml/workspaces/carousel/CarouselCommandPaletteContent.qml
    qml/workspaces/carousel/CarouselPreflightIgnoreDialogContent.qml
    qml/workspaces/carousel/CarouselBrandApplicationDialogContent.qml
    qml/workspaces/carousel/CarouselBrandLintDialogContent.qml
    qml/workspaces/carousel/CarouselBrandKitEditorDialogContent.qml
    qml/workspaces/carousel/CarouselReadingOrderDialogContent.qml
    qml/workspaces/carousel/CarouselDocumentStructureDialogContent.qml
    qml/workspaces/carousel/CarouselBoardRangeCommandDialogContent.qml
    qml/workspaces/carousel/CarouselNeighborComparisonDialogContent.qml
    qml/workspaces/carousel/CarouselContinuityPreviewDialogContent.qml
    qml/workspaces/carousel/CarouselStatusNoticesDialogContent.qml
    qml/workspaces/carousel/CarouselRecoveryDialogContent.qml
    qml/workspaces/carousel/CarouselRelinkDialogContent.qml
    qml/workspaces/carousel/CarouselCopyProofDialogContent.qml
    qml/workspaces/carousel/CarouselFontBrowserDialogContent.qml
    qml/workspaces/carousel/CarouselFontRecoveryDialogContent.qml
    qml/workspaces/carousel/CarouselExportSheetContent.qml
    qml/workspaces/carousel/CarouselCutoutRefineDialogContent.qml
    qml/workspaces/carousel/CarouselTemplatePackUpdateDialogContent.qml
    qml/workspaces/carousel/CarouselSequenceTemplateDialogContent.qml
    qml/workspaces/carousel/CarouselStoryboardDialogContent.qml
    qml/workspaces/carousel/CarouselRepeatGridDialogContent.qml
    qml/workspaces/carousel/CarouselSelectionBatchDialogContent.qml
    qml/workspaces/carousel/CarouselMediaUsageDialogContent.qml
    qml/workspaces/carousel/CarouselVideoPreviewDialogContent.qml
    qml/workspaces/carousel/CarouselMediaAutofillDialogContent.qml
    qml/workspaces/carousel/CarouselDesignPackageImportDialogContent.qml
    qml/workspaces/ExportView.qml
    qml/workspaces/ExportWorkspaceHost.qml
    qml/workspaces/ImportView.qml
    qml/workspaces/LibraryView.qml
    qml/workspaces/LibraryWorkspace.qml
    qml/workspaces/MaskCanvasOverlay.qml
    qml/workspaces/MaskInspectorSections.qml
    qml/workspaces/MaskView.qml
    qml/workspaces/RenderingMigrationEvidenceSummary.qml
    qml/workspaces/SequenceWorkspace.qml
    qml/workspaces/SharedMaskOverlay.qml
    qml/workspaces/StudioWorkspace.qml
    qml/workspaces/LibraryCullView.qml
    qml/workspaces/TimelineSetupPage.qml
    qml/workspaces/TimelineView.qml
)

qt_add_shaders(photo_editor_app "character_crt_shaders"
  BATCHABLE
  PREFIX "/photo-editor/shaders"
  BASE "shaders"
  FILES shaders/agent_crt.vert shaders/agent_crt.frag
    shaders/agent_robot.vert shaders/agent_robot.frag
    shaders/optical_light.vert shaders/optical_light.frag)

qt_add_shaders(photo_editor_app "carousel_color_vision_shaders"
  BATCHABLE
  PREFIX "/photo-editor/shaders"
  BASE "shaders"
  FILES
    shaders/carousel_color_vision.vert
    shaders/carousel_color_vision.frag
)

qt_add_shaders(photo_editor_app "timeline_perspective_shaders"
  BATCHABLE
  PREFIX "/photo-editor/shaders"
  BASE "shaders"
  FILES
    shaders/timeline_perspective.vert
    shaders/timeline_perspective.frag
)

qt_add_shaders(photo_editor_app "timeline_scene_projection_shaders"
  BATCHABLE
  PREFIX "/photo-editor/shaders"
  BASE "shaders"
  FILES shaders/timeline_scene_projection.vert shaders/timeline_scene_projection.frag)

qt_add_shaders(photo_editor_app "timeline_effect_mask_shaders"
  BATCHABLE
  PREFIX "/photo-editor/shaders"
  BASE "shaders"
  FILES
    shaders/timeline_effect_mask.vert
    shaders/timeline_effect_mask.frag
)

target_include_directories(photo_editor_app PRIVATE src)

target_link_libraries(
  photo_editor_app
  PRIVATE
    Qt6::Core
    photo_editor_customer_access
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    SQLite3::SQLite3
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Widgets
    photo_engine
    photo_editor_raw_decoder_backend
    photo_rust_ffi)

target_compile_definitions(photo_editor_app
  PRIVATE
    PHOTO_EDITOR_APP_VERSION="${PROJECT_VERSION}"
    PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=$<BOOL:${PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY}>
    PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND=$<BOOL:${PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND}>
    PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND=$<BOOL:${PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND}>)

if(LOOKSY_ENABLE_CRASH_REPORTING)
  if(WIN32)
    set(looksy_crash_handler_name "crashpad_handler.exe")
  else()
    set(looksy_crash_handler_name "crashpad_handler")
  endif()
  add_custom_command(TARGET photo_editor_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LOOKSY_CRASHPAD_HANDLER}"
      "$<TARGET_FILE_DIR:photo_editor_app>/${looksy_crash_handler_name}"
    VERBATIM)
endif()

if(APPLE)
  set(looksy_customer_license_dir "$<TARGET_FILE_DIR:photo_editor_app>/../Resources/licenses")
else()
  set(looksy_customer_license_dir "$<TARGET_FILE_DIR:photo_editor_app>/licenses")
endif()
add_custom_command(TARGET photo_editor_app POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${looksy_customer_license_dir}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${PROJECT_SOURCE_DIR}/docs/licenses/OpenSSL-3.txt"
    "${PROJECT_SOURCE_DIR}/docs/licenses/Sentry-Native-0.17.txt"
    "${looksy_customer_license_dir}"
  VERBATIM)

if(PHOTO_EDITOR_ENABLE_AI_INFERENCE)
  if(NOT PHOTO_EDITOR_ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "PHOTO_EDITOR_ENABLE_AI_INFERENCE=ON requires PHOTO_EDITOR_ONNXRUNTIME_ROOT.")
  endif()
  if(NOT EXISTS "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "PHOTO_EDITOR_ONNXRUNTIME_ROOT must contain include/onnxruntime_cxx_api.h.")
  endif()
  if(PHOTO_EDITOR_REQUIRE_PINNED_AI_RUNTIME)
    if(NOT PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
      message(FATAL_ERROR
        "Pinned AI inference requires PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR from install-ai-runtime.py.")
    endif()
    find_program(PHOTO_EDITOR_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
    get_filename_component(
      PHOTO_EDITOR_AI_RUNTIME_INSTALL_ROOT
      "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/.."
      REALPATH)
    get_filename_component(
      PHOTO_EDITOR_AI_RUNTIME_EXPECTED_BUNDLE
      "${PHOTO_EDITOR_AI_RUNTIME_INSTALL_ROOT}/model-bundle"
      REALPATH)
    get_filename_component(
      PHOTO_EDITOR_AI_RUNTIME_CONFIGURED_BUNDLE
      "${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}"
      REALPATH)
    if(NOT PHOTO_EDITOR_AI_RUNTIME_CONFIGURED_BUNDLE STREQUAL
        PHOTO_EDITOR_AI_RUNTIME_EXPECTED_BUNDLE)
      message(FATAL_ERROR
        "Pinned AI inference requires the model-bundle and onnxruntime directories from the same verified install root.")
    endif()
    execute_process(
      COMMAND
        "${PHOTO_EDITOR_PYTHON3_EXECUTABLE}"
        -B
        "${PROJECT_SOURCE_DIR}/scripts/install-ai-runtime.py"
        --install-root
        "${PHOTO_EDITOR_AI_RUNTIME_INSTALL_ROOT}"
        --verify-only
      RESULT_VARIABLE PHOTO_EDITOR_AI_RUNTIME_VERIFY_RESULT
      OUTPUT_VARIABLE PHOTO_EDITOR_AI_RUNTIME_VERIFY_OUTPUT
      ERROR_VARIABLE PHOTO_EDITOR_AI_RUNTIME_VERIFY_ERROR
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_STRIP_TRAILING_WHITESPACE)
    if(NOT PHOTO_EDITOR_AI_RUNTIME_VERIFY_RESULT EQUAL 0)
      message(FATAL_ERROR
        "Pinned AI runtime verification failed: ${PHOTO_EDITOR_AI_RUNTIME_VERIFY_ERROR}")
    endif()
    message(STATUS "Pinned AI runtime installation verified.")
  endif()

  find_library(
    PHOTO_EDITOR_ONNXRUNTIME_LIBRARY
    NAMES onnxruntime libonnxruntime
    PATHS "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/lib"
    NO_DEFAULT_PATH)
  if(NOT PHOTO_EDITOR_ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "Unable to find libonnxruntime in ${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/lib.")
  endif()

  add_library(photo_editor_onnxruntime INTERFACE)
  target_include_directories(
    photo_editor_onnxruntime
    SYSTEM INTERFACE "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/include")
  target_link_libraries(
    photo_editor_onnxruntime
    INTERFACE "${PHOTO_EDITOR_ONNXRUNTIME_LIBRARY}")
  target_compile_definitions(
    photo_editor_onnxruntime
    INTERFACE
      PHOTO_EDITOR_HAS_ONNXRUNTIME=1
      PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML=$<BOOL:${PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML}>)
  target_link_libraries(photo_editor_app PRIVATE photo_editor_onnxruntime)

  if(APPLE)
    set(PHOTO_EDITOR_ONNXRUNTIME_BUNDLE_LIBRARY
      "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/lib/libonnxruntime.1.dylib")
    if(NOT EXISTS "${PHOTO_EDITOR_ONNXRUNTIME_BUNDLE_LIBRARY}")
      set(PHOTO_EDITOR_ONNXRUNTIME_BUNDLE_LIBRARY
        "${PHOTO_EDITOR_ONNXRUNTIME_LIBRARY}")
    endif()
    add_custom_command(
      TARGET photo_editor_app
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E rm -f
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Frameworks/libonnxruntime.dylib"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PHOTO_EDITOR_ONNXRUNTIME_BUNDLE_LIBRARY}"
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Frameworks/libonnxruntime.1.dylib")
  else()
    add_custom_command(
      TARGET photo_editor_app
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PHOTO_EDITOR_ONNXRUNTIME_LIBRARY}"
              "$<TARGET_FILE_DIR:photo_editor_app>")
  endif()
endif()

if(APPLE)
  find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
  find_library(VISION_FRAMEWORK Vision)
  find_library(COREIMAGE_FRAMEWORK CoreImage)
  find_library(COREVIDEO_FRAMEWORK CoreVideo)
  find_library(IMAGEIO_FRAMEWORK ImageIO)
  find_library(METAL_FRAMEWORK Metal)
  find_library(FOUNDATION_FRAMEWORK Foundation)
  find_library(SPEECH_FRAMEWORK Speech)
  if(VISION_FRAMEWORK AND COREGRAPHICS_FRAMEWORK AND COREIMAGE_FRAMEWORK AND COREVIDEO_FRAMEWORK AND IMAGEIO_FRAMEWORK)
    target_link_libraries(
      photo_editor_app
      PRIVATE
        ${COREGRAPHICS_FRAMEWORK}
        ${VISION_FRAMEWORK}
        ${COREIMAGE_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK})
  endif()
  if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND AND METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
    target_link_libraries(
      photo_editor_app
      PRIVATE
        ${METAL_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK})
  endif()
  if(SPEECH_FRAMEWORK AND FOUNDATION_FRAMEWORK)
    target_link_libraries(
      photo_editor_app
      PRIVATE
        ${SPEECH_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK})
  endif()
  set_source_files_properties(src/raw_image_decoder.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/vision_segmentation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/timeline_motion_runtime.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(
    src/timeline_document_controller.cpp
    PROPERTIES
      LANGUAGE OBJCXX
      COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/brush_live_preview_item.cpp PROPERTIES LANGUAGE OBJCXX)
  if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    set_source_files_properties(
      src/brush_preview_compositor_metal.mm
      src/editor_preview_texture_metal.mm
      PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  endif()
  if(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
    add_custom_command(
      TARGET photo_editor_app
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
              "${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}"
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Resources/models")
  endif()
elseif(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
  add_custom_command(
    TARGET photo_editor_app
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}"
            "$<TARGET_FILE_DIR:photo_editor_app>/models")
endif()

if(APPLE AND PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER)
  get_filename_component(PHOTO_EDITOR_SPARKLE_FRAMEWORK_PARENT "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}" DIRECTORY)
  target_compile_options(photo_editor_app PRIVATE "-F${PHOTO_EDITOR_SPARKLE_FRAMEWORK_PARENT}")
  target_link_options(photo_editor_app PRIVATE
    "-F${PHOTO_EDITOR_SPARKLE_FRAMEWORK_PARENT}"
    "-Wl,-rpath,@loader_path/../Frameworks")
  target_link_libraries(photo_editor_app PRIVATE "${PHOTO_EDITOR_SPARKLE_FRAMEWORK_DIR}")
  set_source_files_properties(src/macos_updater_sparkle.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

photo_editor_apply_warnings(photo_editor_app)
set(PHOTO_EDITOR_AGENT_RUNTIME_DIR "" CACHE PATH "Verified app-private Ollama runtime from prepare-agent-runtime.py")
if(PHOTO_EDITOR_AGENT_RUNTIME_DIR)
  if(NOT APPLE)
    message(FATAL_ERROR "The pinned agent runtime currently supports macOS only.")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/prepare-agent-runtime.py"
            --verify-only --output "${PHOTO_EDITOR_AGENT_RUNTIME_DIR}"
    RESULT_VARIABLE PHOTO_EDITOR_AGENT_RUNTIME_VERIFY_RESULT
    OUTPUT_VARIABLE PHOTO_EDITOR_AGENT_RUNTIME_VERIFY_OUTPUT
    ERROR_VARIABLE PHOTO_EDITOR_AGENT_RUNTIME_VERIFY_ERROR)
  if(NOT PHOTO_EDITOR_AGENT_RUNTIME_VERIFY_RESULT EQUAL 0)
    message(FATAL_ERROR "Agent runtime verification failed: ${PHOTO_EDITOR_AGENT_RUNTIME_VERIFY_ERROR}")
  endif()
  add_custom_command(TARGET photo_editor_app POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/prepare-agent-runtime.py"
            --verify-only --output "${PHOTO_EDITOR_AGENT_RUNTIME_DIR}"
    COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE=${PHOTO_EDITOR_AGENT_RUNTIME_DIR}"
            "-DDESTINATION=$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Resources/agent-runtime"
            -P "${PROJECT_SOURCE_DIR}/cmake/CopyAgentRuntime.cmake"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/prepare-agent-runtime.py"
            --verify-only --output "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Resources/agent-runtime")
  set_property(TARGET photo_editor_app APPEND PROPERTY LINK_DEPENDS
    "${PHOTO_EDITOR_AGENT_RUNTIME_DIR}/framehouse-runtime.json"
    "${PROJECT_SOURCE_DIR}/apps/desktop/models/agent-runtime.lock.json")
endif()
set(PHOTO_EDITOR_SEARCH_RUNTIME_DIR "" CACHE PATH "Verified self-contained search worker bundle")
set(PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT "")
if(PHOTO_EDITOR_SEARCH_RUNTIME_DIR)
  if(NOT APPLE)
    message(FATAL_ERROR "The pinned search runtime currently supports macOS arm64 only.")
  endif()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  file(SHA256 "${PHOTO_EDITOR_SEARCH_RUNTIME_DIR}/framehouse-search-runtime.json" PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT)
  execute_process(
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-search-runtime.py"
            --verify "${PHOTO_EDITOR_SEARCH_RUNTIME_DIR}" --receipt-sha256 "${PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT}"
    RESULT_VARIABLE PHOTO_EDITOR_SEARCH_RUNTIME_RESULT
    OUTPUT_QUIET ERROR_VARIABLE PHOTO_EDITOR_SEARCH_RUNTIME_ERROR)
  if(NOT PHOTO_EDITOR_SEARCH_RUNTIME_RESULT EQUAL 0)
    message(FATAL_ERROR "Search runtime verification failed: ${PHOTO_EDITOR_SEARCH_RUNTIME_ERROR}")
  endif()
  target_compile_definitions(photo_editor_app PRIVATE
    PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT_SHA256="${PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT}")
  add_custom_command(TARGET photo_editor_app POST_BUILD
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-search-runtime.py"
            --verify "${PHOTO_EDITOR_SEARCH_RUNTIME_DIR}" --receipt-sha256 "${PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT}"
    COMMAND "${CMAKE_COMMAND}"
            "-DSOURCE=${PHOTO_EDITOR_SEARCH_RUNTIME_DIR}"
            "-DDESTINATION=$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Resources/search-runtime"
            -P "${PROJECT_SOURCE_DIR}/cmake/CopySearchRuntime.cmake"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build-search-runtime.py"
            --verify "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Resources/search-runtime"
            --receipt-sha256 "${PHOTO_EDITOR_SEARCH_RUNTIME_RECEIPT}")
  set_property(TARGET photo_editor_app APPEND PROPERTY LINK_DEPENDS
    "${PHOTO_EDITOR_SEARCH_RUNTIME_DIR}/framehouse-search-runtime.json")
endif()

if(APPLE)
  # Finalize the plist after selecting the runtime. The signer and executable
  # must anchor exactly the same prepared (already signed) receipt.
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    @ONLY)
endif()

photo_editor_install_desktop_runtime(photo_editor_app)
