cmake_minimum_required(VERSION 3.24)
project(import_preview_check LANGUAGES CXX OBJCXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 ShaderTools)
get_filename_component(PHOTO_EDITOR_ROOT "../.." ABSOLUTE)
add_executable(import_preview_image_provider_test ../import_preview_image_provider_test.cpp
  ../../apps/desktop/src/import_preview_image_provider.cpp
  ../../apps/desktop/src/import_preview_image_provider_macos.mm)
target_include_directories(import_preview_image_provider_test PRIVATE ../../apps/desktop/src)
target_compile_definitions(import_preview_image_provider_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PHOTO_EDITOR_ROOT}")
target_compile_options(import_preview_image_provider_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
set_source_files_properties(../../apps/desktop/src/import_preview_image_provider_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
target_link_libraries(import_preview_image_provider_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
  "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics")
qt_add_shaders(import_preview_image_provider_test "import_preview_shaders" BATCHABLE
  PREFIX "/photo-editor/shaders" BASE "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders"
  FILES "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders/optical_light.vert" "${PHOTO_EDITOR_ROOT}/apps/desktop/shaders/optical_light.frag")
enable_testing()
add_test(NAME import_preview_image_provider_test COMMAND import_preview_image_provider_test)
