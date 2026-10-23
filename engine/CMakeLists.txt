set(photo_engine_sources
  src/catalog/asset_location.cpp
  src/pipeline/color_adjustment_kernel_runtime.cpp
  src/pipeline/color_adjustment_scheduler.cpp
  src/pipeline/color_adjustments.cpp
  src/pipeline/color_blur_runtime.cpp
  src/pipeline/photo_effects.cpp
  src/pipeline/photo_effect_codec.cpp
  src/pipeline/photo_optical_effects.cpp
  src/pipeline/photo_artistic_effects.cpp
  src/pipeline/color_control_semantics.cpp
  src/pipeline/color_curve_evaluator.cpp
  src/pipeline/color_curve_stage_runtime.cpp
  src/pipeline/color_curve_compilation.cpp
  src/pipeline/color_detail_runtime.cpp
  src/pipeline/color_fine_detail_reference.cpp
  src/pipeline/color_fill_layer_program.cpp
  src/pipeline/color_fill_layer_blend_program.cpp
  src/pipeline/color_fill_layer_blend_reference.cpp
  src/pipeline/color_fill_layer_geometry_program.cpp
  src/pipeline/color_fill_layer_lut_program.cpp
  src/pipeline/color_fill_layer_reference.cpp
  src/pipeline/color_fill_layer_effect_reference.cpp
  src/pipeline/color_fill_layer_detail_program.cpp
  src/pipeline/color_fill_layer_chroma_reference.cpp
  src/pipeline/color_fill_layer_admission.cpp
  src/pipeline/color_fill_layer_curve_program.cpp
  src/pipeline/color_fill_layer_look_program.cpp
  src/pipeline/color_geometry_program.cpp
  src/pipeline/color_hdr_local_tone_program.cpp
  src/pipeline/color_highlight_runtime.cpp
  src/pipeline/color_tone_highlight_rolloff.cpp
  src/pipeline/color_input_runtime.cpp
  src/pipeline/color_input_white_balance.cpp
  src/pipeline/color_input_stage_identity.cpp
  src/pipeline/color_layer_runtime.cpp
  src/pipeline/color_look_runtime.cpp
  src/pipeline/color_look_compilation.cpp
  src/pipeline/color_look_compilation_runtime.cpp
  src/pipeline/color_look_grading_program.cpp
  src/pipeline/color_look_harmony_program.cpp
  src/pipeline/color_look_hue.cpp
  src/pipeline/color_look_identity.cpp
  src/pipeline/color_look_mixer_program.cpp
  src/pipeline/color_look_protected_program.cpp
  src/pipeline/color_local_layer_runtime.cpp
  src/pipeline/color_local_fast_path_policy.cpp
  src/pipeline/color_lut_runtime.cpp
  src/pipeline/color_user_lut_program.cpp
  src/pipeline/color_mask_hash_runtime.cpp
  src/pipeline/color_mask_coverage_cache.cpp
  src/pipeline/color_mask_runtime.cpp
  src/pipeline/color_math_runtime.cpp
  src/pipeline/color_mixer_runtime.cpp
  src/pipeline/color_output_runtime.cpp
  src/pipeline/color_metrics.cpp
  src/pipeline/color_pipeline.cpp
  src/pipeline/color_tone_runtime.cpp
  src/pipeline/color_tone_compilation.cpp
  src/pipeline/color_profile_asset.cpp
  src/pipeline/color_spatial_runtime.cpp
  src/pipeline/color_spatial_detail_program.cpp
  src/pipeline/color_transform.cpp
  src/pipeline/video_color_transform.cpp
  src/pipeline/color_working_space_runtime.cpp
  src/pipeline/cube_lut.cpp
  src/pipeline/dcp_profile.cpp
  src/pipeline/dcp_profile_asset.cpp
  src/pipeline/edit_recipe_v13.cpp
  src/io/hdr_avif_io.cpp
  src/io/openexr_io.cpp
  src/io/tiff_metadata_io.cpp
  src/pipeline/output_rendering.cpp
  src/pipeline/ocio_project_config.cpp
  src/pipeline/recipe_compiler.cpp
  src/pipeline/render_executor.cpp
  src/pipeline/render_invalidation_policy.cpp
  src/pipeline/render_tile_planner.cpp
)

if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  enable_language(OBJCXX)
  list(APPEND photo_engine_sources
    src/pipeline/metal/metal_pipeline_backend.mm
  )
endif()

set(PHOTO_EDITOR_CUDA_BACKEND_AVAILABLE OFF)
set(PHOTO_EDITOR_CUDA_ARCHITECTURES "75;86")
set(PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION "11.1")
if(WIN32 AND PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    find_package(CUDAToolkit QUIET)
    if(NOT CUDAToolkit_FOUND OR NOT CUDAToolkit_VERSION)
      message(WARNING "CUDA compiler was found, but its toolkit could not be verified; Windows previews will use CPU fallback.")
    elseif(CUDAToolkit_VERSION VERSION_LESS PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION)
      message(STATUS
        "CUDA ${CUDAToolkit_VERSION} is too old for the configured sm_75/sm_86 kernels; CUDA ${PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION} or newer is required, so Windows previews will use CPU fallback.")
    elseif(NOT TARGET CUDA::cudart_static)
      message(WARNING "CUDA toolkit was found without the static runtime library; Windows previews will use CPU fallback.")
    else()
      enable_language(CUDA)
      if(NOT CMAKE_CUDA_COMPILER_VERSION OR
         CMAKE_CUDA_COMPILER_VERSION VERSION_LESS PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION)
        message(STATUS
          "CUDA compiler ${CMAKE_CUDA_COMPILER_VERSION} is too old for the configured sm_75/sm_86 kernels; CUDA ${PHOTO_EDITOR_CUDA_MINIMUM_TOOLKIT_VERSION} or newer is required, so Windows previews will use CPU fallback.")
      else()
        list(APPEND photo_engine_sources
          src/pipeline/cuda/cuda_pipeline_backend.cpp
          src/pipeline/cuda/cuda_pipeline_kernels.cu
        )
        set(PHOTO_EDITOR_CUDA_BACKEND_AVAILABLE ON)
      endif()
    endif()
  else()
    message(STATUS "CUDA compiler was not found; Windows previews will use CPU fallback.")
  endif()
endif()

add_library(photo_engine ${photo_engine_sources})

find_package(TIFF QUIET)
if(TARGET TIFF::TIFF)
  target_link_libraries(
    photo_engine
    PRIVATE
      TIFF::TIFF)
  target_compile_definitions(
    photo_engine
    PRIVATE
      PHOTO_EDITOR_HAS_LIBTIFF=1)
endif()

if(PHOTO_EDITOR_ENABLE_OPENEXR)
  find_package(
    OpenEXR ${PHOTO_EDITOR_OPENEXR_MINIMUM_VERSION}
    QUIET
    CONFIG)
endif()
if(TARGET OpenEXR::OpenEXR)
  if(PHOTO_EDITOR_STRICT_OPENEXR_VERSION AND
      NOT OpenEXR_VERSION VERSION_EQUAL PHOTO_EDITOR_OPENEXR_VALIDATED_VERSION)
    message(FATAL_ERROR
      "OpenEXR ${PHOTO_EDITOR_OPENEXR_VALIDATED_VERSION} is required for strict color builds; found ${OpenEXR_VERSION}")
  endif()
  target_link_libraries(photo_engine PRIVATE OpenEXR::OpenEXR)
  target_compile_definitions(
    photo_engine
    PRIVATE
      PHOTO_EDITOR_HAS_OPENEXR=1
      PHOTO_EDITOR_OPENEXR_COMPILED_VERSION="${OpenEXR_VERSION}")
elseif(PHOTO_EDITOR_REQUIRE_OPENEXR)
  message(FATAL_ERROR
    "OpenEXR >= ${PHOTO_EDITOR_OPENEXR_MINIMUM_VERSION} is required but was not found")
endif()

if(PHOTO_EDITOR_ENABLE_LIBHEIF_HDR)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(
      PHOTO_EDITOR_LIBHEIF
      QUIET
      IMPORTED_TARGET
      "libheif>=${PHOTO_EDITOR_LIBHEIF_MINIMUM_VERSION}")
  endif()
endif()
if(TARGET PkgConfig::PHOTO_EDITOR_LIBHEIF)
  if(PHOTO_EDITOR_STRICT_LIBHEIF_VERSION AND
      NOT PHOTO_EDITOR_LIBHEIF_VERSION VERSION_EQUAL PHOTO_EDITOR_LIBHEIF_VALIDATED_VERSION)
    message(FATAL_ERROR
      "libheif ${PHOTO_EDITOR_LIBHEIF_VALIDATED_VERSION} is required for strict HDR builds; found ${PHOTO_EDITOR_LIBHEIF_VERSION}")
  endif()
  target_link_libraries(photo_engine PRIVATE PkgConfig::PHOTO_EDITOR_LIBHEIF)
  target_compile_definitions(
    photo_engine
    PRIVATE
      PHOTO_EDITOR_HAS_LIBHEIF_HDR=1
      PHOTO_EDITOR_LIBHEIF_COMPILED_VERSION="${PHOTO_EDITOR_LIBHEIF_VERSION}")
elseif(PHOTO_EDITOR_REQUIRE_LIBHEIF_HDR)
  message(FATAL_ERROR
    "libheif >= ${PHOTO_EDITOR_LIBHEIF_MINIMUM_VERSION} is required for HDR AVIF delivery but was not found")
endif()

function(photo_editor_lcms_config_target_version target_name output_var)
  set(_version "")
  foreach(_version_variable IN ITEMS
      lcms2_VERSION lcms2_VERSION_STRING LCMS2_VERSION LCMS2_VERSION_STRING
      LCMS_VERSION)
    if(DEFINED ${_version_variable} AND
       "${${_version_variable}}" MATCHES "^[0-9]+(\\.[0-9]+)+$")
      set(_version "${${_version_variable}}")
      break()
    endif()
  endforeach()

  if(NOT _version)
    get_target_property(_include_dirs ${target_name} INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _include_dirs OR _include_dirs MATCHES "-NOTFOUND$")
      set(_include_dirs "")
    endif()
    foreach(_include_variable IN ITEMS
        lcms2_INCLUDE_DIRS lcms2_INCLUDE_DIR LCMS2_INCLUDE_DIRS
        LCMS2_INCLUDE_DIR)
      if(DEFINED ${_include_variable})
        list(APPEND _include_dirs ${${_include_variable}})
      endif()
    endforeach()

    foreach(_include_dir IN LISTS _include_dirs)
      string(GENEX_STRIP "${_include_dir}" _include_path)
      foreach(_header IN ITEMS
          "${_include_path}/lcms2.h"
          "${_include_path}/lcms2/lcms2.h")
        if(EXISTS "${_header}")
          file(STRINGS "${_header}" _version_lines
            REGEX "^[ \\t]*#define[ \\t]+LCMS_VERSION[ \\t]+[0-9]+")
          foreach(_version_line IN LISTS _version_lines)
            if(_version_line MATCHES "LCMS_VERSION[ \\t]+([0-9]+)")
              set(_encoded_version "${CMAKE_MATCH_1}")
              math(EXPR _major "${_encoded_version} / 1000")
              math(EXPR _minor "(${_encoded_version} / 10) % 100")
              math(EXPR _patch "${_encoded_version} % 10")
              set(_version "${_major}.${_minor}.${_patch}")
              break()
            endif()
          endforeach()
        endif()
        if(_version)
          break()
        endif()
      endforeach()
      if(_version)
        break()
      endif()
    endforeach()
  endif()

  set(${output_var} "${_version}" PARENT_SCOPE)
endfunction()

if(PHOTO_EDITOR_ENABLE_LCMS_COLOR_MANAGEMENT)
  if(PHOTO_EDITOR_USE_PINNED_LCMS_COLOR_MANAGEMENT)
    include(LittleCmsPinned)
    photo_editor_add_pinned_lcms2()
    set(PHOTO_EDITOR_LCMS_TARGET photo_editor::lcms2_pinned)
    set(PHOTO_EDITOR_LCMS2_VERSION "2.18")
  else()
    # vcpkg's Windows port exports a CMake config target but does not require
    # pkg-config. Prefer that target so native Windows builds do not silently
    # compile without ICC support while the package is installed.
    find_package(lcms2 CONFIG QUIET)
    if(TARGET lcms2::lcms2)
      photo_editor_lcms_config_target_version(
        lcms2::lcms2 _photo_editor_lcms_config_version)
      if(_photo_editor_lcms_config_version)
        set(PHOTO_EDITOR_LCMS_TARGET lcms2::lcms2)
        set(PHOTO_EDITOR_LCMS2_VERSION
          "${_photo_editor_lcms_config_version}")
      else()
        message(WARNING
          "Little CMS config target was found, but its version could not be determined from package metadata or lcms2.h; ignoring it.")
      endif()
    endif()

    if(NOT PHOTO_EDITOR_LCMS_TARGET)
      find_package(PkgConfig QUIET)
      if(PkgConfig_FOUND)
        pkg_check_modules(
          PHOTO_EDITOR_LCMS2
          QUIET
          IMPORTED_TARGET
          "lcms2>=${PHOTO_EDITOR_LCMS_MINIMUM_VERSION}")
      endif()
      if(TARGET PkgConfig::PHOTO_EDITOR_LCMS2)
        set(PHOTO_EDITOR_LCMS_TARGET PkgConfig::PHOTO_EDITOR_LCMS2)
      endif()
    endif()
  endif()
endif()
if(PHOTO_EDITOR_LCMS_TARGET)
  if(PHOTO_EDITOR_LCMS2_VERSION VERSION_LESS PHOTO_EDITOR_LCMS_MINIMUM_VERSION)
    message(WARNING
      "Little CMS ${PHOTO_EDITOR_LCMS2_VERSION} is older than the required minimum ${PHOTO_EDITOR_LCMS_MINIMUM_VERSION}; ICC color management will not be enabled.")
    unset(PHOTO_EDITOR_LCMS_TARGET)
  elseif(PHOTO_EDITOR_STRICT_LCMS_VERSION AND
      NOT PHOTO_EDITOR_LCMS2_VERSION VERSION_EQUAL PHOTO_EDITOR_LCMS_VALIDATED_VERSION)
    message(FATAL_ERROR
      "Little CMS ${PHOTO_EDITOR_LCMS_VALIDATED_VERSION} is required for strict color builds; found ${PHOTO_EDITOR_LCMS2_VERSION}")
  endif()
endif()
if(PHOTO_EDITOR_LCMS_TARGET)
  target_link_libraries(photo_engine PRIVATE ${PHOTO_EDITOR_LCMS_TARGET})
  target_compile_definitions(
    photo_engine
    PRIVATE
      PHOTO_EDITOR_HAS_LCMS2=1
      PHOTO_EDITOR_LCMS_COMPILED_VERSION="${PHOTO_EDITOR_LCMS2_VERSION}"
      PHOTO_EDITOR_LCMS_VALIDATED_VERSION="${PHOTO_EDITOR_LCMS_VALIDATED_VERSION}"
      PHOTO_EDITOR_LCMS_STRICT_VERSION=$<BOOL:${PHOTO_EDITOR_STRICT_LCMS_VERSION}>)
elseif(PHOTO_EDITOR_REQUIRE_LCMS_COLOR_MANAGEMENT OR
       PHOTO_EDITOR_STRICT_LCMS_VERSION)
  message(FATAL_ERROR
    "Little CMS >= ${PHOTO_EDITOR_LCMS_MINIMUM_VERSION} with a verifiable version is required but was not found")
endif()

if(PHOTO_EDITOR_ENABLE_OCIO_ACES2)
  find_package(
    OpenColorIO ${PHOTO_EDITOR_OCIO_MINIMUM_VERSION}
    QUIET
    CONFIG)
endif()
if(TARGET OpenColorIO::OpenColorIO)
  if(PHOTO_EDITOR_STRICT_OCIO_VERSION AND
      NOT OpenColorIO_VERSION VERSION_EQUAL PHOTO_EDITOR_OCIO_VALIDATED_VERSION)
    message(FATAL_ERROR
      "OpenColorIO ${PHOTO_EDITOR_OCIO_VALIDATED_VERSION} is required for strict color builds; found ${OpenColorIO_VERSION}")
  endif()
  target_link_libraries(photo_engine PRIVATE OpenColorIO::OpenColorIO)
  target_compile_definitions(
    photo_engine
    PRIVATE
      PHOTO_EDITOR_HAS_OCIO=1
      PHOTO_EDITOR_HAS_OCIO_ACES2=1
      PHOTO_EDITOR_OCIO_COMPILED_VERSION="${OpenColorIO_VERSION}")
elseif(PHOTO_EDITOR_REQUIRE_OCIO_ACES2)
  message(FATAL_ERROR
    "OpenColorIO >= ${PHOTO_EDITOR_OCIO_MINIMUM_VERSION} is required for the official ACES 2 output transform but was not found")
endif()

target_include_directories(photo_engine PUBLIC include)
target_include_directories(photo_engine PRIVATE src)
target_compile_features(photo_engine PUBLIC cxx_std_20)

if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  target_link_libraries(photo_engine PRIVATE "-framework Metal" "-framework Foundation")
  set(color_metal_header "${CMAKE_CURRENT_BINARY_DIR}/generated/color_adjustments_metal_source.h")
  add_custom_command(
    OUTPUT "${color_metal_header}"
    COMMAND "${CMAKE_COMMAND}"
      "-DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline/metal/color_adjustments.metal"
      "-DOUTPUT=${color_metal_header}"
      -P "${PROJECT_SOURCE_DIR}/cmake/EmbedColorMetal.cmake"
    DEPENDS src/pipeline/metal/color_adjustments.metal
      src/pipeline/color_look_harmony_math.h
      src/pipeline/color_look_grading_math.h
      "${PROJECT_SOURCE_DIR}/cmake/EmbedColorMetal.cmake"
    VERBATIM)
  target_sources(photo_engine PRIVATE "${color_metal_header}")
  target_include_directories(photo_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_compile_definitions(photo_engine PUBLIC PHOTO_EDITOR_HAS_METAL_BACKEND=1)
  set_source_files_properties(
    src/pipeline/metal/metal_pipeline_backend.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

if(PHOTO_EDITOR_CUDA_BACKEND_AVAILABLE)
  target_link_libraries(photo_engine PRIVATE CUDA::cudart_static)
  target_compile_definitions(photo_engine PUBLIC PHOTO_EDITOR_HAS_CUDA_BACKEND=1)
  set_target_properties(photo_engine PROPERTIES
    CUDA_STANDARD 14
    CUDA_STANDARD_REQUIRED ON
    CUDA_ARCHITECTURES "${PHOTO_EDITOR_CUDA_ARCHITECTURES}"
  )
endif()

photo_editor_apply_warnings(photo_engine)

include(ArchitectureBoundaries)
photo_editor_enforce_qt_free_target(photo_engine)
