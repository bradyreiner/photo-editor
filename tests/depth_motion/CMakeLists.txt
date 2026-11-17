cmake_minimum_required(VERSION 3.24)
project(depth_motion_qualification LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia Test ShaderTools Concurrent)
enable_testing()
get_filename_component(PHOTO_EDITOR_ROOT "../.." ABSOLUTE)
add_executable(depth_motion_ui_test ../depth_motion_ui_test.cpp
  ../../apps/desktop/src/depth_motion_controller.cpp
  ../../apps/desktop/src/depth_motion_controller.h)
target_sources(depth_motion_ui_test PRIVATE ../../apps/desktop/src/depth_point_cloud_viewport.cpp
  ../../apps/desktop/src/depth_point_cloud_viewport.h ../../apps/desktop/src/scene_video_recorder.cpp ../../apps/desktop/src/scene_video_recorder.h ../../apps/desktop/src/scene_library_controller.cpp ../../apps/desktop/src/scene_library_controller.h)
target_include_directories(depth_motion_ui_test PRIVATE ../../apps/desktop/src)
target_compile_definitions(depth_motion_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PHOTO_EDITOR_ROOT}")
target_compile_options(depth_motion_ui_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
target_link_libraries(depth_motion_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Test Qt6::Concurrent)
set_source_files_properties("${PHOTO_EDITOR_ROOT}/scripts/depth-motion-worker.py"
  PROPERTIES QT_RESOURCE_ALIAS "depth-motion-worker.py")
qt_add_resources(depth_motion_ui_test "depth_motion_worker" PREFIX "/photo-editor/runtime"
  FILES "${PHOTO_EDITOR_ROOT}/scripts/depth-motion-worker.py")
qt_add_shaders(depth_motion_ui_test "depth_motion_ui_shaders" BATCHABLE
  PREFIX "/photo-editor/shaders" BASE "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders"
  FILES "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders/optical_light.vert"
    "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders/optical_light.frag")
add_test(NAME depth_motion_ui_test COMMAND depth_motion_ui_test)
add_executable(depth_point_cloud_render_test ../depth_point_cloud_render_test.cpp
  ../../apps/desktop/src/depth_point_cloud_viewport.cpp ../../apps/desktop/src/depth_point_cloud_viewport.h)
target_include_directories(depth_point_cloud_render_test PRIVATE ../../apps/desktop/src)
target_compile_options(depth_point_cloud_render_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
target_link_libraries(depth_point_cloud_render_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent)
add_test(NAME depth_point_cloud_render_test COMMAND depth_point_cloud_render_test)
add_executable(scene_library_controller_test ../scene_library_controller_test.cpp
  ../../apps/desktop/src/scene_library_controller.cpp ../../apps/desktop/src/scene_library_controller.h)
target_include_directories(scene_library_controller_test PRIVATE ../../apps/desktop/src)
target_link_libraries(scene_library_controller_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
add_test(NAME scene_library_controller_test COMMAND scene_library_controller_test)
find_program(DEPTH_MOTION_TEST_PYTHON NAMES python
  PATHS "${PHOTO_EDITOR_ROOT}/.photo-editor/depth-motion/venv/bin" NO_DEFAULT_PATH)
if(DEPTH_MOTION_TEST_PYTHON)
  add_test(NAME depth_motion_worker_test
    COMMAND "${DEPTH_MOTION_TEST_PYTHON}" -B "${PHOTO_EDITOR_ROOT}/tests/depth_motion_worker_test.py")
  set_tests_properties(depth_motion_worker_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
