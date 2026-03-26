add_executable(render_graph_smoke_test render_graph_smoke_test.cpp)
target_link_libraries(render_graph_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_graph_smoke_test)

add_test(NAME render_graph_smoke_test COMMAND render_graph_smoke_test)

add_executable(catalog_contract_smoke_test catalog_contract_smoke_test.cpp)
target_link_libraries(catalog_contract_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(catalog_contract_smoke_test)

add_test(NAME catalog_contract_smoke_test COMMAND catalog_contract_smoke_test)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Multimedia Widgets)

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
  carousel_document_controller_test
  carousel_document_controller_test.cpp
  ../apps/desktop/src/carousel_document_controller.h
  ../apps/desktop/src/carousel_document_controller.cpp)
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
    photo_engine)
photo_editor_apply_warnings(carousel_document_controller_test)

add_test(NAME carousel_document_controller_test COMMAND carousel_document_controller_test)

add_executable(
  timeline_document_controller_test
  timeline_document_controller_test.cpp
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
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)
photo_editor_apply_warnings(timeline_document_controller_test)

add_test(NAME timeline_document_controller_test COMMAND timeline_document_controller_test)

add_executable(
  timeline_playback_controller_test
  timeline_playback_controller_test.cpp
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
