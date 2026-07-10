cmake_minimum_required(VERSION 3.28)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PHOTO_EDITOR_PROJECT_VERSION_RAW)
string(STRIP "${PHOTO_EDITOR_PROJECT_VERSION_RAW}" PHOTO_EDITOR_PROJECT_VERSION)
if(NOT PHOTO_EDITOR_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version in the form X.Y.Z")
endif()

set(VIREL_MACOS_MINIMUM_VERSION "13.0" CACHE STRING "Minimum supported macOS version.")
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(
    CMAKE_OSX_DEPLOYMENT_TARGET
    "${VIREL_MACOS_MINIMUM_VERSION}"
    CACHE STRING
    "Minimum macOS deployment target for Virel Studio.")
endif()

project(PhotoEditor VERSION ${PHOTO_EDITOR_PROJECT_VERSION} LANGUAGES CXX)

set(VIREL_PRODUCT_NAME "Virel Studio" CACHE STRING "Public product name used by installers and metadata.")
set(VIREL_PRODUCT_SLUG "VirelStudio" CACHE STRING "Filesystem-safe product name used by binaries and artifacts.")
set(VIREL_VENDOR_NAME "Virel" CACHE STRING "Public vendor/publisher display name.")
set(VIREL_MACOS_BUNDLE_IDENTIFIER "com.virel.studio" CACHE STRING "Stable macOS bundle identifier.")
set(VIREL_SUPPORT_URL "https://virel.studio/support" CACHE STRING "Public support URL embedded in release metadata.")
set(VIREL_PRIVACY_URL "https://virel.studio/privacy" CACHE STRING "Public privacy-policy URL embedded in release metadata.")
set(VIREL_TERMS_URL "https://virel.studio/terms" CACHE STRING "Public terms URL embedded in release metadata.")

option(PHOTO_EDITOR_BUILD_DESKTOP "Build the Qt/QML desktop application." ON)
option(PHOTO_EDITOR_ENABLE_RUST "Enable Rust workspace integration targets." ON)
option(PHOTO_EDITOR_BUILD_TESTS "Build lightweight native tests." ON)
option(PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER "Enable Sparkle-based macOS update integration." OFF)
option(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND "Enable the Metal preview backend on supported macOS builds." ON)
option(PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND "Enable the CUDA preview backend on supported Windows builds." ON)
option(PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS "Write private local diagnostics in production builds." ON)

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
