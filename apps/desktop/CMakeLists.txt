find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Quick Qml QuickControls2 Widgets)

qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

qt_add_executable(photo_editor_app
  src/catalog_asset_model.cpp
  src/editor_preview_controller.cpp
  src/main.cpp
  src/app_bootstrap.cpp
)

qt_add_qml_module(photo_editor_app
  URI PhotoEditor
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/components/ActionButton.qml
    qml/components/BrandPanel.qml
    qml/components/ColorChannelButton.qml
    qml/components/FilterChip.qml
    qml/components/InfoPill.qml
    qml/components/InspectorSection.qml
    qml/components/InspectorSlider.qml
    qml/components/MetricCard.qml
    qml/components/SidebarButton.qml
    qml/components/ToneComboBox.qml
    qml/components/ToneCurveEditor.qml
    qml/components/ToneTextField.qml
    qml/workspaces/EditView.qml
    qml/workspaces/ExportView.qml
    qml/workspaces/ImportView.qml
    qml/workspaces/LibraryView.qml
    qml/workspaces/MaskView.qml
)

target_include_directories(photo_editor_app PRIVATE src)

target_link_libraries(
  photo_editor_app
  PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui
    Qt6::Quick
    Qt6::Qml
    Qt6::QuickControls2
    Qt6::Widgets
    photo_engine
    photo_rust_ffi)

photo_editor_apply_warnings(photo_editor_app)
