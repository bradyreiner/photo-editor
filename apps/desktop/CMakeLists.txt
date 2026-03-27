find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Quick Qml QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

set(photo_editor_desktop_sources
  src/app_shell_controller.cpp
  src/carousel_document_controller.cpp
  src/catalog_asset_model.cpp
  src/editor_preview_controller.cpp
  src/export_job_controller.cpp
  src/main.cpp
  src/app_bootstrap.cpp
  src/timeline_asset_store.cpp
  src/timeline_document_controller.cpp
  src/timeline_playback_controller.cpp
  src/macos_updater_stub.cpp
)

if(APPLE)
  set(PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER "com.photoeditor.desktop")
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

qt_add_executable(photo_editor_app
  ${photo_editor_desktop_sources}
)

set_target_properties(
  photo_editor_app
  PROPERTIES
    OUTPUT_NAME "PhotoEditor"
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE)

if(APPLE)
  set_target_properties(
    photo_editor_app
    PROPERTIES
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME "PhotoEditor"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${PHOTO_EDITOR_MACOS_BUNDLE_IDENTIFIER}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
endif()

qt_add_qml_module(photo_editor_app
  URI PhotoEditor
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/ActionButton.qml
    qml/components/AppIcon.qml
    qml/components/AppPreferencesDialog.qml
    qml/components/BrandPanel.qml
    qml/components/ColorChannelButton.qml
    qml/components/CropOverlay.qml
    qml/components/FilterChip.qml
    qml/components/InfoPill.qml
    qml/components/InspectorSection.qml
    qml/components/InspectorSlider.qml
    qml/components/KeyboardShortcutsDialog.qml
    qml/components/MetricCard.qml
    qml/components/SectionHeader.qml
    qml/components/SidebarButton.qml
    qml/components/Theme.qml
    qml/components/TimelineEffectsPanel.qml
    qml/components/TimelineSourcePanel.qml
    qml/components/TimelineTrackHeader.qml
    qml/components/ToneComboBox.qml
    qml/components/ToneCurveEditor.qml
    qml/components/ToneTextField.qml
    qml/workspaces/CarouselView.qml
    qml/workspaces/EditView.qml
    qml/workspaces/EditWorkspace.qml
    qml/workspaces/ExportView.qml
    qml/workspaces/ImportView.qml
    qml/workspaces/LibraryView.qml
    qml/workspaces/LibraryWorkspace.qml
    qml/workspaces/MaskView.qml
    qml/workspaces/SequenceWorkspace.qml
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
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Widgets
    photo_engine
    photo_rust_ffi)

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
