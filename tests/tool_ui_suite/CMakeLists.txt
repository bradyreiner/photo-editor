cmake_minimum_required(VERSION 3.28)
project(LooksyToolUiProof LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Test)
enable_testing()
get_filename_component(PHOTO_EDITOR_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

foreach(tool_ui_test photo_effect_browser_test photo_typography_panel_ui_test retouch_tool_hud_ui_test retouch_cancel_ui_test visual_example_card_ui_test preset_browser_ui_test)
  add_executable(${tool_ui_test} "${PHOTO_EDITOR_SOURCE_DIR}/tests/${tool_ui_test}.cpp")
  target_compile_definitions(${tool_ui_test} PRIVATE
    PHOTO_EDITOR_SOURCE_DIR="${PHOTO_EDITOR_SOURCE_DIR}"
    PHOTO_EFFECT_TEST_ROOT="${PHOTO_EDITOR_SOURCE_DIR}"
    RETOUCH_TEST_ROOT="${PHOTO_EDITOR_SOURCE_DIR}")
  target_link_libraries(${tool_ui_test} PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  target_compile_options(${tool_ui_test} PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME ${tool_ui_test} COMMAND ${tool_ui_test})
  set_tests_properties(${tool_ui_test} PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
