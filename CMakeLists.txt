cmake_minimum_required(VERSION 3.28)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PHOTO_EDITOR_PROJECT_VERSION_RAW)
string(STRIP "${PHOTO_EDITOR_PROJECT_VERSION_RAW}" PHOTO_EDITOR_PROJECT_VERSION)
if(NOT PHOTO_EDITOR_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version in the form X.Y.Z")
endif()

project(PhotoEditor VERSION ${PHOTO_EDITOR_PROJECT_VERSION} LANGUAGES CXX)

option(PHOTO_EDITOR_BUILD_DESKTOP "Build the Qt/QML desktop application." ON)
option(PHOTO_EDITOR_ENABLE_RUST "Enable Rust workspace integration targets." ON)
option(PHOTO_EDITOR_BUILD_TESTS "Build lightweight native tests." ON)
option(PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER "Enable Sparkle-based macOS update integration." OFF)
option(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND "Enable the Metal preview backend on supported macOS builds." ON)
option(PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND "Enable the CUDA preview backend on supported Windows builds." ON)

set(PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY_DEFAULT OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY_DEFAULT ON)
endif()
option(
  PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY
  "Enable development-only observability and structured diagnostics."
  ${PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY_DEFAULT})

if(APPLE AND (PHOTO_EDITOR_BUILD_DESKTOP OR PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER))
  enable_language(OBJCXX)
endif()

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
