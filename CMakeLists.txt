cmake_minimum_required(VERSION 3.28)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" PHOTO_EDITOR_PROJECT_VERSION_RAW)
string(STRIP "${PHOTO_EDITOR_PROJECT_VERSION_RAW}" PHOTO_EDITOR_PROJECT_VERSION)
if(NOT PHOTO_EDITOR_PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "VERSION must contain a semantic version in the form X.Y.Z")
endif()

set(FRAMEHOUSE_MACOS_MINIMUM_VERSION "13.0" CACHE STRING "Minimum supported macOS version.")
if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(
    CMAKE_OSX_DEPLOYMENT_TARGET
    "${FRAMEHOUSE_MACOS_MINIMUM_VERSION}"
    CACHE STRING
    "Minimum macOS deployment target for Looksy.")
endif()

project(PhotoEditor VERSION ${PHOTO_EDITOR_PROJECT_VERSION} LANGUAGES CXX)

set(FRAMEHOUSE_PRODUCT_NAME "Looksy" CACHE STRING "Public product name used by installers and metadata.")
set(FRAMEHOUSE_PRODUCT_SLUG "Looksy" CACHE STRING "Filesystem-safe product name used by binaries and artifacts.")
if(FRAMEHOUSE_PRODUCT_NAME STREQUAL "Framehouse")
  set(FRAMEHOUSE_PRODUCT_NAME "Looksy" CACHE STRING "Public product name used by installers and metadata." FORCE)
endif()
if(FRAMEHOUSE_PRODUCT_SLUG STREQUAL "Framehouse")
  set(FRAMEHOUSE_PRODUCT_SLUG "Looksy" CACHE STRING "Filesystem-safe product name used by binaries and artifacts." FORCE)
endif()
set(
  FRAMEHOUSE_LEGAL_PUBLISHER
  "Publisher identity pending release approval"
  CACHE STRING
  "Approved legal publisher shown in package metadata; production packaging rejects the pending placeholder.")
set(FRAMEHOUSE_MACOS_BUNDLE_IDENTIFIER "com.virel.studio" CACHE STRING "Stable macOS bundle identifier.")
set(FRAMEHOUSE_SUPPORT_URL "https://github.com/bradyreiner/photo-editor/issues" CACHE STRING "Public support URL embedded in release metadata.")
set(FRAMEHOUSE_PRIVACY_URL "https://github.com/bradyreiner/photo-editor/blob/main/site/privacy.html" CACHE STRING "Public privacy-policy URL embedded in release metadata.")
set(FRAMEHOUSE_TERMS_URL "https://github.com/bradyreiner/photo-editor/blob/main/site/terms.html" CACHE STRING "Public terms URL embedded in release metadata.")

option(PHOTO_EDITOR_BUILD_DESKTOP "Build the Qt/QML desktop application." ON)
option(PHOTO_EDITOR_ENABLE_RUST "Enable Rust workspace integration targets." ON)
option(PHOTO_EDITOR_BUILD_TESTS "Build lightweight native tests." ON)
option(PHOTO_EDITOR_ENABLE_MACOS_SPARKLE_UPDATER "Enable Sparkle-based macOS update integration." OFF)
option(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND "Enable the Metal preview backend on supported macOS builds." ON)
option(PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND "Enable the CUDA preview backend on supported Windows builds." ON)
option(PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS "Write private local diagnostics in production builds." ON)
option(PHOTO_EDITOR_ENABLE_LCMS_COLOR_MANAGEMENT "Enable the Little CMS ICC transform backend when available." ON)
option(PHOTO_EDITOR_REQUIRE_LCMS_COLOR_MANAGEMENT "Fail configuration when the Little CMS ICC backend is unavailable." OFF)
option(PHOTO_EDITOR_STRICT_LCMS_VERSION "Require the exact validated Little CMS version." OFF)
option(PHOTO_EDITOR_USE_PINNED_LCMS_COLOR_MANAGEMENT "Build the verified Little CMS source archive instead of using a host package." OFF)
set(PHOTO_EDITOR_LCMS_MINIMUM_VERSION "2.14" CACHE STRING "Minimum supported Little CMS version.")
set(PHOTO_EDITOR_LCMS_VALIDATED_VERSION "2.18" CACHE STRING "Exact Little CMS version validated for strict/release color builds.")
option(PHOTO_EDITOR_ENABLE_OCIO_ACES2 "Use the official OpenColorIO ACES 2 output transform when available." ON)
option(PHOTO_EDITOR_REQUIRE_OCIO_ACES2 "Fail configuration when the official OpenColorIO ACES 2 backend is unavailable." OFF)
option(PHOTO_EDITOR_STRICT_OCIO_VERSION "Require the exact validated OpenColorIO version." OFF)
set(PHOTO_EDITOR_OCIO_MINIMUM_VERSION "2.5" CACHE STRING "Minimum OpenColorIO version with the validated ACES 2 implementation.")
set(PHOTO_EDITOR_OCIO_VALIDATED_VERSION "2.5.1" CACHE STRING "Exact OpenColorIO version validated for strict/release color builds.")
option(PHOTO_EDITOR_ENABLE_OPENEXR "Enable standards-based OpenEXR scene-linear interchange." ON)
option(PHOTO_EDITOR_REQUIRE_OPENEXR "Fail configuration when the OpenEXR backend is unavailable." OFF)
option(PHOTO_EDITOR_STRICT_OPENEXR_VERSION "Require the exact validated OpenEXR version." OFF)
set(PHOTO_EDITOR_OPENEXR_MINIMUM_VERSION "3.2" CACHE STRING "Minimum supported OpenEXR version.")
set(PHOTO_EDITOR_OPENEXR_VALIDATED_VERSION "3.4.4" CACHE STRING "Exact OpenEXR version validated for strict color builds.")
option(PHOTO_EDITOR_ENABLE_LIBHEIF_HDR "Enable 10-bit PQ/HLG AVIF delivery through libheif." ON)
option(PHOTO_EDITOR_REQUIRE_LIBHEIF_HDR "Fail configuration when libheif HDR delivery is unavailable." OFF)
option(PHOTO_EDITOR_STRICT_LIBHEIF_VERSION "Require the exact validated libheif version." OFF)
set(PHOTO_EDITOR_LIBHEIF_MINIMUM_VERSION "1.19" CACHE STRING "Minimum supported libheif version.")
set(PHOTO_EDITOR_LIBHEIF_VALIDATED_VERSION "1.21.1" CACHE STRING "Validated libheif version.")
option(PHOTO_EDITOR_ENABLE_LIBRAW_CAMERA_LINEAR "Enable cross-platform camera-linear RAW decoding through LibRaw." ON)
option(PHOTO_EDITOR_REQUIRE_LIBRAW_CAMERA_LINEAR "Fail configuration when the LibRaw camera-linear backend is unavailable." OFF)
option(PHOTO_EDITOR_STRICT_LIBRAW_VERSION "Require the exact validated LibRaw version." OFF)
set(PHOTO_EDITOR_LIBRAW_MINIMUM_VERSION "0.21" CACHE STRING "Minimum supported LibRaw version.")
set(PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION "0.21.5" CACHE STRING "Exact LibRaw version validated for camera-linear RAW decoding.")

if(PHOTO_EDITOR_ENABLE_LCMS_COLOR_MANAGEMENT AND PHOTO_EDITOR_USE_PINNED_LCMS_COLOR_MANAGEMENT)
  enable_language(C)
endif()

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

add_library(photo_editor_raw_decoder_backend INTERFACE)
target_compile_definitions(
  photo_editor_raw_decoder_backend
  INTERFACE
    PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION="${PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION}")
set(PHOTO_EDITOR_LIBRAW_FOUND OFF)
set(PHOTO_EDITOR_LIBRAW_VERSION "")
if(PHOTO_EDITOR_ENABLE_LIBRAW_CAMERA_LINEAR)
  find_package(unofficial-libraw CONFIG QUIET)
  find_package(LibRaw CONFIG QUIET)
  find_package(libraw CONFIG QUIET)
  set(PHOTO_EDITOR_LIBRAW_CMAKE_TARGET "")
  foreach(candidate
      unofficial::libraw::libraw_r
      unofficial::libraw::raw_r
      LibRaw::LibRaw
      LibRaw::raw_r
      libraw::libraw_r
      libraw::raw_r
      libraw::raw)
    if(TARGET ${candidate})
      set(PHOTO_EDITOR_LIBRAW_CMAKE_TARGET ${candidate})
      break()
    endif()
  endforeach()
  if(PHOTO_EDITOR_LIBRAW_CMAKE_TARGET)
    target_link_libraries(
      photo_editor_raw_decoder_backend
      INTERFACE ${PHOTO_EDITOR_LIBRAW_CMAKE_TARGET})
    set(PHOTO_EDITOR_LIBRAW_FOUND ON)
    if(DEFINED LibRaw_VERSION)
      set(PHOTO_EDITOR_LIBRAW_VERSION "${LibRaw_VERSION}")
    elseif(DEFINED libraw_VERSION)
      set(PHOTO_EDITOR_LIBRAW_VERSION "${libraw_VERSION}")
    elseif(DEFINED unofficial-libraw_VERSION)
      set(PHOTO_EDITOR_LIBRAW_VERSION "${unofficial-libraw_VERSION}")
    endif()
  else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(
        PHOTO_EDITOR_LIBRAW
        QUIET
        IMPORTED_TARGET
        GLOBAL
        "libraw_r>=${PHOTO_EDITOR_LIBRAW_MINIMUM_VERSION}")
    endif()
    if(TARGET PkgConfig::PHOTO_EDITOR_LIBRAW)
      if(APPLE)
        # Homebrew's LibRaw pkg-config metadata still advertises the retired
        # `stdc++` compatibility library. Apple Clang already links libc++ and
        # resolves that compatibility name back to the same runtime, producing
        # duplicate-library warnings on every executable. Keep the imported
        # target's paths/includes while removing only that redundant runtime.
        get_target_property(
          photo_editor_libraw_link_libraries
          PkgConfig::PHOTO_EDITOR_LIBRAW
          INTERFACE_LINK_LIBRARIES)
        if(photo_editor_libraw_link_libraries)
          list(FILTER photo_editor_libraw_link_libraries
            EXCLUDE REGEX "(^|-)l?stdc\\+\\+$")
          set_property(
            TARGET PkgConfig::PHOTO_EDITOR_LIBRAW
            PROPERTY INTERFACE_LINK_LIBRARIES
              "${photo_editor_libraw_link_libraries}")
        endif()
      endif()
      target_link_libraries(
        photo_editor_raw_decoder_backend
        INTERFACE PkgConfig::PHOTO_EDITOR_LIBRAW)
      set(PHOTO_EDITOR_LIBRAW_FOUND ON)
    endif()
  endif()
endif()

if(PHOTO_EDITOR_LIBRAW_FOUND)
  target_compile_definitions(
    photo_editor_raw_decoder_backend
    INTERFACE PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR=1)
else()
  target_compile_definitions(
    photo_editor_raw_decoder_backend
    INTERFACE PHOTO_EDITOR_HAS_LIBRAW_CAMERA_LINEAR=0)
endif()

if(PHOTO_EDITOR_REQUIRE_LIBRAW_CAMERA_LINEAR AND NOT PHOTO_EDITOR_LIBRAW_FOUND)
  message(FATAL_ERROR "The LibRaw camera-linear backend is required but unavailable.")
endif()
if(PHOTO_EDITOR_STRICT_LIBRAW_VERSION AND
   NOT PHOTO_EDITOR_LIBRAW_VERSION STREQUAL PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION)
  message(FATAL_ERROR
    "Strict LibRaw validation requires ${PHOTO_EDITOR_LIBRAW_VALIDATED_VERSION}; found '${PHOTO_EDITOR_LIBRAW_VERSION}'.")
endif()

add_subdirectory(engine)
add_subdirectory(rust)

if(PHOTO_EDITOR_BUILD_DESKTOP)
  add_subdirectory(apps/desktop)
endif()

if(PHOTO_EDITOR_BUILD_TESTS)
  add_subdirectory(tests)
endif()

photo_editor_configure_packaging()
