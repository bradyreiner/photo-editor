cmake_minimum_required(VERSION 3.28)

project(PhotoEditor VERSION 0.1.0 LANGUAGES CXX)

option(PHOTO_EDITOR_BUILD_DESKTOP "Build the Qt/QML desktop application." ON)
option(PHOTO_EDITOR_ENABLE_RUST "Enable Rust workspace integration targets." ON)
option(PHOTO_EDITOR_BUILD_TESTS "Build lightweight native tests." ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(CompilerWarnings)
include(Packaging)
include(RustInterop)

add_subdirectory(engine)
add_subdirectory(rust)

if(PHOTO_EDITOR_BUILD_DESKTOP)
  add_subdirectory(apps/desktop)
endif()

if(PHOTO_EDITOR_BUILD_TESTS)
  add_subdirectory(tests)
endif()

photo_editor_configure_packaging()
