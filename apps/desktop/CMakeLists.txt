find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Network Quick Qml QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
  qt_policy(SET QTP0004 NEW)
endif()

set(photo_editor_desktop_sources
  src/agent_edit_controller.cpp
  src/agent_edit_recipe_patch.cpp
  src/agent_edit_style.cpp
  src/agent_image_validation.cpp
  src/agent_local_model.cpp
  src/agent_memory.cpp
  src/agent_mcp_client.cpp
  src/ai_model_runtime.cpp
  src/app_shell_controller.cpp
  src/macos_updater.h
  src/brush_live_preview_item.cpp
  src/brush_preview_compositor.cpp
  src/carousel_document_controller.cpp
  src/catalog_asset_model.cpp
  src/decoded_image_cache.cpp
  src/editor_preview_controller.cpp
  src/editor_preview_image_provider.cpp
  src/editor_preview_texture.cpp
  src/editor_preview_texture_item.cpp
  src/export_job_controller.cpp
  src/job_queue_model.cpp
  src/job_runner.cpp
  src/job_store.cpp
  src/mask_alpha_provider.cpp
  src/observability.cpp
  src/photo_render_utils.cpp
  src/photo_automation_bridge.cpp
  src/photo_mcp_server.cpp
  src/qml_performance_monitor.cpp
  src/raw_image_decoder_stub.cpp
  src/main.cpp
  src/preview_backend_controller.cpp
  src/app_bootstrap.cpp
  src/tablet_input_area.cpp
  src/thumbnail_refresh_queue_service.cpp
  src/timeline_asset_store.cpp
  src/timeline_document_controller.cpp
  src/timeline_playback_controller.cpp
  src/worker_queue_registry.cpp
  src/macos_updater_stub.cpp
  src/vision_segmentation_stub.cpp
)

if(APPLE)
  if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    list(APPEND photo_editor_desktop_sources
      src/brush_preview_compositor_metal.mm
      src/editor_preview_texture_metal.mm)
    set_source_files_properties(src/editor_preview_texture_item.cpp PROPERTIES LANGUAGE OBJCXX)
  endif()
  list(REMOVE_ITEM photo_editor_desktop_sources src/raw_image_decoder_stub.cpp)
  list(APPEND photo_editor_desktop_sources src/raw_image_decoder.mm)
  list(REMOVE_ITEM photo_editor_desktop_sources src/vision_segmentation_stub.cpp)
  list(APPEND photo_editor_desktop_sources src/vision_segmentation.mm)

  set(PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER "${VIREL_MACOS_BUNDLE_IDENTIFIER}")
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

  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    ${CMAKE_CURRENT_BINARY_DIR}/Info.plist
    @ONLY)
endif()

set(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR "" CACHE PATH
  "Optional directory containing bundled AI model manifest.json plus selection/ and inpaint/ model artifacts.")
option(PHOTO_EDITOR_ENABLE_AI_INFERENCE "Enable ONNX Runtime-backed AI inference." OFF)
set(PHOTO_EDITOR_ONNXRUNTIME_ROOT "" CACHE PATH
  "Path to an ONNX Runtime SDK root containing include/ and lib/.")
option(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML "Enable ONNX Runtime CoreML execution provider registration." OFF)

qt_add_executable(photo_editor_app
  ${photo_editor_desktop_sources}
)

if(APPLE)
  set_source_files_properties(
    assets/macos/VirelStudio.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
  target_sources(photo_editor_app PRIVATE assets/macos/VirelStudio.icns)
elseif(WIN32)
  target_sources(photo_editor_app PRIVATE assets/windows/VirelStudio.rc)
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

set_target_properties(
  photo_editor_app
  PROPERTIES
    OUTPUT_NAME "${VIREL_PRODUCT_SLUG}"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE)

if(APPLE)
  set_target_properties(
    photo_editor_app
    PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME "${VIREL_PRODUCT_NAME}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

target_compile_definitions(photo_editor_app PRIVATE
  VIREL_PRODUCT_NAME="${VIREL_PRODUCT_NAME}"
  VIREL_VENDOR_NAME="${VIREL_VENDOR_NAME}"
  VIREL_SUPPORT_URL="${VIREL_SUPPORT_URL}"
  VIREL_PRIVACY_URL="${VIREL_PRIVACY_URL}"
  VIREL_TERMS_URL="${VIREL_TERMS_URL}")

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
    qml/components/AppIcon.qml
    qml/components/AppMenu.qml
    qml/components/AppMenuItem.qml
    qml/components/AppPreferencesDialog.qml
    qml/components/BrandPanel.qml
    qml/components/BrandWordmark.qml
    qml/components/BrushContextPanel.qml
    qml/components/BrushLayerInspectorHeader.qml
    qml/components/BrushToolHud.qml
    qml/components/CanvasCoordinateMapper.qml
    qml/components/CarouselBoardThumb.qml
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
    qml/components/GalleryAssetCard.qml
    qml/components/GalleryPickerDialog.qml
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
    qml/components/KeyboardShortcutsDialog.qml
    qml/components/LayerRowActionZone.qml
    qml/components/LayerRow.qml
    qml/components/LayerRowChrome.qml
    qml/components/LayerTransformOverlay.qml
    qml/components/MenuButton.qml
    qml/components/MiniButton.qml
    qml/components/MetricCard.qml
    qml/components/ProgressStrip.qml
    qml/components/PopupSurface.qml
    qml/components/PopupMenuRow.qml
    qml/components/PreferenceNavRow.qml
    qml/components/ProjectMembershipRow.qml
    qml/components/RecentDocumentRow.qml
    qml/components/ReviewPreview.qml
    qml/components/SectionHeader.qml
    qml/components/SidebarButton.qml
    qml/components/SplitButton.qml
    qml/components/StatusChip.qml
    qml/components/StartupLoadingPopup.qml
    qml/components/Theme.qml
    qml/components/TimelineEffectsPanel.qml
    qml/components/TimelineClipContent.qml
    qml/components/TimelineEffectLibraryRow.qml
    qml/components/TimelineMediaAssetRow.qml
    qml/components/TimelineMarkerCard.qml
    qml/components/TimelineMarkerPin.qml
    qml/components/TimelineSourcePanel.qml
    qml/components/TimelineTransitionLibraryRow.qml
    qml/components/TimelineTrackHeader.qml
    qml/components/TimelineTrackToggleButton.qml
    qml/components/ThumbnailStateUtils.qml
    qml/components/ToneCurveChannelButton.qml
    qml/components/ToneComboBox.qml
    qml/components/ToneCurveEditor.qml
    qml/components/ToneCurvePointHandle.qml
    qml/components/ToneCurvePresetButton.qml
    qml/components/ToneTextField.qml
    qml/components/ToolOptionToggle.qml
    qml/components/ToolCommandBar.qml
    qml/components/ToolValueSlider.qml
    qml/components/ToolHud.qml
    qml/components/ToolRail.qml
    qml/components/ToolButton.qml
    qml/components/TransformOverlay.qml
    qml/workspaces/CarouselView.qml
    qml/workspaces/EditView.qml
    qml/workspaces/EditWorkspaceHost.qml
    qml/workspaces/EditWorkspace.qml
    qml/workspaces/ExportView.qml
    qml/workspaces/ExportWorkspaceHost.qml
    qml/workspaces/ImportView.qml
    qml/workspaces/LibraryView.qml
    qml/workspaces/LibraryWorkspace.qml
    qml/workspaces/MaskCanvasOverlay.qml
    qml/workspaces/MaskInspectorSections.qml
    qml/workspaces/MaskView.qml
    qml/workspaces/SequenceWorkspace.qml
    qml/workspaces/SharedMaskOverlay.qml
    qml/workspaces/StudioWorkspace.qml
    qml/workspaces/TimelineView.qml
)

target_include_directories(photo_editor_app PRIVATE src)

target_link_libraries(
  photo_editor_app
  PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Multimedia
    Qt6::Network
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Widgets
    photo_engine
    photo_rust_ffi)

target_compile_definitions(photo_editor_app
  PRIVATE
    PHOTO_EDITOR_APP_VERSION="${PROJECT_VERSION}"
    PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=$<BOOL:${PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY}>
    PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND=$<BOOL:${PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND}>
    PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND=$<BOOL:${PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND}>)

if(PHOTO_EDITOR_ENABLE_AI_INFERENCE)
  if(NOT PHOTO_EDITOR_ONNXRUNTIME_ROOT)
    message(FATAL_ERROR "PHOTO_EDITOR_ENABLE_AI_INFERENCE=ON requires PHOTO_EDITOR_ONNXRUNTIME_ROOT.")
  endif()
  if(NOT EXISTS "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "PHOTO_EDITOR_ONNXRUNTIME_ROOT must contain include/onnxruntime_cxx_api.h.")
  endif()

  find_library(
    PHOTO_EDITOR_ONNXRUNTIME_LIBRARY
    NAMES onnxruntime libonnxruntime
    PATHS "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/lib"
    NO_DEFAULT_PATH)
  if(NOT PHOTO_EDITOR_ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "Unable to find libonnxruntime in ${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/lib.")
  endif()

  target_include_directories(photo_editor_app PRIVATE "${PHOTO_EDITOR_ONNXRUNTIME_ROOT}/include")
  target_link_libraries(photo_editor_app PRIVATE "${PHOTO_EDITOR_ONNXRUNTIME_LIBRARY}")
  target_compile_definitions(
    photo_editor_app
    PRIVATE
      PHOTO_EDITOR_HAS_ONNXRUNTIME=1
      PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML=$<BOOL:${PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML}>)

  if(APPLE)
    target_link_options(photo_editor_app PRIVATE "-Wl,-rpath,@executable_path/../Frameworks")
    add_custom_command(
      TARGET photo_editor_app
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Frameworks"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              "${PHOTO_EDITOR_ONNXRUNTIME_LIBRARY}"
              "$<TARGET_BUNDLE_CONTENT_DIR:photo_editor_app>/Frameworks")
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
  set_source_files_properties(src/raw_image_decoder.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/vision_segmentation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_source_files_properties(src/brush_live_preview_item.cpp PROPERTIES LANGUAGE OBJCXX)
  if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    set_source_files_properties(src/brush_preview_compositor_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
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
photo_editor_install_desktop_runtime(photo_editor_app)
