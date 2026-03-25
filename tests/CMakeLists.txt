add_executable(render_graph_smoke_test render_graph_smoke_test.cpp)
target_link_libraries(render_graph_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_graph_smoke_test)

add_test(NAME render_graph_smoke_test COMMAND render_graph_smoke_test)

add_executable(catalog_contract_smoke_test catalog_contract_smoke_test.cpp)
target_link_libraries(catalog_contract_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(catalog_contract_smoke_test)

add_test(NAME catalog_contract_smoke_test COMMAND catalog_contract_smoke_test)

find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui)

add_executable(
  editor_preview_math_test
  editor_preview_math_test.cpp
  ../apps/desktop/src/editor_preview_controller.h)
target_include_directories(editor_preview_math_test PRIVATE ../apps/desktop/src)
target_link_libraries(
  editor_preview_math_test
  PRIVATE
    Qt6::Core
    Qt6::Concurrent
    Qt6::Gui)
set_property(TARGET editor_preview_math_test PROPERTY AUTOMOC ON)
photo_editor_apply_warnings(editor_preview_math_test)

add_test(NAME editor_preview_math_test COMMAND editor_preview_math_test)
