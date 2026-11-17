function(photo_editor_link_private_visual_search_index target_name)
  if(WIN32)
    target_link_libraries(${target_name} PRIVATE Advapi32)
  endif()
endfunction()

if(WIN32 AND PHOTO_EDITOR_ENABLE_CUDA_PREVIEW_BACKEND)
  add_executable(
    cuda_aces2_output_lut_test
    cuda_aces2_output_lut_test.cpp)
  target_link_libraries(cuda_aces2_output_lut_test PRIVATE photo_engine)
  photo_editor_apply_warnings(cuda_aces2_output_lut_test)
  add_test(NAME cuda_aces2_output_lut_test COMMAND cuda_aces2_output_lut_test)
  set_tests_properties(
    cuda_aces2_output_lut_test
    PROPERTIES SKIP_RETURN_CODE 77 LABELS "windows;cuda;color;preview")
endif()

add_executable(gaussian_compound_lens_test gaussian_compound_lens_test.cpp)
target_include_directories(gaussian_compound_lens_test PRIVATE ../apps/desktop/src)
photo_editor_apply_warnings(gaussian_compound_lens_test)
add_test(NAME gaussian_compound_lens_test COMMAND gaussian_compound_lens_test)
add_executable(gaussian_film_plane_test gaussian_film_plane_test.cpp)
target_include_directories(gaussian_film_plane_test PRIVATE ../apps/desktop/src)
photo_editor_apply_warnings(gaussian_film_plane_test)
add_test(NAME gaussian_film_plane_test COMMAND gaussian_film_plane_test)

add_executable(timeline_graphics_track_compaction_test
  timeline_graphics_track_compaction_test.cpp
  ../apps/desktop/src/timeline_graphics_track_compaction.cpp)
photo_editor_apply_warnings(timeline_graphics_track_compaction_test)
add_test(NAME timeline_graphics_track_compaction_test
  COMMAND timeline_graphics_track_compaction_test)
set_tests_properties(timeline_graphics_track_compaction_test PROPERTIES
  TIMEOUT 30 LABELS "timeline;graphics;stacking;planning")

foreach(effect_test photo_optical_effects_test photo_artistic_effects_test photo_effect_reference_accuracy_test photo_extended_effect_stack_test layer_styles_test)
  add_executable(${effect_test} ${effect_test}.cpp)
  target_link_libraries(${effect_test} PRIVATE photo_engine)
  photo_editor_apply_warnings(${effect_test})
  add_test(NAME ${effect_test} COMMAND ${effect_test})
endforeach()

add_executable(layer_styles_integration_test layer_styles_integration_test.cpp)
target_include_directories(layer_styles_integration_test PRIVATE ../engine/src/pipeline)
target_link_libraries(layer_styles_integration_test PRIVATE photo_engine)
photo_editor_apply_warnings(layer_styles_integration_test)
add_test(NAME layer_styles_integration_test COMMAND layer_styles_integration_test)

add_executable(hdr_retouch_invariants_test hdr_retouch_invariants_test.cpp)
target_link_libraries(hdr_retouch_invariants_test PRIVATE photo_engine)
photo_editor_apply_warnings(hdr_retouch_invariants_test)
add_test(NAME hdr_retouch_invariants_test COMMAND hdr_retouch_invariants_test)

add_executable(film_grain_detail_test film_grain_detail_test.cpp)
target_link_libraries(film_grain_detail_test PRIVATE photo_engine)
photo_editor_apply_warnings(film_grain_detail_test)
add_test(NAME film_grain_detail_test COMMAND film_grain_detail_test)

add_executable(photo_effect_spatial_test photo_effect_spatial_test.cpp)
add_executable(photo_effect_kernel_benchmark photo_effect_kernel_benchmark.cpp)
add_executable(photo_layer_detail_benchmark photo_layer_detail_benchmark.cpp)
target_link_libraries(photo_layer_detail_benchmark PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_detail_benchmark)
target_link_libraries(photo_effect_kernel_benchmark PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_kernel_benchmark)
target_link_libraries(photo_effect_spatial_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_spatial_test)
add_test(NAME photo_effect_spatial_test COMMAND photo_effect_spatial_test)


add_executable(photo_effect_posterize_test photo_effect_posterize_test.cpp)
target_link_libraries(photo_effect_posterize_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_posterize_test)
add_test(NAME photo_effect_posterize_test COMMAND photo_effect_posterize_test)

add_executable(photo_effect_gaussian_test photo_effect_gaussian_test.cpp)
target_link_libraries(photo_effect_gaussian_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_gaussian_test)
add_test(NAME photo_effect_gaussian_test COMMAND photo_effect_gaussian_test)

add_executable(photo_effect_codec_test photo_effect_codec_test.cpp)
target_link_libraries(photo_effect_codec_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_effect_codec_test)
add_test(NAME photo_effect_codec_test COMMAND photo_effect_codec_test)

add_executable(render_tile_planner_test render_tile_planner_test.cpp)
target_link_libraries(render_tile_planner_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_tile_planner_test)

add_test(NAME render_tile_planner_test COMMAND render_tile_planner_test)

add_executable(geometry_transform_invariants_test geometry_transform_invariants_test.cpp)
target_include_directories(geometry_transform_invariants_test PRIVATE ../engine/src)
target_link_libraries(geometry_transform_invariants_test PRIVATE photo_engine)
photo_editor_apply_warnings(geometry_transform_invariants_test)
add_test(NAME geometry_transform_invariants_test COMMAND geometry_transform_invariants_test)
set_tests_properties(geometry_transform_invariants_test PROPERTIES
  TIMEOUT 30 LABELS "engine;geometry;performance")

add_executable(removal_kernel_perf_test removal_kernel_perf_test.cpp)
target_include_directories(removal_kernel_perf_test PRIVATE ../engine/src/pipeline)
target_link_libraries(removal_kernel_perf_test PRIVATE photo_engine)
photo_editor_apply_warnings(removal_kernel_perf_test)
add_test(NAME removal_kernel_perf_test COMMAND removal_kernel_perf_test)
set_tests_properties(removal_kernel_perf_test PROPERTIES
  TIMEOUT 60 LABELS "engine;retouch;performance")

add_executable(photo_layer_alpha_test photo_layer_alpha_test.cpp)
target_link_libraries(photo_layer_alpha_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_alpha_test)
add_test(NAME photo_layer_alpha_test COMMAND photo_layer_alpha_test)
add_executable(photo_layer_effect_test photo_layer_effect_test.cpp)
target_link_libraries(photo_layer_effect_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_effect_test)
add_test(NAME photo_layer_effect_test COMMAND photo_layer_effect_test)
add_executable(photo_layer_content_padding_test photo_layer_content_padding_test.cpp)
target_link_libraries(photo_layer_content_padding_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_content_padding_test)
add_test(NAME photo_layer_content_padding_test COMMAND photo_layer_content_padding_test)

add_executable(photo_layer_sampling_test photo_layer_sampling_test.cpp)
target_link_libraries(photo_layer_sampling_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_sampling_test)
add_test(NAME photo_layer_sampling_test COMMAND photo_layer_sampling_test)

add_executable(photo_layer_payload_reuse_test photo_layer_payload_reuse_test.cpp)
target_link_libraries(photo_layer_payload_reuse_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_payload_reuse_test)
add_test(NAME photo_layer_payload_reuse_test COMMAND photo_layer_payload_reuse_test)

add_executable(photo_layer_detail_admission_test photo_layer_detail_admission_test.cpp)
target_link_libraries(photo_layer_detail_admission_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_detail_admission_test)
add_test(NAME photo_layer_detail_admission_test COMMAND photo_layer_detail_admission_test)

find_program(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE NAMES python3 REQUIRED)
if(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE)
  set(PHOTO_EDITOR_DEPTH_TEST_PYTHON "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" CACHE FILEPATH
    "Python with NumPy, Pillow and OpenCV for depth-motion pixel and codec tests")
  add_test(NAME depth_motion_worker_test
    COMMAND "${PHOTO_EDITOR_DEPTH_TEST_PYTHON}" -B "${PROJECT_SOURCE_DIR}/tests/depth_motion_worker_test.py")
  set_tests_properties(depth_motion_worker_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 LABELS "depth;geometry;codec;cache")
  add_test(
    NAME editor_regression_tooling_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
      "${PROJECT_SOURCE_DIR}/scripts/perf/test-editor-regression-gate.py")
  set_tests_properties(editor_regression_tooling_test PROPERTIES TIMEOUT 30 LABELS "regression;source-contract")
  add_test(NAME popup_system_inventory_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/audit-popup-system.py")
  add_test(NAME ui_style_audit_regression_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B "${PROJECT_SOURCE_DIR}/scripts/test-popup-style-audit.py")
  set_tests_properties(popup_system_inventory_test ui_style_audit_regression_test
    PROPERTIES TIMEOUT 30 LABELS "desktop;ui-style;source-contract")
  add_test(NAME macos_bundle_signing_contract
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/scripts/test-sign-macos-bundle.py")
  set_tests_properties(macos_bundle_signing_contract PROPERTIES TIMEOUT 30 LABELS "desktop;packaging")
  add_test(NAME search_runtime_bundle_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tests/test_search_runtime_bundle.py")
  set_tests_properties(search_runtime_bundle_test PROPERTIES TIMEOUT 30 LABELS "desktop;search;models")
  add_test(NAME search_model_installer_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tests/test_search_model_installer.py")
  set_tests_properties(search_model_installer_test PROPERTIES TIMEOUT 30 LABELS "desktop;search;models")
  add_test(NAME search_model_worker_protocol_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/tests/test_search_model_worker.py")
  set_tests_properties(search_model_worker_protocol_test PROPERTIES TIMEOUT 30 LABELS "desktop;search;models")
  add_test(NAME agent_runtime_preparation_test
    COMMAND "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}" -B
            "${PROJECT_SOURCE_DIR}/scripts/test-prepare-agent-runtime.py")
  add_test(
    NAME ai_model_bundle_verifier_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/verify-ai-model-bundle.py"
      --self-test)
  set_tests_properties(
    ai_model_bundle_verifier_test
    PROPERTIES LABELS "desktop;ai-edit;models;packaging")
  add_test(
    NAME ai_runtime_installer_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/install-ai-runtime.py"
      --self-test)
  set_tests_properties(
    ai_runtime_installer_test
    PROPERTIES LABELS "desktop;ai-edit;models;packaging")
  add_test(
    NAME agent_human_review_packet_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/perf/test-agent-human-review-packet.py")
  set_tests_properties(
    agent_human_review_packet_test
    PROPERTIES LABELS "desktop;ai-edit;agent;quality")
  add_test(
    NAME rendering_migration_review_packet_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/color/test-rendering-migration-review-packet.py")
  set_tests_properties(
    rendering_migration_review_packet_test
    PROPERTIES LABELS "desktop;color;quality")
  add_test(
    NAME plan_rendering_migration_refresh_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/color/test-plan-rendering-migration-refresh.py")
  set_tests_properties(
    plan_rendering_migration_refresh_test
    PROPERTIES LABELS "desktop;color;quality")
  add_test(
    NAME validate_rendering_migration_refresh_chunk_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/color/test-validate-rendering-migration-refresh-chunk.py")
  set_tests_properties(
    validate_rendering_migration_refresh_chunk_test
    PROPERTIES LABELS "desktop;color;quality")
  add_test(
    NAME diagnose_rendering_migration_pairs_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/color/test-diagnose-rendering-migration-pairs.py")
  set_tests_properties(
    diagnose_rendering_migration_pairs_test
    PROPERTIES LABELS "desktop;color;quality")
  add_test(
    NAME agent_delivery_reproducibility_test
    COMMAND
      "${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}"
      -B
      "${PROJECT_SOURCE_DIR}/scripts/perf/test-agent-delivery-reproducibility.py")
  set_tests_properties(
    agent_delivery_reproducibility_test
    PROPERTIES LABELS "desktop;ai-edit;agent;quality")
endif()

add_executable(
  render_invalidation_policy_test
  render_invalidation_policy_test.cpp)
target_link_libraries(render_invalidation_policy_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_invalidation_policy_test)

add_test(
  NAME render_invalidation_policy_test
  COMMAND render_invalidation_policy_test)

add_executable(render_executor_test render_executor_test.cpp)
target_link_libraries(render_executor_test PRIVATE photo_engine)
photo_editor_apply_warnings(render_executor_test)

add_test(NAME render_executor_test COMMAND render_executor_test)

add_executable(edit_recipe_v13_test edit_recipe_v13_test.cpp)
target_link_libraries(edit_recipe_v13_test PRIVATE photo_engine)
photo_editor_apply_warnings(edit_recipe_v13_test)
add_test(NAME edit_recipe_v13_test COMMAND edit_recipe_v13_test)

add_executable(color_pipeline_test color_pipeline_test.cpp)
target_link_libraries(color_pipeline_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_pipeline_test)

add_test(NAME color_pipeline_test COMMAND color_pipeline_test)

add_executable(color_highlight_reconstruction_test color_highlight_reconstruction_test.cpp)
target_link_libraries(color_highlight_reconstruction_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_highlight_reconstruction_test)
add_test(NAME color_highlight_reconstruction_test COMMAND color_highlight_reconstruction_test)
set_tests_properties(color_highlight_reconstruction_test PROPERTIES LABELS "color;raw;highlight")

add_executable(color_output_batch_test color_output_batch_test.cpp)
target_link_libraries(color_output_batch_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_output_batch_test)
add_test(NAME color_output_batch_test COMMAND color_output_batch_test)

add_executable(timeline_perspective_shader_layout_test timeline_perspective_shader_layout_test.cpp)
photo_editor_apply_warnings(timeline_perspective_shader_layout_test)
add_test(NAME timeline_perspective_shader_layout_test
  COMMAND timeline_perspective_shader_layout_test
    ${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/shaders/timeline_perspective.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/shaders/timeline_perspective.frag)
set_tests_properties(timeline_perspective_shader_layout_test PROPERTIES
  LABELS "desktop;timeline;shader;perspective")

add_executable(mask_raster_cache_test mask_raster_cache_test.cpp)
target_include_directories(mask_raster_cache_test PRIVATE ../engine/src/pipeline ../apps/desktop/src)
target_link_libraries(mask_raster_cache_test PRIVATE photo_engine)
photo_editor_apply_warnings(mask_raster_cache_test)
add_test(NAME mask_raster_cache_test COMMAND mask_raster_cache_test)

add_executable(neutral_layer_elision_test neutral_layer_elision_test.cpp)
target_include_directories(neutral_layer_elision_test PRIVATE ../engine/src/pipeline)
target_link_libraries(neutral_layer_elision_test PRIVATE photo_engine)
photo_editor_apply_warnings(neutral_layer_elision_test)
add_test(NAME neutral_layer_elision_test COMMAND neutral_layer_elision_test)

add_executable(photo_layer_direct_color_test photo_layer_direct_color_test.cpp)
target_include_directories(photo_layer_direct_color_test PRIVATE ../engine/src/pipeline)
target_link_libraries(photo_layer_direct_color_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_layer_direct_color_test)
add_test(NAME photo_layer_direct_color_test COMMAND photo_layer_direct_color_test)

add_executable(compiled_local_adjustment_test compiled_local_adjustment_test.cpp)
target_include_directories(compiled_local_adjustment_test PRIVATE ../engine/src/pipeline)
target_link_libraries(compiled_local_adjustment_test PRIVATE photo_engine)
photo_editor_apply_warnings(compiled_local_adjustment_test)
add_test(NAME compiled_local_adjustment_test COMMAND compiled_local_adjustment_test)
add_executable(luminance_projection_test luminance_projection_test.cpp)
target_include_directories(luminance_projection_test PRIVATE ../engine/src/pipeline)
target_link_libraries(luminance_projection_test PRIVATE photo_engine)
photo_editor_apply_warnings(luminance_projection_test)
add_test(NAME luminance_projection_test COMMAND luminance_projection_test)

add_executable(mask_coverage_cache_test mask_coverage_cache_test.cpp)
target_include_directories(mask_coverage_cache_test PRIVATE ../engine/src/pipeline)
target_link_libraries(mask_coverage_cache_test PRIVATE photo_engine)
photo_editor_apply_warnings(mask_coverage_cache_test)
add_test(NAME mask_coverage_cache_test COMMAND mask_coverage_cache_test)

add_executable(color_pipeline_property_test color_pipeline_property_test.cpp)
target_link_libraries(color_pipeline_property_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_pipeline_property_test)

add_test(NAME color_pipeline_property_test COMMAND color_pipeline_property_test)

add_executable(
  color_adjustment_scheduler_test
  color_adjustment_scheduler_test.cpp)
target_include_directories(
  color_adjustment_scheduler_test
  PRIVATE
    ../engine/src/pipeline)
target_link_libraries(
  color_adjustment_scheduler_test
  PRIVATE
    photo_engine)
photo_editor_apply_warnings(color_adjustment_scheduler_test)
add_test(
  NAME color_adjustment_scheduler_test
  COMMAND color_adjustment_scheduler_test)

add_executable(openexr_io_test openexr_io_test.cpp)
target_link_libraries(openexr_io_test PRIVATE photo_engine)
photo_editor_apply_warnings(openexr_io_test)
add_test(NAME openexr_io_test COMMAND openexr_io_test)
set_tests_properties(openexr_io_test PROPERTIES SKIP_RETURN_CODE 77)

set(
  PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/ocio-project-v1")
file(
  SHA256
  "${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}/config.ocio"
  PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256)
file(
  SHA256
  "${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}/luts/scale.clf"
  PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256)
file(
  SHA256
  "${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}/luts/offset.ctf"
  PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256)
add_executable(ocio_project_config_test ocio_project_config_test.cpp)
target_link_libraries(ocio_project_config_test PRIVATE photo_engine)
target_compile_definitions(
  ocio_project_config_test
  PRIVATE
    PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR="${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}"
    PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256="${PHOTO_EDITOR_OCIO_PROJECT_CONFIG_SHA256}"
    PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256="${PHOTO_EDITOR_OCIO_PROJECT_CLF_SHA256}"
    PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256="${PHOTO_EDITOR_OCIO_PROJECT_CTF_SHA256}")
photo_editor_apply_warnings(ocio_project_config_test)
add_test(NAME ocio_project_config_test COMMAND ocio_project_config_test)
set_tests_properties(ocio_project_config_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(
  ocio_project_config_benchmark_driver
  ocio_project_config_benchmark_driver.cpp)
target_link_libraries(ocio_project_config_benchmark_driver PRIVATE photo_engine)
target_compile_definitions(
  ocio_project_config_benchmark_driver
  PRIVATE
    PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR="${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}")
photo_editor_apply_warnings(ocio_project_config_benchmark_driver)

add_executable(hdr_avif_io_test hdr_avif_io_test.cpp)
target_link_libraries(hdr_avif_io_test PRIVATE photo_engine)
photo_editor_apply_warnings(hdr_avif_io_test)
add_test(NAME hdr_avif_io_test COMMAND hdr_avif_io_test)
set_tests_properties(hdr_avif_io_test PROPERTIES SKIP_RETURN_CODE 77)

file(SHA256
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/aces2-ocio-reference-v1.csv"
  PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256)
add_executable(aces2_ocio_reference_test aces2_ocio_reference_test.cpp)
target_link_libraries(aces2_ocio_reference_test PRIVATE photo_engine)
target_compile_definitions(
  aces2_ocio_reference_test
  PRIVATE
    PHOTO_EDITOR_ACES2_OCIO_REFERENCE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/aces2-ocio-reference-v1.csv"
    PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256="${PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256}")
photo_editor_apply_warnings(aces2_ocio_reference_test)
add_test(NAME aces2_ocio_reference_test COMMAND aces2_ocio_reference_test)
set_tests_properties(aces2_ocio_reference_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(
  metal_aces2_output_lut_contract_test
  metal_aces2_output_lut_contract_test.cpp)
target_compile_definitions(
  metal_aces2_output_lut_contract_test
  PRIVATE
    PHOTO_EDITOR_METAL_PIPELINE_BACKEND_PATH="${CMAKE_SOURCE_DIR}/engine/src/pipeline/metal/metal_pipeline_backend.mm"
    PHOTO_EDITOR_METAL_PIPELINE_BACKEND_HEADER_PATH="${CMAKE_SOURCE_DIR}/engine/include/photo_editor/pipeline/metal/metal_pipeline_backend.h"
    PHOTO_EDITOR_METAL_COLOR_ADJUSTMENTS_PATH="${CMAKE_SOURCE_DIR}/engine/src/pipeline/metal/color_adjustments.metal"
    PHOTO_EDITOR_NATIVE_OUTPUT_PUBLICATION_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/native_output_surface_publication.h"
    PHOTO_EDITOR_CANONICAL_NATIVE_PREVIEW_SUFFIX_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/canonical_native_preview_suffix.h"
    PHOTO_EDITOR_CANONICAL_NATIVE_PREVIEW_EXECUTION_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/canonical_native_preview_execution.h"
    PHOTO_EDITOR_CANONICAL_NATIVE_WORKING_FUSION_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/canonical_native_working_fusion.h"
    PHOTO_EDITOR_RENDER_EXECUTOR_PATH="${CMAKE_SOURCE_DIR}/engine/src/pipeline/render_executor.cpp"
    PHOTO_EDITOR_RETAINED_PREVIEW_SURFACE_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/retained_preview_surface.h"
    PHOTO_EDITOR_PHOTO_RENDER_UTILS_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/photo_render_utils.cpp"
    PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/editor_preview_controller.cpp"
    PHOTO_EDITOR_PREVIEW_TEXTURE_METAL_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/editor_preview_texture_metal.mm"
    PHOTO_EDITOR_WORKING_IMAGE_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/working_image.h")
photo_editor_apply_warnings(metal_aces2_output_lut_contract_test)
add_test(
  NAME metal_aces2_output_lut_contract_test
  COMMAND metal_aces2_output_lut_contract_test)

add_executable(
  windows_monitor_profile_source_contract_test
  windows_monitor_profile_source_contract_test.cpp)
target_compile_definitions(
  windows_monitor_profile_source_contract_test
  PRIVATE
    PHOTO_EDITOR_DISPLAY_PROFILE_DISCOVERY_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/display_profile_discovery.h"
    PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/editor_preview_controller.cpp")
photo_editor_apply_warnings(windows_monitor_profile_source_contract_test)
add_test(
  NAME windows_monitor_profile_source_contract_test
  COMMAND windows_monitor_profile_source_contract_test)

if(PHOTO_EDITOR_BUILD_DESKTOP AND APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  add_executable(
    canonical_native_working_fusion_test
    canonical_native_working_fusion_test.cpp)
  target_include_directories(
    canonical_native_working_fusion_test
    PRIVATE ${CMAKE_SOURCE_DIR}/apps/desktop/src)
  target_link_libraries(
    canonical_native_working_fusion_test
    PRIVATE photo_engine Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(canonical_native_working_fusion_test)
  add_test(
    NAME canonical_native_working_fusion_test
    COMMAND canonical_native_working_fusion_test)

  add_executable(
    metal_aces2_output_lut_parity_test
    metal_aces2_output_lut_parity_test.cpp
    native_preview_readback.mm
    ../apps/desktop/src/editor_preview_texture.cpp
    ../apps/desktop/src/editor_preview_texture_metal.mm)
  target_include_directories(
    metal_aces2_output_lut_parity_test
    PRIVATE ${CMAKE_SOURCE_DIR}/apps/desktop/src)
  target_compile_definitions(
    metal_aces2_output_lut_parity_test
    PRIVATE PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND=1)
  target_link_libraries(
    metal_aces2_output_lut_parity_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(metal_aces2_output_lut_parity_test)
  add_test(
    NAME metal_aces2_output_lut_parity_test
    COMMAND metal_aces2_output_lut_parity_test)
  set_tests_properties(
    metal_aces2_output_lut_parity_test
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120
      ENVIRONMENT "MTL_DEBUG_LAYER=1;QT_QPA_PLATFORM=offscreen")
endif()

if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  add_executable(
    metal_ocio_project_config_parity_test
    metal_ocio_project_config_parity_test.cpp)
  target_link_libraries(
    metal_ocio_project_config_parity_test
    PRIVATE photo_engine)
  target_compile_definitions(
    metal_ocio_project_config_parity_test
    PRIVATE
      PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR="${PHOTO_EDITOR_OCIO_PROJECT_FIXTURE_DIR}")
  photo_editor_apply_warnings(metal_ocio_project_config_parity_test)
  add_test(
    NAME metal_ocio_project_config_parity_test
    COMMAND metal_ocio_project_config_parity_test)
  set_tests_properties(
    metal_ocio_project_config_parity_test
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 ENVIRONMENT "MTL_DEBUG_LAYER=1")

  add_executable(
    metal_icc_transform_lut_parity_test
    metal_icc_transform_lut_parity_test.cpp)
  target_link_libraries(
    metal_icc_transform_lut_parity_test
    PRIVATE photo_engine)
  photo_editor_apply_warnings(metal_icc_transform_lut_parity_test)
  add_test(
    NAME metal_icc_transform_lut_parity_test
    COMMAND metal_icc_transform_lut_parity_test)
  set_tests_properties(
    metal_icc_transform_lut_parity_test
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120 ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()

if(UNIX)
  add_executable(output_lut_cache_churn_benchmark output_lut_cache_churn_benchmark.cpp)
  target_link_libraries(output_lut_cache_churn_benchmark PRIVATE photo_engine)
  photo_editor_apply_warnings(output_lut_cache_churn_benchmark)
  add_test(NAME output_lut_cache_churn_contract COMMAND output_lut_cache_churn_benchmark)
  set_tests_properties(output_lut_cache_churn_contract PROPERTIES
    RUN_SERIAL TRUE TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "engine;cache;performance")
endif()

add_executable(
  output_rendering_benchmark_driver
  output_rendering_benchmark_driver.cpp)
target_link_libraries(output_rendering_benchmark_driver PRIVATE photo_engine)
photo_editor_apply_warnings(output_rendering_benchmark_driver)

add_executable(color_profile_asset_test color_profile_asset_test.cpp)
target_link_libraries(color_profile_asset_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_profile_asset_test)
add_test(NAME color_profile_asset_test COMMAND color_profile_asset_test)

add_executable(icc_transform_lut_test icc_transform_lut_test.cpp)
target_link_libraries(icc_transform_lut_test PRIVATE photo_engine)
photo_editor_apply_warnings(icc_transform_lut_test)
add_test(NAME icc_transform_lut_test COMMAND icc_transform_lut_test)
set_tests_properties(icc_transform_lut_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(icc_transform_concurrency_test icc_transform_concurrency_test.cpp)
target_link_libraries(icc_transform_concurrency_test PRIVATE photo_engine)
photo_editor_apply_warnings(icc_transform_concurrency_test)
add_test(NAME icc_transform_concurrency_test COMMAND icc_transform_concurrency_test)
set_tests_properties(icc_transform_concurrency_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(preview_device_icc_identity_test preview_device_icc_identity_test.cpp)
target_include_directories(preview_device_icc_identity_test PRIVATE ${CMAKE_SOURCE_DIR}/apps/desktop/src)
target_link_libraries(preview_device_icc_identity_test PRIVATE photo_engine)
photo_editor_apply_warnings(preview_device_icc_identity_test)
add_test(NAME preview_device_icc_identity_test COMMAND preview_device_icc_identity_test)
set_tests_properties(preview_device_icc_identity_test PROPERTIES SKIP_RETURN_CODE 77)

add_executable(
  icc_transform_lut_benchmark_driver
  icc_transform_lut_benchmark_driver.cpp)
target_link_libraries(icc_transform_lut_benchmark_driver PRIVATE photo_engine)
photo_editor_apply_warnings(icc_transform_lut_benchmark_driver)

add_executable(dcp_profile_test dcp_profile_test.cpp)
target_link_libraries(dcp_profile_test PRIVATE photo_engine)
photo_editor_apply_warnings(dcp_profile_test)
add_test(NAME dcp_profile_test COMMAND dcp_profile_test)

set(PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR
  "${CMAKE_CURRENT_SOURCE_DIR}/generate_dng_conformance_reference.py")
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR}"
  PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR_SHA256)

add_executable(
  dcp_conformance_camera_transform_test
  dcp_conformance_camera_transform_test.cpp)
target_link_libraries(dcp_conformance_camera_transform_test PRIVATE photo_engine)
set(PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-camera-transform-v1.csv")
set(PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-camera-transform-v1.json")
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV}"
  PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV_SHA256)
file(READ
  "${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_CONTENT)
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_SHA256)
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_CONTENT MATCHES
   "\"schema\": \"dng-conformance-camera-transform:v1\"")
  message(FATAL_ERROR "DNG camera-transform fixture has the wrong schema")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_CONTENT MATCHES
   "\"fixture_csv_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV_SHA256}\"")
  message(FATAL_ERROR
    "DNG camera-transform fixture hash does not match its metadata")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_CONTENT MATCHES
   "\"generator_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR_SHA256}\"")
  message(FATAL_ERROR
    "DNG camera-transform fixture was not generated by the checked-in oracle")
endif()
target_compile_definitions(
  dcp_conformance_camera_transform_test
  PRIVATE
    PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV="${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV}")
photo_editor_apply_warnings(dcp_conformance_camera_transform_test)
add_test(
  NAME dcp_conformance_camera_transform_test
  COMMAND dcp_conformance_camera_transform_test)

add_executable(
  dcp_conformance_profile_stages_test
  dcp_conformance_profile_stages_test.cpp)
target_link_libraries(dcp_conformance_profile_stages_test PRIVATE photo_engine)
set(PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-profile-stages-v1.csv")
set(PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-profile-stages-v1.json")
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV}"
  PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV_SHA256)
file(READ
  "${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_CONTENT)
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_SHA256)
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_CONTENT MATCHES
   "\"schema\": \"dng-conformance-profile-stages:v1\"")
  message(FATAL_ERROR "DNG profile-stage fixture has the wrong schema")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_CONTENT MATCHES
   "\"fixture_csv_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV_SHA256}\"")
  message(FATAL_ERROR
    "DNG profile-stage fixture hash does not match its metadata")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_CONTENT MATCHES
   "\"generator_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR_SHA256}\"")
  message(FATAL_ERROR
    "DNG profile-stage fixture was not generated by the checked-in oracle")
endif()
target_compile_definitions(
  dcp_conformance_profile_stages_test
  PRIVATE
    PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV="${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV}")
photo_editor_apply_warnings(dcp_conformance_profile_stages_test)
add_test(
  NAME dcp_conformance_profile_stages_test
  COMMAND dcp_conformance_profile_stages_test)

set(PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-profile-gain-table-map-v1.csv")
set(PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-profile-gain-table-map-v1.json")
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV}"
  PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV_SHA256)
file(READ
  "${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_CONTENT)
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_SHA256)
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_CONTENT MATCHES
   "\"schema\": \"dng-conformance-profile-gain-table-map:v1\"")
  message(FATAL_ERROR "DNG gain-table fixture has the wrong schema")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_CONTENT MATCHES
   "\"fixture_csv_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV_SHA256}\"")
  message(FATAL_ERROR
    "DNG gain-table fixture hash does not match its metadata")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_CONTENT MATCHES
   "\"generator_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR_SHA256}\"")
  message(FATAL_ERROR
    "DNG gain-table fixture was not generated by the checked-in oracle")
endif()
add_executable(
  dcp_conformance_gain_table_map_test
  dcp_conformance_gain_table_map_test.cpp)
target_link_libraries(dcp_conformance_gain_table_map_test PRIVATE photo_engine)
target_compile_definitions(
  dcp_conformance_gain_table_map_test
  PRIVATE
    PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV="${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV}")
photo_editor_apply_warnings(dcp_conformance_gain_table_map_test)
add_test(
  NAME dcp_conformance_gain_table_map_test
  COMMAND dcp_conformance_gain_table_map_test)

set(PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-rgb-tables-v1.csv")
set(PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/dng-conformance-rgb-tables-v1.json")
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV}"
  PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV_SHA256)
file(READ
  "${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_CONTENT)
file(SHA256
  "${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA}"
  PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_SHA256)
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_CONTENT MATCHES
   "\"schema\": \"dng-conformance-rgb-tables:v1\"")
  message(FATAL_ERROR "DNG RGBTables fixture has the wrong schema")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_CONTENT MATCHES
   "\"fixture_csv_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV_SHA256}\"")
  message(FATAL_ERROR
    "DNG RGBTables fixture hash does not match its metadata")
endif()
if(NOT PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_CONTENT MATCHES
   "\"generator_sha256\": \"${PHOTO_EDITOR_DNG_CONFORMANCE_GENERATOR_SHA256}\"")
  message(FATAL_ERROR
    "DNG RGBTables fixture was not generated by the checked-in oracle")
endif()
add_executable(
  dcp_conformance_rgb_tables_test
  dcp_conformance_rgb_tables_test.cpp)
target_link_libraries(dcp_conformance_rgb_tables_test PRIVATE photo_engine)
target_compile_definitions(
  dcp_conformance_rgb_tables_test
  PRIVATE
    PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV="${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV}")
photo_editor_apply_warnings(dcp_conformance_rgb_tables_test)
add_test(
  NAME dcp_conformance_rgb_tables_test
  COMMAND dcp_conformance_rgb_tables_test)

add_executable(dcp_profile_asset_test dcp_profile_asset_test.cpp)
target_link_libraries(dcp_profile_asset_test PRIVATE photo_engine)
photo_editor_apply_warnings(dcp_profile_asset_test)
add_test(NAME dcp_profile_asset_test COMMAND dcp_profile_asset_test)

add_executable(color_transform_reference_test color_transform_reference_test.cpp)
target_link_libraries(color_transform_reference_test PRIVATE photo_engine)
file(SHA256
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/transform-reference-v1.csv"
  PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256)
target_compile_definitions(
  color_transform_reference_test
  PRIVATE
    PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/transform-reference-v1.csv"
    PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256="${PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256}")
photo_editor_apply_warnings(color_transform_reference_test)

add_test(NAME color_transform_reference_test COMMAND color_transform_reference_test)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LCMS2 QUIET IMPORTED_TARGET lcms2)
endif()
if(TARGET PkgConfig::LCMS2)
  file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/cmm-comparison-patches-v1.csv"
    PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256)
  file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/color-stage-baseline-macos-m4pro-v1.json"
    PHOTO_EDITOR_COLOR_STAGE_BASELINE_SHA256)
  file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/icc/manifest-v1.json"
    PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256)
  file(SHA256
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/aces2-ocio-reference-v1.json"
    PHOTO_EDITOR_ACES2_OCIO_PROVENANCE_SHA256)
  string(SHA256 PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256
    "Linear Display P3|D65:0.3127,0.3290|R:0.6800,0.3200|G:0.2650,0.6900|B:0.1500,0.0600|linear-gamma:1.0")
  string(SHA256 PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256
    "Little CMS cmsCreate_sRGBProfile|IEC 61966-2-1 primaries and transfer curve")
  file(READ
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/color-quality-manifest-v1.json"
    PHOTO_EDITOR_COLOR_QUALITY_MANIFEST)
  foreach(required_hash IN ITEMS
      "${PHOTO_EDITOR_COLOR_REFERENCE_CORPUS_SHA256}"
      "${PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256}"
      "${PHOTO_EDITOR_COLOR_STAGE_BASELINE_SHA256}"
      "${PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256}"
      "${PHOTO_EDITOR_ACES2_OCIO_REFERENCE_SHA256}"
      "${PHOTO_EDITOR_ACES2_OCIO_PROVENANCE_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_CSV_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_CAMERA_TRANSFORM_METADATA_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_CSV_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_PROFILE_STAGES_METADATA_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_CSV_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_GAIN_TABLE_METADATA_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_CSV_SHA256}"
      "${PHOTO_EDITOR_DNG_CONFORMANCE_RGB_TABLES_METADATA_SHA256}"
      "${PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256}"
      "${PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256}")
    string(FIND "${PHOTO_EDITOR_COLOR_QUALITY_MANIFEST}" "${required_hash}" hash_position)
    if(hash_position EQUAL -1)
      message(FATAL_ERROR
        "Color quality manifest is stale; missing SHA-256 ${required_hash}")
    endif()
  endforeach()
  add_executable(color_cmm_comparison_test color_cmm_comparison_test.cpp)
  target_link_libraries(color_cmm_comparison_test PRIVATE photo_engine PkgConfig::LCMS2)
  target_compile_definitions(
    color_cmm_comparison_test
    PRIVATE
      PHOTO_EDITOR_CMM_COMPARISON_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/cmm-comparison-patches-v1.csv"
      PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256="${PHOTO_EDITOR_CMM_COMPARISON_CORPUS_SHA256}"
      PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256="${PHOTO_EDITOR_CMM_SOURCE_PROFILE_SHA256}"
      PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256="${PHOTO_EDITOR_CMM_DESTINATION_PROFILE_SHA256}"
      PHOTO_EDITOR_ICC_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/color/icc"
      PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256="${PHOTO_EDITOR_ICC_CORPUS_MANIFEST_SHA256}")
  photo_editor_apply_warnings(color_cmm_comparison_test)

  add_test(NAME color_cmm_comparison_test COMMAND color_cmm_comparison_test)
  set_tests_properties(color_cmm_comparison_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(cube_lut_test cube_lut_test.cpp)
target_link_libraries(cube_lut_test PRIVATE photo_engine)
photo_editor_apply_warnings(cube_lut_test)

add_test(NAME cube_lut_test COMMAND cube_lut_test)

add_executable(video_color_transform_test video_color_transform_test.cpp)
target_link_libraries(video_color_transform_test PRIVATE photo_engine)
photo_editor_apply_warnings(video_color_transform_test)

add_test(NAME video_color_transform_test COMMAND video_color_transform_test)

add_executable(
  synthetic_raw_pipeline_invariant_test
  synthetic_raw_pipeline_invariant_test.cpp)
target_link_libraries(synthetic_raw_pipeline_invariant_test PRIVATE photo_engine)
photo_editor_apply_warnings(synthetic_raw_pipeline_invariant_test)

add_test(
  NAME synthetic_raw_pipeline_invariant_test
  COMMAND synthetic_raw_pipeline_invariant_test)

add_executable(lens_profile_test lens_profile_test.cpp)
target_link_libraries(lens_profile_test PRIVATE photo_engine)
photo_editor_apply_warnings(lens_profile_test)

add_test(NAME lens_profile_test COMMAND lens_profile_test)

add_executable(color_chart_validation_test color_chart_validation_test.cpp)
target_link_libraries(color_chart_validation_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_chart_validation_test)

add_test(NAME color_chart_validation_test COMMAND color_chart_validation_test)

add_executable(color_tool_invariant_test color_tool_invariant_test.cpp)
target_link_libraries(color_tool_invariant_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_tool_invariant_test)

add_test(NAME color_tool_invariant_test COMMAND color_tool_invariant_test)

add_executable(color_selection_stability_test color_selection_stability_test.cpp)
target_link_libraries(color_selection_stability_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_selection_stability_test)
add_test(NAME color_selection_stability_test COMMAND color_selection_stability_test)
if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  add_test(NAME metal_color_selection_stability_test COMMAND color_selection_stability_test --metal)
  set_tests_properties(metal_color_selection_stability_test PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 120 ENVIRONMENT "MTL_DEBUG_LAYER=1")
endif()

# Creative-control and profile contracts include independent fixed references.
foreach(color_contract color_grading_contract color_harmony_continuity color_profile_edit_matrix color_creative_contract color_mixer_noise_gain)
  add_executable(${color_contract}_test ${color_contract}_test.cpp)
  target_link_libraries(${color_contract}_test PRIVATE photo_engine)
  target_include_directories(${color_contract}_test PRIVATE "${PROJECT_SOURCE_DIR}/engine/src/pipeline")
  photo_editor_apply_warnings(${color_contract}_test)
  add_test(NAME ${color_contract}_test COMMAND ${color_contract}_test)
endforeach()
if(PHOTO_EDITOR_ENABLE_LCMS_COLOR_MANAGEMENT AND TARGET PkgConfig::LCMS2)
  target_link_libraries(color_profile_edit_matrix_test PRIVATE PkgConfig::LCMS2)
  target_compile_definitions(color_profile_edit_matrix_test PRIVATE PHOTO_EDITOR_COLOR_MATRIX_TEST_HAS_LCMS2=1)
endif()
if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
  add_test(NAME metal_color_mixer_noise_gain_test COMMAND color_mixer_noise_gain_test --metal)
  set_tests_properties(metal_color_mixer_noise_gain_test PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 240 ENVIRONMENT "MTL_DEBUG_LAYER=1")
  add_test(NAME metal_color_creative_contract_test COMMAND color_creative_contract_test --metal)
  set_tests_properties(metal_color_creative_contract_test PROPERTIES
    SKIP_RETURN_CODE 77 TIMEOUT 240 ENVIRONMENT "MTL_DEBUG_LAYER=1")
  enable_language(OBJCXX)
  add_executable(metal_shader_source_test metal_shader_source_test.mm)
  target_link_libraries(metal_shader_source_test PRIVATE photo_engine "-framework Foundation" "-framework Metal")
  target_include_directories(metal_shader_source_test PRIVATE "${PROJECT_BINARY_DIR}/engine/generated")
  target_compile_definitions(metal_shader_source_test PRIVATE
    PHOTO_EDITOR_METAL_CANONICAL_SOURCE_PATH="${PROJECT_SOURCE_DIR}/engine/src/pipeline/metal/color_adjustments.metal"
    PHOTO_EDITOR_METAL_GRADING_HELPER_PATH="${PROJECT_SOURCE_DIR}/engine/src/pipeline/color_look_grading_math.h"
    PHOTO_EDITOR_METAL_HARMONY_HELPER_PATH="${PROJECT_SOURCE_DIR}/engine/src/pipeline/color_look_harmony_math.h")
  set_source_files_properties(metal_shader_source_test.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  photo_editor_apply_warnings(metal_shader_source_test)
  add_test(NAME metal_shader_source_test COMMAND metal_shader_source_test)
  add_test(NAME metal_shader_compile_test COMMAND metal_shader_source_test --compile)
  set_tests_properties(metal_shader_compile_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()

add_executable(color_curve_headroom_test color_curve_headroom_test.cpp)
target_link_libraries(color_curve_headroom_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_curve_headroom_test)
add_test(NAME color_curve_headroom_test COMMAND color_curve_headroom_test)

add_executable(color_science_sweep_test color_science_sweep_test.cpp)
target_link_libraries(color_science_sweep_test PRIVATE photo_engine)
photo_editor_apply_warnings(color_science_sweep_test)

add_test(NAME color_science_sweep_test COMMAND color_science_sweep_test)

add_executable(catalog_contract_smoke_test catalog_contract_smoke_test.cpp)
target_link_libraries(catalog_contract_smoke_test PRIVATE photo_engine)
photo_editor_apply_warnings(catalog_contract_smoke_test)

add_test(NAME catalog_contract_smoke_test COMMAND catalog_contract_smoke_test)

if(PHOTO_EDITOR_BUILD_DESKTOP)
  find_package(Qt6 REQUIRED COMPONENTS Concurrent Core Gui Multimedia Network Qml Quick QuickControls2 ShaderTools Widgets)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  if(APPLE)
    add_executable(import_preview_image_provider_test import_preview_image_provider_test.cpp
      ../apps/desktop/src/import_preview_image_provider.cpp ../apps/desktop/src/import_preview_image_provider_macos.mm)
    target_include_directories(import_preview_image_provider_test PRIVATE ../apps/desktop/src)
    target_compile_definitions(import_preview_image_provider_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(import_preview_image_provider_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
      "-framework Foundation" "-framework ImageIO" "-framework CoreGraphics")
    set_source_files_properties(../apps/desktop/src/import_preview_image_provider_macos.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    qt_add_shaders(import_preview_image_provider_test "import_preview_test_shaders" BATCHABLE
      PREFIX "/photo-editor/shaders" BASE "${PROJECT_SOURCE_DIR}/apps/desktop/shaders"
      FILES "${PROJECT_SOURCE_DIR}/apps/desktop/shaders/optical_light.vert" "${PROJECT_SOURCE_DIR}/apps/desktop/shaders/optical_light.frag")
    photo_editor_apply_warnings(import_preview_image_provider_test)
    add_test(NAME import_preview_image_provider_test COMMAND import_preview_image_provider_test)
    set_tests_properties(import_preview_image_provider_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 30 LABELS "desktop;import;raw")
  endif()
  if(UNIX)
    add_executable(depth_motion_lifecycle_test depth_motion_lifecycle_test.cpp
      ../apps/desktop/src/depth_motion_controller.cpp
      ../apps/desktop/src/depth_motion_controller.h)
    set_target_properties(depth_motion_lifecycle_test PROPERTIES AUTOMOC ON)
    target_include_directories(depth_motion_lifecycle_test PRIVATE ../apps/desktop/src)
    target_link_libraries(depth_motion_lifecycle_test PRIVATE Qt6::Core Qt6::Qml)
    photo_editor_apply_warnings(depth_motion_lifecycle_test)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/depth-motion-worker.py"
      PROPERTIES QT_RESOURCE_ALIAS "depth-motion-worker.py")
    qt_add_resources(depth_motion_lifecycle_test "depth_motion_lifecycle_worker"
      PREFIX "/photo-editor/runtime"
      FILES "${PROJECT_SOURCE_DIR}/scripts/depth-motion-worker.py")
    add_test(NAME depth_motion_lifecycle_test COMMAND depth_motion_lifecycle_test)
    set_tests_properties(depth_motion_lifecycle_test PROPERTIES
      RUN_SERIAL TRUE TIMEOUT 45 LABELS "desktop;depth;lifecycle;performance")
  endif()
  add_executable(depth_motion_ui_test depth_motion_ui_test.cpp
    ../apps/desktop/src/depth_motion_controller.cpp ../apps/desktop/src/depth_motion_controller.h
    ../apps/desktop/src/depth_point_cloud_viewport.cpp ../apps/desktop/src/depth_point_cloud_viewport.h
    ../apps/desktop/src/scene_video_recorder.cpp ../apps/desktop/src/scene_video_recorder.h ../apps/desktop/src/scene_library_controller.cpp ../apps/desktop/src/scene_library_controller.h)
  set_target_properties(depth_motion_ui_test PROPERTIES AUTOMOC ON)
  target_include_directories(depth_motion_ui_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(depth_motion_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(depth_motion_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Multimedia Qt6::Test Qt6::Concurrent)
  photo_editor_apply_warnings(depth_motion_ui_test)
  set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/depth-motion-worker.py"
    PROPERTIES QT_RESOURCE_ALIAS "depth-motion-worker.py")
  qt_add_resources(depth_motion_ui_test "depth_motion_worker_test" PREFIX "/photo-editor/runtime"
    FILES "${PROJECT_SOURCE_DIR}/scripts/depth-motion-worker.py")
  qt_add_shaders(depth_motion_ui_test "depth_motion_ui_shaders" BATCHABLE
    PREFIX "/photo-editor/shaders" BASE "${PROJECT_SOURCE_DIR}/apps/desktop/shaders"
    FILES "${PROJECT_SOURCE_DIR}/apps/desktop/shaders/optical_light.vert"
      "${PROJECT_SOURCE_DIR}/apps/desktop/shaders/optical_light.frag")
  add_test(NAME depth_motion_ui_test COMMAND depth_motion_ui_test)
  set_tests_properties(depth_motion_ui_test PROPERTIES TIMEOUT 60 LABELS "desktop;depth;ui")
  add_executable(depth_point_cloud_render_test depth_point_cloud_render_test.cpp
    ../apps/desktop/src/depth_point_cloud_viewport.cpp ../apps/desktop/src/depth_point_cloud_viewport.h)
  set_target_properties(depth_point_cloud_render_test PROPERTIES AUTOMOC ON)
  target_include_directories(depth_point_cloud_render_test PRIVATE ../apps/desktop/src)
  target_link_libraries(depth_point_cloud_render_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(depth_point_cloud_render_test)
  add_test(NAME depth_point_cloud_render_test COMMAND depth_point_cloud_render_test)
  set_tests_properties(depth_point_cloud_render_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;depth;geometry")
  add_executable(scene_library_controller_test scene_library_controller_test.cpp
    ../apps/desktop/src/scene_library_controller.cpp ../apps/desktop/src/scene_library_controller.h)
  set_target_properties(scene_library_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(scene_library_controller_test PRIVATE ../apps/desktop/src)
  target_link_libraries(scene_library_controller_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick)
  photo_editor_apply_warnings(scene_library_controller_test)
  add_test(NAME scene_library_controller_test COMMAND scene_library_controller_test)
  set_tests_properties(scene_library_controller_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;scene;persistence")
  find_package(ZLIB REQUIRED)
  add_executable(timeline_multicam_projection_index_test timeline_multicam_projection_index_test.cpp)
  target_include_directories(timeline_multicam_projection_index_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_multicam_projection_index_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_multicam_projection_index_test)
  add_test(NAME timeline_multicam_projection_index_test COMMAND timeline_multicam_projection_index_test)
  set_tests_properties(timeline_multicam_projection_index_test PROPERTIES
    TIMEOUT 30 LABELS "desktop;timeline;performance")

  add_executable(timeline_asset_record_lookup_test
    timeline_asset_record_lookup_test.cpp
    ../apps/desktop/src/timeline_asset_record_lookup.cpp)
  target_include_directories(timeline_asset_record_lookup_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_asset_record_lookup_test
    PRIVATE Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(timeline_asset_record_lookup_test)
  add_test(NAME timeline_asset_record_lookup_test
    COMMAND timeline_asset_record_lookup_test)
  set_tests_properties(timeline_asset_record_lookup_test PROPERTIES
    TIMEOUT 15 LABELS "desktop;timeline;responsiveness")

  add_executable(timeline_edit_journal_codec_test
    timeline_edit_journal_codec_test.cpp
    ../apps/desktop/src/timeline_edit_journal_codec.cpp)
  target_include_directories(timeline_edit_journal_codec_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_edit_journal_codec_test
    PRIVATE Qt6::Core ZLIB::ZLIB)
  photo_editor_apply_warnings(timeline_edit_journal_codec_test)
  add_test(NAME timeline_edit_journal_codec_test
    COMMAND timeline_edit_journal_codec_test)
  set_tests_properties(timeline_edit_journal_codec_test PROPERTIES
    TIMEOUT 120 LABELS "desktop;timeline;journal;codec;recovery")
  find_package(ZLIB REQUIRED)

add_executable(timeline_scene_runtime_test timeline_scene_runtime_test.cpp ../apps/desktop/src/timeline_scene_runtime.cpp)
target_include_directories(timeline_scene_runtime_test PRIVATE ../apps/desktop/src)
target_link_libraries(timeline_scene_runtime_test PRIVATE Qt6::Core Qt6::Gui)
photo_editor_apply_warnings(timeline_scene_runtime_test)
add_test(NAME timeline_scene_runtime_test COMMAND timeline_scene_runtime_test)
set_tests_properties(timeline_scene_runtime_test PROPERTIES TIMEOUT 30 LABELS "desktop;timeline;scene")
add_executable(timeline_scene_runtime_colmap_test timeline_scene_runtime_colmap_test.cpp ../apps/desktop/src/timeline_scene_runtime.cpp)
target_include_directories(timeline_scene_runtime_colmap_test PRIVATE ../apps/desktop/src)
target_link_libraries(timeline_scene_runtime_colmap_test PRIVATE Qt6::Core Qt6::Gui)
photo_editor_apply_warnings(timeline_scene_runtime_colmap_test)
add_test(NAME timeline_scene_runtime_colmap_test COMMAND timeline_scene_runtime_colmap_test)
set_tests_properties(timeline_scene_runtime_colmap_test PROPERTIES TIMEOUT 600 SKIP_RETURN_CODE 77 LABELS "desktop;timeline;scene;native")

add_executable(
  timeline_motion_runtime_test
  timeline_motion_runtime_test.cpp
  ../apps/desktop/src/timeline_motion_runtime.h
  ../apps/desktop/src/timeline_motion_runtime_common.cpp
  ../apps/desktop/src/timeline_motion_runtime_stub.cpp)
target_include_directories(timeline_motion_runtime_test PRIVATE ../apps/desktop/src)
target_link_libraries(timeline_motion_runtime_test PRIVATE Qt6::Core Qt6::Gui)
photo_editor_apply_warnings(timeline_motion_runtime_test)
add_test(NAME timeline_motion_runtime_test COMMAND timeline_motion_runtime_test)

add_executable(
  timeline_sam2_fake_worker_success
  timeline_sam2_fake_worker.cpp)
target_compile_definitions(timeline_sam2_fake_worker_success PRIVATE TIMELINE_SAM2_FAKE_MODE=0)
target_link_libraries(timeline_sam2_fake_worker_success PRIVATE Qt6::Core Qt6::Gui)
add_executable(
  timeline_sam2_fake_worker_unavailable
  timeline_sam2_fake_worker.cpp)
target_compile_definitions(timeline_sam2_fake_worker_unavailable PRIVATE TIMELINE_SAM2_FAKE_MODE=1)
target_link_libraries(timeline_sam2_fake_worker_unavailable PRIVATE Qt6::Core Qt6::Gui)
add_executable(
  timeline_sam2_fake_worker_wait
  timeline_sam2_fake_worker.cpp)
target_compile_definitions(timeline_sam2_fake_worker_wait PRIVATE TIMELINE_SAM2_FAKE_MODE=2)
target_link_libraries(timeline_sam2_fake_worker_wait PRIVATE Qt6::Core Qt6::Gui)
add_executable(
  timeline_sam2_runtime_test
  timeline_sam2_runtime_test.cpp
  ../apps/desktop/src/timeline_sam2_runtime.h
  ../apps/desktop/src/timeline_sam2_runtime.cpp
  ../apps/desktop/src/timeline_motion_runtime.h
  ../apps/desktop/src/timeline_motion_runtime_common.cpp)
target_include_directories(timeline_sam2_runtime_test PRIVATE ../apps/desktop/src)
target_compile_definitions(
  timeline_sam2_runtime_test
  PRIVATE
    PHOTO_EDITOR_SAM2_FAKE_SUCCESS="$<TARGET_FILE:timeline_sam2_fake_worker_success>"
    PHOTO_EDITOR_SAM2_FAKE_UNAVAILABLE="$<TARGET_FILE:timeline_sam2_fake_worker_unavailable>"
    PHOTO_EDITOR_SAM2_FAKE_WAIT="$<TARGET_FILE:timeline_sam2_fake_worker_wait>")
target_link_libraries(timeline_sam2_runtime_test PRIVATE Qt6::Core Qt6::Gui)
add_dependencies(
  timeline_sam2_runtime_test
  timeline_sam2_fake_worker_success
  timeline_sam2_fake_worker_unavailable
  timeline_sam2_fake_worker_wait)
photo_editor_apply_warnings(timeline_sam2_runtime_test)
add_test(
  NAME timeline_sam2_runtime_test
  COMMAND timeline_sam2_runtime_test)
set_tests_properties(timeline_sam2_runtime_test PROPERTIES TIMEOUT 30 LABELS "desktop;timeline;sam2;protocol")

add_executable(
  timeline_foreground_matte_test
  timeline_foreground_matte_test.cpp
  ../apps/desktop/src/timeline_foreground_matte.h
  ../apps/desktop/src/timeline_foreground_matte.cpp)
target_include_directories(timeline_foreground_matte_test PRIVATE ../apps/desktop/src)
target_link_libraries(timeline_foreground_matte_test PRIVATE Qt6::Core Qt6::Gui)
photo_editor_apply_warnings(timeline_foreground_matte_test)
add_test(NAME timeline_foreground_matte_test COMMAND timeline_foreground_matte_test)



if(APPLE)
  add_executable(
    timeline_motion_runtime_macos_test
    timeline_motion_runtime_macos_test.mm
    ../apps/desktop/src/timeline_motion_runtime.h
    ../apps/desktop/src/timeline_motion_runtime_common.cpp
    ../apps/desktop/src/timeline_motion_runtime.mm)
  target_compile_definitions(
    timeline_motion_runtime_macos_test
    PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_include_directories(timeline_motion_runtime_macos_test PRIVATE ../apps/desktop/src)
  target_link_libraries(
    timeline_motion_runtime_macos_test
    PRIVATE Qt6::Core Qt6::Gui "-framework CoreGraphics" "-framework CoreVideo" "-framework Vision")
  set_source_files_properties(
    timeline_motion_runtime_macos_test.mm
    ../apps/desktop/src/timeline_motion_runtime.mm
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  photo_editor_apply_warnings(timeline_motion_runtime_macos_test)
  add_test(NAME timeline_motion_runtime_macos_test COMMAND timeline_motion_runtime_macos_test)
  set_tests_properties(
    timeline_motion_runtime_macos_test
    PROPERTIES LABELS "desktop;macos;vision;timeline" SKIP_RETURN_CODE 77)
endif()

  include(${PROJECT_SOURCE_DIR}/cmake/CreativeFonts.cmake)
  add_executable(creative_fonts_test creative_fonts_test.cpp)
  target_include_directories(creative_fonts_test PRIVATE ../apps/desktop/src)
  target_link_libraries(creative_fonts_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_add_creative_fonts(creative_fonts_test)
  photo_editor_apply_warnings(creative_fonts_test)
  add_test(NAME creative_fonts_test COMMAND creative_fonts_test)
  set_tests_properties(creative_fonts_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(photo_effect_looks_test photo_effect_looks_test.cpp)
  target_include_directories(photo_effect_looks_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_effect_looks_test PRIVATE photo_engine)
  photo_editor_apply_warnings(photo_effect_looks_test)
  add_test(NAME photo_effect_looks_test COMMAND photo_effect_looks_test)

  add_executable(photo_font_picker_ui_test photo_font_picker_ui_test.cpp)
  target_link_libraries(photo_font_picker_ui_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  target_compile_definitions(photo_font_picker_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(photo_font_picker_ui_test)
  add_test(NAME photo_font_picker_ui_test COMMAND photo_font_picker_ui_test)
  set_tests_properties(photo_font_picker_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(photo_text_content_editor_test photo_text_content_editor_test.cpp)
  set_target_properties(photo_text_content_editor_test PROPERTIES AUTOMOC ON)
  target_link_libraries(photo_text_content_editor_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  target_compile_definitions(photo_text_content_editor_test PRIVATE PHOTO_TEXT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(photo_text_content_editor_test)
  add_test(NAME photo_text_content_editor_test COMMAND photo_text_content_editor_test)
  set_tests_properties(photo_text_content_editor_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(creative_typography_styles_test creative_typography_styles_test.cpp)
  set_target_properties(creative_typography_styles_test PROPERTIES AUTOMOC ON)
  target_include_directories(creative_typography_styles_test PRIVATE ../apps/desktop/src)
  target_link_libraries(creative_typography_styles_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  target_compile_definitions(creative_typography_styles_test PRIVATE PHOTO_TEXT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  photo_editor_add_creative_fonts(creative_typography_styles_test)
  photo_editor_apply_warnings(creative_typography_styles_test)
  add_test(NAME creative_typography_styles_test COMMAND creative_typography_styles_test)
  set_tests_properties(creative_typography_styles_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(photo_effect_browser_test photo_effect_browser_test.cpp)
  target_link_libraries(photo_effect_browser_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  target_compile_definitions(photo_effect_browser_test PRIVATE PHOTO_EFFECT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(photo_effect_browser_test)
  add_test(NAME photo_effect_browser_test COMMAND photo_effect_browser_test)
  set_tests_properties(photo_effect_browser_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

  add_executable(photo_text_overlay_test photo_text_overlay_test.cpp ../apps/desktop/src/photo_text_document_style.h)
  set_target_properties(photo_text_overlay_test PROPERTIES AUTOMOC ON)
  target_include_directories(photo_text_overlay_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_overlay_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  target_compile_definitions(photo_text_overlay_test PRIVATE PHOTO_TEXT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(photo_text_overlay_test)
  add_test(NAME photo_text_overlay_test COMMAND photo_text_overlay_test)
  set_tests_properties(photo_text_overlay_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(photo_text_layout_test photo_text_layout_test.cpp)
  target_include_directories(photo_text_layout_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_layout_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_layout_test)
  add_test(NAME photo_text_layout_test COMMAND photo_text_layout_test)
  set_tests_properties(photo_text_layout_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(photo_text_style_test photo_text_style_test.cpp)
  target_include_directories(photo_text_style_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_style_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_style_test)
  add_test(NAME photo_text_style_test COMMAND photo_text_style_test)

  add_executable(photo_auto_horizon_test photo_auto_horizon_test.cpp)
  target_include_directories(photo_auto_horizon_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(photo_auto_horizon_test PRIVATE
    PHOTO_AUTO_HORIZON_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  target_link_libraries(photo_auto_horizon_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_auto_horizon_test)
  add_test(NAME photo_auto_horizon_test COMMAND photo_auto_horizon_test)

  add_executable(render_executor_icc_parallel_test render_executor_icc_parallel_test.cpp)
  target_link_libraries(render_executor_icc_parallel_test PRIVATE photo_engine)
  photo_editor_apply_warnings(render_executor_icc_parallel_test)
  add_test(NAME render_executor_icc_parallel_test COMMAND render_executor_icc_parallel_test)
  set_tests_properties(render_executor_icc_parallel_test PROPERTIES TIMEOUT 60)

  add_executable(photo_paint_commit_test photo_paint_commit_test.cpp)
  add_executable(photo_paint_responsiveness_test photo_paint_responsiveness_test.cpp)
  target_include_directories(photo_paint_responsiveness_test PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(photo_paint_responsiveness_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_paint_responsiveness_test)
  add_test(NAME photo_paint_responsiveness_test COMMAND photo_paint_responsiveness_test)
  set_tests_properties(photo_paint_responsiveness_test PROPERTIES TIMEOUT 60 LABELS "desktop;paint;brush;correctness")
  target_include_directories(photo_paint_commit_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_paint_commit_test PRIVATE photo_engine Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_paint_commit_test)
  add_test(NAME photo_paint_commit_test COMMAND photo_paint_commit_test)
  set_tests_properties(photo_paint_commit_test PROPERTIES TIMEOUT 60)

  add_executable(editor_preview_alpha_test editor_preview_alpha_test.cpp)
  target_include_directories(editor_preview_alpha_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_preview_alpha_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(editor_preview_alpha_test)
  add_test(NAME editor_preview_alpha_test COMMAND editor_preview_alpha_test)

  add_executable(photo_text_rasterizer_test photo_text_rasterizer_test.cpp)
  target_include_directories(photo_text_rasterizer_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_rasterizer_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_rasterizer_test)
  add_test(NAME photo_text_rasterizer_test COMMAND photo_text_rasterizer_test)
  add_test(NAME photo_text_missing_runtime_test COMMAND photo_text_rasterizer_test --without-gui)
  set_tests_properties(photo_text_rasterizer_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(
    edit_recipe_v13_adapter_test
    edit_recipe_v13_adapter_test.cpp
    ../apps/desktop/src/edit_recipe_content_validation.cpp
    ../apps/desktop/src/edit_recipe_curve_codec.cpp
    ../apps/desktop/src/edit_recipe_document_codec.cpp
    ../apps/desktop/src/edit_recipe_group_normalization.cpp
    ../apps/desktop/src/edit_recipe_json_readers.cpp
    ../apps/desktop/src/edit_recipe_layer_codec.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp
    ../apps/desktop/src/edit_recipe_profile_codec.cpp
    ../apps/desktop/src/edit_recipe_profile_validation.cpp
    ../apps/desktop/src/edit_recipe_validation.cpp
    ../apps/desktop/src/edit_recipe_v13_adapter.h
    ../apps/desktop/src/edit_recipe_v13_adapter.cpp)
  target_include_directories(
    edit_recipe_v13_adapter_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    edit_recipe_v13_adapter_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(edit_recipe_v13_adapter_test)
  add_test(
    NAME edit_recipe_v13_adapter_test
    COMMAND edit_recipe_v13_adapter_test)
  set_tests_properties(
    edit_recipe_v13_adapter_test
    PROPERTIES LABELS "desktop;color;recipe")

  add_executable(
    edit_recipe_pipeline_bridge_test
    edit_recipe_pipeline_bridge_test.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp)
  target_include_directories(
    edit_recipe_pipeline_bridge_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    edit_recipe_pipeline_bridge_test
    PRIVATE
      PHOTO_EDITOR_PREVIEW_CONTROLLER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/editor_preview_controller.cpp"
      PHOTO_EDITOR_PHOTO_RENDER_UTILS_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/photo_render_utils.cpp"
      PHOTO_EDITOR_DESKTOP_PIPELINE_COMPILER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/desktop_pipeline_state_compiler.cpp"
      PHOTO_EDITOR_RENDER_PLAN_STORE_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/render_plan_store.cpp"
      PHOTO_EDITOR_JOB_RUNNER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/job_runner.cpp"
      PHOTO_EDITOR_CATALOG_ASSET_MODEL_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/catalog_asset_model.cpp"
      PHOTO_EDITOR_CAROUSEL_CONTROLLER_PATH="${CMAKE_SOURCE_DIR}/apps/desktop/src/carousel_document_controller.cpp")
  target_link_libraries(
    edit_recipe_pipeline_bridge_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(edit_recipe_pipeline_bridge_test)
  add_test(
    NAME edit_recipe_pipeline_bridge_test
    COMMAND edit_recipe_pipeline_bridge_test)
  set_tests_properties(
    edit_recipe_pipeline_bridge_test
    PROPERTIES LABELS "desktop;color;recipe;source-contract")

  add_executable(
    edit_render_policy_test
    edit_render_policy_test.cpp)
  target_include_directories(
    edit_render_policy_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    edit_render_policy_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(edit_render_policy_test)
  add_test(
    NAME edit_render_policy_test
    COMMAND edit_render_policy_test)
  set_tests_properties(
    edit_render_policy_test
    PROPERTIES LABELS "desktop;preview;policy")

  add_executable(preview_png_cache_codec_test preview_png_cache_codec_test.cpp)
  target_link_libraries(preview_png_cache_codec_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(preview_png_cache_codec_test)
  add_test(NAME preview_png_cache_codec_test COMMAND preview_png_cache_codec_test)
  set_tests_properties(preview_png_cache_codec_test
    PROPERTIES LABELS "desktop;preview;cache" TIMEOUT 30)

  add_executable(
    preview_publisher_test
    preview_publisher_test.cpp)
  target_include_directories(
    preview_publisher_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    preview_publisher_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(preview_publisher_test)
  add_test(
    NAME preview_publisher_test
    COMMAND preview_publisher_test)
  set_tests_properties(
    preview_publisher_test
    PROPERTIES LABELS "desktop;preview;policy")

  add_executable(
    preview_session_test
    preview_session_test.cpp)
  target_include_directories(
    preview_session_test
    PRIVATE
      ../apps/desktop/src)
  photo_editor_apply_warnings(preview_session_test)
  add_test(
    NAME preview_session_test
    COMMAND preview_session_test)
  set_tests_properties(
    preview_session_test
    PROPERTIES LABELS "desktop;preview;session")

  add_executable(
    preset_service_test
    preset_service_test.cpp)
  target_include_directories(
    preset_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    preset_service_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(preset_service_test)
  add_test(
    NAME preset_service_test
    COMMAND preset_service_test)
  set_tests_properties(
    preset_service_test
    PROPERTIES LABELS "desktop;preset;service")

  add_executable(
    preset_registry_test
    preset_registry_test.cpp
    ../apps/desktop/src/preset_registry.cpp)
  target_include_directories(
    preset_registry_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    preset_registry_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(preset_registry_test)
  add_test(
    NAME preset_registry_test
    COMMAND preset_registry_test)
  set_tests_properties(
    preset_registry_test
    PROPERTIES LABELS "desktop;preset;registry")

  add_executable(
    photo_layer_adjustment_recipe_test
    photo_layer_adjustment_recipe_test.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp)
  target_include_directories(
    photo_layer_adjustment_recipe_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    photo_layer_adjustment_recipe_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(photo_layer_adjustment_recipe_test)
  add_test(
    NAME photo_layer_adjustment_recipe_test
    COMMAND photo_layer_adjustment_recipe_test)
  set_tests_properties(
    photo_layer_adjustment_recipe_test
    PROPERTIES LABELS "desktop;thumbnail;recipe;unit")

  add_executable(
    history_service_test
    history_service_test.cpp
    ../apps/desktop/src/history_revision.cpp
    ../apps/desktop/src/history_payload_store.cpp
    ../apps/desktop/src/history_state_record.cpp)
  target_include_directories(
    history_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    history_service_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(history_service_test)
  add_test(
    NAME history_service_test
    COMMAND history_service_test)
  set_tests_properties(
    history_service_test
    PROPERTIES LABELS "desktop;history;service")

  add_executable(scope_compute_benchmark scope_compute_benchmark.cpp
    ../apps/desktop/src/histogram_scope_service.cpp)
  target_include_directories(scope_compute_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(scope_compute_benchmark PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(scope_compute_benchmark)

  add_executable(scope_plot_ui_test scope_plot_ui_test.cpp
    ../apps/desktop/src/scope_plot_item.h ../apps/desktop/src/scope_plot_item.cpp)
  set_target_properties(scope_plot_ui_test PROPERTIES AUTOMOC ON)
  target_include_directories(scope_plot_ui_test PRIVATE ../apps/desktop/src)
  target_link_libraries(scope_plot_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(scope_plot_ui_test)
  add_test(NAME scope_plot_ui_test COMMAND scope_plot_ui_test)
  add_test(NAME scope_plot_software_test COMMAND scope_plot_ui_test)
  set_tests_properties(scope_plot_software_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;scope;ui;fallback")
  set_tests_properties(scope_plot_ui_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
    LABELS "desktop;scope;ui")
  if(APPLE)
    set_tests_properties(scope_plot_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal")
  else()
    set_tests_properties(scope_plot_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()

  add_executable(
    histogram_scope_service_test
    histogram_scope_service_test.cpp
    ../apps/desktop/src/histogram_scope_service.cpp)
  target_include_directories(
    histogram_scope_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    histogram_scope_service_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(histogram_scope_service_test)
  add_test(
    NAME histogram_scope_service_test
    COMMAND histogram_scope_service_test)
  set_tests_properties(
    histogram_scope_service_test
    PROPERTIES LABELS "desktop;histogram;scope;service")

  add_executable(
    histogram_scope_coordinator_test
    histogram_scope_coordinator_test.cpp
    ../apps/desktop/src/histogram_scope_service.cpp)
  target_include_directories(
    histogram_scope_coordinator_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    histogram_scope_coordinator_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Concurrent)
  photo_editor_apply_warnings(histogram_scope_coordinator_test)
  add_test(
    NAME histogram_scope_coordinator_test
    COMMAND histogram_scope_coordinator_test)
  set_tests_properties(
    histogram_scope_coordinator_test
    PROPERTIES LABELS "desktop;histogram;scope;worker;coordinator")

  add_executable(
    histogram_scope_session_test
    histogram_scope_session_test.cpp)
  target_include_directories(
    histogram_scope_session_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    histogram_scope_session_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(histogram_scope_session_test)
  add_test(
    NAME histogram_scope_session_test
    COMMAND histogram_scope_session_test)
  set_tests_properties(
    histogram_scope_session_test
    PROPERTIES LABELS "desktop;histogram;scope;publication;session")

  add_executable(
    mask_session_test
    mask_session_test.cpp)
  target_include_directories(
    mask_session_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    mask_session_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(mask_session_test)
  add_test(
    NAME mask_session_test
    COMMAND mask_session_test)
  set_tests_properties(
    mask_session_test
    PROPERTIES LABELS "desktop;mask;session")

  add_executable(
    thumbnail_refresh_coordinator_test
    thumbnail_refresh_coordinator_test.cpp)
  target_include_directories(
    thumbnail_refresh_coordinator_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    thumbnail_refresh_coordinator_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(thumbnail_refresh_coordinator_test)
  add_test(
    NAME thumbnail_refresh_coordinator_test
    COMMAND thumbnail_refresh_coordinator_test)
  set_tests_properties(
    thumbnail_refresh_coordinator_test
    PROPERTIES LABELS "desktop;thumbnail;worker;coordinator")

  add_executable(
    thumbnail_refresh_queue_service_test
    thumbnail_refresh_queue_service_test.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp)
  target_include_directories(
    thumbnail_refresh_queue_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    thumbnail_refresh_queue_service_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(thumbnail_refresh_queue_service_test)
  add_test(
    NAME thumbnail_refresh_queue_service_test
    COMMAND thumbnail_refresh_queue_service_test)
  set_tests_properties(
    thumbnail_refresh_queue_service_test
    PROPERTIES LABELS "desktop;thumbnail;worker;queue;service")

  add_executable(
    thumbnail_cache_session_test
    thumbnail_cache_session_test.cpp)
  target_include_directories(
    thumbnail_cache_session_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    thumbnail_cache_session_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(thumbnail_cache_session_test)
  add_test(
    NAME thumbnail_cache_session_test
    COMMAND thumbnail_cache_session_test)
  set_tests_properties(
    thumbnail_cache_session_test
    PROPERTIES LABELS "desktop;thumbnail;cache;session")

  add_executable(
    thumbnail_presentation_service_test
    thumbnail_presentation_service_test.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/mask_alpha_provider.cpp)
  target_include_directories(
    thumbnail_presentation_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    thumbnail_presentation_service_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(thumbnail_presentation_service_test)
  add_test(
    NAME thumbnail_presentation_service_test
    COMMAND thumbnail_presentation_service_test)
  set_tests_properties(
    thumbnail_presentation_service_test
    PROPERTIES LABELS "desktop;thumbnail;presentation;service")

  add_executable(
    mask_worker_coordinator_test
    mask_worker_coordinator_test.cpp)
  target_include_directories(
    mask_worker_coordinator_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    mask_worker_coordinator_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(mask_worker_coordinator_test)
  add_test(
    NAME mask_worker_coordinator_test
    COMMAND mask_worker_coordinator_test)
  set_tests_properties(
    mask_worker_coordinator_test
    PROPERTIES LABELS "desktop;mask;worker;session")

  add_executable(
    layer_session_test
    layer_session_test.cpp)
  target_include_directories(
    layer_session_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    layer_session_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(layer_session_test)
  add_test(
    NAME layer_session_test
    COMMAND layer_session_test)
  set_tests_properties(
    layer_session_test
    PROPERTIES LABELS "desktop;layer;session")

  add_executable(
    render_scheduler_test
    render_scheduler_test.cpp)
  target_include_directories(
    render_scheduler_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_scheduler_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(render_scheduler_test)
  add_test(
    NAME render_scheduler_test
    COMMAND render_scheduler_test)
  set_tests_properties(
    render_scheduler_test
    PROPERTIES LABELS "desktop;preview;scheduler")

  add_executable(
    carousel_color_vision_effect_test
    carousel_color_vision_effect_test.cpp)
  target_link_libraries(
    carousel_color_vision_effect_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  qt_add_shaders(carousel_color_vision_effect_test "carousel_color_vision_test_shaders"
    BATCHABLE
    PREFIX "/photo-editor/shaders"
    BASE "../apps/desktop/shaders"
    FILES
      ../apps/desktop/shaders/carousel_color_vision.vert
      ../apps/desktop/shaders/carousel_color_vision.frag)
  photo_editor_apply_warnings(carousel_color_vision_effect_test)
  add_test(
    NAME carousel_color_vision_effect_test
    COMMAND carousel_color_vision_effect_test)
  set_tests_properties(
    carousel_color_vision_effect_test
    PROPERTIES LABELS "desktop;carousel;visual")

  add_executable(
    color_corpus_comparison_tool
    color_corpus_comparison_tool.cpp)
  target_link_libraries(
    color_corpus_comparison_tool
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(color_corpus_comparison_tool)
  add_test(
    NAME color_corpus_comparison_tool_self_test
    COMMAND color_corpus_comparison_tool --self-test)

  add_executable(
    independent_chart_profile_tag_tool
    independent_chart_profile_tag_tool.cpp)
  target_link_libraries(
    independent_chart_profile_tag_tool
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(independent_chart_profile_tag_tool)
  add_test(
    NAME independent_chart_profile_tag_tool_self_test
    COMMAND independent_chart_profile_tag_tool --self-test)

  add_executable(
    system_monitor_profile_state_test
    system_monitor_profile_state_test.cpp)
  target_include_directories(
    system_monitor_profile_state_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    system_monitor_profile_state_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(system_monitor_profile_state_test)
  add_test(
    NAME system_monitor_profile_state_test
    COMMAND system_monitor_profile_state_test)

  add_executable(
    qimage_color_conversion_test
    qimage_color_conversion_test.cpp)
  target_include_directories(
    qimage_color_conversion_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    qimage_color_conversion_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(qimage_color_conversion_test)
  add_test(
    NAME qimage_color_conversion_test
    COMMAND qimage_color_conversion_test)

  if(TARGET color_cmm_comparison_test)
    target_link_libraries(color_cmm_comparison_test PRIVATE Qt6::Gui)
    target_compile_definitions(
      color_cmm_comparison_test
      PRIVATE PHOTO_EDITOR_HAS_QT_COLOR_ORACLE=1)
    if(APPLE)
      find_library(COLOR_CMM_COREGRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
      target_link_libraries(
        color_cmm_comparison_test
        PRIVATE ${COLOR_CMM_COREGRAPHICS_FRAMEWORK})
      target_compile_definitions(
        color_cmm_comparison_test
        PRIVATE PHOTO_EDITOR_HAS_COLORSYNC_ORACLE=1)
    endif()
  endif()

  set(photo_editor_desktop_image_decoder_sources
    ../apps/desktop/src/raw_image_decoder_libraw.cpp
    ../apps/desktop/src/raw_image_decoder_stub.cpp)
  set(photo_editor_desktop_image_decoder_frameworks
    photo_editor_raw_decoder_backend)
  set(photo_editor_desktop_observability_sources
    ../apps/desktop/src/observability.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp)
  set(photo_editor_desktop_brush_preview_sources
    ../apps/desktop/src/brush_preview_compositor.h
    ../apps/desktop/src/brush_preview_compositor.cpp
    ../apps/desktop/src/editor_preview_texture.h
    ../apps/desktop/src/editor_preview_texture.cpp)
  set(photo_editor_desktop_ai_model_sources
    ../apps/desktop/src/ai_model_runtime.h
    ../apps/desktop/src/ai_model_runtime.cpp)
  set(photo_editor_desktop_mask_alpha_provider_sources
    ../apps/desktop/src/mask_alpha_provider.h
    ../apps/desktop/src/mask_alpha_provider.cpp
    ../apps/desktop/src/mask_source_revision.h
    ../apps/desktop/src/scaled_mask_matte_service.h
    ../apps/desktop/src/scaled_mask_matte_service.cpp
    ../apps/desktop/src/canonical_composite_mask_alpha.h
    ../apps/desktop/src/canonical_composite_mask_alpha.cpp
    ../apps/desktop/src/mask_thumbnail_payload_service.h
    ../apps/desktop/src/mask_thumbnail_payload_service.cpp
    ../apps/desktop/src/generated_mask_cleanup.h
    ../apps/desktop/src/generated_mask_cleanup.cpp
    ../apps/desktop/src/generated_mask_metadata.h
    ../apps/desktop/src/generated_mask_metadata.cpp
    ../apps/desktop/src/generated_mask_object_remove_assist.h
    ../apps/desktop/src/generated_mask_object_remove_assist.cpp
    ../apps/desktop/src/generated_mask_quality.h
    ../apps/desktop/src/generated_mask_quality.cpp
    ../apps/desktop/src/segmentation_mask_ops.h
    ../apps/desktop/src/segmentation_mask_ops.cpp
    ../apps/desktop/src/sensor_dust_detection.h
    ../apps/desktop/src/sensor_dust_detection.cpp
    ../apps/desktop/src/sensor_dust_map_store.h
    ../apps/desktop/src/sensor_dust_map_store.cpp
    ../apps/desktop/src/retouch_mask_detection.h
    ../apps/desktop/src/retouch_mask_detection.cpp
    ../apps/desktop/src/smart_selection_geometry.h
    ../apps/desktop/src/smart_selection_geometry.cpp
    ../apps/desktop/src/smart_selection_instance_policy.h
    ../apps/desktop/src/smart_selection_instance_policy.cpp
    ../apps/desktop/src/smart_selection_polygon_geometry.h
    ../apps/desktop/src/smart_selection_polygon_geometry.cpp
    ../apps/desktop/src/smart_selection_prompt_schema.h
    ../apps/desktop/src/smart_selection_prompt_schema.cpp
    ../apps/desktop/src/segmentation_result_schema.h
    ../apps/desktop/src/segmentation_result_schema.cpp
    ../apps/desktop/src/segmentation_request_policy.h
    ../apps/desktop/src/segmentation_request_policy.cpp
    ../apps/desktop/src/object_pick_policy.h
    ../apps/desktop/src/object_pick_policy.cpp
    ../apps/desktop/src/localized_object_refinement.h
    ../apps/desktop/src/localized_object_refinement.cpp
    ../apps/desktop/src/brush_input_schema.h
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_stroke_sampling.h
    ../apps/desktop/src/brush_stroke_sampling.cpp
    ../apps/desktop/src/brush_rasterizer.h
    ../apps/desktop/src/brush_rasterizer.cpp
    ../apps/desktop/src/brush_matte_rebuild.h
    ../apps/desktop/src/brush_matte_rebuild.cpp
    ../apps/desktop/src/brush_outline_geometry.h
    ../apps/desktop/src/brush_outline_geometry.cpp
    ../apps/desktop/src/mask_outline_simplification.h
    ../apps/desktop/src/mask_outline_simplification.cpp
    ../apps/desktop/src/mask_outline_presentation.h
    ../apps/desktop/src/mask_outline_presentation.cpp
    ../apps/desktop/src/mask_outline_tracing.h
    ../apps/desktop/src/mask_outline_tracing.cpp
    ../apps/desktop/src/mask_range_preview_math.h
    ../apps/desktop/src/mask_range_preview_math.cpp)

  add_executable(
    generated_mask_cleanup_test
    generated_mask_cleanup_test.cpp
    ../apps/desktop/src/generated_mask_cleanup.cpp)
  target_include_directories(
    generated_mask_cleanup_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    generated_mask_cleanup_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(generated_mask_cleanup_test)
  add_test(
    NAME generated_mask_cleanup_test
    COMMAND generated_mask_cleanup_test)
  set_tests_properties(
    generated_mask_cleanup_test
    PROPERTIES LABELS "desktop;mask;generated;cleanup")

  add_executable(
    generated_mask_metadata_test
    generated_mask_metadata_test.cpp
    ../apps/desktop/src/generated_mask_metadata.cpp)
  target_include_directories(
    generated_mask_metadata_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    generated_mask_metadata_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(generated_mask_metadata_test)
  add_test(
    NAME generated_mask_metadata_test
    COMMAND generated_mask_metadata_test)
  set_tests_properties(
    generated_mask_metadata_test
    PROPERTIES LABELS "desktop;mask;generated;metadata")

  add_executable(
    generated_mask_quality_test
    generated_mask_quality_test.cpp
    ../apps/desktop/src/generated_mask_quality.cpp)
  target_include_directories(
    generated_mask_quality_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    generated_mask_quality_test
    PRIVATE
      Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(generated_mask_quality_test)
  add_test(
    NAME generated_mask_quality_test
    COMMAND generated_mask_quality_test)
  set_tests_properties(
    generated_mask_quality_test
    PROPERTIES LABELS "desktop;mask;generated;quality")

  add_executable(
    generated_mask_object_remove_assist_test
    generated_mask_object_remove_assist_test.cpp
    ../apps/desktop/src/generated_mask_object_remove_assist.cpp)
  target_include_directories(
    generated_mask_object_remove_assist_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    generated_mask_object_remove_assist_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(generated_mask_object_remove_assist_test)
  add_test(
    NAME generated_mask_object_remove_assist_test
    COMMAND generated_mask_object_remove_assist_test)
  set_tests_properties(
    generated_mask_object_remove_assist_test
    PROPERTIES LABELS "desktop;mask;generated;object-remove")

  add_executable(
    segmentation_mask_ops_test
    segmentation_mask_ops_test.cpp
    ../apps/desktop/src/segmentation_mask_ops.cpp)
  target_include_directories(
    segmentation_mask_ops_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    segmentation_mask_ops_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(segmentation_mask_ops_test)
  add_test(
    NAME segmentation_mask_ops_test
    COMMAND segmentation_mask_ops_test)
  set_tests_properties(
    segmentation_mask_ops_test
    PROPERTIES LABELS "desktop;mask;raster;service")

  add_executable(
    sensor_dust_detection_test
    sensor_dust_detection_test.cpp
    ../apps/desktop/src/sensor_dust_detection.cpp)
  target_include_directories(
    sensor_dust_detection_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    sensor_dust_detection_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(sensor_dust_detection_test)
  add_test(
    NAME sensor_dust_detection_test
    COMMAND sensor_dust_detection_test)
  set_tests_properties(
    sensor_dust_detection_test
    PROPERTIES LABELS "desktop;mask;dust;service")

  add_executable(
    sensor_dust_map_store_test
    sensor_dust_map_store_test.cpp
    ../apps/desktop/src/sensor_dust_map_store.cpp)
  target_include_directories(
    sensor_dust_map_store_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    sensor_dust_map_store_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(sensor_dust_map_store_test)
  add_test(
    NAME sensor_dust_map_store_test
    COMMAND sensor_dust_map_store_test)
  set_tests_properties(
    sensor_dust_map_store_test
    PROPERTIES LABELS "desktop;mask;dust;store")

  add_executable(
    retouch_mask_detection_test
    retouch_mask_detection_test.cpp
    ../apps/desktop/src/retouch_mask_detection.cpp)
  target_include_directories(
    retouch_mask_detection_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    retouch_mask_detection_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(retouch_mask_detection_test)
  add_test(
    NAME retouch_mask_detection_test
    COMMAND retouch_mask_detection_test)
  set_tests_properties(
    retouch_mask_detection_test
    PROPERTIES LABELS "desktop;mask;retouch;service")

  add_executable(
    smart_selection_geometry_test
    smart_selection_geometry_test.cpp
    ../apps/desktop/src/smart_selection_geometry.cpp)
  target_include_directories(
    smart_selection_geometry_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    smart_selection_geometry_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(smart_selection_geometry_test)
  add_test(
    NAME smart_selection_geometry_test
    COMMAND smart_selection_geometry_test)
  set_tests_properties(
    smart_selection_geometry_test
    PROPERTIES LABELS "desktop;mask;smart-selection;service")

  add_executable(
    smart_selection_prompt_schema_test
    smart_selection_prompt_schema_test.cpp
    ../apps/desktop/src/smart_selection_prompt_schema.cpp)
  target_include_directories(
    smart_selection_prompt_schema_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    smart_selection_prompt_schema_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(smart_selection_prompt_schema_test)
  add_test(
    NAME smart_selection_prompt_schema_test
    COMMAND smart_selection_prompt_schema_test)
  set_tests_properties(
    smart_selection_prompt_schema_test
    PROPERTIES LABELS "desktop;mask;smart-selection;schema")

  add_executable(
    smart_selection_instance_policy_test
    smart_selection_instance_policy_test.cpp
    ../apps/desktop/src/smart_selection_geometry.cpp
    ../apps/desktop/src/smart_selection_prompt_schema.cpp
    ../apps/desktop/src/smart_selection_instance_policy.cpp)
  target_include_directories(
    smart_selection_instance_policy_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    smart_selection_instance_policy_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(smart_selection_instance_policy_test)
  add_test(
    NAME smart_selection_instance_policy_test
    COMMAND smart_selection_instance_policy_test)
  set_tests_properties(
    smart_selection_instance_policy_test
    PROPERTIES LABELS "desktop;mask;smart-selection;instance-policy")

  add_executable(
    smart_selection_polygon_geometry_test
    smart_selection_polygon_geometry_test.cpp
    ../apps/desktop/src/smart_selection_polygon_geometry.cpp)
  target_include_directories(
    smart_selection_polygon_geometry_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    smart_selection_polygon_geometry_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(smart_selection_polygon_geometry_test)
  add_test(
    NAME smart_selection_polygon_geometry_test
    COMMAND smart_selection_polygon_geometry_test)
  set_tests_properties(
    smart_selection_polygon_geometry_test
    PROPERTIES LABELS "desktop;mask;smart-selection;polygon-geometry")

  add_executable(
    segmentation_result_schema_test
    segmentation_result_schema_test.cpp
    ../apps/desktop/src/segmentation_result_schema.cpp)
  target_include_directories(
    segmentation_result_schema_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    segmentation_result_schema_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(segmentation_result_schema_test)
  add_test(
    NAME segmentation_result_schema_test
    COMMAND segmentation_result_schema_test)
  set_tests_properties(
    segmentation_result_schema_test
    PROPERTIES LABELS "desktop;mask;segmentation;schema")

  add_executable(
    segmentation_request_policy_test
    segmentation_request_policy_test.cpp
    ../apps/desktop/src/segmentation_request_policy.cpp
    ../apps/desktop/src/segmentation_result_schema.cpp)
  target_include_directories(
    segmentation_request_policy_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    segmentation_request_policy_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(segmentation_request_policy_test)
  add_test(
    NAME segmentation_request_policy_test
    COMMAND segmentation_request_policy_test)
  set_tests_properties(
    segmentation_request_policy_test
    PROPERTIES LABELS "desktop;mask;segmentation;policy")

  add_executable(
    object_pick_policy_test
    object_pick_policy_test.cpp
    ../apps/desktop/src/object_pick_policy.cpp
    ../apps/desktop/src/segmentation_result_schema.cpp)
  target_include_directories(
    object_pick_policy_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    object_pick_policy_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(object_pick_policy_test)
  add_test(
    NAME object_pick_policy_test
    COMMAND object_pick_policy_test)
  set_tests_properties(
    object_pick_policy_test
    PROPERTIES LABELS "desktop;mask;object-pick;policy")

  add_executable(
    localized_object_refinement_test
    localized_object_refinement_test.cpp
    ../apps/desktop/src/localized_object_refinement.cpp
    ../apps/desktop/src/segmentation_result_schema.cpp)
  target_include_directories(
    localized_object_refinement_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    localized_object_refinement_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(localized_object_refinement_test)
  add_test(
    NAME localized_object_refinement_test
    COMMAND localized_object_refinement_test)
  set_tests_properties(
    localized_object_refinement_test
    PROPERTIES LABELS "desktop;mask;object-pick;refinement")

  add_executable(
    brush_input_schema_test
    brush_input_schema_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp)
  target_include_directories(
    brush_input_schema_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    brush_input_schema_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(brush_input_schema_test)
  add_test(
    NAME brush_input_schema_test
    COMMAND brush_input_schema_test)
  set_tests_properties(
    brush_input_schema_test
    PROPERTIES LABELS "desktop;mask;brush;schema")

  add_executable(
    brush_stroke_sampling_test
    brush_stroke_sampling_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_stroke_sampling.cpp)
  target_include_directories(
    brush_stroke_sampling_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    brush_stroke_sampling_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(brush_stroke_sampling_test)
  add_test(
    NAME brush_stroke_sampling_test
    COMMAND brush_stroke_sampling_test)
  set_tests_properties(
    brush_stroke_sampling_test
    PROPERTIES LABELS "desktop;mask;brush;sampling")

  add_executable(
    brush_rasterizer_test
    brush_rasterizer_test.cpp
    ../apps/desktop/src/brush_rasterizer.cpp)
  target_include_directories(
    brush_rasterizer_test
    PRIVATE
      ../apps/desktop/src
      ../engine/include)
  target_link_libraries(
    brush_rasterizer_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(brush_rasterizer_test)
  add_test(
    NAME brush_rasterizer_test
    COMMAND brush_rasterizer_test)
  set_tests_properties(
    brush_rasterizer_test
    PROPERTIES LABELS "desktop;mask;brush;rasterizer")

  add_executable(brush_fidelity_test
    brush_fidelity_test.cpp
    ../apps/desktop/src/brush_rasterizer.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_stroke_sampling.cpp
    ../apps/desktop/src/brush_matte_rebuild.cpp)
  target_include_directories(brush_fidelity_test PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(brush_fidelity_test PRIVATE photo_engine Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(brush_fidelity_test)
  add_test(NAME brush_fidelity_test COMMAND brush_fidelity_test)

  add_executable(
    brush_matte_rebuild_test
    brush_matte_rebuild_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_rasterizer.cpp
    ../apps/desktop/src/brush_matte_rebuild.cpp
    ../apps/desktop/src/scaled_mask_matte_service.cpp
    ../apps/desktop/src/segmentation_mask_ops.cpp)
  target_include_directories(
    brush_matte_rebuild_test
    PRIVATE
      ../apps/desktop/src
      ../engine/include)
  target_link_libraries(
    brush_matte_rebuild_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(brush_matte_rebuild_test)
  add_test(
    NAME brush_matte_rebuild_test
    COMMAND brush_matte_rebuild_test)
  set_tests_properties(
    brush_matte_rebuild_test
    PROPERTIES LABELS "desktop;mask;brush;rebuild")

  add_executable(
    brush_outline_geometry_test
    brush_outline_geometry_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_outline_geometry.cpp)
  target_include_directories(
    brush_outline_geometry_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    brush_outline_geometry_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(brush_outline_geometry_test)
  add_test(
    NAME brush_outline_geometry_test
    COMMAND brush_outline_geometry_test)
  set_tests_properties(
    brush_outline_geometry_test
    PROPERTIES LABELS "desktop;mask;brush;outline")

  add_executable(
    mask_outline_simplification_test
    mask_outline_simplification_test.cpp
    ../apps/desktop/src/smart_selection_polygon_geometry.cpp
    ../apps/desktop/src/mask_outline_simplification.cpp)
  target_include_directories(
    mask_outline_simplification_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    mask_outline_simplification_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(mask_outline_simplification_test)
  add_test(
    NAME mask_outline_simplification_test
    COMMAND mask_outline_simplification_test)
  set_tests_properties(
    mask_outline_simplification_test
    PROPERTIES LABELS "desktop;mask;outline;simplification")

  add_executable(
    mask_outline_tracing_test
    mask_outline_tracing_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_outline_geometry.cpp
    ../apps/desktop/src/smart_selection_polygon_geometry.cpp
    ../apps/desktop/src/mask_outline_simplification.cpp
    ../apps/desktop/src/mask_outline_tracing.cpp)
  target_include_directories(
    mask_outline_tracing_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    mask_outline_tracing_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(mask_outline_tracing_test)
  add_test(
    NAME mask_outline_tracing_test
    COMMAND mask_outline_tracing_test)
  set_tests_properties(
    mask_outline_tracing_test
    PROPERTIES LABELS "desktop;mask;outline;tracing")

  add_executable(
    mask_outline_presentation_test
    mask_outline_presentation_test.cpp
    ../apps/desktop/src/brush_input_schema.cpp
    ../apps/desktop/src/brush_rasterizer.cpp
    ../apps/desktop/src/brush_matte_rebuild.cpp
    ../apps/desktop/src/brush_outline_geometry.cpp
    ../apps/desktop/src/smart_selection_polygon_geometry.cpp
    ../apps/desktop/src/mask_outline_simplification.cpp
    ../apps/desktop/src/mask_outline_tracing.cpp
    ../apps/desktop/src/mask_outline_presentation.cpp)
  target_include_directories(
    mask_outline_presentation_test
    PRIVATE
      ../apps/desktop/src
      ../engine/include)
  target_link_libraries(
    mask_outline_presentation_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(mask_outline_presentation_test)
  add_test(
    NAME mask_outline_presentation_test
    COMMAND mask_outline_presentation_test)
  set_tests_properties(
    mask_outline_presentation_test
    PROPERTIES LABELS "desktop;mask;outline;presentation-policy")

  add_executable(
    mask_range_preview_math_test
    mask_range_preview_math_test.cpp
    ../apps/desktop/src/mask_range_preview_math.cpp)
  target_include_directories(
    mask_range_preview_math_test
    PRIVATE
      ../apps/desktop/src
      ../engine/include)
  target_link_libraries(
    mask_range_preview_math_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(mask_range_preview_math_test)
  add_test(
    NAME mask_range_preview_math_test
    COMMAND mask_range_preview_math_test)
  set_tests_properties(
    mask_range_preview_math_test
    PROPERTIES LABELS "desktop;mask;color-range;refinement;preview-math")

  set(photo_editor_desktop_render_plan_store_sources
    ../apps/desktop/src/raw_decode_policy.h
    ../apps/desktop/src/render_plan_store.h
    ../apps/desktop/src/render_plan_store.cpp)
  set(photo_editor_desktop_recipe_v13_sources
    ../apps/desktop/src/preview_color_pipeline_config.h
    ../apps/desktop/src/preview_color_pipeline_config.cpp
    ../apps/desktop/src/preview_display_publication.h
    ../apps/desktop/src/preview_display_publication.cpp
    ../apps/desktop/src/preview_output_color_space.h
    ../apps/desktop/src/edit_adjustment_patch.h
    ../apps/desktop/src/edit_adjustment_patch.cpp
    ../apps/desktop/src/edit_history_snapshot_store.h
    ../apps/desktop/src/edit_history_snapshot_store.cpp
    ../apps/desktop/src/history_revision.cpp
    ../apps/desktop/src/history_payload_store.cpp
    ../apps/desktop/src/history_state_record.cpp
    ../apps/desktop/src/render_executor_service.h
    ../apps/desktop/src/render_executor_service.cpp
    ../apps/desktop/src/recipe_asset_store.h
    ../apps/desktop/src/recipe_asset_store.cpp
    ../apps/desktop/src/edit_recipe_content_validation.cpp
    ../apps/desktop/src/edit_recipe_curve_codec.cpp
    ../apps/desktop/src/edit_recipe_diff.cpp
    ../apps/desktop/src/edit_recipe_document_codec.cpp
    ../apps/desktop/src/edit_recipe_group_normalization.cpp
    ../apps/desktop/src/edit_recipe_history.cpp
    ../apps/desktop/src/edit_recipe_layer_codec.cpp
    ../apps/desktop/src/edit_recipe_mask_schema.h
    ../apps/desktop/src/edit_recipe_json_readers.cpp
    ../apps/desktop/src/edit_recipe_migration.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp
    ../apps/desktop/src/edit_recipe_profile_codec.cpp
    ../apps/desktop/src/edit_recipe_profile_validation.cpp
    ../apps/desktop/src/edit_recipe_validation.cpp
    ../apps/desktop/src/edit_recipe_v13_adapter.h
    ../apps/desktop/src/edit_recipe_v13_adapter.cpp
    ../apps/desktop/src/edit_recipe_v13_persistence.h
    ../apps/desktop/src/edit_recipe_v13_persistence.cpp
    ../apps/desktop/src/desktop_recipe_persistence_service.h
    ../apps/desktop/src/desktop_recipe_persistence_service.cpp
    ../apps/desktop/src/catalog_recipe_projection.h
    ../apps/desktop/src/catalog_recipe_projection.cpp
    ../apps/desktop/src/rendering_migration_evidence.h
    ../apps/desktop/src/rendering_migration_evidence.cpp
    ../apps/desktop/src/editor_rendering_migration.inc)
  set(photo_editor_desktop_photo_render_utils_sources
    ../apps/desktop/src/photo_text_dimension_mesh.cpp
    ../apps/desktop/src/photo_text_dimension_outline.cpp
    ../apps/desktop/src/photo_text_dimension_render.cpp
    ../apps/desktop/src/photo_smart_object.cpp
    ../apps/desktop/src/photo_smart_document.cpp
    ../apps/desktop/src/photo_smart_document_expansion.cpp
    ../apps/desktop/src/photo_vector_mask_rebase.cpp
    ../apps/desktop/src/photo_vector_path_io.cpp
    ../apps/desktop/src/photo_vector_path.cpp
    ../apps/desktop/src/photo_vector_path_io.cpp
    ../apps/desktop/src/photo_vector_mask_rebase.cpp
    ../apps/desktop/src/photo_smart_document.cpp
    ../apps/desktop/src/photo_smart_document_expansion.cpp
    ../apps/desktop/src/photo_vector_mask.cpp
    ../apps/desktop/src/photo_render_crop_utils.cpp
    ../apps/desktop/src/photo_render_utils.cpp
    ../apps/desktop/src/desktop_pipeline_state_compiler.h
    ../apps/desktop/src/desktop_pipeline_state_compiler.cpp)
  add_executable(
    preview_color_pipeline_config_test
    preview_color_pipeline_config_test.cpp
    ../apps/desktop/src/preview_color_pipeline_config.cpp
    ../apps/desktop/src/edit_recipe_content_validation.cpp
    ../apps/desktop/src/edit_recipe_curve_codec.cpp
    ../apps/desktop/src/edit_recipe_document_codec.cpp
    ../apps/desktop/src/edit_recipe_group_normalization.cpp
    ../apps/desktop/src/edit_recipe_json_readers.cpp
    ../apps/desktop/src/edit_recipe_layer_codec.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp
    ../apps/desktop/src/edit_recipe_profile_codec.cpp
    ../apps/desktop/src/edit_recipe_profile_validation.cpp
    ../apps/desktop/src/edit_recipe_validation.cpp
    ../apps/desktop/src/edit_recipe_v13_adapter.cpp)
  target_include_directories(
    preview_color_pipeline_config_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    preview_color_pipeline_config_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(preview_color_pipeline_config_test)
  add_test(
    NAME preview_color_pipeline_config_test
    COMMAND preview_color_pipeline_config_test)
  add_executable(
    preview_display_publication_test
    preview_display_publication_test.cpp
    ../apps/desktop/src/preview_display_publication.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp)
  target_include_directories(
    preview_display_publication_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    preview_display_publication_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(preview_display_publication_test)
  add_test(
    NAME preview_display_publication_test
    COMMAND preview_display_publication_test)
  add_executable(
    render_executor_service_test
    render_executor_service_test.cpp
    ../apps/desktop/src/render_executor_service.h
    ../apps/desktop/src/render_executor_service.cpp)
  target_include_directories(
    render_executor_service_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_executor_service_test
    PRIVATE
      Qt6::Core
      photo_engine)
  photo_editor_apply_warnings(render_executor_service_test)
  add_test(
    NAME render_executor_service_test
    COMMAND render_executor_service_test)
  add_executable(
    edit_recipe_v13_persistence_test
    edit_recipe_v13_persistence_test.cpp
    ${photo_editor_desktop_recipe_v13_sources})
  target_include_directories(
    edit_recipe_v13_persistence_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    edit_recipe_v13_persistence_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(
    edit_recipe_v13_persistence_test)
  add_test(
    NAME edit_recipe_v13_persistence_test
    COMMAND edit_recipe_v13_persistence_test)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND AND UNIX)
    add_test(NAME recipe_catalog_contract_test
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/test-recipe-catalog-contract.py"
        --desktop-test "$<TARGET_FILE:edit_recipe_v13_persistence_test>"
        --catalog-bin-dir "${PHOTO_EDITOR_RUST_BINARY_DIR}")
    set_tests_properties(recipe_catalog_contract_test PROPERTIES
      TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "desktop;persistence;postgres;contract")
  endif()
  add_executable(
    recipe_asset_store_test
    recipe_asset_store_test.cpp
    ../apps/desktop/src/recipe_asset_store.h
    ../apps/desktop/src/recipe_asset_store.cpp)
  target_include_directories(
    recipe_asset_store_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    recipe_asset_store_test
    PRIVATE
      Qt6::Core
      photo_engine)
  photo_editor_apply_warnings(recipe_asset_store_test)
  add_test(
    NAME recipe_asset_store_test
    COMMAND recipe_asset_store_test)
  # TimelineDocumentController calls the subject-segmentation API for auto
  # reframe analysis. Desktop tests use the deterministic stub so every target
  # that embeds the controller also owns the matching implementation.
  set(photo_editor_desktop_vision_sources
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set(photo_editor_desktop_brush_preview_frameworks)

  if(APPLE)
    if(PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
      list(APPEND photo_editor_desktop_brush_preview_sources
        ../apps/desktop/src/brush_preview_compositor_metal.mm
        ../apps/desktop/src/editor_preview_texture_metal.mm)
      set(photo_editor_desktop_brush_preview_frameworks
        "-framework Metal"
        "-framework Foundation")
      set_source_files_properties(
        ../apps/desktop/src/brush_preview_compositor_metal.mm
        ../apps/desktop/src/editor_preview_texture_metal.mm
        PROPERTIES
          COMPILE_OPTIONS "-fobjc-arc")
    endif()
    list(REMOVE_ITEM photo_editor_desktop_image_decoder_sources ../apps/desktop/src/raw_image_decoder_stub.cpp)
    list(APPEND photo_editor_desktop_image_decoder_sources ../apps/desktop/src/raw_image_decoder.mm)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(COREIMAGE_FRAMEWORK CoreImage)
    find_library(IMAGEIO_FRAMEWORK ImageIO)
    find_library(SPEECH_FRAMEWORK Speech)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if(COREGRAPHICS_FRAMEWORK AND COREIMAGE_FRAMEWORK AND IMAGEIO_FRAMEWORK)
      set(photo_editor_desktop_image_decoder_frameworks
        photo_editor_raw_decoder_backend
        ${COREGRAPHICS_FRAMEWORK}
        ${COREIMAGE_FRAMEWORK}
        ${IMAGEIO_FRAMEWORK})
    endif()
    if(SPEECH_FRAMEWORK AND FOUNDATION_FRAMEWORK)
      list(APPEND photo_editor_desktop_image_decoder_frameworks
        ${SPEECH_FRAMEWORK}
        ${FOUNDATION_FRAMEWORK})
    endif()
    set_source_files_properties(
      ../apps/desktop/src/raw_image_decoder.mm
      PROPERTIES
        COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(
      ../apps/desktop/src/timeline_document_controller.cpp
      ../apps/desktop/src/timeline_graphics_track_compaction.cpp
      PROPERTIES
        LANGUAGE OBJCXX
        COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(
      ../apps/desktop/src/brush_live_preview_item.cpp
      ../apps/desktop/src/editor_preview_texture_item.cpp
      PROPERTIES
        LANGUAGE OBJCXX)
  endif()

  add_executable(
    raw_camera_linear_decoder_test
    raw_camera_linear_decoder_test.cpp
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    raw_camera_linear_decoder_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(
    raw_camera_linear_decoder_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine
      ${photo_editor_desktop_image_decoder_frameworks})
  photo_editor_apply_warnings(raw_camera_linear_decoder_test)
  add_test(
    NAME raw_camera_linear_decoder_test
    COMMAND raw_camera_linear_decoder_test)
  set_tests_properties(
    raw_camera_linear_decoder_test
    PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(
    render_plan_store_test
    render_plan_store_test.cpp
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    render_plan_store_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_plan_store_test
    PRIVATE
      Qt6::Core
      Qt6::Concurrent
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(render_plan_store_test)
  add_test(
    NAME render_plan_store_test
    COMMAND render_plan_store_test)
  set_tests_properties(
    render_plan_store_test
    PROPERTIES LABELS "desktop;color;recipe;service")

  add_executable(
    app_shell_controller_test
    app_shell_controller_test.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ${photo_editor_desktop_recipe_v13_sources})
  set_target_properties(app_shell_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    app_shell_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    app_shell_controller_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(app_shell_controller_test)

  add_test(NAME app_shell_controller_test COMMAND app_shell_controller_test)

  add_executable(
    app_bootstrap_test
    app_bootstrap_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp)
  set_target_properties(app_bootstrap_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    app_bootstrap_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    app_bootstrap_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(app_bootstrap_test)

  add_test(NAME app_bootstrap_test COMMAND app_bootstrap_test)

  add_executable(observability_benchmark observability_benchmark.cpp
    ../apps/desktop/src/observability.cpp)
  target_include_directories(observability_benchmark PRIVATE ../apps/desktop/src)
  target_compile_definitions(observability_benchmark PRIVATE PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS=1)
  target_link_libraries(observability_benchmark PRIVATE Qt6::Core)
  photo_editor_apply_warnings(observability_benchmark)

  add_executable(observability_writer_test observability_writer_test.cpp
    ../apps/desktop/src/observability.cpp)
  target_include_directories(observability_writer_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(observability_writer_test PRIVATE PHOTO_EDITOR_ENABLE_LOCAL_DIAGNOSTICS=1)
  target_link_libraries(observability_writer_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(observability_writer_test)
  add_test(NAME observability_writer_test COMMAND observability_writer_test)
  set_tests_properties(observability_writer_test PROPERTIES TIMEOUT 90 LABELS "desktop;logging;performance")

  add_executable(catalog_readiness_probe_test catalog_readiness_probe_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(catalog_readiness_probe_test PROPERTIES AUTOMOC ON)
  target_include_directories(catalog_readiness_probe_test PRIVATE ../apps/desktop/src)
  target_link_libraries(catalog_readiness_probe_test PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(catalog_readiness_probe_test)
  add_test(NAME catalog_readiness_probe_test COMMAND catalog_readiness_probe_test)
  set_tests_properties(catalog_readiness_probe_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)

  add_executable(
    agent_edit_style_test
    agent_edit_style_test.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_edit_style_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_edit_style_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(agent_edit_style_test)

  add_test(NAME agent_edit_style_test COMMAND agent_edit_style_test)

  add_executable(
    agent_edit_recipe_patch_test
    agent_edit_recipe_patch_test.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_edit_recipe_patch_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_edit_recipe_patch_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(agent_edit_recipe_patch_test)

  add_test(NAME agent_edit_recipe_patch_test COMMAND agent_edit_recipe_patch_test)

  add_executable(
    agent_local_model_test
    agent_local_model_test.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_local_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_local_model_test
    PRIVATE
      photo_engine
      Qt6::Core
      Qt6::Gui
      Qt6::Network)
  photo_editor_apply_warnings(agent_local_model_test)

  add_test(NAME agent_local_model_test COMMAND agent_local_model_test)
  add_executable(agent_decision_frame_test agent_decision_frame_test.cpp)
  target_include_directories(agent_decision_frame_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_decision_frame_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_decision_frame_test)
  add_test(NAME agent_decision_frame_test COMMAND agent_decision_frame_test)
  add_executable(agent_session_controller_test
    agent_session_controller_test.cpp
    ../apps/desktop/src/agent_session_controller.h
    ../apps/desktop/src/agent_session_controller.cpp
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.cpp)
  set_target_properties(agent_session_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(agent_session_controller_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_session_controller_test PRIVATE photo_engine
    Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network)
  photo_editor_apply_warnings(agent_session_controller_test)
  add_test(NAME agent_session_controller_test COMMAND agent_session_controller_test)
  add_executable(agent_execution_admission_test agent_execution_admission_test.cpp)
  target_include_directories(agent_execution_admission_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_execution_admission_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_execution_admission_test)
  add_test(NAME agent_execution_admission_test COMMAND agent_execution_admission_test)
  add_executable(agent_session_live_planning_driver
    agent_session_live_planning_driver.cpp
    ../apps/desktop/src/agent_session_controller.h
    ../apps/desktop/src/agent_session_controller.cpp
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.cpp)
  set_target_properties(agent_session_live_planning_driver PROPERTIES AUTOMOC ON)
  target_include_directories(agent_session_live_planning_driver PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_session_live_planning_driver PRIVATE photo_engine
    Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network)
  photo_editor_apply_warnings(agent_session_live_planning_driver)
  add_executable(agent_blank_canvas_source_test agent_blank_canvas_source_test.cpp)
  target_include_directories(agent_blank_canvas_source_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_blank_canvas_source_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(agent_blank_canvas_source_test)
  add_test(NAME agent_blank_canvas_source_test COMMAND agent_blank_canvas_source_test)
  add_executable(agent_created_file_resource_test agent_created_file_resource_test.cpp)
  target_include_directories(agent_created_file_resource_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_created_file_resource_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_created_file_resource_test)
  add_test(NAME agent_created_file_resource_test COMMAND agent_created_file_resource_test)
  add_executable(agent_photo_output_task_test agent_photo_output_task_test.cpp)
  target_include_directories(agent_photo_output_task_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_photo_output_task_test PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(agent_photo_output_task_test)
  add_test(NAME agent_photo_output_task_test COMMAND agent_photo_output_task_test)
  set_tests_properties(agent_photo_output_task_test PROPERTIES TIMEOUT 60
    LABELS "desktop;agent;output;ownership")
  add_executable(agent_task_permissions_test agent_task_permissions_test.cpp)
  target_include_directories(agent_task_permissions_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_task_permissions_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_task_permissions_test)
  add_test(NAME agent_task_permissions_test COMMAND agent_task_permissions_test)
  add_executable(agent_task_journal_test agent_task_journal_test.cpp)
  target_include_directories(agent_task_journal_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_task_journal_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_task_journal_test)
  add_test(NAME agent_task_journal_test COMMAND agent_task_journal_test)
  add_executable(agent_task_coordinator_test agent_task_coordinator_test.cpp)
  target_include_directories(agent_task_coordinator_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_task_coordinator_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_task_coordinator_test)
  add_test(NAME agent_task_coordinator_test COMMAND agent_task_coordinator_test)
  add_executable(agent_tool_progress_test agent_tool_progress_test.cpp)
  target_include_directories(agent_tool_progress_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_tool_progress_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_tool_progress_test)
  add_test(NAME agent_tool_progress_test COMMAND agent_tool_progress_test)
  add_executable(agent_app_command_registry_test agent_app_command_registry_test.cpp)
  set_target_properties(agent_app_command_registry_test PROPERTIES AUTOMOC ON)
  target_include_directories(agent_app_command_registry_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_app_command_registry_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_app_command_registry_test)
  add_test(NAME agent_app_command_registry_test COMMAND agent_app_command_registry_test)
  add_test(NAME agent_model_unlimited_wait_contract COMMAND agent_local_model_test --unlimited-wait-contract)
  set_tests_properties(agent_model_unlimited_wait_contract PROPERTIES TIMEOUT 90)

  add_executable(agent_proof_review_replay
    agent_proof_review_replay.cpp
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(agent_proof_review_replay PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_proof_review_replay PRIVATE photo_engine Qt6::Core Qt6::Gui Qt6::Network)
  photo_editor_apply_warnings(agent_proof_review_replay)

  add_executable(agent_report_boundary_benchmark agent_report_boundary_benchmark.cpp)
  set_target_properties(agent_report_boundary_benchmark PROPERTIES AUTOMOC ON)
  target_link_libraries(agent_report_boundary_benchmark PRIVATE Qt6::Core Qt6::Qml)
  photo_editor_apply_warnings(agent_report_boundary_benchmark)

  add_executable(agent_runtime_paths_test agent_runtime_paths_test.cpp)
  target_include_directories(agent_runtime_paths_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_runtime_paths_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_runtime_paths_test)
  add_test(NAME agent_runtime_paths_test COMMAND agent_runtime_paths_test)

  add_executable(agent_model_artifact_test agent_model_artifact_test.cpp)
  target_include_directories(agent_model_artifact_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_model_artifact_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(agent_model_artifact_test)
  add_test(NAME agent_model_artifact_test COMMAND agent_model_artifact_test)

  add_executable(agent_model_download_test agent_model_download_test.cpp)
  target_include_directories(agent_model_download_test PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_model_download_test PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)
  photo_editor_apply_warnings(agent_model_download_test)
  add_test(NAME agent_model_download_test COMMAND agent_model_download_test)

  add_executable(search_model_install_test search_model_install_test.cpp)
  target_include_directories(search_model_install_test PRIVATE ../apps/desktop/src)
  target_link_libraries(search_model_install_test PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)
  photo_editor_apply_warnings(search_model_install_test)
  add_test(NAME search_model_install_test COMMAND search_model_install_test)
  set_tests_properties(search_model_install_test PROPERTIES TIMEOUT 120 LABELS "desktop;search;models")

  add_executable(agent_model_setup_dialog_test agent_model_setup_dialog_test.cpp)
  target_compile_definitions(agent_model_setup_dialog_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(agent_model_setup_dialog_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(agent_model_setup_dialog_test)
  add_test(NAME agent_model_setup_dialog_test COMMAND agent_model_setup_dialog_test)
  set_tests_properties(agent_model_setup_dialog_test PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;qml;ui-shard;accessibility;ui")

  add_executable(library_search_ui_test library_search_ui_test.cpp)
  target_compile_definitions(library_search_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(library_search_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(library_search_ui_test)
  add_test(NAME library_search_ui_test COMMAND library_search_ui_test)
  set_tests_properties(library_search_ui_test PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(library_selection_actions_ui_test library_selection_actions_ui_test.cpp)
  target_compile_definitions(library_selection_actions_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(library_selection_actions_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(library_selection_actions_ui_test)
  add_test(NAME library_selection_actions_ui_test COMMAND library_selection_actions_ui_test)
  set_tests_properties(library_selection_actions_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;library;ui;persistence")

  add_executable(search_edit_dialog_ui_test search_edit_dialog_ui_test.cpp)
  target_compile_definitions(search_edit_dialog_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(search_edit_dialog_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(search_edit_dialog_ui_test)
  add_test(NAME search_edit_dialog_ui_test COMMAND search_edit_dialog_ui_test)
  set_tests_properties(search_edit_dialog_ui_test PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;qml;ui-shard;accessibility;ui")

  add_executable(managed_agent_runtime_test managed_agent_runtime_test.cpp
    ../apps/desktop/src/managed_agent_runtime.h ../apps/desktop/src/managed_agent_runtime.cpp)
  set_target_properties(managed_agent_runtime_test PROPERTIES AUTOMOC ON)
  target_include_directories(managed_agent_runtime_test PRIVATE ../apps/desktop/src)
  target_link_libraries(managed_agent_runtime_test PRIVATE Qt6::Core Qt6::Network Qt6::Concurrent)
  photo_editor_apply_warnings(managed_agent_runtime_test)
  add_test(NAME managed_agent_runtime_test COMMAND managed_agent_runtime_test)

  add_executable(gaussian_scene_controller_test gaussian_scene_controller_test.cpp
    ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp)
  set_target_properties(gaussian_scene_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(gaussian_scene_controller_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_scene_controller_test PRIVATE Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(gaussian_scene_controller_test)
  add_test(NAME gaussian_scene_controller_test COMMAND gaussian_scene_controller_test)

  add_executable(gaussian_controller_lifetime_test gaussian_controller_lifetime_test.cpp
    ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp)
  set_target_properties(gaussian_controller_lifetime_test PROPERTIES AUTOMOC ON)
  target_include_directories(gaussian_controller_lifetime_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_controller_lifetime_test PRIVATE Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(gaussian_controller_lifetime_test)
  add_test(NAME gaussian_controller_lifetime_test COMMAND gaussian_controller_lifetime_test)
  set_tests_properties(gaussian_controller_lifetime_test PROPERTIES TIMEOUT 30)

  add_executable(preview_future_drain_test preview_future_drain_test.cpp)
  target_include_directories(preview_future_drain_test PRIVATE ../apps/desktop/src)
  target_link_libraries(preview_future_drain_test PRIVATE Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(preview_future_drain_test)
  add_test(NAME preview_future_drain_test COMMAND preview_future_drain_test)
  set_tests_properties(preview_future_drain_test PROPERTIES TIMEOUT 30)

  find_package(SQLite3 REQUIRED)
  if(NOT TARGET SQLite3::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
  endif()
  add_executable(visual_search_controller_test
    visual_search_controller_test.cpp
    ../apps/desktop/src/managed_agent_runtime.h
    ../apps/desktop/src/managed_agent_runtime.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ../apps/desktop/src/visual_search_index.cpp)
  set_target_properties(visual_search_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(visual_search_controller_test PRIVATE ../apps/desktop/src)
  target_link_libraries(visual_search_controller_test PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network SQLite3::SQLite3)
  photo_editor_link_private_visual_search_index(visual_search_controller_test)
  photo_editor_apply_warnings(visual_search_controller_test)
  add_test(NAME visual_search_controller_test COMMAND visual_search_controller_test)
  add_executable(visual_search_preparation_benchmark
    visual_search_preparation_benchmark.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ../apps/desktop/src/visual_search_index.cpp)
  set_target_properties(visual_search_preparation_benchmark PROPERTIES AUTOMOC ON)
  target_include_directories(visual_search_preparation_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(visual_search_preparation_benchmark PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network SQLite3::SQLite3)
  photo_editor_link_private_visual_search_index(visual_search_preparation_benchmark)
  photo_editor_apply_warnings(visual_search_preparation_benchmark)
  add_test(NAME visual_search_preparation_benchmark COMMAND visual_search_preparation_benchmark)
  set_tests_properties(visual_search_preparation_benchmark PROPERTIES TIMEOUT 180 LABELS "desktop;search;performance")
  add_test(NAME visual_search_preparation_contract
    COMMAND visual_search_preparation_benchmark visual-search-preparation-contract.json 16 --contracts)
  set_tests_properties(visual_search_preparation_contract PROPERTIES TIMEOUT 60 LABELS "desktop;search;cache")
  add_executable(visual_embedding_index_test visual_embedding_index_test.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  target_include_directories(visual_embedding_index_test PRIVATE ../apps/desktop/src)
  target_link_libraries(visual_embedding_index_test PRIVATE Qt6::Core SQLite3::SQLite3)
  photo_editor_apply_warnings(visual_embedding_index_test)
  add_test(NAME visual_embedding_index_test COMMAND visual_embedding_index_test)
  set_tests_properties(visual_embedding_index_test PROPERTIES TIMEOUT 30)
  add_executable(local_image_text_encoder_test local_image_text_encoder_test.cpp
    ../apps/desktop/src/local_image_text_encoder.h ../apps/desktop/src/local_image_text_encoder.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  add_executable(search_runtime_bundle_native_test search_runtime_bundle_native_test.cpp
    ../apps/desktop/src/search_runtime_bundle.cpp)
  target_include_directories(search_runtime_bundle_native_test PRIVATE ../apps/desktop/src)
  target_link_libraries(search_runtime_bundle_native_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(search_runtime_bundle_native_test)
  add_test(NAME search_runtime_bundle_native_test COMMAND search_runtime_bundle_native_test)
  set_target_properties(local_image_text_encoder_test PROPERTIES AUTOMOC ON)
  target_include_directories(local_image_text_encoder_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(local_image_text_encoder_test PRIVATE
    PHOTO_EDITOR_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
    PHOTO_EDITOR_TEST_PYTHON="${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}")
  target_link_libraries(local_image_text_encoder_test PRIVATE Qt6::Core SQLite3::SQLite3)
  photo_editor_apply_warnings(local_image_text_encoder_test)
  if(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE)
    add_test(NAME local_image_text_encoder_test COMMAND local_image_text_encoder_test)
    set_tests_properties(local_image_text_encoder_test PROPERTIES TIMEOUT 60)
  endif()
  add_executable(agent_region_pixel_probe agent_region_pixel_probe.cpp)
  add_executable(search_model_setup_controller_test search_model_setup_controller_test.cpp
    ../apps/desktop/src/search_model_setup_controller.h ../apps/desktop/src/search_model_setup_controller.cpp
    ../apps/desktop/src/local_similarity_search.h ../apps/desktop/src/local_similarity_search.cpp
    ../apps/desktop/src/local_image_text_encoder.h ../apps/desktop/src/local_image_text_encoder.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  set_target_properties(search_model_setup_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(search_model_setup_controller_test PRIVATE ../apps/desktop/src)
  target_link_libraries(search_model_setup_controller_test PRIVATE Qt6::Core Qt6::Concurrent Qt6::Network SQLite3::SQLite3)
  photo_editor_apply_warnings(search_model_setup_controller_test)
  add_test(NAME search_model_setup_controller_test COMMAND search_model_setup_controller_test)
  set_tests_properties(search_model_setup_controller_test PROPERTIES TIMEOUT 30 LABELS "desktop;search;models")
  add_executable(local_similarity_search_test local_similarity_search_test.cpp
    ../apps/desktop/src/local_similarity_search.h ../apps/desktop/src/local_similarity_search.cpp
    ../apps/desktop/src/local_image_text_encoder.h ../apps/desktop/src/local_image_text_encoder.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  set_target_properties(local_similarity_search_test PROPERTIES AUTOMOC ON)
  target_include_directories(local_similarity_search_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(local_similarity_search_test PRIVATE
    PHOTO_EDITOR_SOURCE_ROOT="${PROJECT_SOURCE_DIR}"
    PHOTO_EDITOR_TEST_PYTHON="${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}")
  target_link_libraries(local_similarity_search_test PRIVATE Qt6::Core Qt6::Concurrent SQLite3::SQLite3)
  photo_editor_apply_warnings(local_similarity_search_test)
  if(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE)
    add_test(NAME local_similarity_search_test COMMAND local_similarity_search_test)
    set_tests_properties(local_similarity_search_test PROPERTIES TIMEOUT 60)
  endif()
  target_link_libraries(agent_region_pixel_probe PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(agent_region_pixel_probe)

  add_executable(
    agent_live_model_smoke_test
    agent_live_model_smoke_test.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  target_include_directories(
    agent_live_model_smoke_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_live_model_smoke_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Network
      photo_engine)
  photo_editor_apply_warnings(agent_live_model_smoke_test)

  add_executable(agent_walkthrough_contract_test agent_walkthrough_contract_test.cpp)
  target_compile_definitions(
    agent_walkthrough_contract_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(agent_walkthrough_contract_test)

  add_test(NAME agent_walkthrough_contract_test COMMAND agent_walkthrough_contract_test)

  add_executable(
    agent_image_validation_test
    agent_image_validation_test.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp)
  target_include_directories(
    agent_image_validation_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    agent_image_validation_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(agent_image_validation_test)

  add_test(NAME agent_image_validation_test COMMAND agent_image_validation_test)

  add_executable(
    ai_model_runtime_bundle_test
    ai_model_runtime_bundle_test.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  target_include_directories(
    ai_model_runtime_bundle_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    ai_model_runtime_bundle_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(ai_model_runtime_bundle_test)

  add_test(NAME ai_model_runtime_bundle_test COMMAND ai_model_runtime_bundle_test)

  if(TARGET photo_editor_onnxruntime AND PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
    add_executable(
      ai_model_runtime_inference_test
      ai_model_runtime_inference_test.cpp
      ../apps/desktop/src/object_pick_policy.cpp
      ../apps/desktop/src/segmentation_result_schema.cpp
      ${photo_editor_desktop_ai_model_sources}
      ../apps/desktop/src/vision_segmentation.h
      ../apps/desktop/src/vision_segmentation_stub.cpp)
    target_include_directories(
      ai_model_runtime_inference_test
      PRIVATE
        ../apps/desktop/src)
    target_link_libraries(
      ai_model_runtime_inference_test
      PRIVATE
        Qt6::Core
        Qt6::Gui
        photo_editor_onnxruntime)
    target_compile_definitions(
      ai_model_runtime_inference_test
      PRIVATE
        PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE="${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}")
    photo_editor_apply_warnings(ai_model_runtime_inference_test)
    add_test(
      NAME ai_model_runtime_inference_test
      COMMAND ai_model_runtime_inference_test)
    set_tests_properties(
      ai_model_runtime_inference_test
      PROPERTIES LABELS "desktop;ai-edit;models;inference")
  endif()

  add_executable(
    agent_edit_controller_test
    agent_edit_controller_test.cpp
    search_edit_workflow_contract.cpp
    ../apps/desktop/src/search_edit_workflow.h
    ../apps/desktop/src/search_edit_workflow.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ../apps/desktop/src/visual_search_index.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.h
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_source_matte.h
    ../apps/desktop/src/timeline_source_matte.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(agent_edit_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    agent_edit_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    agent_edit_controller_test
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1
      PHOTO_EDITOR_PHOTOGRAPHER_CALIBRATION_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/agent/photographer-calibration-profile-v1.json")
  target_link_libraries(
    agent_edit_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Network
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  if(TARGET photo_editor_onnxruntime AND PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
    target_link_libraries(
      agent_edit_controller_test
      PRIVATE
        photo_editor_onnxruntime)
    target_compile_definitions(
      agent_edit_controller_test
      PRIVATE
        PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE="${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}")
  endif()
  photo_editor_apply_warnings(agent_edit_controller_test)
  target_link_libraries(agent_edit_controller_test PRIVATE SQLite3::SQLite3)
  photo_editor_link_private_visual_search_index(agent_edit_controller_test)

  add_test(NAME agent_edit_controller_test COMMAND agent_edit_controller_test)
  add_test(NAME search_edit_workflow_contract COMMAND agent_edit_controller_test --search-edit-contract)
  add_test(
    NAME agent_consistency_director_test
    COMMAND agent_edit_controller_test --consistency-director-contract)
  set_tests_properties(
    agent_consistency_director_test
    PROPERTIES
      TIMEOUT 15
      LABELS "desktop;ai-edit;agent;quality;consistency")
  add_test(
    NAME agent_suspend_resilience_test
    COMMAND agent_edit_controller_test --suspend-resilience-contract)
  add_test(
    NAME agent_approved_tool_schema_budget_test
    COMMAND agent_edit_controller_test --agent-approved-tool-schema-budget-contract)
  set_tests_properties(
    agent_suspend_resilience_test
    PROPERTIES
      TIMEOUT 15
      LABELS "desktop;ai-edit;agent;proof;resilience")
  set_tests_properties(
    agent_approved_tool_schema_budget_test
    PROPERTIES
      TIMEOUT 15
      LABELS "desktop;ai-edit;agent;prompt-budget")
  add_test(
    NAME agent_edit_controller_cancel_render_test
    COMMAND agent_edit_controller_test --cancel-render-contract)
  set_tests_properties(
    agent_edit_controller_cancel_render_test
    PROPERTIES TIMEOUT 15)

  add_executable(
    photo_automation_bridge_test
    photo_automation_bridge_test.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.h
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_automation_bridge.h
    ../apps/desktop/src/photo_automation_bridge.cpp
    ../apps/desktop/src/timeline_draft_lifecycle.h
    ../apps/desktop/src/timeline_draft_lifecycle.cpp
    ../apps/desktop/src/search_edit_workflow.h
    ../apps/desktop/src/search_edit_workflow.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ../apps/desktop/src/visual_search_index.cpp
    ../apps/desktop/src/photo_mcp_server.h
    ../apps/desktop/src/photo_mcp_server.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_graphics_track_compaction.cpp
    ../apps/desktop/src/timeline_audio_output.h
    ../apps/desktop/src/timeline_audio_output.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp)
  set_target_properties(photo_automation_bridge_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    photo_automation_bridge_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    photo_automation_bridge_test
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    photo_automation_bridge_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Network
      Qt6::Quick
      Qt6::Multimedia
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  if(TARGET photo_editor_onnxruntime AND PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
    target_link_libraries(
      photo_automation_bridge_test
      PRIVATE
        photo_editor_onnxruntime)
    target_compile_definitions(
      photo_automation_bridge_test
      PRIVATE
        PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE="${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}")
  endif()
  photo_editor_apply_warnings(photo_automation_bridge_test)
  target_link_libraries(photo_automation_bridge_test PRIVATE SQLite3::SQLite3)
  photo_editor_link_private_visual_search_index(photo_automation_bridge_test)
  if(APPLE)
    # This integration runner keeps its mock model, bridge, task controller,
    # and many scenario-local fixtures in one large main() function. Reserve
    # enough main-thread stack for the debug build as the scenarios grow.
    target_link_options(
      photo_automation_bridge_test
      PRIVATE
        "LINKER:-stack_size,0x2000000")
  elseif(MSVC)
    # Windows defaults executable stack reserve to 1 MiB; the monolithic
    # integration-test frame can exceed that before an individual test slice
    # reaches its own assertions.
    target_link_options(
      photo_automation_bridge_test
      PRIVATE
        "/STACK:33554432")
  endif()

  add_test(NAME photo_automation_bridge_test COMMAND photo_automation_bridge_test)
  add_test(NAME agent_mcp_discovery_contract_test COMMAND photo_automation_bridge_test --mcp-discovery-contract)
  set_tests_properties(agent_mcp_discovery_contract_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;agent;mcp;discovery")
  add_test(NAME timeline_render_jobs_inspection_test
    COMMAND photo_automation_bridge_test --timeline-render-jobs-inspection)
  set_tests_properties(timeline_render_jobs_inspection_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME timeline_used_media_inspection_test
    COMMAND photo_automation_bridge_test --timeline-used-media-inspection)
  set_tests_properties(timeline_used_media_inspection_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;media;inspection;read-only")
  add_test(NAME timeline_source_replacement_contract_test
    COMMAND photo_automation_bridge_test --timeline-source-replacement-contract)
  set_tests_properties(timeline_source_replacement_contract_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;source-replacement;undo;workflow")
  add_test(NAME timeline_grade_effect_command_scan_bypass_test
    COMMAND photo_automation_bridge_test --timeline-grade-effect-command-scan-bypass)
  set_tests_properties(timeline_grade_effect_command_scan_bypass_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;grade;effects;undo;revert;performance")
  add_test(NAME timeline_typed_mutation_projection_bypass_test
    COMMAND photo_automation_bridge_test --timeline-typed-mutation-projection-bypass)
  set_tests_properties(timeline_typed_mutation_projection_bypass_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;creator;retime;undo;revert;performance")
  add_test(NAME timeline_review_notes_inspection_test
    COMMAND photo_automation_bridge_test --timeline-review-notes-inspection)
  set_tests_properties(timeline_review_notes_inspection_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;review;inspection;notes")
  add_test(NAME timeline_passive_wheel_filter_contract_test
    COMMAND photo_automation_bridge_test --timeline-passive-wheel-filter-contract)
  set_tests_properties(timeline_passive_wheel_filter_contract_test PROPERTIES
    TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;input;diagnostic;read-only")
  add_test(NAME timeline_visible_proof_contract_test
    COMMAND photo_automation_bridge_test --timeline-visible-proof-contract)
  set_tests_properties(timeline_visible_proof_contract_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME timeline_visible_proof_cache_isolation_test
    COMMAND photo_automation_bridge_test --timeline-visible-proof-cache-isolation-contract)
  set_tests_properties(timeline_visible_proof_cache_isolation_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;proof;cache;isolation;read-only")
  add_test(NAME timeline_structure_inspection_test
    COMMAND photo_automation_bridge_test --timeline-structure-inspection)
  set_tests_properties(timeline_structure_inspection_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;inspection;read-only")
  add_test(NAME timeline_preview_at_time_contract_test
    COMMAND photo_automation_bridge_test --timeline-preview-at-time-contract)
  set_tests_properties(timeline_preview_at_time_contract_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;preview;navigation;read-only")
  add_test(NAME timeline_view_navigation_contract_test
    COMMAND photo_automation_bridge_test --timeline-view-navigation-contract)
  set_tests_properties(timeline_view_navigation_contract_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;preview;navigation;read-only")
  add_test(NAME timeline_h264_export_contract_test
    COMMAND photo_automation_bridge_test --timeline-h264-export-contract)
  set_tests_properties(timeline_h264_export_contract_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;export;h264;native-worker;automation")
  add_test(NAME timeline_export_integrity_domains_contract_test
    COMMAND photo_automation_bridge_test --timeline-export-integrity-domains-contract)
  set_tests_properties(timeline_export_integrity_domains_contract_test PROPERTIES
    TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;export;integrity;source;generated;automation")
  add_test(NAME timeline_agent_draft_batch_contract_test
    COMMAND photo_automation_bridge_test --timeline-agent-draft-batch-contract)
  set_tests_properties(timeline_agent_draft_batch_contract_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;batch;ownership;workflow")
  add_test(NAME timeline_draft_lifecycle_contract_test
    COMMAND photo_automation_bridge_test --timeline-draft-lifecycle-contract)
  set_tests_properties(timeline_draft_lifecycle_contract_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;navigation;workflow")
  add_test(NAME timeline_project_input_copy_contract_test
    COMMAND photo_automation_bridge_test --timeline-project-input-copy-contract)
  set_tests_properties(timeline_project_input_copy_contract_test PROPERTIES
    TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;project-import;workflow")
  add_test(NAME agent_internal_tools_test COMMAND photo_automation_bridge_test --internal-tools-contract)
  add_test(
    NAME agent_model_mcp_tool_rounds_contract
    COMMAND photo_automation_bridge_test --model-tool-rounds-contract)
  add_test(
    NAME agent_composition_inspection_routing_contract
    COMMAND photo_automation_bridge_test --composition-inspection-routing-contract)
  add_test(
    NAME agent_composition_chain_contract
    COMMAND photo_automation_bridge_test --composition-chain-contract)
  add_test(NAME agent_composition_ready_contract COMMAND photo_automation_bridge_test --composition-ready-contract)
  add_test(NAME agent_task_lifecycle_contract COMMAND photo_automation_bridge_test --task-lifecycle-contract)
  add_test(NAME timeline_task_tool_contract COMMAND photo_automation_bridge_test --timeline-task-tool-contract)
  add_test(NAME agent_revision_continuation_contract COMMAND photo_automation_bridge_test --revision-continuation-contract)
  add_test(NAME agent_fresh_planning_proof_contract COMMAND photo_automation_bridge_test --fresh-planning-proof-contract)
  add_test(NAME agent_planning_proof_conflict_contract COMMAND photo_automation_bridge_test --planning-proof-conflict-contract)
  add_test(NAME agent_planning_proof_cancel_contract COMMAND photo_automation_bridge_test --planning-proof-cancel-contract)
  add_test(NAME agent_planning_proof_failure_contract COMMAND photo_automation_bridge_test --planning-proof-failure-contract)
  add_test(NAME agent_composition_repair_contract COMMAND photo_automation_bridge_test --composition-repair-contract)
  add_test(NAME agent_composition_batch_review_contract COMMAND photo_automation_bridge_test --composition-batch-review-contract)
  add_test(NAME agent_composition_batch_invalid_terminal_contract COMMAND photo_automation_bridge_test --composition-batch-invalid-terminal-contract)
  add_test(NAME agent_composition_repair_cancel_contract COMMAND photo_automation_bridge_test --composition-repair-cancel-contract)
  add_test(NAME agent_composition_repair_conflict_contract COMMAND photo_automation_bridge_test --composition-repair-conflict-contract)
  add_test(NAME agent_composition_repair_proof_failure_contract COMMAND photo_automation_bridge_test --composition-repair-proof-failure-contract)
  add_test(NAME agent_composition_repair_rejected_critique_contract COMMAND photo_automation_bridge_test --composition-repair-rejected-critique-contract)
  set_tests_properties(agent_composition_repair_contract agent_composition_repair_cancel_contract
    agent_composition_repair_conflict_contract agent_composition_repair_proof_failure_contract
    agent_composition_repair_rejected_critique_contract agent_composition_batch_review_contract
    agent_composition_batch_invalid_terminal_contract
    PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  set_tests_properties(agent_fresh_planning_proof_contract agent_planning_proof_conflict_contract
    agent_planning_proof_cancel_contract agent_planning_proof_failure_contract PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  add_test(NAME agent_revision_cancel_contract COMMAND photo_automation_bridge_test --revision-cancel-contract)
  add_test(NAME agent_revision_early_cancel_contract COMMAND photo_automation_bridge_test --revision-early-cancel-contract)
  add_test(NAME agent_revision_failure_contract COMMAND photo_automation_bridge_test --revision-failure-contract)
  add_test(NAME agent_revision_conflict_contract COMMAND photo_automation_bridge_test --revision-conflict-contract)
  set_tests_properties(agent_revision_continuation_contract agent_revision_cancel_contract agent_revision_early_cancel_contract
    agent_revision_failure_contract agent_revision_conflict_contract PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  add_test(NAME agent_sync_task_lifecycle_contract COMMAND photo_automation_bridge_test --sync-task-lifecycle-contract)
  add_test(NAME agent_sync_task_cancel_contract COMMAND photo_automation_bridge_test --sync-task-cancel-contract)
  add_test(NAME agent_sync_task_failure_contract COMMAND photo_automation_bridge_test --sync-task-failure-contract)
  add_test(NAME agent_sync_task_admission_contract COMMAND photo_automation_bridge_test --sync-task-admission-contract)
  add_test(NAME agent_sync_task_conflict_contract COMMAND photo_automation_bridge_test --sync-task-conflict-contract)
  set_tests_properties(agent_sync_task_lifecycle_contract agent_sync_task_cancel_contract
    agent_sync_task_failure_contract agent_sync_task_admission_contract agent_sync_task_conflict_contract PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  add_test(NAME agent_fast_task_finalization_contract COMMAND agent_edit_controller_test --fast-task-finalization-contract)
  set_tests_properties(agent_fast_task_finalization_contract PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  add_test(NAME agent_long_task_contract COMMAND photo_automation_bridge_test --long-task-contract)
  add_test(NAME agent_long_task_cancel_contract COMMAND photo_automation_bridge_test --long-task-cancel-contract)
  add_test(NAME agent_repeated_tool_results_contract COMMAND photo_automation_bridge_test --repeated-tool-results-contract)
  add_test(NAME agent_tool_correction_contract COMMAND photo_automation_bridge_test --tool-correction-contract)
  add_test(NAME agent_tool_correction_repeat_contract COMMAND photo_automation_bridge_test --tool-correction-repeat-contract)
  add_test(NAME agent_tool_correction_cancel_contract COMMAND photo_automation_bridge_test --tool-correction-cancel-contract)
  add_test(NAME agent_tool_correction_unresolved_contract COMMAND photo_automation_bridge_test --tool-correction-unresolved-contract)
  add_test(NAME agent_tool_plan_validation_recovery_contract
    COMMAND photo_automation_bridge_test --tool-plan-validation-recovery-contract)
  add_test(NAME agent_tool_plan_validation_progress_contract
    COMMAND photo_automation_bridge_test --tool-plan-validation-progress-contract)
  add_test(NAME agent_tool_plan_validation_repeat_contract
    COMMAND photo_automation_bridge_test --tool-plan-validation-repeat-contract)
  set_tests_properties(agent_tool_plan_validation_recovery_contract
    agent_tool_plan_validation_progress_contract agent_tool_plan_validation_repeat_contract PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE)
  set_tests_properties(agent_long_task_contract agent_long_task_cancel_contract
    agent_repeated_tool_results_contract PROPERTIES TIMEOUT 180)
  add_test(NAME agent_task_cancel_contract COMMAND photo_automation_bridge_test --task-cancel-contract)
  add_test(NAME agent_task_failure_contract COMMAND photo_automation_bridge_test --task-failure-contract)
  add_test(NAME agent_composition_unlimited_wait_contract COMMAND photo_automation_bridge_test --composition-unlimited-wait-contract)
  set_tests_properties(agent_composition_unlimited_wait_contract PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
  add_test(NAME agent_composition_error_contract COMMAND photo_automation_bridge_test --composition-error-contract)
  add_test(NAME agent_composition_cancel_contract COMMAND photo_automation_bridge_test --composition-cancel-contract)
  add_test(NAME agent_composition_failed_call_contract COMMAND photo_automation_bridge_test --composition-failed-call-contract)
  add_test(NAME customer_automation_access_test COMMAND photo_automation_bridge_test --customer-access)
  add_test(NAME composition_tool_contract COMMAND photo_automation_bridge_test --composition-tools-contract)
  add_test(NAME search_edit_tool_contract COMMAND photo_automation_bridge_test --search-tools-contract)

  add_executable(
    catalog_asset_model_test
    catalog_asset_model_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  target_sources(catalog_asset_model_test PRIVATE
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp)
  set_target_properties(catalog_asset_model_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    catalog_asset_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    catalog_asset_model_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(catalog_asset_model_test)

  add_test(NAME catalog_asset_model_test COMMAND catalog_asset_model_test)
  target_sources(catalog_asset_model_test PRIVATE agent_prepared_canvas_import_contract.cpp)
  add_test(NAME agent_prepared_canvas_import_contract
    COMMAND catalog_asset_model_test --prepared-canvas-import-contract)
  set_tests_properties(agent_prepared_canvas_import_contract PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
  add_test(NAME agent_prepared_canvas_ownership_contract
    COMMAND catalog_asset_model_test --prepared-canvas-ownership-contract)
  set_tests_properties(agent_prepared_canvas_ownership_contract PROPERTIES
    TIMEOUT 360 RUN_SERIAL TRUE SKIP_RETURN_CODE 77)
  add_test(
    NAME catalog_incremental_timeline_model_contract
    COMMAND catalog_asset_model_test --incremental-timeline-model-contract)
  add_test(NAME catalog_gallery_projection_test COMMAND catalog_asset_model_test --gallery-projection-contract)
  add_test(NAME catalog_library_collection_test COMMAND catalog_asset_model_test --library-collection-contract)
  target_sources(catalog_asset_model_test PRIVATE
    catalog_similarity_search_contract.cpp
    ../apps/desktop/src/catalog_similarity_search.h ../apps/desktop/src/catalog_similarity_search.cpp
    ../apps/desktop/src/local_similarity_search.h ../apps/desktop/src/local_similarity_search.cpp
    ../apps/desktop/src/local_image_text_encoder.h ../apps/desktop/src/local_image_text_encoder.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  target_link_libraries(catalog_asset_model_test PRIVATE SQLite3::SQLite3)
  target_compile_definitions(catalog_asset_model_test PRIVATE PHOTO_EDITOR_TEST_PYTHON="${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}")
  if(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE)
    add_test(NAME catalog_similarity_search_contract COMMAND catalog_asset_model_test --similarity-binding-contract)
    set_tests_properties(catalog_similarity_search_contract PROPERTIES TIMEOUT 60)
  endif()
  add_test(NAME catalog_search_preview_contract COMMAND catalog_asset_model_test --search-preview-contract)
  set_tests_properties(catalog_search_preview_contract PROPERTIES TIMEOUT 60)

  add_executable(
    thumbnail_benchmark_driver
    thumbnail_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(thumbnail_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    thumbnail_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    thumbnail_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    thumbnail_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(thumbnail_benchmark_driver)

  # This driver includes the entire shell's scenario suite. Optimizing its
  # orchestration consumes many GiB and minutes without improving render tests;
  # keep the application/engine sources in this library fully optimized.
  function(photo_editor_add_shell_visual_resources target)
    set(test_character_art
      "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/agent-phosphor/compact-character-parts-v1.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/agent-phosphor/compact-crt-bezel-v1.png")
    foreach(character_asset IN LISTS test_character_art)
      get_filename_component(character_filename "${character_asset}" NAME)
      set_source_files_properties("${character_asset}" PROPERTIES QT_RESOURCE_ALIAS "${character_filename}")
    endforeach()
    qt_add_resources(${target} "looksy_compact_character"
      PREFIX "/photo-editor/agent" FILES ${test_character_art})
    set(test_loading_scenes
      "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/loading-scenes/library-scene-v2.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/loading-scenes/timeline-scene-v2.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/loading-scenes/startup-scene-v2.png")
    foreach(scene_asset IN LISTS test_loading_scenes)
      get_filename_component(scene_filename "${scene_asset}" NAME)
      set_source_files_properties("${scene_asset}" PROPERTIES QT_RESOURCE_ALIAS "${scene_filename}")
    endforeach()
    qt_add_resources(${target} "looksy_loading_scenes"
      PREFIX "/photo-editor/loading" FILES ${test_loading_scenes})
    set(test_agent_screen "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/looksy-agent-screen-v2.png")
    set_source_files_properties("${test_agent_screen}" PROPERTIES QT_RESOURCE_ALIAS looksy-agent-screen-v2.png)
    qt_add_resources(
      ${target}
      "looksy_agent_screen"
      PREFIX "/photo-editor/agent"
      FILES "${test_agent_screen}")
    set(test_agent_sprite "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/looksy-agent-sprite-sheet.png")
    set(test_rgb_orb "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/looksy-rgb-orb.png")
    set(test_app_mark "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/looksy-app-icon-master.png")
    set_source_files_properties("${test_app_mark}" PROPERTIES QT_RESOURCE_ALIAS looksy-app-mark.png)
    set(test_brand_logo_dark "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/brand/looksy-logo-dark.png")
    set(test_brand_logo_light "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/brand/looksy-logo-light.png")
    set(test_brand_wordmark_dark "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/brand/looksy-wordmark-dark.png")
    set(test_brand_wordmark_light "${CMAKE_CURRENT_SOURCE_DIR}/../apps/desktop/assets/brand/looksy-wordmark-light.png")
    set_source_files_properties("${test_agent_sprite}" PROPERTIES QT_RESOURCE_ALIAS looksy-agent-sprite-sheet.png)
    set_source_files_properties("${test_rgb_orb}" PROPERTIES QT_RESOURCE_ALIAS looksy-rgb-orb.png)
    set_source_files_properties("${test_brand_logo_dark}" PROPERTIES QT_RESOURCE_ALIAS looksy-logo-dark.png)
    set_source_files_properties("${test_brand_logo_light}" PROPERTIES QT_RESOURCE_ALIAS looksy-logo-light.png)
    set_source_files_properties("${test_brand_wordmark_dark}" PROPERTIES QT_RESOURCE_ALIAS looksy-wordmark-dark.png)
    set_source_files_properties("${test_brand_wordmark_light}" PROPERTIES QT_RESOURCE_ALIAS looksy-wordmark-light.png)
    qt_add_resources(
      ${target}
      "looksy_agent_sprite"
      PREFIX "/photo-editor/agent"
      FILES "${test_agent_sprite}")
    qt_add_resources(
      ${target}
      "looksy_brand_assets"
      PREFIX "/photo-editor/brand"
      FILES
        "${test_rgb_orb}"
        "${test_app_mark}"
        "${test_brand_logo_dark}"
        "${test_brand_logo_light}"
        "${test_brand_wordmark_dark}"
        "${test_brand_wordmark_light}")
    qt_add_shaders(${target} "shell_visual_test_shaders"
      BATCHABLE
      PREFIX "/photo-editor/shaders"
      BASE "../apps/desktop/shaders"
      FILES
        ../apps/desktop/shaders/agent_crt.vert
        ../apps/desktop/shaders/agent_crt.frag
        ../apps/desktop/shaders/agent_robot.vert
        ../apps/desktop/shaders/agent_robot.frag
        ../apps/desktop/shaders/optical_light.vert
        ../apps/desktop/shaders/optical_light.frag
        ../apps/desktop/shaders/carousel_color_vision.vert
        ../apps/desktop/shaders/carousel_color_vision.frag
        ../apps/desktop/shaders/timeline_perspective.vert
        ../apps/desktop/shaders/timeline_perspective.frag
        ../apps/desktop/shaders/timeline_source_matte.vert
        ../apps/desktop/shaders/timeline_source_matte.frag
        ../apps/desktop/shaders/timeline_blend_composite.vert
        ../apps/desktop/shaders/timeline_blend_composite.frag
        ../apps/desktop/shaders/timeline_desktop_light.vert
        ../apps/desktop/shaders/timeline_desktop_light.frag
        ../apps/desktop/shaders/timeline_scene_projection.vert
        ../apps/desktop/shaders/timeline_scene_projection.frag
        ../apps/desktop/shaders/timeline_effect_mask.vert
        ../apps/desktop/shaders/timeline_effect_mask.frag)
  endfunction()

  set_source_files_properties(desktop_shell_smoke_runner.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-O0>")
  add_library(
    desktop_shell_test_runtime STATIC
    desktop_shell_smoke_runner.cpp
    desktop_shell_smoke/source_contract.inc
    desktop_shell_smoke/board_sidebar_visual_proof.inc
    ../apps/desktop/src/library_workflow_controller.h
    ../apps/desktop/src/library_workflow_controller.cpp
    ../apps/desktop/src/library_original_image_provider.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ../apps/desktop/src/scope_plot_item.h
    ../apps/desktop/src/scope_plot_item.cpp
    ../apps/desktop/src/brush_live_preview_item.h
    ../apps/desktop/src/photo_text_document_style.h
    ../apps/desktop/src/brush_live_preview_item.cpp
    ../apps/desktop/src/tablet_input_area.h
    ../apps/desktop/src/tablet_input_area.cpp
    ../apps/desktop/src/editor_preview_texture_item.h
    ../apps/desktop/src/editor_preview_texture_item.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/editor_text_dimension_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/editor_preview_image_provider.h
    ../apps/desktop/src/editor_preview_image_provider.cpp
    ../apps/desktop/src/creative_example_thumbnail_service.h
    ../apps/desktop/src/creative_example_thumbnail_service.cpp
    ../apps/desktop/src/creative_example_thumbnail_image_provider.h
    ../apps/desktop/src/creative_example_thumbnail_image_provider.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/macos_updater.h
    ../apps/desktop/src/macos_updater_stub.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_graphics_track_compaction.cpp
    ../apps/desktop/src/timeline_audio_output.h
    ../apps/desktop/src/timeline_audio_output.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp)
  set_target_properties(desktop_shell_test_runtime PROPERTIES AUTOMOC ON)
  target_sources(desktop_shell_test_runtime PRIVATE
    ../apps/desktop/src/scene_video_recorder.cpp ../apps/desktop/src/scene_video_recorder.h ../apps/desktop/src/scene_library_controller.cpp ../apps/desktop/src/scene_library_controller.h)
  target_sources(desktop_shell_test_runtime PRIVATE
    ../apps/desktop/src/timeline_color_edit_controller.h
    ../apps/desktop/src/timeline_color_edit_controller.cpp)
  target_sources(desktop_shell_test_runtime PRIVATE
    ../apps/desktop/src/timeline_voiceover_recorder.h
    ../apps/desktop/src/timeline_voiceover_recorder.cpp)
  photo_editor_add_shell_visual_resources(desktop_shell_test_runtime)
  include(${PROJECT_SOURCE_DIR}/cmake/PresetPreview.cmake)
  photo_editor_add_preset_preview(desktop_shell_test_runtime)
  target_include_directories(
    desktop_shell_test_runtime
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    desktop_shell_test_runtime
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    desktop_shell_test_runtime
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Multimedia
      Qt6::Qml
      Qt6::Quick
      Qt6::QuickControls2
      Qt6::Widgets
      native_window_test_input
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine
      photo_rust_ffi)
  photo_editor_apply_warnings(desktop_shell_test_runtime)

  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    target_compile_definitions(desktop_shell_test_runtime PRIVATE
      PHOTO_EDITOR_TEST_NATIVE_PREVIEW_TEXTURE=1)
  endif()

  add_library(
    photo_composition_bridge_runtime STATIC
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_image_validation.h
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_memory.h
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/search_edit_workflow.h
    ../apps/desktop/src/search_edit_workflow.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ../apps/desktop/src/visual_search_index.cpp
    ../apps/desktop/src/photo_automation_bridge.h
    ../apps/desktop/src/photo_automation_bridge.cpp
    ../apps/desktop/src/photo_mcp_server.h
    ../apps/desktop/src/photo_mcp_server.cpp)
  set_target_properties(photo_composition_bridge_runtime PROPERTIES AUTOMOC ON)
  target_include_directories(photo_composition_bridge_runtime PRIVATE ../apps/desktop/src)
  target_link_libraries(
    photo_composition_bridge_runtime
    PRIVATE desktop_shell_test_runtime Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::Multimedia SQLite3::SQLite3 photo_engine)
  photo_editor_link_private_visual_search_index(photo_composition_bridge_runtime)
  photo_editor_apply_warnings(photo_composition_bridge_runtime)

  function(photo_editor_apply_desktop_shell_smoke_stack target)
    if(MSVC)
      # The shared smoke runner dispatches a large inlined proof suite. Its
      # stack frame exceeds Windows' 1 MiB executable default before the
      # selected shard can return, so reserve the same 32 MiB used by the
      # similarly large editor-preview integration test.
      target_link_options(${target} PRIVATE "/STACK:33554432")
    endif()
  endfunction()

  add_executable(desktop_shell_smoke_test desktop_shell_smoke_test.cpp)
  target_link_libraries(desktop_shell_smoke_test PRIVATE desktop_shell_test_runtime)
  photo_editor_apply_desktop_shell_smoke_stack(desktop_shell_smoke_test)
  photo_editor_apply_warnings(desktop_shell_smoke_test)

  add_test(NAME desktop_shell_smoke_test COMMAND desktop_shell_smoke_test)
  add_test(NAME preset_library_ui_test COMMAND desktop_shell_smoke_test)
  set_tests_properties(preset_library_ui_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_STOP_AFTER_PRESET_LIBRARY=1;QT_QPA_PLATFORM=offscreen"
    RESOURCE_LOCK photo_editor_desktop_runtime LABELS "desktop;preset;ui")
  add_executable(custom_preset_authoring_test custom_preset_authoring_test.cpp)
  add_executable(shoot_match_workflow_test shoot_match_workflow_test.cpp)
  target_include_directories(shoot_match_workflow_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(shoot_match_workflow_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(shoot_match_workflow_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(shoot_match_workflow_test)
  add_test(NAME shoot_match_workflow_test COMMAND shoot_match_workflow_test)
  add_test(NAME shoot_match_ui_test COMMAND shoot_match_workflow_test --ui)
  add_test(NAME shoot_match_real_photo_test COMMAND shoot_match_workflow_test --photo)
  set_tests_properties(shoot_match_workflow_test shoot_match_ui_test shoot_match_real_photo_test PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" LABELS "desktop;photo;matching")
  target_include_directories(custom_preset_authoring_test PRIVATE ../apps/desktop/src)
  target_link_libraries(custom_preset_authoring_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(custom_preset_authoring_test)
  add_test(NAME custom_preset_authoring_test COMMAND custom_preset_authoring_test)
  set_tests_properties(custom_preset_authoring_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    RESOURCE_LOCK photo_editor_desktop_runtime LABELS "desktop;preset")
  set_tests_properties(
    desktop_shell_smoke_test
    PROPERTIES
      TIMEOUT 3000
      SKIP_RETURN_CODE 77
      ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;manual-soak;monolithic;slow"
      RESOURCE_LOCK photo_editor_desktop_runtime
  )

  add_executable(
    timeline_ui_consistency_contract_test
    timeline_ui_consistency_contract_test.cpp)
  target_compile_definitions(
    timeline_ui_consistency_contract_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(timeline_ui_consistency_contract_test)
  add_test(
    NAME timeline_ui_consistency_contract_test
    COMMAND timeline_ui_consistency_contract_test)
  set_tests_properties(
    timeline_ui_consistency_contract_test
    PROPERTIES
      LABELS "desktop;timeline;contract"
  )

  add_executable(edit_identity_payload_cache_test edit_identity_payload_cache_test.cpp)
  target_include_directories(edit_identity_payload_cache_test PRIVATE ../apps/desktop/src)
  target_link_libraries(edit_identity_payload_cache_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(edit_identity_payload_cache_test)
  add_test(NAME edit_identity_payload_cache_test COMMAND edit_identity_payload_cache_test)
  set_tests_properties(edit_identity_payload_cache_test PROPERTIES LABELS "desktop;recipe;identity")

  function(add_desktop_shell_shard target stop_environment timeout labels)
    add_executable(${target} desktop_shell_shard_main.cpp)
    target_compile_definitions(
      ${target}
      PRIVATE
        PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV="${stop_environment}")
    target_link_libraries(${target} PRIVATE Qt6::Core desktop_shell_test_runtime)
    photo_editor_apply_desktop_shell_smoke_stack(${target})
    photo_editor_apply_warnings(${target})

    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(
      ${target}
      PROPERTIES
        TIMEOUT ${timeout}
        ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
        LABELS "${labels}")
  endfunction()

  add_desktop_shell_shard(
    desktop_shell_source_contract_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_COPY_VOCABULARY_SOURCE
    60
    "desktop;source-contract")
  add_desktop_shell_shard(
    desktop_shell_session_recovery_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_SESSION_RECOVERY
    60
    "desktop;ui-shard;recovery")
  add_desktop_shell_shard(
    desktop_shell_carousel_responsive_layout_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_RESPONSIVE_LAYOUT
    60
    "desktop;ui-shard;carousel;responsive")
  add_desktop_shell_shard(
    desktop_shell_carousel_locale_stress_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_LOCALE_STRESS
    120
    "desktop;ui-shard;carousel;locale;rtl")
  add_desktop_shell_shard(
    desktop_shell_carousel_visual_regression_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CAROUSEL_VISUAL_REGRESSION
    180
    "desktop;ui-shard;carousel;visual-regression")
  set_property(
    TEST desktop_shell_carousel_visual_regression_test
    APPEND PROPERTY ENVIRONMENT "QT_SCALE_FACTOR=2")
  add_desktop_shell_shard(
    desktop_shell_ai_edit_ux_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_AI_EDIT_UX
    120
    "desktop;ui-shard;ai-edit")
  add_desktop_shell_shard(
    desktop_shell_crash_recovery_ux_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CRASH_RECOVERY_UX
    120
    "desktop;ui-shard;recovery")
  add_desktop_shell_shard(
    desktop_shell_export_preflight_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_EXPORT_PREFLIGHT
    60
    "desktop;ui-shard;export;performance")
  add_desktop_shell_shard(
    desktop_shell_studio_polish_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_STUDIO_POLISH
    300
    "desktop;ui-shard;edit;timeline;accessibility;visual-proof")
  add_test(NAME desktop_shell_typography_inspector_test COMMAND desktop_shell_studio_polish_test)
  set_tests_properties(desktop_shell_typography_inspector_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_TYPOGRAPHY_ONLY=1"
    TIMEOUT 120 RUN_SERIAL TRUE LABELS "desktop;typography;ui-shard;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_timeline_workflow_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_WORKFLOW
    180
    "desktop;ui-shard;timeline;workflow;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_ui_primitives_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_UI_PRIMITIVES
    120
    "desktop;ui-shard;primitives;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_layout_visual_proof_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYOUT_VISUAL_PROOF
    120
    "desktop;ui-shard;library;import;edit;export;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_viewer_tools_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYOUT_VISUAL_PROOF
    90
    "desktop;ui-shard;scope;overlays;interaction;visual-proof")
  set_property(TEST desktop_shell_viewer_tools_test APPEND PROPERTY
    ENVIRONMENT "PHOTO_EDITOR_SMOKE_VIEWER_TOOLS_PROOF_ONLY=1")
  add_desktop_shell_shard(
    desktop_shell_real_shoot_workflow_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_REAL_SHOOT_WORKFLOW
    600
    "desktop;ui-shard;library;import;edit;export;recovery;real-media;workflow")
  set_tests_properties(desktop_shell_real_shoot_workflow_test PROPERTIES
    SKIP_RETURN_CODE 77 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_desktop_shell_shard(
    desktop_shell_library_collection_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LIBRARY_COLLECTIONS
    300
    "desktop;ui-shard;library;interaction;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_professional_library_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_PROFESSIONAL_LIBRARY
    120
    "desktop;ui-shard;library;metadata;organization;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_cull_workflow_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CULL_WORKFLOW
    120
    "desktop;ui-shard;library;culling;interaction;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_timeline_visual_review_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_VISUAL_REVIEW
    120
    "desktop;ui-shard;timeline;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_timeline_keyframe_workflow_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_VISUAL_REVIEW
    120
    "desktop;ui-shard;timeline;keyframes;visual-proof")
  set_property(TEST desktop_shell_timeline_keyframe_workflow_test APPEND PROPERTY
    ENVIRONMENT "PHOTO_EDITOR_SMOKE_TIMELINE_KEYFRAME_PROOF_ONLY=1")
  add_test(NAME desktop_shell_timeline_trim_workflow_test COMMAND desktop_shell_timeline_keyframe_workflow_test)
  set_tests_properties(desktop_shell_timeline_trim_workflow_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_TIMELINE_KEYFRAME_PROOF_ONLY=1;PHOTO_EDITOR_SMOKE_TIMELINE_TRIM_PROOF_ONLY=1"
    TIMEOUT 120 RUN_SERIAL TRUE LABELS "desktop;timeline;trim;workflow;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_import_preflight_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_PREFLIGHT
    60
    "desktop;ui-shard;import;performance")
  add_desktop_shell_shard(
    desktop_shell_import_safety_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_IMPORT_SAFETY
    180
    "desktop;ui-shard;import")
  add_desktop_shell_shard(
    desktop_shell_offline_recovery_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_OFFLINE_RECOVERY
    180
    "desktop;ui-shard;recovery")
  add_desktop_shell_shard(
    desktop_shell_delivery_export_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_DELIVERY_EXPORT
    180
    "desktop;ui-shard;export")
  add_desktop_shell_shard(
    desktop_shell_timeline_ui_proof_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_TIMELINE_UI_PROOF
    420
    "desktop;ui-shard;timeline;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_accessibility_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_ACCESSIBILITY
    180
    "desktop;ui-shard;accessibility")
  add_desktop_shell_shard(
    desktop_shell_edit_rail_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_EDIT_RAIL
    120
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_brush_workflow_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_BRUSH_WORKFLOW
    180
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_adjustments_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_ADJUST_INSPECTOR_BASELINE
    180
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_color_controls_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_COLOR_ALIGNMENT
    240
    "desktop;ui-shard;color;visual-proof")
  add_desktop_shell_shard(
    desktop_shell_photo_tools_test
    PHOTO_EDITOR_SMOKE_PHOTO_TOOLS_WORKFLOW
    180
    "desktop;ui-shard;crop;retouch;compare")
  add_desktop_shell_shard(
    desktop_shell_component_workspace_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_COMPONENT_WORKSPACE
    180
    "desktop;professional;ui-shard;layers;workflow")
  set_property(TEST desktop_shell_component_workspace_test APPEND PROPERTY
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  add_desktop_shell_shard(
    desktop_shell_mask_layer_interaction_matrix_test
    PHOTO_EDITOR_SMOKE_MASK_LAYER_INTERACTION_MATRIX
    240
    "desktop;professional;ui-shard;selection;layers;workflow")
  set_property(TEST desktop_shell_mask_layer_interaction_matrix_test APPEND PROPERTY
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  add_desktop_shell_shard(
    desktop_shell_crop_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_CROP_WORKFLOW
    180
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_selection_geometry_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_MASK_GEOMETRY_PROOFS
    240
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_tool_exit_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_TOOL_EXIT_PATHS
    420
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_retouch_hud_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_RETOUCH_HUD
    180
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_layer_hover_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_HOVER_LIGHTWEIGHT
    180
    "desktop;ui-shard")
  add_desktop_shell_shard(
    desktop_shell_layer_workspace_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_WORKSPACE
    300
    "desktop;ui-shard;layers;visual-proof")
  set_property(TEST desktop_shell_layer_workspace_test APPEND PROPERTY
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_desktop_shell_shard(
    desktop_shell_layer_row_interaction_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_ROW_INTERACTIONS
    180
    "desktop;ui-shard;layers;interaction")
  set_property(TEST desktop_shell_layer_row_interaction_test APPEND PROPERTY
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1")
  add_desktop_shell_shard(
    desktop_shell_mask_reselection_pointer_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_HOVER_LIGHTWEIGHT
    180
    "desktop;ui-shard;mask;pointer")
  target_compile_definitions(
    desktop_shell_mask_reselection_pointer_test
    PRIVATE PHOTO_EDITOR_SKIP_SHARED_SOURCE_CONTRACT_PREFLIGHT=1)
  set_tests_properties(desktop_shell_mask_reselection_pointer_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1")
  add_desktop_shell_shard(
    desktop_shell_selection_range_controls_main_test
    PHOTO_EDITOR_SMOKE_STOP_AFTER_LAYER_HOVER_LIGHTWEIGHT
    180
    "desktop;ui-shard;selection;range;pointer;workflow")
  target_compile_definitions(
    desktop_shell_selection_range_controls_main_test
    PRIVATE PHOTO_EDITOR_SKIP_SHARED_SOURCE_CONTRACT_PREFLIGHT=1)
  set_tests_properties(desktop_shell_selection_range_controls_main_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_SELECTION_RANGE_CONTROLS_MAIN=1")
  add_test(NAME desktop_shell_selection_followup_workflow_test COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_selection_followup_workflow_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;selection;layers;workflow;ui"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_SELECTION_FOLLOWUP_WORKFLOW=1")
  add_test(NAME desktop_shell_selection_cutout_workflow_test COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_selection_cutout_workflow_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;selection;cutout;workflow;ui"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_SELECTION_FOLLOWUP_WORKFLOW=1;PHOTO_EDITOR_SELECTION_CUTOUT_WORKFLOW=1")
  add_test(NAME desktop_shell_linear_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_linear_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=linear")
  add_test(NAME desktop_shell_compound_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_compound_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=compound")
  add_test(NAME desktop_shell_brush_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_brush_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=brush")
  add_test(NAME desktop_shell_generated_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_generated_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=generated")
  add_test(NAME desktop_shell_mask_asset_return_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_mask_asset_return_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=compound;PHOTO_EDITOR_MASK_POINTER_ASSET_RETURN=1")
  add_test(NAME desktop_shell_hidden_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_hidden_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_HIDDEN=1")
  add_test(NAME desktop_shell_layer_away_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_layer_away_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_LAYER_AWAY=1")
  add_test(NAME desktop_shell_locked_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_locked_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_LOCKED=1")
  add_test(NAME desktop_shell_renamed_layer_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_renamed_layer_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_LAYER_AWAY=1;PHOTO_EDITOR_MASK_POINTER_RENAMED_LAYER=1")
  add_test(NAME desktop_shell_collapsed_group_mask_reselection_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_collapsed_group_mask_reselection_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_COLLAPSED_GROUP=1")
  add_test(NAME desktop_shell_mask_refinement_narrow_layout_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_mask_refinement_narrow_layout_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_REFINE_NARROW=1")
  add_test(NAME desktop_shell_mask_refinement_cancel_worker_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_mask_refinement_cancel_worker_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_REFINE_CANCEL=1")
  add_test(NAME desktop_shell_mask_refinement_cancel_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_mask_refinement_cancel_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_REFINE_UI_CANCEL=1;PHOTO_EDITOR_REFINEMENT_RAPID_PAGES=1")
  add_test(NAME desktop_shell_generated_mask_refinement_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_generated_mask_refinement_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_REFINE_UI_CANCEL=1;PHOTO_EDITOR_REFINEMENT_RAPID_PAGES=1;PHOTO_EDITOR_MASK_POINTER_KIND=generated;PHOTO_EDITOR_REFINEMENT_PAGES_NARROW=1")
  add_test(NAME desktop_shell_luminance_mask_refinement_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_luminance_mask_refinement_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_REFINE_UI_CANCEL=1;PHOTO_EDITOR_REFINEMENT_RAPID_PAGES=1;PHOTO_EDITOR_MASK_POINTER_KIND=luminance;PHOTO_EDITOR_REFINEMENT_PAGES_NARROW=1")
  add_test(NAME desktop_shell_selection_brush_lasso_switch_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_selection_brush_lasso_switch_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;mask;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=generated;PHOTO_EDITOR_MASK_LASSO_SWITCH=1")
  add_test(NAME desktop_shell_selection_brush_lasso_cycle_smoke_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_selection_brush_lasso_cycle_smoke_test PROPERTIES
    TIMEOUT 300 LABELS "desktop;ui-shard;mask;pointer;stress-smoke"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=generated;PHOTO_EDITOR_MASK_LASSO_SWITCH=1;PHOTO_EDITOR_MASK_LASSO_CYCLES=2")
  add_test(NAME desktop_shell_selection_brush_lasso_stress_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_selection_brush_lasso_stress_test PROPERTIES
    TIMEOUT 2400 LABELS "desktop;ui-shard;mask;pointer;stress"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_MASK_POINTER_KIND=generated;PHOTO_EDITOR_MASK_LASSO_SWITCH=1;PHOTO_EDITOR_MASK_LASSO_CYCLES=100")
  add_test(NAME desktop_shell_editable_font_pointer_test
    COMMAND desktop_shell_mask_reselection_pointer_test)
  set_tests_properties(desktop_shell_editable_font_pointer_test PROPERTIES
    TIMEOUT 180 LABELS "desktop;ui-shard;text;pointer"
    ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_SMOKE_MASK_RESELECTION_POINTER=1;PHOTO_EDITOR_FONT_ENTRY_PROOF=1")

  add_executable(
    ux_representative_workflow_test
    ux_representative_workflow_test.cpp)
  target_include_directories(
    ux_representative_workflow_test
    PRIVATE
      ../apps/desktop/src
      ../engine/include)
  target_link_libraries(
    ux_representative_workflow_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      desktop_shell_test_runtime)
  photo_editor_apply_warnings(ux_representative_workflow_test)
  add_test(NAME ux_representative_workflow_test COMMAND ux_representative_workflow_test)
  set_tests_properties(
    ux_representative_workflow_test
    PROPERTIES
      TIMEOUT 120
      LABELS "desktop;ux-acceptance;representative-workflow"
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_executable(
    thumbnail_state_utils_test
    thumbnail_state_utils_test.cpp)
  target_compile_definitions(
    thumbnail_state_utils_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    thumbnail_state_utils_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  photo_editor_apply_warnings(thumbnail_state_utils_test)

  add_test(NAME thumbnail_state_utils_test COMMAND thumbnail_state_utils_test)

  add_executable(
    canvas_coordinate_mapper_test
    canvas_coordinate_mapper_test.cpp)
  target_compile_definitions(
    canvas_coordinate_mapper_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    canvas_coordinate_mapper_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  photo_editor_apply_warnings(canvas_coordinate_mapper_test)

  add_test(NAME canvas_coordinate_mapper_test COMMAND canvas_coordinate_mapper_test)

  add_executable(
    studio_bounded_input_test
    studio_bounded_input_test.cpp
    ../apps/desktop/src/scope_plot_item.h ../apps/desktop/src/scope_plot_item.cpp)
  target_include_directories(studio_bounded_input_test PRIVATE ../apps/desktop/src)
  add_executable(studio_target_identity_test studio_target_identity_test.cpp)
  target_compile_definitions(studio_target_identity_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(studio_target_identity_test PRIVATE Qt6::Core Qt6::Qml)
  photo_editor_apply_warnings(studio_target_identity_test)
  add_test(NAME studio_target_identity_test COMMAND studio_target_identity_test)
  set_target_properties(studio_bounded_input_test PROPERTIES AUTOMOC ON)
  target_compile_definitions(
    studio_bounded_input_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    studio_bounded_input_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick)
  photo_editor_apply_warnings(studio_bounded_input_test)
  add_test(NAME studio_bounded_input_test COMMAND studio_bounded_input_test)
  set_tests_properties(
    studio_bounded_input_test
    PROPERTIES LABELS "desktop;ui-shard;interaction;performance")

  add_executable(
    job_queue_model_test
    job_queue_model_test.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(job_queue_model_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    job_queue_model_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    job_queue_model_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(job_queue_model_test)

  add_test(NAME job_queue_model_test COMMAND job_queue_model_test)
  target_link_libraries(job_queue_model_test PRIVATE Qt6::Concurrent)

  add_executable(job_queue_loading_test job_queue_loading_test.cpp)
  add_executable(export_job_loading_test export_job_loading_test.cpp)
  target_include_directories(export_job_loading_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_job_loading_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent Qt6::Widgets)
  photo_editor_apply_warnings(export_job_loading_test)
  add_test(NAME export_job_loading_test COMMAND export_job_loading_test)
  set_tests_properties(export_job_loading_test PROPERTIES TIMEOUT 30 LABELS "desktop;loading;export")
  add_executable(job_runner_wake_test job_runner_wake_test.cpp)
  add_executable(job_runner_scan_test job_runner_scan_test.cpp)
  add_executable(job_recovery_status_test job_recovery_status_test.cpp)
  add_executable(export_admission_test export_admission_test.cpp)
  target_include_directories(export_admission_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_admission_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(export_admission_test)
  add_test(NAME export_admission_test COMMAND export_admission_test)
  set_tests_properties(export_admission_test PROPERTIES TIMEOUT 20 LABELS "desktop;loading;export")
  target_include_directories(job_recovery_status_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_recovery_status_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_recovery_status_test)
  add_test(NAME job_recovery_status_test COMMAND job_recovery_status_test)
  set_tests_properties(job_recovery_status_test PROPERTIES TIMEOUT 10 LABELS "desktop;loading;recovery")
  add_executable(job_store_concurrency_test job_store_concurrency_test.cpp)
  target_include_directories(job_store_concurrency_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_store_concurrency_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_store_concurrency_test)
  add_test(NAME job_store_concurrency_test COMMAND job_store_concurrency_test)
  set_tests_properties(job_store_concurrency_test PROPERTIES TIMEOUT 45 SKIP_RETURN_CODE 77 LABELS "desktop;jobs;recovery")
  add_executable(job_runner_startup_test job_runner_startup_test.cpp)
  target_include_directories(job_runner_startup_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_startup_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_runner_startup_test)
  add_test(NAME job_runner_startup_test COMMAND job_runner_startup_test)
  set_tests_properties(job_runner_startup_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "desktop;loading;jobs;recovery")
  target_include_directories(job_runner_scan_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_scan_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_runner_scan_test)
  add_test(NAME job_runner_scan_test COMMAND job_runner_scan_test)
  set_tests_properties(job_runner_scan_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "desktop;loading;jobs")
  target_include_directories(job_runner_wake_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_wake_test PRIVATE desktop_shell_test_runtime Qt6::Core)
  photo_editor_apply_warnings(job_runner_wake_test)
  add_test(NAME job_runner_wake_test COMMAND job_runner_wake_test)
  set_tests_properties(job_runner_wake_test PROPERTIES TIMEOUT 15 SKIP_RETURN_CODE 77 LABELS "desktop;loading;jobs")
  add_executable(job_admission_policy_test job_admission_policy_test.cpp)
  target_include_directories(job_admission_policy_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_admission_policy_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(job_admission_policy_test)
  add_test(NAME job_admission_policy_test COMMAND job_admission_policy_test)
  set_tests_properties(job_admission_policy_test PROPERTIES TIMEOUT 15 LABELS "desktop;jobs")
  add_executable(job_runner_admission_test job_runner_admission_test.cpp)
  target_include_directories(job_runner_admission_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_admission_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_runner_admission_test)
  add_test(NAME job_runner_admission_test COMMAND job_runner_admission_test)
  set_tests_properties(job_runner_admission_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "desktop;jobs;performance")
  add_executable(job_runner_attempt_test job_runner_attempt_test.cpp)
  target_include_directories(job_runner_attempt_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_attempt_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_runner_attempt_test)
  add_test(NAME job_runner_attempt_test COMMAND job_runner_attempt_test)
  set_tests_properties(job_runner_attempt_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "desktop;jobs")
  add_executable(job_runner_cancellation_test job_runner_cancellation_test.cpp)
  target_include_directories(job_runner_cancellation_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_runner_cancellation_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_runner_cancellation_test)
  add_test(NAME job_runner_cancellation_test COMMAND job_runner_cancellation_test)
  set_tests_properties(job_runner_cancellation_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "desktop;loading;jobs")
  target_include_directories(job_queue_loading_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_queue_loading_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
  photo_editor_apply_warnings(job_queue_loading_test)
  add_test(NAME job_queue_loading_test COMMAND job_queue_loading_test)
  set_tests_properties(job_queue_loading_test PROPERTIES TIMEOUT 30 LABELS "desktop;loading;recovery")

  add_executable(
    crash_recovery_relaunch_test
    crash_recovery_relaunch_test.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_recipe_v13_sources})
  set_target_properties(crash_recovery_relaunch_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    crash_recovery_relaunch_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    crash_recovery_relaunch_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(crash_recovery_relaunch_test)

  add_test(NAME crash_recovery_relaunch_test COMMAND crash_recovery_relaunch_test)
  target_link_libraries(crash_recovery_relaunch_test PRIVATE Qt6::Concurrent)

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
    mask_lasso_render_math_test
    mask_lasso_render_math_test.cpp)
  target_link_libraries(
    mask_lasso_render_math_test
    PRIVATE
      photo_engine)
  photo_editor_apply_warnings(mask_lasso_render_math_test)

  add_test(NAME mask_lasso_render_math_test COMMAND mask_lasso_render_math_test)

  add_executable(
    edit_recipe_state_test
    edit_recipe_state_test.cpp
    ../apps/desktop/src/edit_adjustment_patch.cpp
    ../apps/desktop/src/edit_history_snapshot_store.cpp
    ../apps/desktop/src/edit_recipe_content_validation.cpp
    ../apps/desktop/src/edit_recipe_curve_codec.cpp
    ../apps/desktop/src/edit_recipe_diff.cpp
    ../apps/desktop/src/edit_recipe_document_codec.cpp
    ../apps/desktop/src/edit_recipe_group_normalization.cpp
    ../apps/desktop/src/edit_recipe_history.cpp
    ../apps/desktop/src/edit_recipe_layer_codec.cpp
    ../apps/desktop/src/edit_recipe_mask_schema.h
    ../apps/desktop/src/edit_recipe_json_readers.cpp
    ../apps/desktop/src/edit_recipe_migration.cpp
    ../apps/desktop/src/edit_recipe_model_normalization.cpp
    ../apps/desktop/src/edit_recipe_profile_codec.cpp
    ../apps/desktop/src/edit_recipe_profile_validation.cpp
    ../apps/desktop/src/edit_recipe_sidecar.cpp
    ../apps/desktop/src/edit_recipe_validation.cpp)
  target_include_directories(
    edit_recipe_state_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    edit_recipe_state_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(edit_recipe_state_test)

  add_test(NAME edit_recipe_state_test COMMAND edit_recipe_state_test)

  add_executable(
    editor_preview_controller_test
    editor_preview_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  target_compile_definitions(
    editor_preview_controller_test
    PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  set_target_properties(editor_preview_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_controller_test)
  if(APPLE)
    # This legacy integration test intentionally exercises hundreds of full
    # render requests in one process. Keep its debug build from exhausting the
    # default 8 MiB main-thread stack as request contracts evolve.
    target_link_options(
      editor_preview_controller_test
      PRIVATE
        "LINKER:-stack_size,0x1000000")
  elseif(MSVC)
    # MSVC's debug frame for this monolithic test is about 16 MiB; Windows'
    # default 1 MiB stack overflows in the prologue before main can run.
    target_link_options(
      editor_preview_controller_test
      PRIVATE
        "/STACK:33554432")
  endif()

  add_test(NAME editor_preview_controller_test COMMAND editor_preview_controller_test)
  add_test(NAME photo_layer_style_duplicate_policy_test COMMAND editor_preview_controller_test)
  set_tests_properties(photo_layer_style_duplicate_policy_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;PHOTO_EDITOR_TEST_DUPLICATE_MASK_POLICY_ONLY=1"
      TIMEOUT 90 LABELS "desktop;professional;layer-style;mask;history")
  add_executable(editor_full_original_test editor_preview_controller_test.cpp)
  target_include_directories(editor_full_original_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_full_original_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(editor_full_original_test)
  if(APPLE)
    target_link_options(editor_full_original_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  add_test(NAME editor_full_original_test COMMAND editor_full_original_test)
  set_tests_properties(editor_full_original_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_FULL_ORIGINAL_ONLY=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90 RUN_SERIAL TRUE LABELS "desktop;authoring;original;undo;workflow")
  add_test(NAME editor_preview_cutout_adjustment_test COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_cutout_adjustment_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_CUTOUT_ADJUSTMENT_ONLY=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 90
    LABELS "desktop;preview;layers;cutout;regression")
  add_test(NAME editor_preview_native_draft_publish_test COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_native_draft_publish_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_NATIVE_DRAFT_PUBLISH_ONLY=1"
    TIMEOUT 30
    LABELS "desktop;preview;paint;typography;regression")
  add_test(NAME editor_preview_grading_reset_test COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_grading_reset_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_GRADING_RESET_ONLY=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 60
    RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;grading;undo")
  add_test(NAME editor_preview_photo_layer_group_test COMMAND editor_preview_controller_test)
  set_tests_properties(
    editor_preview_photo_layer_group_test
    PROPERTIES
      ENVIRONMENT "PHOTO_EDITOR_TEST_PHOTO_LAYER_GROUP_ONLY=1"
      TIMEOUT 60
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME editor_preview_live_brush_test COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_live_brush_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_LIVE_BRUSH_ONLY=1"
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(
    NAME editor_preview_color_tool_sampling_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_color_tool_sampling_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_COLOR_TOOL_SAMPLING_ONLY=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;color;sampling;regression")
  add_test(
    NAME editor_preview_monitor_prewarm_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_monitor_prewarm_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_MONITOR_PREWARM_ONLY=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;color;monitor;prewarm;regression")
  add_test(
    NAME editor_preview_display_p3_boundary_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_display_p3_boundary_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_DISPLAY_P3_BOUNDARY_ONLY=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;color;display;regression")
  add_test(
    NAME editor_preview_display_p3_boundary_cpu_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_display_p3_boundary_cpu_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_DISPLAY_P3_BOUNDARY_ONLY=1;PHOTO_EDITOR_DISABLE_CUDA_PREVIEW=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;color;display;cpu;regression")
  add_test(NAME editor_preview_brush_automask_idempotence_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_brush_automask_idempotence_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_BRUSH_AUTOMASK_IDEMPOTENCE_ONLY=1;QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;masks;performance;undo")
  add_test(NAME editor_preview_active_viewport_draft_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(editor_preview_active_viewport_draft_test PROPERTIES
    ENVIRONMENT "PHOTO_EDITOR_TEST_ACTIVE_VIEWPORT_DRAFT_ONLY=1"
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(
    NAME editor_preview_aces_brush_patch_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(
    editor_preview_aces_brush_patch_test
    PROPERTIES
      ENVIRONMENT "PHOTO_EDITOR_TEST_ACES_BRUSH_PATCH_ONLY=1"
      TIMEOUT 120
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(
    NAME application_capability_report_test
    COMMAND editor_preview_controller_test)
  set_tests_properties(
    application_capability_report_test
    PROPERTIES
      ENVIRONMENT "PHOTO_EDITOR_TEST_CAPABILITY_REPORT_ONLY=1"
      PASS_REGULAR_EXPRESSION "runtime_capability_report_status=valid"
      TIMEOUT 60
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)
  set_tests_properties(
    editor_preview_controller_test
    PROPERTIES
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime
  )
  if(MSVC)
    # This integration test processes many large previews serially in Debug;
    # leave enough time for the measured multi-minute Windows run to finish.
    set_tests_properties(editor_preview_controller_test PROPERTIES TIMEOUT 1200)
  else()
    set_tests_properties(editor_preview_controller_test PROPERTIES TIMEOUT 600)
  endif()

  add_executable(
    editor_preview_history_test
    editor_preview_history_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_history_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_history_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_history_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_history_test)
  add_test(NAME editor_preview_history_test COMMAND editor_preview_history_test)
  set_tests_properties(
    editor_preview_history_test
    PROPERTIES
      TIMEOUT 120
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_executable(
    editor_preview_feedback_test
    editor_preview_feedback_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_feedback_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_feedback_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_feedback_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_feedback_test)
  add_test(NAME editor_preview_feedback_test COMMAND editor_preview_feedback_test)
  set_tests_properties(
    editor_preview_feedback_test
    PROPERTIES
      TIMEOUT 120
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_executable(
    editor_preview_before_after_recipe_test
    editor_preview_before_after_recipe_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_before_after_recipe_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_before_after_recipe_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_before_after_recipe_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_before_after_recipe_test)

  add_test(
    NAME editor_preview_before_after_recipe_test
    COMMAND editor_preview_before_after_recipe_test)

  add_executable(
    rendering_migration_proof_test
    rendering_migration_proof_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ../apps/desktop/src/rendering_migration_asset_assessor.h
    ../apps/desktop/src/rendering_migration_asset_assessor.cpp
    ../apps/desktop/src/rendering_migration_batch_report.h
    ../apps/desktop/src/rendering_migration_batch_report.cpp
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(rendering_migration_proof_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    rendering_migration_proof_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    rendering_migration_proof_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(rendering_migration_proof_test)

  add_test(
    NAME rendering_migration_proof_test
    COMMAND rendering_migration_proof_test)
  set_tests_properties(
    rendering_migration_proof_test
    PROPERTIES
      SKIP_RETURN_CODE 77
      TIMEOUT 120
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_executable(
    rendering_migration_batch_report_test
    rendering_migration_batch_report_test.cpp
    ../apps/desktop/src/rendering_migration_evidence.h
    ../apps/desktop/src/rendering_migration_evidence.cpp
    ../apps/desktop/src/rendering_migration_batch_report.h
    ../apps/desktop/src/rendering_migration_batch_report.cpp
    ../apps/desktop/src/rendering_migration_batch_runner.h
    ../apps/desktop/src/rendering_migration_batch_runner.cpp
    ../apps/desktop/src/rendering_migration_chunk_receipt.h
    ../apps/desktop/src/rendering_migration_chunk_receipt.cpp)
  target_include_directories(
    rendering_migration_batch_report_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    rendering_migration_batch_report_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(
    rendering_migration_batch_report_test)

  add_test(
    NAME rendering_migration_batch_report_test
    COMMAND rendering_migration_batch_report_test)

  add_executable(
    editor_preview_coalesced_undo_test
    editor_preview_coalesced_undo_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_coalesced_undo_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_coalesced_undo_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_coalesced_undo_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_coalesced_undo_test)

  add_test(
    NAME editor_preview_coalesced_undo_test
    COMMAND editor_preview_coalesced_undo_test)

  add_executable(
    editor_preview_local_edit_target_test
    editor_preview_local_edit_target_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_local_edit_target_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_local_edit_target_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_local_edit_target_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_local_edit_target_test)

  add_test(
    NAME editor_preview_local_edit_target_test
    COMMAND editor_preview_local_edit_target_test)

  add_executable(
    editor_preview_pixel_zoom_test
    editor_preview_pixel_zoom_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/worker_queue_registry.h
    ../apps/desktop/src/worker_queue_registry.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_pixel_zoom_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_pixel_zoom_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_pixel_zoom_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  target_compile_definitions(
    editor_preview_pixel_zoom_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(editor_preview_pixel_zoom_test)

  add_test(NAME editor_preview_pixel_zoom_test COMMAND editor_preview_pixel_zoom_test)
  add_test(NAME editor_preview_resident_zoom_test COMMAND editor_preview_pixel_zoom_test --resident-zoom)
  add_test(NAME editor_preview_zoom_out_coverage_test COMMAND editor_preview_pixel_zoom_test --zoom-out-coverage)

  add_executable(
    editor_preview_smart_selection_stale_test
    editor_preview_smart_selection_stale_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_smart_selection_stale_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_smart_selection_stale_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_smart_selection_stale_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_smart_selection_stale_test)

  add_test(NAME editor_preview_smart_selection_stale_test COMMAND editor_preview_smart_selection_stale_test)

  add_executable(
    editor_preview_color_cache_test
    editor_preview_color_cache_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_color_cache_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_color_cache_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_color_cache_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_color_cache_test)

  add_test(NAME editor_preview_color_cache_test COMMAND editor_preview_color_cache_test)

  add_executable(
    editor_preview_layer_mask_history_test
    editor_preview_layer_mask_history_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_layer_mask_history_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_layer_mask_history_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_layer_mask_history_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_layer_mask_history_test)

  add_test(NAME editor_preview_layer_mask_history_test COMMAND editor_preview_layer_mask_history_test)

  add_executable(editor_authoring_sequence_test editor_authoring_sequence_test.cpp)
  target_include_directories(editor_authoring_sequence_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_authoring_sequence_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(editor_authoring_sequence_test)
  add_test(NAME editor_authoring_sequence_test COMMAND editor_authoring_sequence_test)
  set_tests_properties(editor_authoring_sequence_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 120 LABELS "desktop;authoring;workflow;exploration")

  add_executable(editor_mask_repair_workflow_test editor_mask_repair_workflow_test.cpp)
  target_include_directories(editor_mask_repair_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_mask_repair_workflow_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(editor_mask_repair_workflow_test)
  add_test(NAME editor_mask_repair_workflow_test COMMAND editor_mask_repair_workflow_test)
  set_tests_properties(editor_mask_repair_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 120 LABELS "desktop;authoring;selection;retouch;workflow")

  add_executable(photo_smart_object_test photo_smart_object_test.cpp ../apps/desktop/src/photo_smart_object.cpp)
  target_include_directories(photo_smart_object_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_smart_object_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(photo_smart_object_test)
  add_test(NAME photo_smart_object_test COMMAND photo_smart_object_test)

  add_executable(photo_vector_path_test photo_vector_path_test.cpp ../apps/desktop/src/photo_vector_path.cpp)
  target_include_directories(photo_vector_path_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_vector_path_test PRIVATE Qt6::Gui)
  photo_editor_apply_warnings(photo_vector_path_test)
  add_test(NAME photo_vector_path_test COMMAND photo_vector_path_test)

  add_executable(photo_vector_mask_test photo_vector_mask_test.cpp ../apps/desktop/src/photo_vector_mask.cpp ../apps/desktop/src/photo_vector_path.cpp)
  target_include_directories(photo_vector_mask_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_vector_mask_test PRIVATE Qt6::Gui)
  photo_editor_apply_warnings(photo_vector_mask_test)
  add_test(NAME photo_vector_mask_test COMMAND photo_vector_mask_test)

  add_executable(photo_smart_document_test photo_smart_document_test.cpp ../apps/desktop/src/photo_smart_document.cpp ../apps/desktop/src/photo_smart_object.cpp)
  target_include_directories(photo_smart_document_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_smart_document_test PRIVATE Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_smart_document_test)
  add_test(NAME photo_smart_document_test COMMAND photo_smart_document_test)

  add_executable(photo_component_object_table_test photo_component_object_table_test.cpp)
  target_include_directories(photo_component_object_table_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_component_object_table_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(photo_component_object_table_test)
  add_test(NAME photo_component_object_table_test COMMAND photo_component_object_table_test)

  add_executable(photo_component_asset_storage_test photo_component_asset_storage_test.cpp
      ../apps/desktop/src/recipe_asset_store.cpp)
  target_include_directories(photo_component_asset_storage_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_component_asset_storage_test PRIVATE Qt6::Core photo_engine)
  photo_editor_apply_warnings(photo_component_asset_storage_test)
  add_test(NAME photo_component_asset_storage_test COMMAND photo_component_asset_storage_test)

  add_executable(photo_smart_document_expansion_test photo_smart_document_expansion_test.cpp ../apps/desktop/src/photo_smart_document.cpp ../apps/desktop/src/photo_smart_document_expansion.cpp ../apps/desktop/src/photo_smart_object.cpp)
  target_include_directories(photo_smart_document_expansion_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_smart_document_expansion_test PRIVATE Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_smart_document_expansion_test)
  add_test(NAME photo_smart_document_expansion_test COMMAND photo_smart_document_expansion_test)

  add_executable(photo_vector_mask_rebase_test photo_vector_mask_rebase_test.cpp ../apps/desktop/src/photo_vector_mask_rebase.cpp ../apps/desktop/src/photo_vector_mask.cpp ../apps/desktop/src/photo_vector_path.cpp)
  target_include_directories(photo_vector_mask_rebase_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_vector_mask_rebase_test PRIVATE Qt6::Gui)
  photo_editor_apply_warnings(photo_vector_mask_rebase_test)
  add_test(NAME photo_vector_mask_rebase_test COMMAND photo_vector_mask_rebase_test)

  add_executable(photo_vector_path_io_test photo_vector_path_io_test.cpp ../apps/desktop/src/photo_vector_path_io.cpp ../apps/desktop/src/photo_vector_path.cpp)
  target_include_directories(photo_vector_path_io_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_vector_path_io_test PRIVATE Qt6::Gui)
  photo_editor_apply_warnings(photo_vector_path_io_test)
  add_test(NAME photo_vector_path_io_test COMMAND photo_vector_path_io_test)

  find_package(Qt6 REQUIRED COMPONENTS Test)
  foreach(professional_ui_test photo_pen_ui_test photo_blend_if_ui_test photo_smart_object_ui_test photo_vector_mask_ui_test photo_vector_mask_drag_ui_test photo_smart_document_ui_test photo_vector_path_file_ui_test photo_component_workspace_ui_test photo_layer_styles_ui_test photo_layer_style_canvas_ui_test photo_text_dimension_ui_test)
    add_executable(${professional_ui_test} ${professional_ui_test}.cpp)
    target_include_directories(${professional_ui_test} PRIVATE ../apps/desktop/src)
    target_compile_definitions(${professional_ui_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(${professional_ui_test} PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
    set_target_properties(${professional_ui_test} PROPERTIES AUTOMOC ON)
    photo_editor_apply_warnings(${professional_ui_test})
    add_test(NAME ${professional_ui_test} COMMAND ${professional_ui_test})
    set_tests_properties(${professional_ui_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 90 LABELS "desktop;professional;ui")
  endforeach()

  add_executable(photo_professional_tools_workflow_test photo_professional_tools_workflow_test.cpp)
  add_executable(live_scope_workflow_test live_scope_workflow_test.cpp)
  target_include_directories(live_scope_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(live_scope_workflow_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(live_scope_workflow_test)
  add_test(NAME live_scope_workflow_test COMMAND live_scope_workflow_test)
  set_tests_properties(live_scope_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 180 LABELS "desktop;scope;workflow")
  target_link_libraries(photo_layer_styles_ui_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Concurrent)
  target_link_libraries(photo_layer_style_canvas_ui_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Concurrent)
  target_link_libraries(photo_text_dimension_ui_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Concurrent)
  set_tests_properties(photo_layer_style_canvas_ui_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" TIMEOUT 180)
  set_tests_properties(photo_layer_styles_ui_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      TIMEOUT 180)
  target_include_directories(photo_professional_tools_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_professional_tools_workflow_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(photo_professional_tools_workflow_test)
  add_test(NAME photo_professional_tools_workflow_test COMMAND photo_professional_tools_workflow_test)
  set_tests_properties(photo_professional_tools_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" TIMEOUT 180 LABELS "desktop;professional;workflow")

  foreach(professional_workflow_test photo_vector_mask_workflow_test photo_smart_link_workflow_test photo_path_selection_async_test photo_smart_document_workflow_test photo_vector_svg_workflow_test photo_component_workspace_workflow_test photo_component_source_guard_test photo_component_retouch_workflow_test photo_component_raw_hdr_workflow_test hdr_retouch_workflow_test compound_mask_style_workflow_test photo_document_style_light_test photo_layer_style_crop_basis_test photo_layer_style_workflow_test photo_layer_style_persistence_test mask_layer_interaction_matrix_test component_selection_sampling_workflow_test component_range_mask_workflow_test component_edge_refinement_workflow_test selection_source_order_workflow_test ordinary_range_sampling_workflow_test layer_mask_preview_publication_workflow_test photo_text_dimension_workflow_test)
    add_executable(${professional_workflow_test} ${professional_workflow_test}.cpp)
    target_include_directories(${professional_workflow_test} PRIVATE ../apps/desktop/src)
    target_link_libraries(${professional_workflow_test} PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent Qt6::Test)
    photo_editor_apply_warnings(${professional_workflow_test})
    add_test(NAME ${professional_workflow_test} COMMAND ${professional_workflow_test})
    set_tests_properties(${professional_workflow_test} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" TIMEOUT 180 LABELS "desktop;professional;workflow")
  endforeach()

  target_compile_definitions(photo_text_dimension_workflow_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(photo_text_dimension_workflow_test PRIVATE Qt6::Qml Qt6::Quick)
  add_executable(photo_text_dimension_render_test photo_text_dimension_render_test.cpp
      ../apps/desktop/src/photo_text_dimension_mesh.cpp
      ../apps/desktop/src/photo_text_dimension_outline.cpp
      ../apps/desktop/src/photo_text_dimension_render.cpp)
  target_include_directories(photo_text_dimension_render_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_dimension_render_test PRIVATE Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_text_dimension_render_test)
  add_test(NAME photo_text_dimension_render_test COMMAND photo_text_dimension_render_test)
  set_tests_properties(photo_text_dimension_render_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "desktop;typography;render")

  add_executable(photo_text_dimension_outline_test photo_text_dimension_outline_test.cpp
      ../apps/desktop/src/photo_text_dimension_outline.cpp)
  target_include_directories(photo_text_dimension_outline_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_dimension_outline_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_dimension_outline_test)
  add_test(NAME photo_text_dimension_outline_test COMMAND photo_text_dimension_outline_test)
  set_tests_properties(photo_text_dimension_outline_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "desktop;typography;geometry")

  add_executable(photo_text_dimension_state_test photo_text_dimension_state_test.cpp)
  target_include_directories(photo_text_dimension_state_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_dimension_state_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_dimension_state_test)
  add_test(NAME photo_text_dimension_state_test COMMAND photo_text_dimension_state_test)
  set_tests_properties(photo_text_dimension_state_test PROPERTIES LABELS "desktop;typography;persistence")

  add_executable(photo_text_dimension_mesh_test photo_text_dimension_mesh_test.cpp
      ../apps/desktop/src/photo_text_dimension_mesh.cpp)
  target_include_directories(photo_text_dimension_mesh_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_text_dimension_mesh_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_text_dimension_mesh_test)
  add_test(NAME photo_text_dimension_mesh_test COMMAND photo_text_dimension_mesh_test)
  set_tests_properties(photo_text_dimension_mesh_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 LABELS "desktop;typography;geometry;render")

  add_executable(selection_cutout_export_test selection_cutout_export_test.cpp)
  target_include_directories(selection_cutout_export_test PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_cutout_export_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(selection_cutout_export_test)
  add_test(NAME selection_cutout_export_test COMMAND selection_cutout_export_test)
  set_tests_properties(selection_cutout_export_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 120 LABELS "desktop;selection;cutout;export;workflow")

  add_executable(selection_cutout_benchmark selection_cutout_benchmark.cpp)
  target_include_directories(selection_cutout_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_cutout_benchmark PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(selection_cutout_benchmark)

  add_executable(selection_mask_ui_test selection_mask_ui_test.cpp)
  # This aggregate correctness matrix includes hundreds of settled render and
  # history transitions. It is not a per-interaction latency benchmark.
  set_tests_properties(mask_layer_interaction_matrix_test PROPERTIES TIMEOUT 600)
  target_include_directories(selection_mask_ui_test PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_mask_ui_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  target_compile_definitions(selection_mask_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(selection_mask_ui_test)
  add_test(NAME selection_mask_ui_test COMMAND selection_mask_ui_test)
  set_tests_properties(selection_mask_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 120 LABELS "desktop;ui;selection;layers;workflow")

  add_executable(selection_range_controls_ui_test selection_range_controls_ui_test.cpp)
  target_include_directories(selection_range_controls_ui_test PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_range_controls_ui_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  target_compile_definitions(selection_range_controls_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(selection_range_controls_ui_test)
  add_test(NAME selection_range_controls_ui_test COMMAND selection_range_controls_ui_test)
  set_tests_properties(selection_range_controls_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" TIMEOUT 180 LABELS "desktop;ui;selection;range;workflow")

  add_executable(selection_layer_mask_workflow_test selection_layer_mask_workflow_test.cpp)
  target_include_directories(selection_layer_mask_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_layer_mask_workflow_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  target_compile_definitions(selection_layer_mask_workflow_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(selection_layer_mask_workflow_test)
  add_test(NAME selection_layer_mask_workflow_test COMMAND selection_layer_mask_workflow_test)
  set_tests_properties(selection_layer_mask_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 180 LABELS "desktop;authoring;selection;layers;workflow")

  add_executable(selection_tool_composition_test selection_tool_composition_test.cpp)
  target_compile_definitions(selection_tool_composition_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_include_directories(selection_tool_composition_test PRIVATE ../apps/desktop/src)
  target_link_libraries(selection_tool_composition_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Concurrent)
  photo_editor_apply_warnings(selection_tool_composition_test)
  add_test(NAME selection_tool_composition_test COMMAND selection_tool_composition_test)
  set_tests_properties(selection_tool_composition_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    TIMEOUT 120 LABELS "desktop;authoring;selection;workflow")

  add_executable(editor_real_inpaint_workflow_test editor_real_inpaint_workflow_test.cpp)
  target_include_directories(editor_real_inpaint_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_real_inpaint_workflow_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
  if(PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR)
    target_compile_definitions(editor_real_inpaint_workflow_test PRIVATE
      PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE="${PHOTO_EDITOR_BUNDLED_AI_MODELS_DIR}")
    add_test(NAME editor_real_inpaint_workflow_test COMMAND editor_real_inpaint_workflow_test
      "${CMAKE_SOURCE_DIR}/marketing/media/stock/landscape-mountain.jpg")
    set_tests_properties(editor_real_inpaint_workflow_test PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE
      LABELS "desktop;authoring;retouch;models;inference")
  endif()
  photo_editor_apply_warnings(editor_real_inpaint_workflow_test)
  if(TARGET photo_editor_onnxruntime)
    # Controller/native fixtures must exercise the same inference implementation
    # as the application, rather than quietly compiling the unavailable stub.
    target_link_libraries(desktop_shell_test_runtime PRIVATE photo_editor_onnxruntime)
  endif()

  add_executable(
    editor_preview_outline_trace_test
    editor_preview_outline_trace_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_outline_trace_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_outline_trace_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    editor_preview_outline_trace_test
    PRIVATE
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    editor_preview_outline_trace_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_outline_trace_test)

  add_test(NAME editor_preview_outline_trace_test COMMAND editor_preview_outline_trace_test)
  # This test enforces a 16 ms scheduling budget and must not compete with
  # unrelated render processes for CPU time.
  set_tests_properties(editor_preview_outline_trace_test PROPERTIES RUN_SERIAL TRUE)

  add_executable(
    editor_preview_layer_adjustment_test
    editor_preview_layer_adjustment_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(editor_preview_layer_adjustment_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    editor_preview_layer_adjustment_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    editor_preview_layer_adjustment_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(editor_preview_layer_adjustment_test)

  add_test(NAME editor_preview_layer_adjustment_test COMMAND editor_preview_layer_adjustment_test)
  # This test asserts a 100 ms thumbnail latency budget. CPU contention from
  # unrelated render tests makes that performance contract nondeterministic.
  set_tests_properties(editor_preview_layer_adjustment_test PROPERTIES RUN_SERIAL TRUE)

  add_executable(
    preview_benchmark_driver
    preview_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(preview_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    preview_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    preview_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    preview_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(preview_benchmark_driver)

  add_executable(
    interaction_workflow_benchmark_driver
    interaction_workflow_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ${photo_editor_desktop_ai_model_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/vision_segmentation.h
    ../apps/desktop/src/vision_segmentation_stub.cpp)
  set_target_properties(interaction_workflow_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    interaction_workflow_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    interaction_workflow_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    interaction_workflow_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(interaction_workflow_benchmark_driver)
  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    target_compile_definitions(interaction_workflow_benchmark_driver PRIVATE
      PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND=1)
    target_sources(interaction_workflow_benchmark_driver PRIVATE native_preview_readback.mm)
    set_source_files_properties(native_preview_readback.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(interaction_workflow_benchmark_driver PRIVATE "-framework Metal")
    add_test(NAME local_mask_native_interaction_test COMMAND interaction_workflow_benchmark_driver
      --local-temperature --iterations 3 --timeout-ms 30000)
    set_tests_properties(local_mask_native_interaction_test PROPERTIES TIMEOUT 90
      ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1;PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
      LABELS "desktop;macos;metal;mask;preview")
    foreach(color_control IN ITEMS curve saturation temperature tint)
      add_test(NAME global_${color_control}_native_parity_test
        COMMAND interaction_workflow_benchmark_driver --global-color-preview
          --color-control ${color_control} --iterations 1 --timeout-ms 30000)
      set_tests_properties(global_${color_control}_native_parity_test PROPERTIES
        ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1" TIMEOUT 120)
    endforeach()
    add_test(NAME global_slider_mask_native_test COMMAND interaction_workflow_benchmark_driver
      --global-mask-preview --timeout-ms 30000)
    set_tests_properties(global_slider_mask_native_test PROPERTIES TIMEOUT 120
      ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1;PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
      LABELS "desktop;macos;metal;mask;preview;color")
    add_test(NAME local_mask_native_cropped_interaction_test COMMAND interaction_workflow_benchmark_driver
      --local-temperature --local-crop --iterations 3 --timeout-ms 30000)
    set_tests_properties(local_mask_native_cropped_interaction_test PROPERTIES TIMEOUT 90
      ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1;PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
      LABELS "desktop;macos;metal;mask;preview")
    foreach(control IN ITEMS opacity expansion)
      add_test(NAME local_mask_native_${control}_test COMMAND interaction_workflow_benchmark_driver
        --local-${control} --iterations 3 --timeout-ms 30000)
      set_tests_properties(local_mask_native_${control}_test PROPERTIES TIMEOUT 120
        ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1;PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
        LABELS "desktop;macos;metal;mask;preview")
    endforeach()
    foreach(delivery IN ITEMS native provider)
      add_test(NAME initial_selection_${delivery}_retry_test COMMAND interaction_workflow_benchmark_driver
        --initial-selection-retry --timeout-ms 30000)
      if(delivery STREQUAL "native")
        set(native_delivery 1)
      else()
        set(native_delivery 0)
      endif()
      set_tests_properties(initial_selection_${delivery}_retry_test PROPERTIES TIMEOUT 90
        ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=${native_delivery};PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
        LABELS "desktop;macos;mask;preview;recovery")
      add_test(NAME initial_selection_${delivery}_warm_retry_test COMMAND interaction_workflow_benchmark_driver
        --initial-selection-retry --warm-first --small-fixture --timeout-ms 30000)
      set_tests_properties(initial_selection_${delivery}_warm_retry_test PROPERTIES TIMEOUT 90
        ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=${native_delivery};PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
        LABELS "desktop;macos;mask;preview;loading;recovery")
    endforeach()
    add_test(NAME initial_selection_native_small_retry_test COMMAND interaction_workflow_benchmark_driver
      --initial-selection-retry --small-fixture --timeout-ms 30000)
    set_tests_properties(initial_selection_native_small_retry_test PROPERTIES TIMEOUT 90
      ENVIRONMENT "PHOTO_EDITOR_BENCHMARK_NATIVE_PREVIEW=1;PHOTO_EDITOR_BENCHMARK_SCOPES_VISIBLE=0"
      LABELS "desktop;macos;mask;preview;recovery")
  endif()

  add_executable(
    actual_app_workflow_benchmark_driver
    actual_app_workflow_benchmark_driver.cpp
    ../apps/desktop/src/search_edit_workflow.h
    ../apps/desktop/src/search_edit_workflow.cpp
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.cpp
    ../apps/desktop/src/visual_search_index.cpp
    ../apps/desktop/src/visual_search_controller.h
    ../apps/desktop/src/visual_search_controller.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/app_shell_controller.h
    ../apps/desktop/src/app_shell_controller.cpp
    ../apps/desktop/src/scope_plot_item.h
    ../apps/desktop/src/scope_plot_item.cpp
    ../apps/desktop/src/brush_live_preview_item.h
    ../apps/desktop/src/photo_text_document_style.h
    ../apps/desktop/src/brush_live_preview_item.cpp
    ../apps/desktop/src/tablet_input_area.h
    ../apps/desktop/src/tablet_input_area.cpp
    ../apps/desktop/src/editor_preview_texture_item.h
    ../apps/desktop/src/editor_preview_texture_item.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/catalog_asset_model.h
    ../apps/desktop/src/catalog_asset_model.cpp
    ../apps/desktop/src/catalog_duplicate_analysis.cpp
    ${photo_editor_desktop_brush_preview_sources}
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/editor_preview_controller.h
    ../apps/desktop/src/editor_preview_controller.cpp
    ../apps/desktop/src/editor_selection_controller.cpp
    ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp
    ../apps/desktop/src/preset_registry.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ../apps/desktop/src/histogram_scope_service.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_presentation_service.cpp
    ../apps/desktop/src/editor_preview_image_provider.h
    ../apps/desktop/src/editor_preview_image_provider.cpp
    ${photo_editor_desktop_ai_model_sources}
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/macos_updater.h
    ../apps/desktop/src/macos_updater_stub.cpp
    ../apps/desktop/src/job_queue_model.h
    ../apps/desktop/src/job_queue_model.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ../apps/desktop/src/preview_backend_controller.h
    ../apps/desktop/src/preview_backend_controller.cpp
    ../apps/desktop/src/qml_performance_monitor.h
    ../apps/desktop/src/qml_performance_monitor.cpp
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_graphics_track_compaction.cpp
    ../apps/desktop/src/timeline_audio_output.h
    ../apps/desktop/src/timeline_audio_output.cpp
    ../apps/desktop/src/timeline_playback_controller.h
    ../apps/desktop/src/timeline_playback_controller.cpp
    ../apps/desktop/src/recent_user_input_filter.h)
  set_target_properties(actual_app_workflow_benchmark_driver PROPERTIES AUTOMOC ON)
  target_sources(actual_app_workflow_benchmark_driver PRIVATE
    ../apps/desktop/src/scene_library_controller.h ../apps/desktop/src/scene_library_controller.cpp
    ../apps/desktop/src/scene_video_recorder.h ../apps/desktop/src/scene_video_recorder.cpp
    ../apps/desktop/src/depth_motion_controller.h ../apps/desktop/src/depth_motion_controller.cpp
    ../apps/desktop/src/depth_point_cloud_viewport.h ../apps/desktop/src/depth_point_cloud_viewport.cpp)
  target_sources(actual_app_workflow_benchmark_driver PRIVATE
    ../apps/desktop/src/timeline_color_edit_controller.h
    ../apps/desktop/src/timeline_color_edit_controller.cpp)
  target_sources(actual_app_workflow_benchmark_driver PRIVATE
    ../apps/desktop/src/creative_example_thumbnail_service.cpp
    ../apps/desktop/src/creative_example_thumbnail_image_provider.cpp)
  photo_editor_add_shell_visual_resources(actual_app_workflow_benchmark_driver)
  photo_editor_add_preset_preview(actual_app_workflow_benchmark_driver)
  target_sources(actual_app_workflow_benchmark_driver PRIVATE
    ../apps/desktop/src/search_model_setup_controller.h ../apps/desktop/src/search_model_setup_controller.cpp
    ../apps/desktop/src/search_runtime_bundle.cpp
    ../apps/desktop/src/catalog_similarity_search.h ../apps/desktop/src/catalog_similarity_search.cpp
    ../apps/desktop/src/local_similarity_search.h ../apps/desktop/src/local_similarity_search.cpp
    ../apps/desktop/src/local_image_text_encoder.h ../apps/desktop/src/local_image_text_encoder.cpp
    ../apps/desktop/src/visual_embedding_index.cpp)
  if(APPLE)
    # This actual-app harness must exercise real semantic segmentation. The
    # shared controller test source set deliberately uses a deterministic stub.
    get_target_property(actual_app_proof_sources actual_app_workflow_benchmark_driver SOURCES)
    list(REMOVE_ITEM actual_app_proof_sources ../apps/desktop/src/vision_segmentation_stub.cpp)
    set_property(TARGET actual_app_workflow_benchmark_driver PROPERTY SOURCES "${actual_app_proof_sources}")
    target_sources(actual_app_workflow_benchmark_driver PRIVATE ../apps/desktop/src/vision_segmentation.mm)
    set_source_files_properties(../apps/desktop/src/vision_segmentation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(actual_app_workflow_benchmark_driver PRIVATE "-framework Vision" "-framework CoreVideo")
  endif()
  target_include_directories(
    actual_app_workflow_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    actual_app_workflow_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1
      PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    actual_app_workflow_benchmark_driver
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Multimedia
      Qt6::Qml
      Qt6::Quick
      Qt6::QuickControls2
      Qt6::Widgets
      ${photo_editor_desktop_brush_preview_frameworks}
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine
      photo_rust_ffi)
  photo_editor_link_private_visual_search_index(actual_app_workflow_benchmark_driver)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  target_link_libraries(actual_app_workflow_benchmark_driver PRIVATE Qt6::Test)
if(TARGET Qt6::Qml AND TARGET Qt6::Quick AND TARGET Qt6::QuickControls2)
find_package(Qt6 REQUIRED COMPONENTS Test)
add_executable(timeline_scene_projection_effect_test timeline_scene_projection_effect_test.cpp)
set_target_properties(timeline_scene_projection_effect_test PROPERTIES AUTOMOC ON)
target_compile_definitions(timeline_scene_projection_effect_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(timeline_scene_projection_effect_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
qt_add_shaders(timeline_scene_projection_effect_test "timeline_scene_test_shaders"
  BATCHABLE PREFIX "/photo-editor/shaders" BASE "../apps/desktop/shaders"
  FILES
    ../apps/desktop/shaders/timeline_scene_projection.vert
    ../apps/desktop/shaders/timeline_scene_projection.frag
    ../apps/desktop/shaders/timeline_perspective.vert
    ../apps/desktop/shaders/timeline_perspective.frag)
photo_editor_apply_warnings(timeline_scene_projection_effect_test)
add_test(NAME timeline_scene_projection_effect_test COMMAND timeline_scene_projection_effect_test)
set_tests_properties(timeline_scene_projection_effect_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;scene;ui")
add_executable(timeline_tracking_panel_ui_test timeline_tracking_panel_ui_test.cpp)
set_target_properties(timeline_tracking_panel_ui_test PROPERTIES AUTOMOC ON)
target_compile_definitions(
  timeline_tracking_panel_ui_test
  PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(
  timeline_tracking_panel_ui_test
  PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
photo_editor_apply_warnings(timeline_tracking_panel_ui_test)
add_test(NAME timeline_tracking_panel_ui_test COMMAND timeline_tracking_panel_ui_test)
set_tests_properties(
  timeline_tracking_panel_ui_test
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
             TIMEOUT 60 LABELS "desktop;timeline;ui")
endif()
  add_test(NAME catalog_asset_activation_interaction_test
    COMMAND actual_app_workflow_benchmark_driver --asset-selection-proof --asset-count 20)
  set_tests_properties(catalog_asset_activation_interaction_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;library;carousel;interaction" RUN_SERIAL TRUE)
  add_test(NAME catalog_recipe_readiness_activation_test
    COMMAND actual_app_workflow_benchmark_driver --asset-selection-proof --recipe-selection-only --asset-count 20)
  set_tests_properties(catalog_recipe_readiness_activation_test PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;library;recipe;interaction;regression" RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME catalog_large_recipe_selection_test
    COMMAND actual_app_workflow_benchmark_driver --asset-selection-proof --selection-only
      --selection-recipe-mb 64 --selection-budget-ms 50 --asset-count 20)
  set_tests_properties(catalog_large_recipe_selection_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;library;interaction;performance" RUN_SERIAL TRUE)
  add_test(NAME desktop_layer_drop_indicator_test
    COMMAND actual_app_workflow_benchmark_driver --layer-drop-indicator-proof)
  set_tests_properties(desktop_layer_drop_indicator_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;layers;interaction" RUN_SERIAL TRUE)
  add_test(NAME desktop_layer_move_test
    COMMAND actual_app_workflow_benchmark_driver --layer-move-proof)
  set_tests_properties(desktop_layer_move_test PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;layers;interaction" RUN_SERIAL TRUE)
  add_test(NAME desktop_brush_fidelity_test COMMAND actual_app_workflow_benchmark_driver --brush-fidelity-proof)
  set_tests_properties(desktop_brush_fidelity_test PROPERTIES TIMEOUT 90 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;brush;mask;interaction")

  add_test(NAME desktop_direct_cutout_color_test
    COMMAND actual_app_workflow_benchmark_driver --direct-cutout-color-proof)
  set_tests_properties(desktop_direct_cutout_color_test PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;cutout;colors;interaction" RUN_SERIAL TRUE)
  add_executable(section_comparison_interaction_test section_comparison_interaction_test.cpp)
  target_compile_definitions(section_comparison_interaction_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(section_comparison_interaction_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(section_comparison_interaction_test)
  add_test(NAME section_comparison_interaction_test COMMAND section_comparison_interaction_test)
  set_tests_properties(section_comparison_interaction_test PROPERTIES
    TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(exposure_slider_interaction_test exposure_slider_interaction_test.cpp)
  target_compile_definitions(exposure_slider_interaction_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(exposure_slider_interaction_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(exposure_slider_interaction_test)
  add_test(NAME exposure_slider_interaction_test COMMAND exposure_slider_interaction_test)
  set_tests_properties(exposure_slider_interaction_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;inspector;interaction")

  add_executable(adjustment_pad_interaction_test adjustment_pad_interaction_test.cpp)
  target_compile_definitions(adjustment_pad_interaction_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(adjustment_pad_interaction_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(adjustment_pad_interaction_test)
  add_test(NAME adjustment_pad_interaction_test COMMAND adjustment_pad_interaction_test)

  add_executable(tone_curve_interaction_test tone_curve_interaction_test.cpp)
  target_compile_definitions(tone_curve_interaction_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(tone_curve_interaction_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(tone_curve_interaction_test)
  add_test(NAME tone_curve_interaction_test COMMAND tone_curve_interaction_test)
  set_tests_properties(tone_curve_interaction_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;inspector;interaction")

  add_library(native_window_test_input STATIC native_window_test_input.cpp)
  target_link_libraries(native_window_test_input PRIVATE Qt6::Test Qt6::Gui)
  if(APPLE)
    target_sources(native_window_test_input PRIVATE native_window_test_activation.mm)
    target_link_libraries(native_window_test_input PRIVATE "-framework AppKit" "-framework ApplicationServices")
  endif()
  photo_editor_apply_warnings(native_window_test_input)
  if(APPLE)
    add_executable(timeline_source_loading_ui_test timeline_source_loading_ui_test.cpp)
    target_compile_definitions(
      timeline_source_loading_ui_test
      PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(
      timeline_source_loading_ui_test
      PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Multimedia
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Test
        native_window_test_input)
    photo_editor_add_shell_visual_resources(timeline_source_loading_ui_test)
    photo_editor_apply_warnings(timeline_source_loading_ui_test)
    add_test(NAME timeline_source_loading_ui_test COMMAND timeline_source_loading_ui_test)
    set_tests_properties(
      timeline_source_loading_ui_test
      PROPERTIES
        ENVIRONMENT
          "QT_QPA_PLATFORM=cocoa;PHOTO_EDITOR_TIMELINE_SOURCE_LOADING_EVIDENCE_DIR=${PROJECT_BINARY_DIR}/test-evidence/timeline-source-loading"
        TIMEOUT 40
        RUN_SERIAL TRUE
        RESOURCE_LOCK photo_editor_desktop_runtime
        LABELS "desktop;timeline;source;ui;native;cocoa")
  endif()
  target_link_libraries(actual_app_workflow_benchmark_driver PRIVATE native_window_test_input)
  photo_editor_apply_warnings(actual_app_workflow_benchmark_driver)
  target_link_libraries(actual_app_workflow_benchmark_driver PRIVATE SQLite3::SQLite3)
  target_sources(actual_app_workflow_benchmark_driver PRIVATE
    ../apps/desktop/src/recent_user_input_filter.h)
  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    target_compile_definitions(actual_app_workflow_benchmark_driver PRIVATE
      PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND=1)
    target_sources(actual_app_workflow_benchmark_driver PRIVATE native_preview_readback.mm)
    target_sources(actual_app_workflow_benchmark_driver PRIVATE
      ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp
      ../apps/desktop/src/gaussian_scene_viewport.h ../apps/desktop/src/gaussian_scene_viewport.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm ../apps/desktop/src/gaussian_spacetime_artifact.cpp)
    target_link_libraries(actual_app_workflow_benchmark_driver PRIVATE "-framework Metal" "-framework Foundation")
  endif()

  if(APPLE)
    add_test(NAME desktop_shell_library_startup_test
      COMMAND actual_app_workflow_benchmark_driver --library-startup-proof)
    set_tests_properties(desktop_shell_library_startup_test PROPERTIES
      TIMEOUT 60
      ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;ui-shard;library;startup"
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

    add_test(NAME desktop_shell_large_library_grid_smoothness_test
      COMMAND actual_app_workflow_benchmark_driver
        --library-grid-proof --asset-count 500 --require-incremental --require-virtualized)
    set_tests_properties(desktop_shell_large_library_grid_smoothness_test PROPERTIES
      TIMEOUT 120
      ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;ui-shard;library;performance"
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

    add_test(NAME desktop_shell_large_library_unique_thumbnail_smoothness_test
      COMMAND actual_app_workflow_benchmark_driver
        --library-grid-proof --asset-count 500 --require-incremental --require-virtualized
        --unique-cached-thumbnail-sources)
    set_tests_properties(desktop_shell_large_library_unique_thumbnail_smoothness_test PROPERTIES
      TIMEOUT 120
      ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;ui-shard;library;performance;thumbnails"
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)

    add_test(NAME photo_recipe_clipboard_workflow_test
      COMMAND actual_app_workflow_benchmark_driver
        --recipe-clipboard-proof-dir ${CMAKE_CURRENT_BINARY_DIR}/photo-recipe-clipboard-proof)
    set_tests_properties(photo_recipe_clipboard_workflow_test PROPERTIES
      TIMEOUT 120
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;photo;clipboard;visual-proof"
      RUN_SERIAL TRUE
      RESOURCE_LOCK photo_editor_desktop_runtime)
  endif()

  add_test(
    NAME timeline_ui_runtime_proof_test
    COMMAND
      actual_app_workflow_benchmark_driver
      --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-ui-runtime-proof)
  set_tests_properties(
    timeline_ui_runtime_proof_test
    PROPERTIES
      TIMEOUT 420
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;timeline;visual-proof"
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_test(NAME timeline_workspace_ux_workflow_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-workspace-ux-proof --timeline-workspace-ux-proof-only)
  set_tests_properties(timeline_workspace_ux_workflow_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;workspace;visual-proof" RESOURCE_LOCK photo_editor_desktop_runtime)

  add_test(NAME timeline_interaction_feedback_ui_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-interaction-feedback-proof
      --timeline-interaction-feedback-proof-only --timeline-interaction-video-frames --timeout-ms 60000)
  set_tests_properties(timeline_interaction_feedback_ui_test PROPERTIES TIMEOUT 150
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;ui;visual-proof" RESOURCE_LOCK photo_editor_desktop_runtime)

  if(APPLE)
    add_test(NAME timeline_discoverable_workspace_workflow_test
      COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
        ${CMAKE_CURRENT_BINARY_DIR}/timeline-discoverable-workspace-proof --timeline-discoverable-workspace-proof-only)
    set_tests_properties(timeline_discoverable_workspace_workflow_test PROPERTIES TIMEOUT 120
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa;PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;macos;timeline;workspace;visual-proof" RESOURCE_LOCK photo_editor_desktop_runtime)
  endif()

  add_test(NAME timeline_animation_ui_workflow_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-animation-ui-proof --timeline-animation-proof-only)
  set_tests_properties(timeline_animation_ui_workflow_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;animation;visual-proof" RESOURCE_LOCK photo_editor_desktop_runtime)

  add_test(NAME timeline_inspector_animation_ui_workflow_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-inspector-animation-ui-proof
      --timeline-inspector-animation-proof-only)
  set_tests_properties(timeline_inspector_animation_ui_workflow_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;animation;inspector;visual-proof"
    RESOURCE_LOCK photo_editor_desktop_runtime)

  add_test(NAME timeline_channels_trim_functional_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-channels-trim-functional
      --timeline-channels-trim-proof-only)
  set_tests_properties(timeline_channels_trim_functional_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;animation;trim;functional"
    RESOURCE_LOCK photo_editor_desktop_runtime)
  if(APPLE)
    add_test(NAME timeline_channels_trim_native_test
      COMMAND actual_app_workflow_benchmark_driver --timeline-ui-proof-dir
        ${CMAKE_CURRENT_BINARY_DIR}/timeline-channels-trim-native
        --timeline-channels-trim-proof-only)
    set_tests_properties(timeline_channels_trim_native_test PROPERTIES TIMEOUT 120
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa;PHOTO_EDITOR_SMOKE_NATIVE_POINTER=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;timeline;animation;trim;native;visual-proof"
      RESOURCE_LOCK photo_editor_desktop_runtime)
  endif()

  add_test(
    NAME timeline_multicam_runtime_proof_test
    COMMAND
      actual_app_workflow_benchmark_driver
      --timeline-ui-proof-dir
      ${CMAKE_CURRENT_BINARY_DIR}/timeline-multicam-runtime-proof
      --timeline-multicam-proof-only)
  set_tests_properties(
    timeline_multicam_runtime_proof_test
    PROPERTIES
      TIMEOUT 240
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      LABELS "desktop;timeline;visual-proof;multicam"
      RESOURCE_LOCK photo_editor_desktop_runtime)

  add_executable(
    color_adjustment_benchmark_driver
    color_adjustment_benchmark_driver.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp)
  set_target_properties(color_adjustment_benchmark_driver PROPERTIES AUTOMOC ON)
  target_include_directories(
    color_adjustment_benchmark_driver
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(
    color_adjustment_benchmark_driver
    PRIVATE
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  target_link_libraries(
    color_adjustment_benchmark_driver
    PRIVATE
      Qt6::Core
      Qt6::Gui
      photo_engine)
  photo_editor_apply_warnings(color_adjustment_benchmark_driver)

  add_executable(
    render_parity_test
    render_parity_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ../apps/desktop/src/photo_layer_adjustment_recipe.cpp
    ../apps/desktop/src/thumbnail_refresh_queue_service.cpp
    ../apps/desktop/src/thumbnail_worker_renderer_factory.cpp
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    render_parity_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_parity_test
    PRIVATE
      Qt6::Core
      Qt6::Concurrent
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(render_parity_test)

  add_test(NAME render_parity_test COMMAND render_parity_test)
  set_tests_properties(render_parity_test PROPERTIES SKIP_RETURN_CODE 77)

  add_executable(
    independent_chart_candidate_render_tool
    independent_chart_candidate_render_tool.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    independent_chart_candidate_render_tool
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    independent_chart_candidate_render_tool
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(independent_chart_candidate_render_tool)

  add_executable(
    real_raw_candidate_render_tool
    real_raw_candidate_render_tool.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    real_raw_candidate_render_tool
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    real_raw_candidate_render_tool
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(real_raw_candidate_render_tool)

  add_executable(
    raw_exposure_render_test
    raw_exposure_render_test.cpp
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(raw_exposure_render_test PRIVATE ../apps/desktop/src)
  target_link_libraries(raw_exposure_render_test PRIVATE
    Qt6::Core Qt6::Gui ${photo_editor_desktop_image_decoder_frameworks} photo_engine)
  photo_editor_apply_warnings(raw_exposure_render_test)
  add_test(NAME raw_exposure_render_test COMMAND raw_exposure_render_test)
  set_tests_properties(raw_exposure_render_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" SKIP_RETURN_CODE 77
    TIMEOUT 120 LABELS "desktop;color;raw")

  add_executable(
    layer_lut_render_parity_test
    layer_lut_render_parity_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    layer_lut_render_parity_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    layer_lut_render_parity_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(layer_lut_render_parity_test)

  add_test(NAME layer_lut_render_parity_test COMMAND layer_lut_render_parity_test)

  add_executable(photo_paint_performance_test photo_paint_performance_test.cpp)
  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    target_compile_definitions(photo_paint_performance_test PRIVATE PHOTO_PAINT_HAS_NATIVE_TEXTURE=1)
  endif()
  target_include_directories(photo_paint_performance_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_paint_performance_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent photo_engine)
  if(APPLE)
    target_link_libraries(photo_paint_performance_test PRIVATE native_window_test_input)
    target_link_options(photo_paint_performance_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  photo_editor_apply_warnings(photo_paint_performance_test)
  add_test(NAME photo_fragment_layer_tip_cache_contract COMMAND photo_paint_performance_test)
  set_tests_properties(photo_fragment_layer_tip_cache_contract PROPERTIES
    TIMEOUT 60
    LABELS "desktop;paint;performance"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_PAINT_VERIFY_LAYER_TIP_CACHE=1")

  add_executable(retouch_cancel_ui_test retouch_cancel_ui_test.cpp)
  target_link_libraries(retouch_cancel_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  target_compile_definitions(retouch_cancel_ui_test PRIVATE RETOUCH_TEST_ROOT="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(retouch_cancel_ui_test)
  add_test(NAME retouch_cancel_ui_test COMMAND retouch_cancel_ui_test)
  set_tests_properties(retouch_cancel_ui_test PROPERTIES TIMEOUT 30)

  add_executable(photo_retouch_quality_test photo_retouch_quality_test.cpp)
  target_include_directories(photo_retouch_quality_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_retouch_quality_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_retouch_quality_test)
  add_test(NAME photo_retouch_quality_test COMMAND photo_retouch_quality_test)
  set_tests_properties(photo_retouch_quality_test PROPERTIES TIMEOUT 90)

  add_executable(photo_composition_workflow_test
    photo_composition_workflow_test.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp
    ../apps/desktop/src/agent_local_model.h
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.h
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.h
    ../apps/desktop/src/agent_edit_style.cpp)
  set_target_properties(photo_composition_workflow_test PROPERTIES AUTOMOC ON)
  target_include_directories(photo_composition_workflow_test PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(photo_composition_workflow_test PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::Concurrent)
  target_link_libraries(photo_composition_workflow_test PRIVATE photo_composition_bridge_runtime)
  target_compile_definitions(photo_composition_workflow_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  if(APPLE)
    target_link_options(photo_composition_workflow_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  photo_editor_apply_warnings(photo_composition_workflow_test)
  add_test(NAME photo_composition_workflow_test COMMAND photo_composition_workflow_test)
  set_tests_properties(photo_composition_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE)
  add_test(NAME agent_composition_idempotent_contract COMMAND photo_composition_workflow_test --composition-idempotent-contract)
  add_test(NAME agent_composition_task_transaction_contract COMMAND photo_composition_workflow_test --composition-task-transaction-contract)
  add_test(NAME agent_native_command_registry_contract COMMAND photo_composition_workflow_test --native-command-registry-contract)
  add_test(NAME agent_multi_document_contract COMMAND photo_composition_workflow_test --multi-document-agent-contract)
  set_tests_properties(agent_multi_document_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE)
  set_tests_properties(agent_native_command_registry_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE)
  set_tests_properties(agent_composition_task_transaction_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE)
  set_tests_properties(agent_composition_idempotent_contract PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 RUN_SERIAL TRUE)

  if(APPLE)
    # Opt-in local-model evidence needs real Vision segmentation rather than
    # the deterministic segmentation implementation in the shared test archive.
    get_target_property(thumbnail_proof_sources photo_composition_workflow_test SOURCES)
    add_executable(agent_thumbnail_live_proof ${thumbnail_proof_sources}
      ../apps/desktop/src/vision_segmentation.mm)
    set_target_properties(agent_thumbnail_live_proof PROPERTIES AUTOMOC ON)
    set_source_files_properties(../apps/desktop/src/vision_segmentation.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(agent_thumbnail_live_proof PRIVATE ../apps/desktop/src ../engine/include)
    target_compile_definitions(agent_thumbnail_live_proof PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(agent_thumbnail_live_proof PRIVATE photo_composition_bridge_runtime
      Qt6::Core Qt6::Gui Qt6::Network Qt6::Quick Qt6::Concurrent "-framework Vision" "-framework CoreVideo")
    target_link_options(agent_thumbnail_live_proof PRIVATE "LINKER:-stack_size,0x1000000")
    photo_editor_apply_warnings(agent_thumbnail_live_proof)
  endif()

  add_executable(photo_typography_workflow_test photo_typography_workflow_test.cpp)
  target_include_directories(photo_typography_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_typography_workflow_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent photo_engine)
  target_compile_definitions(photo_typography_workflow_test PRIVATE PHOTO_TEXT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  if(APPLE)
    target_link_options(photo_typography_workflow_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  photo_editor_apply_warnings(photo_typography_workflow_test)
  add_test(NAME photo_typography_workflow_test COMMAND photo_typography_workflow_test)
  set_tests_properties(photo_typography_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE)
  add_test(NAME photo_metal_finishes_workflow_test
    COMMAND photo_typography_workflow_test --metal-showcase-only)
  set_tests_properties(photo_metal_finishes_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE
    LABELS "desktop;typography;material;workflow")

  add_executable(photo_effect_workflow_test photo_effect_workflow_test.cpp)
  add_executable(creative_example_thumbnail_test creative_example_thumbnail_test.cpp)
  target_include_directories(creative_example_thumbnail_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(creative_example_thumbnail_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(creative_example_thumbnail_test PRIVATE desktop_shell_test_runtime photo_engine
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Test)
  photo_editor_apply_warnings(creative_example_thumbnail_test)
  add_test(NAME creative_example_thumbnail_test COMMAND creative_example_thumbnail_test)
  set_tests_properties(creative_example_thumbnail_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;thumbnail;visual-browser;render-parity")
  add_executable(creative_example_render_quality_test creative_example_render_quality_test.cpp)
  target_include_directories(creative_example_render_quality_test PRIVATE ../apps/desktop/src)
  target_link_libraries(creative_example_render_quality_test PRIVATE desktop_shell_test_runtime photo_engine
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(creative_example_render_quality_test)
  add_test(NAME creative_example_render_quality_test COMMAND creative_example_render_quality_test)
  set_tests_properties(creative_example_render_quality_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
    LABELS "desktop;thumbnail;visual-browser;render-parity")
  add_executable(preset_live_preview_test preset_live_preview_test.cpp)
  target_include_directories(preset_live_preview_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(preset_live_preview_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(preset_live_preview_test PRIVATE desktop_shell_test_runtime photo_engine
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Concurrent Qt6::Test)
  photo_editor_apply_warnings(preset_live_preview_test)
  add_test(NAME preset_live_preview_test COMMAND preset_live_preview_test)
  set_tests_properties(preset_live_preview_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;preset;visual-browser;workflow")
  target_include_directories(photo_effect_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_effect_workflow_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::Test photo_engine)
  target_compile_definitions(photo_effect_workflow_test PRIVATE PHOTO_EFFECT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  if(APPLE)
    target_link_options(photo_effect_workflow_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  photo_editor_apply_warnings(photo_effect_workflow_test)
  add_test(NAME photo_effect_workflow_test COMMAND photo_effect_workflow_test)
  set_tests_properties(photo_effect_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE)
  add_test(NAME photo_extended_effects_workflow_test COMMAND photo_effect_workflow_test)
  set_tests_properties(photo_extended_effects_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EFFECT_EXTENDED_REVIEW_ONLY=1;PHOTO_EFFECT_TEST_NO_MASK=1"
    TIMEOUT 600 RUN_SERIAL TRUE)
  add_test(NAME film_grain_workflow_test COMMAND photo_effect_workflow_test)
  set_tests_properties(film_grain_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_FILM_STOCK_WORKFLOW_ONLY=1;PHOTO_EFFECT_TEST_NO_MASK=1"
    TIMEOUT 240 RUN_SERIAL TRUE)
  add_test(NAME photo_creative_looks_workflow_test COMMAND photo_effect_workflow_test)
  set_tests_properties(photo_creative_looks_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EFFECT_CREATIVE_REVIEW_ONLY=1;PHOTO_EFFECT_TEST_NO_MASK=1"
    TIMEOUT 240 RUN_SERIAL TRUE)

  add_executable(photo_paint_resampling_test photo_paint_resampling_test.cpp)
  add_executable(photo_paint_coordinates_test photo_paint_coordinates_test.cpp)
  target_include_directories(photo_paint_coordinates_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_paint_coordinates_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_paint_coordinates_test)
  add_test(NAME photo_paint_coordinates_test COMMAND photo_paint_coordinates_test)
  set_tests_properties(photo_paint_coordinates_test PROPERTIES LABELS "desktop;paint;brush;coordinates;correctness")
  target_include_directories(photo_paint_resampling_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_paint_resampling_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(photo_paint_resampling_test)
  add_test(NAME photo_paint_resampling_test COMMAND photo_paint_resampling_test)
  set_tests_properties(photo_paint_resampling_test PROPERTIES LABELS "desktop;paint;brush;color;correctness")

  add_executable(photo_pixel_paint_test photo_pixel_paint_test.cpp)
  add_executable(photo_paint_source_basis_test photo_paint_source_basis_test.cpp)
  target_include_directories(photo_paint_source_basis_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_paint_source_basis_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_paint_source_basis_test)
  add_test(NAME photo_paint_source_basis_test COMMAND photo_paint_source_basis_test)
  set_tests_properties(photo_paint_source_basis_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;paint;brush;crop;correctness")
  add_executable(photo_canvas_coordinates_test photo_canvas_coordinates_test.cpp)
  target_include_directories(photo_canvas_coordinates_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_canvas_coordinates_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent photo_engine)
  target_compile_definitions(photo_canvas_coordinates_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(photo_canvas_coordinates_test)
  add_test(NAME photo_canvas_coordinates_test COMMAND photo_canvas_coordinates_test)
  set_tests_properties(photo_canvas_coordinates_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" TIMEOUT 120 RUN_SERIAL TRUE LABELS "desktop;brush;paint;coordinates;correctness")
  target_include_directories(photo_pixel_paint_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_pixel_paint_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent photo_engine)
  target_compile_definitions(photo_pixel_paint_test PRIVATE PHOTO_PAINT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
  if(APPLE)
    target_link_options(photo_pixel_paint_test PRIVATE "LINKER:-stack_size,0x1000000")
  endif()
  photo_editor_apply_warnings(photo_pixel_paint_test)

  add_executable(preview_linear_cache_test preview_linear_cache_test.cpp)
  target_include_directories(preview_linear_cache_test PRIVATE ../apps/desktop/src)
  target_link_libraries(preview_linear_cache_test PRIVATE desktop_shell_test_runtime
    Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(preview_linear_cache_test)
  add_test(NAME preview_linear_cache_test COMMAND preview_linear_cache_test)
  set_tests_properties(preview_linear_cache_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME raw_exposure_preview_test COMMAND preview_linear_cache_test --raw-exposure)
  set_tests_properties(raw_exposure_preview_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime SKIP_RETURN_CODE 77
    LABELS "desktop;color;raw")
  add_test(NAME raw_highlight_preview_test COMMAND preview_linear_cache_test --raw-highlight)
  set_tests_properties(raw_highlight_preview_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120 RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime SKIP_RETURN_CODE 77
    LABELS "desktop;color;raw;highlight")
  add_test(NAME photo_pixel_paint_test COMMAND photo_pixel_paint_test)
  add_test(NAME photo_text_transform_icc_parity_test
    COMMAND photo_pixel_paint_test --text-transform-icc-parity)
  set_tests_properties(photo_text_transform_icc_parity_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 180 RUN_SERIAL TRUE SKIP_RETURN_CODE 77 LABELS "desktop;text;color;performance")
  add_test(NAME photo_pixel_paint_edge_parity_test COMMAND photo_pixel_paint_test --paint-edge-parity)
  add_test(NAME photo_shape_fill_test COMMAND photo_pixel_paint_test --shape-fills)
  add_test(NAME blank_canvas_document_test COMMAND photo_pixel_paint_test --blank-canvas)
  add_test(NAME photo_document_workflow_test COMMAND photo_pixel_paint_test --document-workflow)
  set_tests_properties(photo_document_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE)
  if(APPLE)
    add_test(NAME photo_document_device_icc_workflow_test
      COMMAND photo_pixel_paint_test --document-workflow --require-native-device-icc)
    add_test(NAME photo_pixel_paint_device_icc_test
      COMMAND photo_pixel_paint_test --require-native-device-icc)
    set_tests_properties(photo_document_device_icc_workflow_test photo_pixel_paint_device_icc_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
      TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
      LABELS "desktop;paint;color;icc;metal;native")
  endif()
  add_test(NAME photo_linked_cutout_clipboard_test COMMAND photo_pixel_paint_test --linked-cutout-clipboard-only)
  set_tests_properties(photo_linked_cutout_clipboard_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    TIMEOUT 90 RUN_SERIAL TRUE LABELS "desktop;cutout;clipboard;regression")
  set_tests_properties(blank_canvas_document_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 RUN_SERIAL TRUE)
  set_tests_properties(photo_shape_fill_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 RUN_SERIAL TRUE)
  set_tests_properties(photo_pixel_paint_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90 RUN_SERIAL TRUE)
  set_tests_properties(photo_pixel_paint_edge_parity_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 RUN_SERIAL TRUE
    LABELS "desktop;paint;brush;correctness")

  add_executable(photo_layer_color_input_test photo_layer_color_input_test.cpp)
  target_include_directories(photo_layer_color_input_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_layer_color_input_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_layer_color_input_test)
  add_test(NAME photo_layer_color_input_test COMMAND photo_layer_color_input_test)
  add_test(NAME photo_layer_text_scale_test COMMAND photo_layer_color_input_test --with-gui)
  set_tests_properties(photo_layer_text_scale_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_tests_properties(photo_layer_color_input_test PROPERTIES TIMEOUT 60 LABELS "desktop;color;layers")

  add_executable(photo_display_text_overlay_test photo_display_text_overlay_test.cpp)
  target_include_directories(photo_display_text_overlay_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_display_text_overlay_test PRIVATE desktop_shell_test_runtime Qt6::Concurrent Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_display_text_overlay_test)
  add_test(NAME photo_display_text_overlay_test COMMAND photo_display_text_overlay_test)
  add_test(NAME photo_group_display_text_test COMMAND photo_display_text_overlay_test --neutral-groups-only)
  set_tests_properties(photo_group_display_text_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;professional;color;layers")
  set_tests_properties(photo_display_text_overlay_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 60
    SKIP_RETURN_CODE 77
    LABELS "desktop;color;layers")

  add_executable(photo_layer_alpha_image_test photo_layer_alpha_image_test.cpp)
  target_include_directories(photo_layer_alpha_image_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_layer_alpha_image_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_layer_alpha_image_test)
  add_test(NAME photo_layer_alpha_image_test COMMAND photo_layer_alpha_image_test)
  set_tests_properties(photo_layer_alpha_image_test PROPERTIES TIMEOUT 60 LABELS "desktop;color;layers")

  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    add_executable(metal_submission_benchmark metal_submission_benchmark.cpp)
    target_link_libraries(metal_submission_benchmark PRIVATE photo_engine Qt6::Core)
    photo_editor_apply_warnings(metal_submission_benchmark)
    add_test(NAME metal_concurrent_publication_test COMMAND metal_submission_benchmark
      --contract ${CMAKE_CURRENT_BINARY_DIR}/metal-concurrent-publication.json)
    set_tests_properties(metal_concurrent_publication_test PROPERTIES
      TIMEOUT 60 SKIP_RETURN_CODE 3 LABELS "metal;performance;concurrency")
  endif()

  add_executable(photo_layer_sampling_image_test photo_layer_sampling_image_test.cpp)
  target_include_directories(photo_layer_sampling_image_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_layer_sampling_image_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(photo_layer_sampling_image_test)
  add_test(NAME photo_layer_sampling_image_test COMMAND photo_layer_sampling_image_test)
  set_tests_properties(photo_layer_sampling_image_test PROPERTIES TIMEOUT 60 LABELS "desktop;color;layers")

  add_executable(linked_photo_layer_source_test linked_photo_layer_source_test.cpp)
  target_include_directories(linked_photo_layer_source_test PRIVATE ../apps/desktop/src)
  target_link_libraries(linked_photo_layer_source_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(linked_photo_layer_source_test)
  add_test(NAME linked_photo_layer_source_test COMMAND linked_photo_layer_source_test)
  set_tests_properties(linked_photo_layer_source_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;color;layers")

  add_executable(linked_image_source_monitor_test linked_image_source_monitor_test.cpp)
  target_include_directories(linked_image_source_monitor_test PRIVATE ../apps/desktop/src)
  target_link_libraries(linked_image_source_monitor_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(linked_image_source_monitor_test)
  add_test(NAME linked_image_source_monitor_test COMMAND linked_image_source_monitor_test)
  set_tests_properties(linked_image_source_monitor_test PROPERTIES TIMEOUT 30 LABELS "desktop;layers")

  add_executable(popup_system_ui_test popup_system_ui_test.cpp)
  target_compile_definitions(popup_system_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(popup_system_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(popup_system_ui_test)
  add_test(NAME popup_system_ui_test COMMAND popup_system_ui_test)
  set_tests_properties(popup_system_ui_test PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;popup;ui")
  add_executable(grouped_options_ui_test grouped_options_ui_test.cpp)
  target_compile_definitions(grouped_options_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(grouped_options_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(grouped_options_ui_test)
  add_test(NAME grouped_options_ui_test COMMAND grouped_options_ui_test)
  set_tests_properties(grouped_options_ui_test PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;popup;ui")
  add_executable(ui_style_policy_test ui_style_policy_test.cpp)
  add_executable(modular_tool_panel_ui_test modular_tool_panel_ui_test.cpp)
  add_executable(brush_customization_ui_test brush_customization_ui_test.cpp
    ../apps/desktop/src/tablet_input_area.cpp ../apps/desktop/src/tablet_input_area.h)
  set_target_properties(brush_customization_ui_test PROPERTIES AUTOMOC ON)
  target_include_directories(brush_customization_ui_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(brush_customization_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(brush_customization_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(brush_customization_ui_test)
  add_test(NAME brush_customization_ui_test COMMAND brush_customization_ui_test)
  set_tests_properties(brush_customization_ui_test PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;brush;responsive")
  add_executable(selection_customization_ui_test selection_customization_ui_test.cpp)
  target_compile_definitions(selection_customization_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(selection_customization_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(selection_customization_ui_test)
  add_test(NAME selection_customization_ui_test COMMAND selection_customization_ui_test)
  set_tests_properties(selection_customization_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;selection;responsive")
  add_executable(layer_actions_menu_ui_test layer_actions_menu_ui_test.cpp)
  target_compile_definitions(layer_actions_menu_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(layer_actions_menu_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(layer_actions_menu_ui_test)
  add_test(NAME layer_actions_menu_ui_test COMMAND layer_actions_menu_ui_test)
  set_tests_properties(layer_actions_menu_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;layers;popup;ui")
  add_executable(photo_typography_panel_ui_test photo_typography_panel_ui_test.cpp)
  foreach(visual_browser_test visual_example_card_ui_test preset_browser_ui_test)
    add_executable(${visual_browser_test} ${visual_browser_test}.cpp)
    target_compile_definitions(${visual_browser_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${visual_browser_test} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    photo_editor_apply_warnings(${visual_browser_test})
    add_test(NAME ${visual_browser_test} COMMAND ${visual_browser_test})
    set_tests_properties(${visual_browser_test} PROPERTIES TIMEOUT 90
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_DISABLE_DISK_CACHE=1"
      LABELS "desktop;qml;visual-browser;responsive")
  endforeach()
  target_compile_definitions(photo_typography_panel_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(photo_typography_panel_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(photo_typography_panel_ui_test)
  add_test(NAME photo_typography_panel_ui_test COMMAND photo_typography_panel_ui_test)
  set_tests_properties(photo_typography_panel_ui_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;typography;responsive")
  add_executable(retouch_tool_hud_ui_test retouch_tool_hud_ui_test.cpp)
  target_compile_definitions(retouch_tool_hud_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(retouch_tool_hud_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(retouch_tool_hud_ui_test)
  add_test(NAME retouch_tool_hud_ui_test COMMAND retouch_tool_hud_ui_test)
  set_tests_properties(retouch_tool_hud_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;retouch;responsive")
  target_compile_definitions(modular_tool_panel_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(modular_tool_panel_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(modular_tool_panel_ui_test)
  add_test(NAME modular_tool_panel_ui_test COMMAND modular_tool_panel_ui_test)
  set_tests_properties(modular_tool_panel_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;tools;responsive")
  target_compile_definitions(ui_style_policy_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(ui_style_policy_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(ui_style_policy_test)
  add_test(NAME ui_style_policy_test COMMAND ui_style_policy_test)
  set_tests_properties(ui_style_policy_test PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;ui-style")
  add_executable(inspector_style_policy_test inspector_style_policy_test.cpp)
  target_compile_definitions(inspector_style_policy_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(inspector_style_policy_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(inspector_style_policy_test)
  add_test(NAME inspector_style_policy_test COMMAND inspector_style_policy_test)
  set_tests_properties(inspector_style_policy_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;ui-style;inspector")
  add_executable(command_style_policy_test command_style_policy_test.cpp)
  add_executable(readability_choice_policy_test readability_choice_policy_test.cpp)
  target_compile_definitions(readability_choice_policy_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(readability_choice_policy_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(readability_choice_policy_test)
  add_test(NAME readability_choice_policy_test COMMAND readability_choice_policy_test)
  set_tests_properties(readability_choice_policy_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;ui-style;accessibility;responsive")
  target_compile_definitions(command_style_policy_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(command_style_policy_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(command_style_policy_test)
  add_test(NAME command_style_policy_test COMMAND command_style_policy_test)
  set_tests_properties(command_style_policy_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;ui-style;commands;accessibility")

  add_executable(menu_popover_bounds_test menu_popover_bounds_test.cpp)
  target_compile_definitions(menu_popover_bounds_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(menu_popover_bounds_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(menu_popover_bounds_test)
  add_test(NAME menu_popover_bounds_test COMMAND menu_popover_bounds_test)
  set_tests_properties(menu_popover_bounds_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;layers")

  foreach(qml_lifecycle_test tool_rail_identity_test crop_loading_qml_lifecycle_test)
    add_executable(${qml_lifecycle_test} ${qml_lifecycle_test}.cpp)
    target_compile_definitions(${qml_lifecycle_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(${qml_lifecycle_test} PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    photo_editor_apply_warnings(${qml_lifecycle_test})
    add_test(NAME ${qml_lifecycle_test} COMMAND ${qml_lifecycle_test})
    set_tests_properties(${qml_lifecycle_test} PROPERTIES TIMEOUT 30
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;performance")
  endforeach()

  add_test(NAME preset_thumbnail_result_cache_test COMMAND editor_preview_controller_test)
  set_tests_properties(preset_thumbnail_result_cache_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "PHOTO_EDITOR_TEST_PRESET_THUMBNAIL_ONLY=1;QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;color;performance")

  add_executable(board_viewer_controls_test board_viewer_controls_test.cpp)
  target_compile_definitions(board_viewer_controls_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(board_viewer_controls_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(board_viewer_controls_test)
  add_test(NAME board_viewer_controls_test COMMAND board_viewer_controls_test)
  set_tests_properties(board_viewer_controls_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(campaign_delivery_ui_test campaign_delivery_ui_test.cpp)
  target_compile_definitions(campaign_delivery_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(campaign_delivery_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(campaign_delivery_ui_test)
  add_test(NAME campaign_delivery_ui_test COMMAND campaign_delivery_ui_test)
  set_tests_properties(campaign_delivery_ui_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;delivery;qml")

  add_executable(photo_board_layout_test photo_board_layout_test.cpp)
  target_include_directories(photo_board_layout_test PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src")
  target_compile_definitions(photo_board_layout_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(photo_board_layout_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml)
  photo_editor_apply_warnings(photo_board_layout_test)
  add_test(NAME photo_board_layout_test COMMAND photo_board_layout_test)

  add_executable(photo_board_controller_test photo_board_controller_test.cpp)
  add_executable(carousel_edit_latency_benchmark carousel_edit_latency_benchmark.cpp)
  target_include_directories(carousel_edit_latency_benchmark PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src")
  target_link_libraries(carousel_edit_latency_benchmark PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(carousel_edit_latency_benchmark)
  target_include_directories(photo_board_controller_test PRIVATE "${PROJECT_SOURCE_DIR}/apps/desktop/src")
  target_link_libraries(photo_board_controller_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(photo_board_controller_test)
  add_test(NAME photo_board_controller_test COMMAND photo_board_controller_test)
  set_tests_properties(photo_board_controller_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1")

  add_executable(agent_robot_popup_ui_test agent_robot_popup_ui_test.cpp)
  add_executable(loading_scene_ui_test loading_scene_ui_test.cpp)
  add_executable(shader_animation_lifecycle_test shader_animation_lifecycle_test.cpp)
  target_compile_definitions(shader_animation_lifecycle_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(shader_animation_lifecycle_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_add_shell_visual_resources(shader_animation_lifecycle_test)
  photo_editor_apply_warnings(shader_animation_lifecycle_test)
  add_test(NAME shader_animation_lifecycle_test COMMAND shader_animation_lifecycle_test)
  set_tests_properties(shader_animation_lifecycle_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;loading;regression")
  target_compile_definitions(loading_scene_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(loading_scene_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_add_shell_visual_resources(loading_scene_ui_test)
  photo_editor_apply_warnings(loading_scene_ui_test)
  add_test(NAME loading_scene_ui_test COMMAND loading_scene_ui_test)
  set_tests_properties(loading_scene_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;loading;ui")
  target_compile_definitions(agent_robot_popup_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(agent_robot_popup_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_add_shell_visual_resources(agent_robot_popup_ui_test)
  photo_editor_apply_warnings(agent_robot_popup_ui_test)
  add_test(NAME agent_robot_popup_ui_test COMMAND agent_robot_popup_ui_test)
  add_test(NAME agent_session_terminal_ui_test COMMAND agent_robot_popup_ui_test)
  add_test(NAME agent_session_large_type_ui_test COMMAND agent_robot_popup_ui_test)
  set_tests_properties(agent_session_large_type_ui_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_SESSION_FLOW_ONLY=1;PHOTO_EDITOR_AGENT_INTERFACE_SCALE=2"
    LABELS "desktop;qml;agent;responsive;accessibility")
  add_test(NAME agent_popup_large_type_ui_test COMMAND agent_robot_popup_ui_test)
  set_tests_properties(agent_popup_large_type_ui_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_AGENT_INTERFACE_SCALE=2"
    LABELS "desktop;qml;agent;responsive;accessibility")
  set_tests_properties(agent_session_terminal_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_SESSION_FLOW_ONLY=1"
    LABELS "desktop;qml;agent;session;ui")
  set_tests_properties(agent_robot_popup_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;agent;ui")

  add_executable(agent_production_motion_ui_test
    agent_production_motion_ui_test.cpp
    ../apps/desktop/src/agent_edit_controller.h
    ../apps/desktop/src/agent_edit_controller.cpp
    ../apps/desktop/src/agent_edit_recipe_patch.cpp
    ../apps/desktop/src/agent_edit_style.cpp
    ../apps/desktop/src/agent_image_validation.cpp
    ../apps/desktop/src/agent_local_model.cpp
    ../apps/desktop/src/agent_memory.cpp
    ../apps/desktop/src/agent_mcp_client.h
    ../apps/desktop/src/agent_mcp_client.cpp)
  set_target_properties(agent_production_motion_ui_test PROPERTIES AUTOMOC ON)
  target_include_directories(agent_production_motion_ui_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(agent_production_motion_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(agent_production_motion_ui_test PRIVATE desktop_shell_test_runtime
    photo_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2
    Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test SQLite3::SQLite3)
  photo_editor_add_shell_visual_resources(agent_production_motion_ui_test)
  photo_editor_apply_warnings(agent_production_motion_ui_test)
  add_test(NAME agent_production_motion_ui_test COMMAND agent_production_motion_ui_test)
  set_tests_properties(agent_production_motion_ui_test PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;qml;agent;production-events;ui")

  add_executable(icon_system_ui_test icon_system_ui_test.cpp)
  target_compile_definitions(icon_system_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(icon_system_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_add_shell_visual_resources(icon_system_ui_test)
  photo_editor_apply_warnings(icon_system_ui_test)
  add_test(NAME icon_system_ui_test COMMAND icon_system_ui_test)
  set_tests_properties(icon_system_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;icons;ui;visual-proof")

  add_executable(button_accessibility_ui_test button_accessibility_ui_test.cpp)
  target_compile_definitions(button_accessibility_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(button_accessibility_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(button_accessibility_ui_test)
  add_test(NAME button_accessibility_ui_test COMMAND button_accessibility_ui_test)
  set_tests_properties(button_accessibility_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(tone_combo_box_ui_test tone_combo_box_ui_test.cpp)
  target_compile_definitions(tone_combo_box_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(tone_combo_box_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(tone_combo_box_ui_test)
  add_test(NAME tone_combo_box_ui_test COMMAND tone_combo_box_ui_test)
  set_tests_properties(tone_combo_box_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(copy_photo_edits_dialog_ui_test copy_photo_edits_dialog_ui_test.cpp)
  target_compile_definitions(copy_photo_edits_dialog_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(copy_photo_edits_dialog_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(copy_photo_edits_dialog_ui_test)
  add_test(NAME copy_photo_edits_dialog_ui_test COMMAND copy_photo_edits_dialog_ui_test)
  add_test(NAME copy_edits_large_type_ui_test COMMAND copy_photo_edits_dialog_ui_test)
  set_tests_properties(copy_edits_large_type_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_TEST_LARGE_TYPE=1"
    LABELS "desktop;qml;responsive;accessibility")
  set_tests_properties(copy_photo_edits_dialog_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;ui-shard;accessibility;ui")

  add_executable(gallery_picker_dialog_ui_test gallery_picker_dialog_ui_test.cpp)
  target_compile_definitions(gallery_picker_dialog_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(gallery_picker_dialog_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(gallery_picker_dialog_ui_test)
  add_test(NAME gallery_picker_dialog_ui_test COMMAND gallery_picker_dialog_ui_test)
  set_tests_properties(gallery_picker_dialog_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;ui-shard;accessibility;ui")

  add_executable(tone_text_field_ui_test tone_text_field_ui_test.cpp)
  target_compile_definitions(tone_text_field_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(tone_text_field_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(tone_text_field_ui_test)
  add_test(NAME tone_text_field_ui_test COMMAND tone_text_field_ui_test)
  set_tests_properties(tone_text_field_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(tone_text_area_ui_test tone_text_area_ui_test.cpp)
  target_compile_definitions(tone_text_area_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(tone_text_area_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(tone_text_area_ui_test)
  add_test(NAME tone_text_area_ui_test COMMAND tone_text_area_ui_test)
  set_tests_properties(tone_text_area_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(layer_snapping_ui_test layer_snapping_ui_test.cpp)
  target_compile_definitions(layer_snapping_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(layer_snapping_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(layer_snapping_ui_test)
  add_test(NAME layer_snapping_ui_test COMMAND layer_snapping_ui_test)
  set_tests_properties(layer_snapping_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;interaction;ui")

  add_executable(tone_spin_box_ui_test tone_spin_box_ui_test.cpp)
  target_compile_definitions(tone_spin_box_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(tone_spin_box_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(tone_spin_box_ui_test)
  add_test(NAME tone_spin_box_ui_test COMMAND tone_spin_box_ui_test)
  set_tests_properties(tone_spin_box_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(shared_control_accessibility_test shared_control_accessibility_test.cpp)
  target_compile_definitions(shared_control_accessibility_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(shared_control_accessibility_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(shared_control_accessibility_test)
  add_test(NAME shared_control_accessibility_test COMMAND shared_control_accessibility_test)
  set_tests_properties(shared_control_accessibility_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(tone_check_box_ui_test tone_check_box_ui_test.cpp)
  target_compile_definitions(tone_check_box_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(tone_check_box_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(tone_check_box_ui_test)
  add_test(NAME tone_check_box_ui_test COMMAND tone_check_box_ui_test)
  set_tests_properties(tone_check_box_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;accessibility;ui")

  add_executable(catalog_helper_payload_test catalog_helper_payload_test.cpp)
  add_executable(recipe_save_status_chip_test recipe_save_status_chip_test.cpp)
  add_executable(editor_close_coordinator_test editor_close_coordinator_test.cpp
    ../apps/desktop/src/editor_close_coordinator.h ../apps/desktop/src/editor_close_coordinator.cpp)
  set_target_properties(editor_close_coordinator_test PROPERTIES AUTOMOC ON)
  target_include_directories(editor_close_coordinator_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_close_coordinator_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(editor_close_coordinator_test)
  add_test(NAME editor_close_coordinator_test COMMAND editor_close_coordinator_test)
  set_tests_properties(editor_close_coordinator_test PROPERTIES TIMEOUT 20 LABELS "desktop;persistence;shutdown")
  add_executable(editor_close_dialog_test editor_close_dialog_test.cpp
    ../apps/desktop/src/editor_close_coordinator.h ../apps/desktop/src/editor_close_coordinator.cpp)
  set_target_properties(editor_close_dialog_test PROPERTIES AUTOMOC ON)
  target_compile_definitions(editor_close_dialog_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_include_directories(editor_close_dialog_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_close_dialog_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(editor_close_dialog_test)
  add_test(NAME editor_close_dialog_test COMMAND editor_close_dialog_test)
  set_tests_properties(editor_close_dialog_test PROPERTIES TIMEOUT 20
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;qml;ui-shard;accessibility;ui;persistence;shutdown")
  target_compile_definitions(recipe_save_status_chip_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(recipe_save_status_chip_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(recipe_save_status_chip_test)
  add_test(NAME recipe_save_status_chip_test COMMAND recipe_save_status_chip_test)
  set_tests_properties(recipe_save_status_chip_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;qml;persistence")
  target_include_directories(catalog_helper_payload_test PRIVATE ../apps/desktop/src)
  target_link_libraries(catalog_helper_payload_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(catalog_helper_payload_test)
  add_test(NAME catalog_helper_payload_test COMMAND catalog_helper_payload_test)
  set_tests_properties(catalog_helper_payload_test PROPERTIES TIMEOUT 30 LABELS "desktop;persistence")

  add_executable(editor_recipe_save_queue_test editor_recipe_save_queue_test.cpp)
  target_include_directories(editor_recipe_save_queue_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_recipe_save_queue_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(editor_recipe_save_queue_test)
  add_test(NAME editor_recipe_save_queue_test COMMAND editor_recipe_save_queue_test)

  add_executable(editor_recipe_catalog_roundtrip_test editor_recipe_catalog_roundtrip_test.cpp)
  target_sources(editor_recipe_catalog_roundtrip_test PRIVATE
    ../apps/desktop/src/editor_close_coordinator.h ../apps/desktop/src/editor_close_coordinator.cpp)
  set_target_properties(editor_recipe_catalog_roundtrip_test PROPERTIES AUTOMOC ON)
  add_executable(export_preflight_worker_test export_preflight_worker_test.cpp)
  target_include_directories(export_preflight_worker_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_preflight_worker_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(export_preflight_worker_test)
  add_test(NAME export_preflight_worker_test COMMAND export_preflight_worker_test)
  set_tests_properties(export_preflight_worker_test PROPERTIES TIMEOUT 60 LABELS "desktop;export;performance")

  add_executable(catalog_import_preflight_test catalog_import_preflight_test.cpp)
  target_include_directories(catalog_import_preflight_test PRIVATE ../apps/desktop/src)
  target_link_libraries(catalog_import_preflight_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent Qt6::Network)
  photo_editor_apply_warnings(catalog_import_preflight_test)
  add_test(NAME catalog_import_preflight_test COMMAND catalog_import_preflight_test)
  set_tests_properties(catalog_import_preflight_test PROPERTIES TIMEOUT 30 LABELS "desktop;catalog;performance")
  add_test(NAME catalog_import_poll_shutdown_contract
    COMMAND catalog_import_preflight_test --import-poll-shutdown-contract)
  set_tests_properties(catalog_import_poll_shutdown_contract PROPERTIES
    TIMEOUT 20 SKIP_RETURN_CODE 77 LABELS "desktop;catalog;lifecycle;shutdown")

  if(UNIX)
    add_executable(history_storage_controller_test history_storage_controller_test.cpp)
    target_include_directories(history_storage_controller_test PRIVATE ../apps/desktop/src)
    target_link_libraries(history_storage_controller_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(history_storage_controller_test)
    add_test(NAME history_storage_controller_test COMMAND history_storage_controller_test)
    set_tests_properties(history_storage_controller_test PROPERTIES TIMEOUT 60 LABELS "desktop;history;performance")
    add_test(NAME photo_layer_clipboard_contract COMMAND history_storage_controller_test --clipboard-contracts)
    set_tests_properties(photo_layer_clipboard_contract PROPERTIES TIMEOUT 60 LABELS "desktop;clipboard;serialization")
    foreach(history_case IN ITEMS history coalesced_undo layer_mask_history)
      set(history_target "editor_preview_${history_case}_storage_regression")
      add_executable(${history_target} "editor_preview_${history_case}_test.cpp")
      target_include_directories(${history_target} PRIVATE ../apps/desktop/src)
      target_link_libraries(${history_target} PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
      photo_editor_apply_warnings(${history_target})
      add_test(NAME ${history_target} COMMAND ${history_target})
      set_tests_properties(${history_target} PROPERTIES TIMEOUT 120 LABELS "desktop;history;performance"
        ENVIRONMENT "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1")
    endforeach()
    add_executable(history_payload_store_test history_payload_store_test.cpp
      ../apps/desktop/src/history_payload_store.cpp ../apps/desktop/src/history_state_record.cpp)
    target_include_directories(history_payload_store_test PRIVATE ../apps/desktop/src)
    target_link_libraries(history_payload_store_test PRIVATE Qt6::Core Qt6::Gui)
    photo_editor_apply_warnings(history_payload_store_test)
    add_test(NAME history_payload_store_test COMMAND history_payload_store_test)
    set_tests_properties(history_payload_store_test PROPERTIES TIMEOUT 60 LABELS "desktop;history;performance")
    add_executable(catalog_filter_benchmark catalog_filter_benchmark.cpp)
    target_include_directories(catalog_filter_benchmark PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_filter_benchmark PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
    photo_editor_apply_warnings(catalog_filter_benchmark)
    add_executable(catalog_filter_projection_test catalog_filter_projection_test.cpp)
    target_include_directories(catalog_filter_projection_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_filter_projection_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_filter_projection_test)
    add_test(NAME catalog_filter_projection_test COMMAND catalog_filter_projection_test)
    set_tests_properties(catalog_filter_projection_test PROPERTIES
      TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;catalog;performance")
    add_executable(catalog_filter_sort_semantics_test catalog_filter_sort_semantics_test.cpp)
    target_include_directories(catalog_filter_sort_semantics_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_filter_sort_semantics_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_filter_sort_semantics_test)
    add_test(NAME catalog_filter_sort_semantics_test COMMAND catalog_filter_sort_semantics_test)
    set_tests_properties(catalog_filter_sort_semantics_test PROPERTIES
      TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;catalog;performance")
    add_executable(catalog_timeline_reconciliation_benchmark catalog_timeline_reconciliation_benchmark.cpp)
    target_include_directories(catalog_timeline_reconciliation_benchmark PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_timeline_reconciliation_benchmark PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_timeline_reconciliation_benchmark)
    add_test(NAME catalog_timeline_reconciliation_contract COMMAND catalog_timeline_reconciliation_benchmark --verify)
    set_tests_properties(catalog_timeline_reconciliation_contract PROPERTIES TIMEOUT 30 LABELS "desktop;catalog;performance")
    add_executable(catalog_thumbnail_reuse_test catalog_thumbnail_reuse_test.cpp)
    add_executable(catalog_snapshot_benchmark catalog_snapshot_benchmark.cpp)
    target_include_directories(catalog_snapshot_benchmark PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_snapshot_benchmark PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_snapshot_benchmark)
    add_executable(catalog_snapshot_contract_test catalog_snapshot_contract_test.cpp)
    target_include_directories(catalog_snapshot_contract_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_snapshot_contract_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_snapshot_contract_test)
    add_test(NAME catalog_snapshot_contract_test COMMAND catalog_snapshot_contract_test)
    set_tests_properties(catalog_snapshot_contract_test PROPERTIES TIMEOUT 60 LABELS "desktop;catalog;performance")
    target_include_directories(catalog_thumbnail_reuse_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_thumbnail_reuse_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_thumbnail_reuse_test)
    add_test(NAME catalog_thumbnail_reuse_test COMMAND catalog_thumbnail_reuse_test)
    set_tests_properties(catalog_thumbnail_reuse_test PROPERTIES TIMEOUT 90 LABELS "desktop;catalog;performance")
    add_executable(catalog_culling_persistence_test catalog_culling_persistence_test.cpp)
    add_executable(catalog_metadata_persistence_test catalog_metadata_persistence_test.cpp)
    target_include_directories(catalog_metadata_persistence_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_metadata_persistence_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_metadata_persistence_test)
    add_test(NAME catalog_metadata_persistence_test COMMAND catalog_metadata_persistence_test)
    set_tests_properties(catalog_metadata_persistence_test PROPERTIES TIMEOUT 180 LABELS "desktop;catalog;metadata;recovery")
    add_executable(metadata_sidecar_test metadata_sidecar_test.cpp)
    target_include_directories(metadata_sidecar_test PRIVATE ../apps/desktop/src)
    target_link_libraries(metadata_sidecar_test PRIVATE Qt6::Core)
    photo_editor_apply_warnings(metadata_sidecar_test)
    add_test(NAME metadata_sidecar_test COMMAND metadata_sidecar_test)
    add_executable(library_workflow_controller_test library_workflow_controller_test.cpp
      ../apps/desktop/src/library_workflow_controller.h ../apps/desktop/src/library_workflow_controller.cpp)
    set_target_properties(library_workflow_controller_test PROPERTIES AUTOMOC ON)
    target_include_directories(library_workflow_controller_test PRIVATE ../apps/desktop/src)
    target_link_libraries(library_workflow_controller_test PRIVATE Qt6::Core Qt6::Test)
    photo_editor_apply_warnings(library_workflow_controller_test)
    add_test(NAME library_workflow_controller_test COMMAND library_workflow_controller_test)
    add_executable(library_delivery_panel_ui_test library_delivery_panel_ui_test.cpp
      ../apps/desktop/src/library_workflow_controller.h ../apps/desktop/src/library_workflow_controller.cpp)
    set_target_properties(library_delivery_panel_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(library_delivery_panel_ui_test PRIVATE ../apps/desktop/src)
    target_compile_definitions(library_delivery_panel_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(library_delivery_panel_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
    photo_editor_apply_warnings(library_delivery_panel_ui_test)
    add_test(NAME library_delivery_panel_ui_test COMMAND library_delivery_panel_ui_test)
    set_tests_properties(library_delivery_panel_ui_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 60 LABELS "desktop;library;delivery;ui")
    add_executable(catalog_duplicate_analysis_test catalog_duplicate_analysis_test.cpp ../apps/desktop/src/catalog_duplicate_analysis.cpp)
    target_include_directories(catalog_duplicate_analysis_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_duplicate_analysis_test PRIVATE Qt6::Core)
    photo_editor_apply_warnings(catalog_duplicate_analysis_test)
    add_test(NAME catalog_duplicate_analysis_test COMMAND catalog_duplicate_analysis_test)
    target_include_directories(catalog_culling_persistence_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_culling_persistence_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_culling_persistence_test)
    add_test(NAME catalog_culling_persistence_test COMMAND catalog_culling_persistence_test)
    add_test(NAME catalog_culling_recovery_test COMMAND catalog_culling_persistence_test --contracts 4)
    set_tests_properties(catalog_culling_recovery_test PROPERTIES TIMEOUT 60 LABELS "desktop;catalog;recovery")
    set_tests_properties(catalog_culling_persistence_test PROPERTIES TIMEOUT 180 LABELS "desktop;catalog;recovery;performance")

    add_executable(catalog_project_membership_test catalog_project_membership_test.cpp)
    target_include_directories(catalog_project_membership_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_project_membership_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_project_membership_test)
    add_test(NAME catalog_project_membership_test COMMAND catalog_project_membership_test)
    set_tests_properties(catalog_project_membership_test PROPERTIES TIMEOUT 60 LABELS "desktop;catalog;recovery;performance")
    add_executable(catalog_media_duration_test catalog_media_duration_test.cpp)
    target_include_directories(catalog_media_duration_test PRIVATE ../apps/desktop/src)
    target_link_libraries(catalog_media_duration_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Concurrent)
    photo_editor_apply_warnings(catalog_media_duration_test)
    add_test(NAME catalog_media_duration_test COMMAND catalog_media_duration_test)
    set_tests_properties(catalog_media_duration_test PROPERTIES TIMEOUT 30 LABELS "desktop;catalog;performance")
  endif()

  add_executable(editor_recipe_save_journal_test editor_recipe_save_journal_test.cpp)
  add_executable(editor_recipe_save_benchmark editor_recipe_save_benchmark.cpp)
  add_executable(preview_cpu_pipeline_benchmark_test preview_cpu_pipeline_benchmark_test.cpp)
  target_include_directories(preview_cpu_pipeline_benchmark_test PRIVATE ../apps/desktop/src)
  target_link_libraries(preview_cpu_pipeline_benchmark_test PRIVATE
    desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(preview_cpu_pipeline_benchmark_test)
  add_test(NAME preview_cpu_pipeline_benchmark_test COMMAND preview_cpu_pipeline_benchmark_test
    "${CMAKE_SOURCE_DIR}/apps/desktop/assets/presets/mountain-preview.jpg"
    "${CMAKE_CURRENT_BINARY_DIR}/preview-cpu-pipeline-receipt.json")
  set_tests_properties(preview_cpu_pipeline_benchmark_test PROPERTIES
    RUN_SERIAL TRUE TIMEOUT 120 LABELS "desktop;preview;performance")
  add_executable(export_exposure_gpu_benchmark_test export_exposure_gpu_benchmark_test.cpp)
  target_include_directories(export_exposure_gpu_benchmark_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_exposure_gpu_benchmark_test PRIVATE
    desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent
    ${photo_editor_desktop_image_decoder_frameworks})
  photo_editor_apply_warnings(export_exposure_gpu_benchmark_test)
  add_test(NAME export_exposure_gpu_benchmark_test COMMAND export_exposure_gpu_benchmark_test
    --report "${CMAKE_CURRENT_BINARY_DIR}/export-exposure-gpu-receipt.json")
  set_tests_properties(export_exposure_gpu_benchmark_test PROPERTIES
    RUN_SERIAL TRUE TIMEOUT 180 SKIP_RETURN_CODE 77 LABELS "desktop;export;metal;performance;qualification")
  target_include_directories(editor_recipe_save_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_recipe_save_benchmark PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(editor_recipe_save_benchmark)
  target_include_directories(editor_recipe_save_journal_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_recipe_save_journal_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(editor_recipe_save_journal_test)
  add_test(NAME editor_recipe_save_journal_test COMMAND editor_recipe_save_journal_test)
  set_tests_properties(editor_recipe_save_journal_test PROPERTIES TIMEOUT 30 LABELS "desktop;persistence;recovery")
  add_executable(editor_recipe_save_journal_portable_test editor_recipe_save_journal_test.cpp
    ../apps/desktop/src/editor_recipe_save_journal.cpp)
  target_include_directories(editor_recipe_save_journal_portable_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(editor_recipe_save_journal_portable_test PRIVATE PHOTO_EDITOR_TEST_PORTABLE_CHECKPOINT_SHA256=1)
  target_link_libraries(editor_recipe_save_journal_portable_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  photo_editor_apply_warnings(editor_recipe_save_journal_portable_test)
  add_test(NAME editor_recipe_save_journal_portable_test COMMAND editor_recipe_save_journal_portable_test)
  set_tests_properties(editor_recipe_save_journal_portable_test PROPERTIES TIMEOUT 30 LABELS "desktop;persistence;recovery")
  target_include_directories(editor_recipe_catalog_roundtrip_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_recipe_catalog_roundtrip_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network photo_engine)
  photo_editor_apply_warnings(editor_recipe_catalog_roundtrip_test)
  add_test(NAME editor_recipe_catalog_roundtrip_test COMMAND editor_recipe_catalog_roundtrip_test)
  set_tests_properties(editor_recipe_catalog_roundtrip_test PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;persistence;postgres")

  add_executable(photo_mask_layer_relaunch_workflow_test photo_mask_layer_relaunch_workflow_test.cpp)
  target_include_directories(photo_mask_layer_relaunch_workflow_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(photo_mask_layer_relaunch_workflow_test PRIVATE
    PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(photo_mask_layer_relaunch_workflow_test PRIVATE
    desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network photo_engine)
  photo_editor_apply_warnings(photo_mask_layer_relaunch_workflow_test)
  add_test(NAME photo_mask_layer_relaunch_workflow_test
    COMMAND photo_mask_layer_relaunch_workflow_test)
  set_tests_properties(photo_mask_layer_relaunch_workflow_test PROPERTIES TIMEOUT 600
    SKIP_RETURN_CODE 77 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;persistence;postgres;layer-mask;real-photo;process-reopen")

  add_executable(source_layer_thumbnail_test source_layer_thumbnail_test.cpp)
  add_executable(openexr_source_color_test openexr_source_color_test.cpp)
  target_include_directories(openexr_source_color_test PRIVATE ../apps/desktop/src)
  target_link_libraries(openexr_source_color_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui photo_engine)
  if(PHOTO_EDITOR_ENABLE_OPENEXR)
    find_package(OpenEXR ${PHOTO_EDITOR_OPENEXR_MINIMUM_VERSION} CONFIG QUIET)
  endif()
  if(TARGET OpenEXR::OpenEXR)
    target_link_libraries(openexr_source_color_test PRIVATE OpenEXR::OpenEXR)
    target_compile_definitions(openexr_source_color_test PRIVATE PHOTO_EDITOR_TEST_OPENEXR_HEADERS=1)
  endif()
  photo_editor_apply_warnings(openexr_source_color_test)
  add_test(NAME openexr_source_color_test COMMAND openexr_source_color_test)
  set_tests_properties(openexr_source_color_test PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 LABELS "desktop;color;exr")
  add_executable(mask_thumbnail_color_test mask_thumbnail_color_test.cpp)
  add_executable(editor_mask_sampling_test editor_mask_sampling_test.cpp)
  add_executable(active_mask_preview_worker_test active_mask_preview_worker_test.cpp)
  target_include_directories(active_mask_preview_worker_test PRIVATE ../apps/desktop/src)
  target_link_libraries(active_mask_preview_worker_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(active_mask_preview_worker_test)
  add_test(NAME active_mask_preview_worker_test COMMAND active_mask_preview_worker_test)
  set_tests_properties(active_mask_preview_worker_test PROPERTIES TIMEOUT 30 LABELS "desktop;masks;async")
  target_include_directories(editor_mask_sampling_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_mask_sampling_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(editor_mask_sampling_test)
  add_test(NAME editor_mask_sampling_test COMMAND editor_mask_sampling_test)
  set_tests_properties(editor_mask_sampling_test PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;color;masks")
  target_include_directories(mask_thumbnail_color_test PRIVATE ../apps/desktop/src)
  target_link_libraries(mask_thumbnail_color_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(mask_thumbnail_color_test)
  add_test(NAME mask_thumbnail_color_test COMMAND mask_thumbnail_color_test)
  set_tests_properties(mask_thumbnail_color_test PROPERTIES TIMEOUT 90 LABELS "desktop;color;masks")
  target_include_directories(source_layer_thumbnail_test PRIVATE ../apps/desktop/src)
  target_link_libraries(source_layer_thumbnail_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(source_layer_thumbnail_test)
  add_test(NAME source_layer_thumbnail_test COMMAND source_layer_thumbnail_test)
  set_tests_properties(source_layer_thumbnail_test PROPERTIES TIMEOUT 60 LABELS "desktop;color;layers")

  add_executable(source_layer_relink_test source_layer_relink_test.cpp)
  add_executable(editor_crop_zoom_test editor_crop_zoom_test.cpp)
  target_include_directories(editor_crop_zoom_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_crop_zoom_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(editor_crop_zoom_test)
  add_test(NAME editor_crop_zoom_test COMMAND editor_crop_zoom_test)
  set_tests_properties(editor_crop_zoom_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;crop;render")
  target_include_directories(source_layer_relink_test PRIVATE ../apps/desktop/src)
  target_link_libraries(source_layer_relink_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(source_layer_relink_test)
  add_test(NAME source_layer_relink_test COMMAND source_layer_relink_test)
  set_tests_properties(source_layer_relink_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;layers")

  add_executable(
    render_golden_test
    render_golden_test.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources})
  target_include_directories(
    render_golden_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    render_golden_test
    PRIVATE
      Qt6::Core
      Qt6::Gui
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(render_golden_test)

  add_test(NAME render_golden_test COMMAND render_golden_test)
  # Preview refinement has a strict wall-clock budget, so isolate it from the
  # parallel desktop suite to keep the performance assertion deterministic.
  set_tests_properties(render_golden_test PROPERTIES RUN_SERIAL TRUE)

  add_executable(
    export_job_controller_test
    export_job_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/export_job_controller.h
    ../apps/desktop/src/export_job_controller.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(export_job_controller_test PROPERTIES AUTOMOC ON)
  target_sources(export_job_controller_test PRIVATE ../apps/desktop/src/timeline_video_grade_runtime.cpp)
  target_include_directories(
    export_job_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    export_job_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(export_job_controller_test)

  add_test(NAME export_job_controller_test COMMAND export_job_controller_test)

  add_executable(
    carousel_document_controller_test
    carousel_document_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/carousel_document_controller.h
    ../apps/desktop/src/carousel_document_controller.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ${photo_editor_desktop_vision_sources})
  set_target_properties(carousel_document_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    carousel_document_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    carousel_document_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(carousel_document_controller_test)

  add_test(NAME carousel_document_controller_test COMMAND carousel_document_controller_test)
  add_test(NAME campaign_delivery_workflow_test COMMAND carousel_document_controller_test --delivery-formats)
  set_tests_properties(campaign_delivery_workflow_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;delivery;workflow")
  add_test(NAME edit_board_document_test COMMAND carousel_document_controller_test --edit-boards)
  add_test(NAME board_origin_lifecycle_test COMMAND carousel_document_controller_test --board-origin)
  set_tests_properties(board_origin_lifecycle_test PROPERTIES RUN_SERIAL TRUE TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;authoring;boards;source;workflow")
  add_test(NAME creative_board_ux_test COMMAND carousel_document_controller_test --creative-ux)
  set_tests_properties(creative_board_ux_test PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
  add_test(NAME board_owned_recipe_persistence_test COMMAND photo_pixel_paint_test --board-owned-persistence)
  set_tests_properties(edit_board_document_test PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
  add_test(
    NAME carousel_adjustment_patch_round_trip_test
    COMMAND carousel_document_controller_test --adjustment-patch-only)

  add_executable(
    timeline_document_controller_test
    timeline_document_controller_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/job_runner.h
    ../apps/desktop/src/job_runner.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp
    ../apps/desktop/src/timeline_graphics_track_compaction.cpp
    ../apps/desktop/src/timeline_edit_journal_codec.h
    ../apps/desktop/src/timeline_edit_journal_codec.cpp
    timeline_mask_frame_cache_test.cpp)
  set_target_properties(timeline_document_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    timeline_document_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_compile_definitions(timeline_document_controller_test PRIVATE
    PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(
    timeline_document_controller_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      ZLIB::ZLIB
      Qt6::Qml
      Qt6::Quick
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_add_creative_fonts(timeline_document_controller_test)
  photo_editor_apply_warnings(timeline_document_controller_test)

  add_executable(
    timeline_mcp_export_range_test
    timeline_mcp_export_range_test.cpp)
  target_include_directories(
    timeline_mcp_export_range_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_mcp_export_range_test
    PRIVATE
      Qt6::Core
      Qt6::Gui)
  photo_editor_apply_warnings(timeline_mcp_export_range_test)

  add_executable(
    timeline_track_audio_state_command_test
    timeline_track_audio_state_command_test.cpp)
  target_include_directories(
    timeline_track_audio_state_command_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_track_audio_state_command_test
    PRIVATE
      desktop_shell_test_runtime
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      photo_engine)
  photo_editor_apply_warnings(timeline_track_audio_state_command_test)

  add_executable(
    timeline_clip_retime_action_test
    timeline_clip_retime_action_test.cpp)
  target_include_directories(
    timeline_clip_retime_action_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_clip_retime_action_test
    PRIVATE
      desktop_shell_test_runtime
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      photo_engine)
  photo_editor_apply_warnings(timeline_clip_retime_action_test)

  find_package(Threads REQUIRED)
  add_executable(
    timeline_keyframe_json_array_cache_test
    timeline_keyframe_json_array_cache_test.cpp)
  target_include_directories(
    timeline_keyframe_json_array_cache_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_keyframe_json_array_cache_test
    PRIVATE
      Qt6::Core
      Threads::Threads)
  photo_editor_apply_warnings(timeline_keyframe_json_array_cache_test)
  add_test(
    NAME timeline_keyframe_json_array_cache_test
    COMMAND timeline_keyframe_json_array_cache_test)

  add_executable(
    timeline_keyframe_projection_cache_test
    timeline_keyframe_projection_cache_test.cpp)
  target_include_directories(
    timeline_keyframe_projection_cache_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_keyframe_projection_cache_test
    PRIVATE
      Qt6::Core)
  photo_editor_apply_warnings(timeline_keyframe_projection_cache_test)
  add_test(
    NAME timeline_keyframe_projection_cache_test
    COMMAND timeline_keyframe_projection_cache_test)
  set_tests_properties(timeline_keyframe_projection_cache_test PROPERTIES
    TIMEOUT 45
    LABELS "desktop;timeline;cache;retime")

  add_executable(timeline_bounded_ffmpeg_memory_policy_test
    timeline_bounded_ffmpeg_memory_policy_test.cpp)
  target_include_directories(timeline_bounded_ffmpeg_memory_policy_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_bounded_ffmpeg_memory_policy_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_bounded_ffmpeg_memory_policy_test)
  add_test(NAME timeline_bounded_ffmpeg_memory_policy_test
    COMMAND timeline_bounded_ffmpeg_memory_policy_test)

  add_executable(timeline_sparse_title_roi_test
    timeline_sparse_title_roi_test.cpp
    ../apps/desktop/src/timeline_sparse_title_roi.cpp)
  target_include_directories(timeline_sparse_title_roi_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_sparse_title_roi_test
    PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_sparse_title_roi_test)
  add_test(NAME timeline_sparse_title_roi_test
    COMMAND timeline_sparse_title_roi_test)
  set_tests_properties(timeline_sparse_title_roi_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;timeline;render;geometry")

  add_test(NAME timeline_document_controller_test COMMAND timeline_document_controller_test)
  add_test(
    NAME timeline_display_label_roundtrip_test
    COMMAND timeline_document_controller_test --timeline-display-label-roundtrip)
  set_tests_properties(timeline_display_label_roundtrip_test PROPERTIES TIMEOUT 90)
  add_test(
    NAME timeline_source_integrity_cancel_future_test
    COMMAND timeline_document_controller_test --timeline-source-integrity-cancel-future)
  set_tests_properties(timeline_source_integrity_cancel_future_test PROPERTIES TIMEOUT 30)
  add_test(
    NAME timeline_project_health_integrity_domains_test
    COMMAND timeline_document_controller_test --timeline-project-health-integrity-domains)
  set_tests_properties(timeline_project_health_integrity_domains_test PROPERTIES
    TIMEOUT 30 LABELS "desktop;timeline;integrity")
  add_test(
    NAME timeline_recovered_source_integrity_release_test
    COMMAND timeline_document_controller_test --timeline-recovered-source-integrity-release)
  set_tests_properties(timeline_recovered_source_integrity_release_test PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;timeline;recovery;integrity")
  add_test(NAME timeline_empty_preview_cache_counts_test
    COMMAND timeline_document_controller_test --timeline-empty-preview-cache-counts)
  set_tests_properties(timeline_empty_preview_cache_counts_test PROPERTIES
    TIMEOUT 30 LABELS "desktop;timeline;performance;render-cache")
  add_test(
    NAME timeline_mask_frame_cache_unit_test
    COMMAND timeline_document_controller_test --timeline-mask-frame-cache-unit)
  set_tests_properties(timeline_mask_frame_cache_unit_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;mask;cache;pixel")
add_test(
  NAME timeline_sequence_track_snapshot_workflow_test
  COMMAND timeline_document_controller_test --timeline-sequence-track-snapshot-workflow)
set_tests_properties(timeline_sequence_track_snapshot_workflow_test PROPERTIES
  TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;projection;ui")
add_test(
  NAME timeline_selection_refresh_coalescing_test
  COMMAND timeline_document_controller_test --timeline-selection-refresh-coalescing)
set_tests_properties(timeline_selection_refresh_coalescing_test PROPERTIES
  TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;qml;selection")
add_test(
  NAME timeline_used_media_snapshot_workflow_test
  COMMAND timeline_document_controller_test --timeline-used-media-snapshot-workflow)
set_tests_properties(timeline_used_media_snapshot_workflow_test PROPERTIES
  TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;media;controller")
add_test(
  NAME timeline_owned_clip_map_snapshot_metacall_workflow_test
  COMMAND timeline_document_controller_test --timeline-owned-clip-map-snapshot-metacall-workflow)
set_tests_properties(timeline_owned_clip_map_snapshot_metacall_workflow_test PROPERTIES
  TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;projection;controller")
  add_test(NAME timeline_program_scope_workflow_test
    COMMAND timeline_document_controller_test --timeline-program-scope-workflow)
  add_test(NAME timeline_generated_source_workflow_test
    COMMAND timeline_document_controller_test --timeline-generated-source-workflow)
  set_tests_properties(timeline_generated_source_workflow_test PROPERTIES TIMEOUT 120)

  add_executable(
    timeline_delivery_test
    timeline_delivery_test.cpp
    ../apps/desktop/src/timeline_delivery.h
    ../apps/desktop/src/timeline_delivery.cpp)
  target_include_directories(timeline_delivery_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_delivery_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_delivery_test)
  add_test(NAME timeline_delivery_test COMMAND timeline_delivery_test)
  add_executable(timeline_interchange_test
    timeline_interchange_test.cpp
    ../apps/desktop/src/timeline_interchange.h
    ../apps/desktop/src/timeline_interchange.cpp)
  target_include_directories(timeline_interchange_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_interchange_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_interchange_test)
  add_test(NAME timeline_interchange_test COMMAND timeline_interchange_test)
  set_tests_properties(timeline_interchange_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;interchange")
  add_test(NAME timeline_otio_import_reopen_workflow_test
    COMMAND timeline_document_controller_test --timeline-otio-import-reopen-workflow)
  set_tests_properties(timeline_otio_import_reopen_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;interchange;workflow")
  set_tests_properties(timeline_delivery_test PROPERTIES TIMEOUT 180 LABELS "desktop;timeline;delivery")
  add_executable(timeline_desktop_light_field_test timeline_desktop_light_field_test.cpp)
  target_include_directories(timeline_desktop_light_field_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_desktop_light_field_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_desktop_light_field_test)
  add_test(NAME timeline_desktop_light_field_test COMMAND timeline_desktop_light_field_test)
  set_tests_properties(timeline_desktop_light_field_test PROPERTIES TIMEOUT 30 LABELS "desktop;timeline;motion")
  add_executable(timeline_vector_path_test timeline_vector_path_test.cpp)
  target_include_directories(timeline_vector_path_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_vector_path_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_vector_path_test)
  add_test(NAME timeline_vector_path_test COMMAND timeline_vector_path_test)
  set_tests_properties(timeline_vector_path_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(timeline_rig_scene_test timeline_rig_scene_test.cpp)
  target_include_directories(timeline_rig_scene_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_rig_scene_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_rig_scene_test)
  add_test(NAME timeline_rig_scene_test COMMAND timeline_rig_scene_test)
  set_tests_properties(timeline_rig_scene_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(timeline_rig_cache_test timeline_rig_cache_test.cpp
    ../apps/desktop/src/timeline_native_generator_frame_cache.cpp
    ../apps/desktop/src/timeline_desktop_light_field_metal_stub.cpp)
  target_include_directories(timeline_rig_cache_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_rig_cache_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_rig_cache_test)
  add_test(NAME timeline_rig_cache_test COMMAND timeline_rig_cache_test)
  set_tests_properties(timeline_rig_cache_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(timeline_fractal_noise_test
    timeline_fractal_noise_test.cpp
    ../apps/desktop/src/timeline_fractal_noise.cpp)
  target_include_directories(timeline_fractal_noise_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_fractal_noise_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_fractal_noise_test)
  add_test(NAME timeline_fractal_noise_test COMMAND timeline_fractal_noise_test)
  set_tests_properties(timeline_fractal_noise_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 45
    LABELS "desktop;timeline;generator;noise")

  add_executable(timeline_fractal_noise_source_test
    timeline_fractal_noise_source_test.cpp
    ../apps/desktop/src/timeline_fractal_noise.cpp
    ../apps/desktop/src/timeline_fractal_noise_source.cpp)
  target_include_directories(timeline_fractal_noise_source_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_fractal_noise_source_test
    PRIVATE Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  photo_editor_apply_warnings(timeline_fractal_noise_source_test)
  add_test(NAME timeline_fractal_noise_source_test COMMAND timeline_fractal_noise_source_test)
  set_tests_properties(timeline_fractal_noise_source_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 120
    LABELS "desktop;timeline;generator;noise;cache;provider")

  add_executable(timeline_frame_quad_warp_test
    timeline_frame_quad_warp_test.cpp
    ../apps/desktop/src/timeline_frame_quad_warp.cpp)
  target_include_directories(timeline_frame_quad_warp_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_frame_quad_warp_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_frame_quad_warp_test)
  add_test(NAME timeline_frame_quad_warp_test COMMAND timeline_frame_quad_warp_test)
  set_tests_properties(timeline_frame_quad_warp_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30
    LABELS "desktop;timeline;geometry;warp")

  add_executable(timeline_frame_quad_overscan_test
    timeline_frame_quad_overscan_test.cpp
    ../apps/desktop/src/timeline_frame_quad_warp.cpp
    ../apps/desktop/src/timeline_frame_quad_overscan.cpp)
  target_include_directories(timeline_frame_quad_overscan_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_frame_quad_overscan_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_frame_quad_overscan_test)
  add_test(NAME timeline_frame_quad_overscan_test COMMAND timeline_frame_quad_overscan_test)
  set_tests_properties(timeline_frame_quad_overscan_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 45
    LABELS "desktop;timeline;geometry;warp;alpha")

  if(APPLE)
    add_executable(timeline_compound_producer_identity_test
      timeline_compound_producer_identity_test.cpp
      ../apps/desktop/src/timeline_compound_producer_identity.cpp)
    target_include_directories(timeline_compound_producer_identity_test PRIVATE ../apps/desktop/src)
    target_link_libraries(timeline_compound_producer_identity_test PRIVATE Qt6::Core)
    photo_editor_apply_warnings(timeline_compound_producer_identity_test)
    add_test(NAME timeline_compound_producer_identity_test COMMAND timeline_compound_producer_identity_test)
    set_tests_properties(timeline_compound_producer_identity_test PROPERTIES TIMEOUT 45)
  endif()
  add_executable(timeline_source_matte_test timeline_source_matte_test.cpp
    ../apps/desktop/src/timeline_source_matte.cpp)
  target_include_directories(timeline_source_matte_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_source_matte_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_source_matte_test)
  add_test(NAME timeline_source_matte_test COMMAND timeline_source_matte_test)
  set_tests_properties(timeline_source_matte_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;render")
  add_test(NAME timeline_delivery_package_reopen_workflow_test
    COMMAND timeline_document_controller_test --timeline-delivery-package-reopen-workflow)
  set_tests_properties(timeline_delivery_package_reopen_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;delivery;workflow")
  add_test(NAME timeline_publish_revision_workflow_test
    COMMAND timeline_document_controller_test --timeline-publish-revision-workflow)
  set_tests_properties(timeline_publish_revision_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;delivery;workflow")

  add_test(NAME timeline_audio_delivery_workflow_test
    COMMAND timeline_document_controller_test --timeline-audio-delivery-workflow)
  set_tests_properties(timeline_audio_delivery_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;audio;delivery;workflow")
  add_test(NAME timeline_video_delivery_fidelity_test
    COMMAND timeline_document_controller_test --timeline-video-delivery-fidelity)
  set_tests_properties(timeline_video_delivery_fidelity_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;color;delivery;workflow")
  add_test(NAME timeline_spatial_source_grade_workflow_test
    COMMAND timeline_document_controller_test --timeline-spatial-source-grade-workflow)
  set_tests_properties(timeline_spatial_source_grade_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;color;effects;delivery;workflow")

  add_executable(timeline_pcm_audio_decoder_test timeline_pcm_audio_decoder_test.cpp)
  target_include_directories(timeline_pcm_audio_decoder_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_pcm_audio_decoder_test PRIVATE Qt6::Core Qt6::Multimedia)
  photo_editor_apply_warnings(timeline_pcm_audio_decoder_test)
  add_test(NAME timeline_pcm_audio_decoder_test COMMAND timeline_pcm_audio_decoder_test)
  set_tests_properties(timeline_pcm_audio_decoder_test PROPERTIES
    TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "desktop;timeline;audio;decode")

  add_test(NAME timeline_creative_finishes_workflow_test
    COMMAND timeline_document_controller_test --timeline-creative-finishes-workflow)
  set_tests_properties(timeline_creative_finishes_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;effects;workflow")
  add_test(NAME timeline_save_render_workflow_test COMMAND timeline_document_controller_test --timeline-save-render-workflow)
  set_tests_properties(timeline_save_render_workflow_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime)
  add_test(NAME timeline_source_crop_workflow_test COMMAND timeline_document_controller_test --timeline-source-crop-workflow)
  set_tests_properties(timeline_source_crop_workflow_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime LABELS "desktop;timeline;crop;workflow")
  add_test(NAME timeline_sequence_mask_workflow_test COMMAND timeline_document_controller_test --timeline-sequence-mask-workflow)
  set_tests_properties(timeline_sequence_mask_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;mask;workflow")
  add_test(NAME timeline_graphics_track_reorder_workflow_test
    COMMAND timeline_document_controller_test --timeline-graphics-track-reorder-workflow)
  set_tests_properties(timeline_graphics_track_reorder_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;graphics;stacking;undo;render;workflow")
  add_test(NAME timeline_graphics_track_compaction_workflow_test
    COMMAND timeline_document_controller_test --timeline-graphics-track-compaction-workflow)
  set_tests_properties(timeline_graphics_track_compaction_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;graphics;stacking;undo;render;workflow")
  add_test(NAME timeline_presentation_url_projection_test
    COMMAND timeline_document_controller_test --timeline-presentation-url-projection)
  set_tests_properties(timeline_presentation_url_projection_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;media;preview;workflow")
  add_test(NAME timeline_anchored_review_notes_workflow_test
    COMMAND timeline_document_controller_test --timeline-anchored-review-notes-workflow)
  set_tests_properties(timeline_anchored_review_notes_workflow_test PROPERTIES
    TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;review;notes;workflow")
  add_test(NAME timeline_gap_playback_notification_workflow_test
    COMMAND timeline_document_controller_test --timeline-gap-playback-notification-workflow)
  set_tests_properties(timeline_gap_playback_notification_workflow_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;playback;notifications;workflow")
  add_test(NAME timeline_geometry_floor_workflow_test
    COMMAND timeline_document_controller_test --timeline-geometry-floor-workflow)
  set_tests_properties(timeline_geometry_floor_workflow_test PROPERTIES
    TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;geometry;workflow")
  add_test(NAME timeline_clip_frame_base_animation_workflow_test
    COMMAND timeline_document_controller_test --timeline-clip-frame-base-animation-workflow)
  set_tests_properties(timeline_clip_frame_base_animation_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;geometry;animation;undo;variant;render;workflow")
  add_test(NAME timeline_animated_xy_perspective_export_workflow_test
    COMMAND timeline_document_controller_test --timeline-animated-xy-perspective-export-workflow)
  set_tests_properties(timeline_animated_xy_perspective_export_workflow_test PROPERTIES
    TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;animation;perspective;pixel;workflow")
  add_test(NAME timeline_frame_quad_workflow_test
    COMMAND timeline_document_controller_test --timeline-frame-quad-workflow)
  set_tests_properties(timeline_frame_quad_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;geometry;warp;workflow")
  add_test(NAME timeline_frame_shadow_render_workflow_test
    COMMAND timeline_document_controller_test --timeline-frame-shadow-render-workflow)
  set_tests_properties(timeline_frame_shadow_render_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;shadow;render;workflow")
  if(APPLE)
    add_test(NAME timeline_task_owned_compound_workflow_test
      COMMAND timeline_document_controller_test --timeline-task-owned-compound-workflow)
    set_tests_properties(timeline_task_owned_compound_workflow_test PROPERTIES
      TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
      LABELS "desktop;timeline;compound;alpha;recovery;workflow")
    add_test(NAME timeline_task_owned_compound_command_workflow_test
      COMMAND timeline_document_controller_test --timeline-task-owned-compound-command-workflow)
    set_tests_properties(timeline_task_owned_compound_command_workflow_test PROPERTIES
      TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
      LABELS "desktop;timeline;compound;mcp;recovery;workflow")
  endif()
  add_test(NAME timeline_matte_blend_render_workflow_test
    COMMAND timeline_document_controller_test --timeline-matte-blend-render-workflow)
  set_tests_properties(timeline_matte_blend_render_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;matte;blend;render;workflow")
  add_test(NAME timeline_dense_keyframe_render_workflow_test
    COMMAND timeline_document_controller_test --timeline-dense-keyframe-render-workflow)
  set_tests_properties(timeline_dense_keyframe_render_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;animation;render;workflow")
  add_test(NAME timeline_animated_zoom_rotation_export_workflow_test
    COMMAND timeline_document_controller_test --timeline-animated-zoom-rotation-export-workflow)
  set_tests_properties(timeline_animated_zoom_rotation_export_workflow_test PROPERTIES
    TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;animation;transform;render;pixel;workflow")
  add_test(NAME timeline_bounded_h264_export_workflow_test
    COMMAND timeline_document_controller_test --timeline-bounded-h264-export-workflow)
  set_tests_properties(timeline_bounded_h264_export_workflow_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;render;cache;workflow")
  add_test(NAME timeline_review_excerpt_export_workflow_test
    COMMAND timeline_document_controller_test --timeline-review-excerpt-export-workflow)
  set_tests_properties(timeline_review_excerpt_export_workflow_test PROPERTIES
    TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;render;range;audio;workflow")
  add_test(NAME timeline_sparse_title_filter_workflow_test
    COMMAND timeline_document_controller_test --timeline-sparse-title-filter)
  set_tests_properties(timeline_sparse_title_filter_workflow_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE
    LABELS "desktop;timeline;render;geometry;unit")
  add_test(NAME timeline_mcp_export_range_test COMMAND timeline_mcp_export_range_test)
  set_tests_properties(timeline_mcp_export_range_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE
    LABELS "desktop;timeline;mcp;range;integrity;unit")
  add_test(NAME timeline_track_audio_state_command_test
    COMMAND timeline_track_audio_state_command_test)
  set_tests_properties(timeline_track_audio_state_command_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;timeline;mcp;audio;track;undo;workflow")
  add_test(NAME timeline_clip_retime_action_test
    COMMAND timeline_clip_retime_action_test)
  set_tests_properties(timeline_clip_retime_action_test PROPERTIES
    TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;timeline;mcp;retime;undo;render;workflow")
  add_executable(timeline_review_output_verification_test
    timeline_review_output_verification_test.cpp)
  target_include_directories(timeline_review_output_verification_test PRIVATE
    ../apps/desktop/src)
  target_link_libraries(timeline_review_output_verification_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_review_output_verification_test)
  add_test(NAME timeline_review_output_verification_test
    COMMAND timeline_review_output_verification_test)
  set_tests_properties(timeline_review_output_verification_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE
    LABELS "desktop;timeline;render;range;verification;unit")
  add_test(NAME timeline_authored_range_probe_test
    COMMAND timeline_document_controller_test --timeline-authored-range-probe-test)
  set_tests_properties(timeline_authored_range_probe_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;render;range;diagnostic")
  add_test(NAME timeline_opacity_window_pruning_workflow_test
    COMMAND timeline_document_controller_test --timeline-opacity-window-pruning)
  set_tests_properties(timeline_opacity_window_pruning_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;render;cache;opacity;workflow")
  add_test(NAME timeline_duration_cache_workflow_test
    COMMAND timeline_document_controller_test --timeline-duration-cache-workflow)
  set_tests_properties(timeline_duration_cache_workflow_test PROPERTIES
    TIMEOUT 90 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;cache;duration;workflow")
  add_test(NAME timeline_mask_cache_pixel_workflow_test
    COMMAND timeline_document_controller_test --timeline-mask-cache-pixel-workflow)
  set_tests_properties(timeline_mask_cache_pixel_workflow_test PROPERTIES
    TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;mask;pixel;render;workflow")
  add_test(NAME timeline_agent_mutation_batch_workflow_test
    COMMAND timeline_document_controller_test --timeline-agent-mutation-batch-workflow)
  set_tests_properties(timeline_agent_mutation_batch_workflow_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;agent;batch;undo;workflow")
  add_test(NAME timeline_export_snapshot_readiness_workflow_test
    COMMAND timeline_document_controller_test --timeline-export-snapshot-readiness-workflow)
  set_tests_properties(timeline_export_snapshot_readiness_workflow_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;export;snapshot;readiness;workflow")
  add_test(NAME timeline_cache_pressure_boundaries_test
    COMMAND timeline_document_controller_test --timeline-cache-pressure-boundaries)
  set_tests_properties(timeline_cache_pressure_boundaries_test PROPERTIES
    TIMEOUT 30 LABELS "desktop;timeline;cache;workflow")
  add_test(
    NAME timeline_precision_trim_workflow_test
    COMMAND timeline_document_controller_test --timeline-precision-trim-workflow)
  set_tests_properties(timeline_precision_trim_workflow_test PROPERTIES
    TIMEOUT 45 LABELS "desktop;timeline;trim;workflow")

  add_test(
    NAME timeline_insertion_publication_workflow_test
    COMMAND timeline_document_controller_test --timeline-insertion-publication-workflow)
  set_tests_properties(timeline_insertion_publication_workflow_test PROPERTIES
    TIMEOUT 45 LABELS "desktop;timeline;performance;workflow")
  add_test(
    NAME timeline_color_grade_curve_workflow_test
    COMMAND timeline_document_controller_test --timeline-color-grade-curve-workflow)
  set_tests_properties(timeline_color_grade_curve_workflow_test PROPERTIES TIMEOUT 45)
  add_test(
    NAME timeline_cube_lut_workflow_test
    COMMAND timeline_document_controller_test --timeline-cube-lut-workflow)
  set_tests_properties(timeline_cube_lut_workflow_test PROPERTIES TIMEOUT 45)
  add_test(
    NAME timeline_shared_keyframe_curve_workflow_test
    COMMAND timeline_document_controller_test --timeline-shared-keyframe-curve-workflow)
  set_tests_properties(timeline_shared_keyframe_curve_workflow_test PROPERTIES TIMEOUT 45)
  add_test(
    NAME timeline_keyframe_coverage_workflow_test
    COMMAND timeline_document_controller_test --timeline-keyframe-coverage-workflow)
  set_tests_properties(timeline_keyframe_coverage_workflow_test PROPERTIES TIMEOUT 90)
  add_test(
    NAME timeline_effect_keyframe_render_workflow_test
    COMMAND timeline_document_controller_test --timeline-effect-keyframe-render-workflow)
  set_tests_properties(timeline_effect_keyframe_render_workflow_test PROPERTIES TIMEOUT 90)
  add_test(
    NAME timeline_title_motion_keyframe_workflow_test
    COMMAND timeline_document_controller_test --timeline-title-motion-keyframe-workflow)
  set_tests_properties(timeline_title_motion_keyframe_workflow_test PROPERTIES TIMEOUT 45)
  add_test(
    NAME timeline_title_motion_render_workflow_test
    COMMAND timeline_document_controller_test --timeline-title-motion-render-workflow)
  set_tests_properties(timeline_title_motion_render_workflow_test PROPERTIES TIMEOUT 90)
  add_test(
    NAME timeline_title_export_layout_workflow_test
    COMMAND timeline_document_controller_test --timeline-title-export-layout-workflow)
  set_tests_properties(timeline_title_export_layout_workflow_test PROPERTIES TIMEOUT 30)
  add_test(
    NAME timeline_title_preview_export_geometry_probe
    COMMAND timeline_document_controller_test --timeline-title-preview-export-geometry-probe)
  set_tests_properties(timeline_title_preview_export_geometry_probe PROPERTIES
    TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;timeline;title;render;diagnostic")
  add_test(
    NAME timeline_title_font_precision_workflow_test
    COMMAND timeline_document_controller_test --timeline-title-font-precision-workflow)
  set_tests_properties(timeline_title_font_precision_workflow_test PROPERTIES
    TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;timeline;title;font;save-reopen;render")
  add_test(
    NAME timeline_media_queue_terminal_paths_test
    COMMAND timeline_document_controller_test --timeline-media-queue-terminal-paths)
  set_tests_properties(timeline_media_queue_terminal_paths_test PROPERTIES
    TIMEOUT 30 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;media-analysis;queue")

  add_executable(
    timeline_recovery_test
    timeline_recovery_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(timeline_recovery_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    timeline_recovery_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_recovery_test
    PRIVATE
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  if(WIN32)
    target_link_libraries(timeline_recovery_test PRIVATE Psapi)
  endif()
  photo_editor_apply_warnings(timeline_recovery_test)

  add_test(NAME timeline_recovery_test COMMAND timeline_recovery_test)
  add_test(NAME timeline_recovery_lifecycle_test
    COMMAND timeline_recovery_test --recovery-lifecycle-only)
  set_tests_properties(timeline_recovery_lifecycle_test PROPERTIES
    TIMEOUT 90 RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;recovery;lifecycle")
  add_test(NAME timeline_journal_validation_cache_test
    COMMAND timeline_recovery_test --journal-validation-cache-only)
  set_tests_properties(timeline_journal_validation_cache_test PROPERTIES
    TIMEOUT 60 RUN_SERIAL TRUE
    RESOURCE_LOCK photo_editor_desktop_runtime
    LABELS "desktop;timeline;recovery;journal;cache")

  add_executable(
    timeline_tracking_workflow_test
    timeline_tracking_workflow_test.cpp
    ${photo_editor_desktop_observability_sources}
    ../apps/desktop/src/app_bootstrap.h
    ../apps/desktop/src/app_bootstrap.cpp
    ../apps/desktop/src/job_store.h
    ../apps/desktop/src/job_store.cpp
    ../apps/desktop/src/decoded_image_cache.h
    ../apps/desktop/src/decoded_image_cache.cpp
    ${photo_editor_desktop_mask_alpha_provider_sources}
    ../apps/desktop/src/photo_render_utils.h
    ${photo_editor_desktop_photo_render_utils_sources}
    ${photo_editor_desktop_recipe_v13_sources}
    ${photo_editor_desktop_render_plan_store_sources}
    ${photo_editor_desktop_image_decoder_sources}
    ../apps/desktop/src/timeline_asset_store.h
    ../apps/desktop/src/timeline_asset_store.cpp
    ${photo_editor_desktop_vision_sources}
    ../apps/desktop/src/timeline_video_grade_runtime.h
    ../apps/desktop/src/timeline_video_grade_runtime.cpp
    ../apps/desktop/src/timeline_document_controller.h
    ../apps/desktop/src/timeline_document_controller.cpp)
  set_target_properties(timeline_tracking_workflow_test PROPERTIES AUTOMOC ON)
  target_include_directories(timeline_tracking_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(
    timeline_tracking_workflow_test
    PRIVATE Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets
            ${photo_editor_desktop_image_decoder_frameworks} photo_engine)
  if(WIN32)
    target_link_libraries(timeline_tracking_workflow_test PRIVATE Psapi)
  endif()
  photo_editor_apply_warnings(timeline_tracking_workflow_test)
  add_test(NAME timeline_tracking_workflow_test COMMAND timeline_tracking_workflow_test)
  set_tests_properties(
    timeline_tracking_workflow_test
    PROPERTIES TIMEOUT 180 LABELS "desktop;timeline;tracking;workflow")

  add_executable(carousel_preview_worker_test carousel_preview_worker_test.cpp)
  target_include_directories(carousel_preview_worker_test PRIVATE ../apps/desktop/src)
  target_link_libraries(carousel_preview_worker_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(carousel_preview_worker_test)
  add_test(NAME carousel_preview_worker_test COMMAND carousel_preview_worker_test)
  set_tests_properties(carousel_preview_worker_test PROPERTIES TIMEOUT 60 LABELS "desktop;carousel;performance")

  add_executable(workspace_picker_refresh_test workspace_picker_refresh_test.cpp)
  set_target_properties(workspace_picker_refresh_test PROPERTIES AUTOMOC ON)
  target_include_directories(workspace_picker_refresh_test PRIVATE ../apps/desktop/src)
  target_compile_definitions(workspace_picker_refresh_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(workspace_picker_refresh_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::QuickControls2)
  photo_editor_apply_warnings(workspace_picker_refresh_test)
  add_test(NAME workspace_picker_refresh_contract COMMAND workspace_picker_refresh_test --verify)
  set_tests_properties(workspace_picker_refresh_contract PROPERTIES TIMEOUT 90)

  add_executable(carousel_projection_test carousel_projection_test.cpp)
  target_include_directories(carousel_projection_test PRIVATE ../apps/desktop/src)
  target_link_libraries(carousel_projection_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(carousel_projection_test)
  add_test(NAME carousel_projection_test COMMAND carousel_projection_test)
  set_tests_properties(carousel_projection_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;carousel;performance")
  foreach(selection_count IN ITEMS 1 8 256)
    add_test(NAME carousel_selection_admission_${selection_count}_test
      COMMAND carousel_projection_test 10000 5
        "${CMAKE_CURRENT_BINARY_DIR}/carousel-selection-admission-${selection_count}.json"
        ${selection_count} admission)
    set_tests_properties(carousel_selection_admission_${selection_count}_test PROPERTIES
      TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;carousel;selection;performance")
  endforeach()

  add_executable(carousel_history_benchmark carousel_history_benchmark.cpp)
  target_include_directories(carousel_history_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(carousel_history_benchmark PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(carousel_history_benchmark)
  add_test(NAME carousel_history_serialization_contract COMMAND carousel_history_benchmark --verify)
  set_tests_properties(carousel_history_serialization_contract PROPERTIES TIMEOUT 60)
  add_executable(carousel_history_workflow_regression carousel_document_controller_test.cpp)
  target_include_directories(carousel_history_workflow_regression PRIVATE ../apps/desktop/src)
  target_link_libraries(carousel_history_workflow_regression PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(carousel_history_workflow_regression)
  add_test(NAME carousel_history_adjustment_regression
    COMMAND carousel_history_workflow_regression --adjustment-patch-only)
  set_tests_properties(carousel_history_adjustment_regression PROPERTIES TIMEOUT 60)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME carousel_history_serialized_inputs_contract
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/carousel_serialized_inputs_contract.py)
  endif()


  add_executable(canvas_guides_ui_test canvas_guides_ui_test.cpp)
  target_compile_definitions(canvas_guides_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(canvas_guides_ui_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(canvas_guides_ui_test)
  add_test(NAME canvas_guides_ui_test COMMAND canvas_guides_ui_test)
  set_tests_properties(canvas_guides_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" LABELS "desktop;guides;interaction;ui")

  add_executable(editor_shortcut_registry_test editor_shortcut_registry_test.cpp)
  target_include_directories(editor_shortcut_registry_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_shortcut_registry_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(editor_shortcut_registry_test)
  add_test(NAME editor_shortcut_registry_test COMMAND editor_shortcut_registry_test)

  add_executable(editor_shortcut_dialog_ui_test editor_shortcut_dialog_ui_test.cpp)
  target_compile_definitions(editor_shortcut_dialog_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_include_directories(editor_shortcut_dialog_ui_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_shortcut_dialog_ui_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(editor_shortcut_dialog_ui_test)
  add_test(NAME editor_shortcut_dialog_ui_test COMMAND editor_shortcut_dialog_ui_test)
  set_tests_properties(editor_shortcut_dialog_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;shortcuts;ui")

  foreach(timeline_ux_test IN ITEMS timeline_effect_discovery_ui_test timeline_media_browser_ui_test timeline_track_surface_ui_test timeline_dense_surface_ui_test timeline_virtual_clip_rows_ui_test timeline_inspector_mini_lanes_ui_test timeline_static_animation_bindings_test)
    add_executable(${timeline_ux_test} ${timeline_ux_test}.cpp)
    target_compile_definitions(${timeline_ux_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(${timeline_ux_test} PRIVATE Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    photo_editor_apply_warnings(${timeline_ux_test})
    add_test(NAME ${timeline_ux_test} COMMAND ${timeline_ux_test})
    set_tests_properties(${timeline_ux_test} PROPERTIES TIMEOUT 60
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;ui")
  endforeach()

  if(TARGET Qt6::Multimedia)
    if(APPLE)
      add_executable(timeline_audio_layer_playback_test timeline_audio_layer_playback_test.cpp
        ../apps/desktop/src/timeline_audio_output.cpp ../apps/desktop/src/timeline_audio_output.h)
      set_target_properties(timeline_audio_layer_playback_test PROPERTIES AUTOMOC ON)
      target_include_directories(timeline_audio_layer_playback_test PRIVATE ../apps/desktop/src)
      target_compile_definitions(timeline_audio_layer_playback_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
      target_link_libraries(timeline_audio_layer_playback_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Test photo_engine)
      photo_editor_apply_warnings(timeline_audio_layer_playback_test)
      add_test(NAME timeline_audio_layer_playback_test COMMAND timeline_audio_layer_playback_test)
      set_tests_properties(timeline_audio_layer_playback_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_MEDIA_BACKEND=darwin" LABELS "desktop;timeline;audio;native-layer")
    endif()
    add_executable(timeline_media_player_lifecycle_test timeline_media_player_lifecycle_test.cpp)
    target_compile_definitions(timeline_media_player_lifecycle_test
      PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(timeline_media_player_lifecycle_test
      PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Multimedia Qt6::Test)
    photo_editor_apply_warnings(timeline_media_player_lifecycle_test)
    add_test(NAME timeline_media_player_lifecycle_test COMMAND timeline_media_player_lifecycle_test)
    set_tests_properties(timeline_media_player_lifecycle_test PROPERTIES
      TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      LABELS "desktop;timeline;playback;lifecycle")
  endif()

  add_executable(timeline_workspace_loader_async_transition_test
    timeline_workspace_loader_async_transition_test.cpp)
  target_compile_definitions(timeline_workspace_loader_async_transition_test
    PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_workspace_loader_async_transition_test
    PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(timeline_workspace_loader_async_transition_test)
  add_test(NAME timeline_workspace_loader_async_transition_test
    COMMAND timeline_workspace_loader_async_transition_test)
  set_tests_properties(timeline_workspace_loader_async_transition_test PROPERTIES
    TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;timeline;workspace;loader")

  add_executable(timeline_virtual_track_rows_ui_test timeline_virtual_track_rows_ui_test.cpp)
  target_compile_definitions(
    timeline_virtual_track_rows_ui_test
    PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    timeline_virtual_track_rows_ui_test
    PRIVATE Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(timeline_virtual_track_rows_ui_test)
  add_test(
    NAME timeline_virtual_track_rows_ui_test
    COMMAND timeline_virtual_track_rows_ui_test)
  set_tests_properties(
    timeline_virtual_track_rows_ui_test
    PROPERTIES
      TIMEOUT 60
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
      LABELS "desktop;timeline;ui;virtualization")

  add_executable(timeline_overview_ui_test timeline_overview_ui_test.cpp)
  target_compile_definitions(timeline_overview_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_overview_ui_test PRIVATE
    Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(timeline_overview_ui_test)
  add_test(NAME timeline_overview_ui_test COMMAND timeline_overview_ui_test)
  set_tests_properties(timeline_overview_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;ui;overview")
  set_target_properties(timeline_overview_ui_test PROPERTIES AUTOMOC ON)

  add_executable(timeline_horizontal_wheel_handler_ui_test
    timeline_horizontal_wheel_handler_ui_test.cpp)
  target_compile_definitions(
    timeline_horizontal_wheel_handler_ui_test
    PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(
    timeline_horizontal_wheel_handler_ui_test
    PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Test)
  photo_editor_apply_warnings(timeline_horizontal_wheel_handler_ui_test)
  add_test(
    NAME timeline_horizontal_wheel_handler_ui_test
    COMMAND timeline_horizontal_wheel_handler_ui_test)
  set_tests_properties(
    timeline_horizontal_wheel_handler_ui_test
    PROPERTIES TIMEOUT 60
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
      LABELS "desktop;timeline;interaction;wheel;ui")

  add_executable(timeline_workspace_layout_state_test timeline_workspace_layout_state_test.cpp)
  target_compile_definitions(timeline_workspace_layout_state_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_workspace_layout_state_test PRIVATE Qt6::Core Qt6::Qml)
  photo_editor_apply_warnings(timeline_workspace_layout_state_test)
  add_test(NAME timeline_workspace_layout_state_test COMMAND timeline_workspace_layout_state_test)
  set_tests_properties(timeline_workspace_layout_state_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;workspace")

  add_executable(timeline_panel_dock_state_test timeline_panel_dock_state_test.cpp)
  target_compile_definitions(timeline_panel_dock_state_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_panel_dock_state_test PRIVATE Qt6::Core Qt6::Qml)
  photo_editor_apply_warnings(timeline_panel_dock_state_test)
  add_test(NAME timeline_panel_dock_state_test COMMAND timeline_panel_dock_state_test)
  set_tests_properties(timeline_panel_dock_state_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;workspace")

  foreach(timeline_visible_ui_test IN ITEMS timeline_panel_dock_ui_test timeline_tool_palette_ui_test)
    add_executable(${timeline_visible_ui_test} ${timeline_visible_ui_test}.cpp)
    target_compile_definitions(${timeline_visible_ui_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(${timeline_visible_ui_test} PRIVATE Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    photo_editor_apply_warnings(${timeline_visible_ui_test})
    add_test(NAME ${timeline_visible_ui_test} COMMAND ${timeline_visible_ui_test})
    set_tests_properties(${timeline_visible_ui_test} PROPERTIES TIMEOUT 60
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;ui")
  endforeach()

  foreach(timeline_direct_ui_test IN ITEMS timeline_color_console_ui_test timeline_audio_mixer_ui_test timeline_direct_edit_tools_test timeline_editorial_panel_ui_test timeline_photo_filters_test)
    add_executable(${timeline_direct_ui_test} ${timeline_direct_ui_test}.cpp)
    target_compile_definitions(${timeline_direct_ui_test} PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_include_directories(${timeline_direct_ui_test} PRIVATE ../apps/desktop/src)
    target_link_libraries(${timeline_direct_ui_test} PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
    photo_editor_apply_warnings(${timeline_direct_ui_test})
    add_test(NAME ${timeline_direct_ui_test} COMMAND ${timeline_direct_ui_test})
    set_tests_properties(${timeline_direct_ui_test} PROPERTIES TIMEOUT 90
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1" LABELS "desktop;timeline;ui")
  endforeach()
  set_target_properties(timeline_color_console_ui_test PROPERTIES AUTOMOC ON)
  add_executable(timeline_effect_catalog_ui_test timeline_effect_catalog_ui_test.cpp)
  target_compile_definitions(timeline_effect_catalog_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_include_directories(timeline_effect_catalog_ui_test PRIVATE ../engine/include)
  target_link_libraries(timeline_effect_catalog_ui_test PRIVATE Qt6::Core Qt6::Qml)
  photo_editor_apply_warnings(timeline_effect_catalog_ui_test)
  add_test(NAME timeline_effect_catalog_ui_test COMMAND timeline_effect_catalog_ui_test)
  set_tests_properties(timeline_effect_catalog_ui_test PROPERTIES TIMEOUT 30 LABELS "desktop;timeline;effects;ui")
  add_executable(timeline_shared_edit_color_test timeline_shared_edit_color_test.cpp)
  target_include_directories(timeline_shared_edit_color_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_shared_edit_color_test PRIVATE desktop_shell_test_runtime
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Concurrent Qt6::Test photo_engine)
  photo_editor_apply_warnings(timeline_shared_edit_color_test)
  add_test(NAME timeline_shared_edit_color_test COMMAND timeline_shared_edit_color_test)
  set_tests_properties(timeline_shared_edit_color_test PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;timeline;color;effects;parity")
  add_executable(timeline_photo_grade_test timeline_photo_grade_test.cpp)
  target_include_directories(timeline_photo_grade_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_photo_grade_test PRIVATE desktop_shell_test_runtime
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent Qt6::Test photo_engine)
  photo_editor_apply_warnings(timeline_photo_grade_test)
  add_test(NAME timeline_photo_grade_test COMMAND timeline_photo_grade_test)
  set_tests_properties(timeline_photo_grade_test PROPERTIES TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    LABELS "desktop;timeline;color;photo;parity")
  add_test(NAME timeline_shared_edit_color_ui_test
    COMMAND actual_app_workflow_benchmark_driver --timeline-shared-edit-color-proof
      --source "${PROJECT_SOURCE_DIR}/marketing/media/stock/landscape-mountain.jpg"
      --capture-dir "${CMAKE_CURRENT_BINARY_DIR}/timeline-shared-edit-color-ui")
  set_tests_properties(timeline_shared_edit_color_ui_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE=1"
    LABELS "desktop;timeline;color;effects;ui;parity")
  set_target_properties(timeline_editorial_panel_ui_test PROPERTIES AUTOMOC ON)
  set_target_properties(timeline_photo_filters_test PROPERTIES AUTOMOC ON)
  target_link_libraries(timeline_photo_filters_test PRIVATE photo_engine)


  add_executable(timeline_voiceover_panel_ui_test timeline_voiceover_panel_ui_test.cpp)
  target_compile_definitions(timeline_voiceover_panel_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_voiceover_panel_ui_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(timeline_voiceover_panel_ui_test)
  add_test(NAME timeline_voiceover_panel_ui_test COMMAND timeline_voiceover_panel_ui_test)
  set_tests_properties(timeline_voiceover_panel_ui_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;audio;ui")

  add_executable(timeline_review_panel_ui_test timeline_review_panel_ui_test.cpp)
  target_compile_definitions(timeline_review_panel_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(timeline_review_panel_ui_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  photo_editor_apply_warnings(timeline_review_panel_ui_test)
  set_target_properties(timeline_review_panel_ui_test PROPERTIES AUTOMOC ON)
  add_test(NAME timeline_review_panel_ui_test COMMAND timeline_review_panel_ui_test)
  set_tests_properties(timeline_review_panel_ui_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;ui")

  add_executable(timeline_audio_output_test timeline_audio_output_test.cpp)
  target_include_directories(timeline_audio_output_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_audio_output_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Multimedia photo_engine)
  photo_editor_apply_warnings(timeline_audio_output_test)
  add_test(NAME timeline_audio_output_test COMMAND timeline_audio_output_test)
  set_tests_properties(timeline_audio_output_test PROPERTIES TIMEOUT 90
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;audio")

  add_executable(timeline_caption_validation_test timeline_caption_validation_test.cpp)
  target_include_directories(timeline_caption_validation_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_caption_validation_test PRIVATE desktop_shell_test_runtime Qt6::Widgets photo_engine)
  photo_editor_apply_warnings(timeline_caption_validation_test)
  add_test(NAME timeline_caption_validation_test COMMAND timeline_caption_validation_test)
  set_tests_properties(timeline_caption_validation_test PROPERTIES TIMEOUT 60
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;captions")

  add_executable(timeline_cache_inspection_test timeline_cache_inspection_test.cpp)
  target_include_directories(timeline_cache_inspection_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_cache_inspection_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(timeline_cache_inspection_test)
  add_test(NAME timeline_cache_inspection_test COMMAND timeline_cache_inspection_test)
  add_test(NAME timeline_cache_recovery_test COMMAND timeline_cache_inspection_test --contracts)
  set_tests_properties(timeline_cache_recovery_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;cache;recovery")
  set_tests_properties(timeline_cache_inspection_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;cache;performance")

  add_executable(mask_count_projection_test mask_count_projection_test.cpp)
  target_include_directories(mask_count_projection_test PRIVATE ../apps/desktop/src)
  target_link_libraries(mask_count_projection_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(mask_count_projection_test)
  add_test(NAME mask_count_projection_test COMMAND mask_count_projection_test --verify)
  set_tests_properties(mask_count_projection_test PROPERTIES
    TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;mask;performance")

  add_executable(timeline_multicam_projection_test timeline_multicam_projection_test.cpp)
  target_include_directories(timeline_multicam_projection_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_multicam_projection_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(timeline_multicam_projection_test)
  add_test(NAME timeline_multicam_projection_test COMMAND timeline_multicam_projection_test)
  set_tests_properties(timeline_multicam_projection_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;performance")

  add_executable(timeline_history_benchmark timeline_history_benchmark.cpp)
  target_include_directories(timeline_history_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_history_benchmark PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(timeline_history_benchmark)
  add_test(NAME timeline_history_contract COMMAND timeline_history_benchmark --verify)
  set_tests_properties(timeline_history_contract PROPERTIES TIMEOUT 60)

  foreach(interaction_test mask_interaction_performance_test photo_sampling_async_test photo_recipe_preparation_async_test timeline_interaction_query_contract_test carousel_text_contrast_cache_test editor_recipe_save_recovery_test)
    add_executable(${interaction_test} ${interaction_test}.cpp)
    target_include_directories(${interaction_test} PRIVATE ../apps/desktop/src)
    target_link_libraries(${interaction_test} PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
    photo_editor_apply_warnings(${interaction_test})
    add_test(NAME ${interaction_test} COMMAND ${interaction_test})
    set_tests_properties(${interaction_test} PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endforeach()
  target_link_libraries(mask_interaction_performance_test PRIVATE Qt6::Qml)

  add_executable(mask_hidden_children_lifecycle_test mask_hidden_children_lifecycle_test.cpp)
  target_compile_definitions(mask_hidden_children_lifecycle_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  target_link_libraries(mask_hidden_children_lifecycle_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
  photo_editor_apply_warnings(mask_hidden_children_lifecycle_test)
  add_test(NAME mask_hidden_children_lifecycle_test COMMAND mask_hidden_children_lifecycle_test)
  set_tests_properties(mask_hidden_children_lifecycle_test PROPERTIES
    TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    LABELS "desktop;qml;mask;visibility;lifecycle")

  add_executable(mask_edit_normalization_test mask_edit_normalization_test.cpp)
  target_include_directories(mask_edit_normalization_test PRIVATE ../apps/desktop/src)
  target_link_libraries(mask_edit_normalization_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(mask_edit_normalization_test)
  add_test(NAME mask_edit_normalization_test COMMAND mask_edit_normalization_test)

  add_executable(timeline_history_workflow_regression timeline_document_controller_test.cpp)
  target_include_directories(timeline_history_workflow_regression PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_history_workflow_regression PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(timeline_history_workflow_regression)
  foreach(workflow color-grade-curve cube-lut shared-keyframe-curve title-motion-keyframe)
    add_test(NAME timeline_history_${workflow}_regression
      COMMAND timeline_history_workflow_regression --timeline-${workflow}-workflow)
    set_tests_properties(timeline_history_${workflow}_regression PROPERTIES TIMEOUT 60)
  endforeach()


  add_executable(timeline_video_conversion_benchmark timeline_video_conversion_benchmark.cpp)
  target_include_directories(timeline_video_conversion_benchmark PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_video_conversion_benchmark PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(timeline_video_conversion_benchmark)

  add_executable(photo_cache_budget_test photo_cache_budget_test.cpp)
  target_include_directories(photo_cache_budget_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_cache_budget_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(photo_cache_budget_test)
  add_test(NAME photo_cache_budget_test COMMAND photo_cache_budget_test)
  set_tests_properties(photo_cache_budget_test PROPERTIES TIMEOUT 30 LABELS "desktop;photo;performance")

  add_executable(timeline_frame_scheduling_test timeline_frame_scheduling_test.cpp)
  add_executable(timeline_transform_workflow_test timeline_transform_workflow_test.cpp)
  target_compile_definitions(timeline_transform_workflow_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_include_directories(timeline_transform_workflow_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_transform_workflow_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Quick Qt6::Qml Qt6::Test Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(timeline_transform_workflow_test)
  add_test(NAME timeline_transform_workflow_test COMMAND timeline_transform_workflow_test)
  set_tests_properties(timeline_transform_workflow_test PROPERTIES TIMEOUT 60 LABELS "desktop;timeline;animation;workflow")
  add_test(NAME timeline_transform_interaction_test COMMAND timeline_transform_workflow_test --interaction-only)
  set_tests_properties(timeline_transform_interaction_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;animation;interaction")
  target_include_directories(timeline_frame_scheduling_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_frame_scheduling_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(timeline_frame_scheduling_test)
  add_test(NAME timeline_frame_scheduling_test COMMAND timeline_frame_scheduling_test)
  set_tests_properties(timeline_frame_scheduling_test PROPERTIES TIMEOUT 30 LABELS "desktop;timeline;performance")

  add_executable(
    timeline_playback_controller_test
    timeline_playback_controller_test.cpp)
  set_target_properties(timeline_playback_controller_test PROPERTIES AUTOMOC ON)
  target_include_directories(
    timeline_playback_controller_test
    PRIVATE
      ../apps/desktop/src)
  target_link_libraries(
    timeline_playback_controller_test
    PRIVATE
      desktop_shell_test_runtime
      Qt6::Concurrent
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      Qt6::Quick
      Qt6::Multimedia
      native_window_test_input
      ${photo_editor_desktop_image_decoder_frameworks}
      photo_engine)
  photo_editor_apply_warnings(timeline_playback_controller_test)

  add_test(NAME timeline_playback_controller_test COMMAND timeline_playback_controller_test)
  if(APPLE)
    add_executable(timeline_sustained_media_lifecycle_test timeline_sustained_media_lifecycle_test.cpp
      timeline_memory_autorelease_pool.mm)
    set_target_properties(timeline_sustained_media_lifecycle_test PROPERTIES AUTOMOC ON)
    target_include_directories(timeline_sustained_media_lifecycle_test PRIVATE ../apps/desktop/src)
    target_link_libraries(timeline_sustained_media_lifecycle_test PRIVATE
      desktop_shell_test_runtime Qt6::Concurrent Qt6::Core Qt6::Gui Qt6::Widgets
      Qt6::Quick Qt6::Multimedia native_window_test_input
      ${photo_editor_desktop_image_decoder_frameworks} photo_engine)
    photo_editor_apply_warnings(timeline_sustained_media_lifecycle_test)
    add_test(NAME timeline_sustained_media_lifecycle_test COMMAND timeline_sustained_media_lifecycle_test)
    set_tests_properties(timeline_sustained_media_lifecycle_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa" TIMEOUT 240 SKIP_RETURN_CODE 77
      LABELS "desktop;timeline;playback;native;cocoa;qualification;performance")
    add_executable(timeline_native_player_memory_test
      timeline_native_player_memory_test.cpp timeline_memory_autorelease_pool.mm)
    target_link_libraries(timeline_native_player_memory_test PRIVATE
      Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia)
    photo_editor_apply_warnings(timeline_native_player_memory_test)
    add_test(NAME timeline_native_player_memory_test COMMAND timeline_native_player_memory_test)
    set_tests_properties(timeline_native_player_memory_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_MEDIA_BACKEND=darwin" TIMEOUT 300 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
      LABELS "desktop;timeline;playback;native;cocoa;memory;qualification")
    add_executable(timeline_audio_sink_memory_test
      timeline_audio_sink_memory_test.cpp timeline_memory_autorelease_pool.mm)
    target_link_libraries(timeline_audio_sink_memory_test PRIVATE
      Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Multimedia)
    photo_editor_apply_warnings(timeline_audio_sink_memory_test)
    add_test(NAME timeline_audio_sink_memory_test COMMAND timeline_audio_sink_memory_test)
    set_tests_properties(timeline_audio_sink_memory_test PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=cocoa" TIMEOUT 120 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE
      LABELS "desktop;timeline;audio;native;cocoa;memory;qualification")
  endif()

  if(APPLE)
    add_test(
      NAME timeline_playback_controller_cocoa_native_test
      COMMAND timeline_playback_controller_test --native-cocoa)
    set_tests_properties(
      timeline_playback_controller_cocoa_native_test
      PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa"
        TIMEOUT 60
        LABELS "desktop;timeline;playback;native;cocoa")
    add_test(
      NAME timeline_playback_controller_cocoa_audio_qualification_test
      COMMAND timeline_playback_controller_test --native-cocoa-audio)
    set_tests_properties(
      timeline_playback_controller_cocoa_audio_qualification_test
      PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa"
        TIMEOUT 60
        LABELS "desktop;timeline;playback;native;cocoa;audio;qualification")
  endif()

  add_executable(
    bounded_process_wait_test
    bounded_process_wait_test.cpp)
  target_include_directories(
    bounded_process_wait_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(
    bounded_process_wait_test
    PRIVATE Qt6::Core)
  photo_editor_apply_warnings(bounded_process_wait_test)
  add_test(NAME bounded_process_wait_test COMMAND bounded_process_wait_test)
  add_executable(cancellable_process_wait_test cancellable_process_wait_test.cpp)
  target_include_directories(cancellable_process_wait_test PRIVATE ../apps/desktop/src)
  target_link_libraries(cancellable_process_wait_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(cancellable_process_wait_test)
  add_test(NAME cancellable_process_wait_test COMMAND cancellable_process_wait_test)
  add_test(NAME native_process_unlimited_wait_contract COMMAND cancellable_process_wait_test --long-work-contract)
  set_tests_properties(native_process_unlimited_wait_contract PROPERTIES TIMEOUT 150)
  add_executable(job_output_transaction_test job_output_transaction_test.cpp)
  target_include_directories(job_output_transaction_test PRIVATE ../apps/desktop/src)
  target_link_libraries(job_output_transaction_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(job_output_transaction_test)
  add_test(NAME job_output_transaction_test COMMAND job_output_transaction_test)

  if(APPLE)
    add_executable(
      vision_segmentation_macos_smoke_test
      vision_segmentation_macos_smoke_test.mm
      ../apps/desktop/src/vision_segmentation.h
      ../apps/desktop/src/vision_segmentation.mm
      ../apps/desktop/src/object_pick_policy.h
      ../apps/desktop/src/object_pick_policy.cpp
      ../apps/desktop/src/segmentation_result_schema.h
      ../apps/desktop/src/segmentation_result_schema.cpp)
    target_compile_definitions(
      vision_segmentation_macos_smoke_test
      PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_include_directories(
      vision_segmentation_macos_smoke_test
      PRIVATE ../apps/desktop/src)
    target_link_libraries(
      vision_segmentation_macos_smoke_test
      PRIVATE
        Qt6::Core
        Qt6::Gui
        "-framework CoreGraphics"
        "-framework CoreImage"
        "-framework CoreVideo"
        "-framework Vision")
    set_source_files_properties(
      vision_segmentation_macos_smoke_test.mm
      ../apps/desktop/src/vision_segmentation.mm
      PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    photo_editor_apply_warnings(vision_segmentation_macos_smoke_test)
    add_test(
      NAME vision_segmentation_macos_smoke_test
      COMMAND vision_segmentation_macos_smoke_test)
    set_tests_properties(
      vision_segmentation_macos_smoke_test
      PROPERTIES LABELS "desktop;macos;vision;agent")
  endif()

  set(photo_editor_preview_controller_split_targets
    actual_app_workflow_benchmark_driver
    desktop_shell_test_runtime
    editor_preview_before_after_recipe_test
    editor_preview_coalesced_undo_test
    editor_preview_color_cache_test
    editor_preview_controller_test
    editor_preview_feedback_test
    editor_preview_history_test
    editor_preview_layer_adjustment_test
    editor_preview_layer_mask_history_test
    editor_preview_local_edit_target_test
    editor_preview_outline_trace_test
    editor_preview_pixel_zoom_test
    editor_preview_smart_selection_stale_test
    interaction_workflow_benchmark_driver
    preview_benchmark_driver
    rendering_migration_proof_test)
  foreach(target IN LISTS photo_editor_preview_controller_split_targets)
    get_target_property(existing_sources ${target} SOURCES)
    foreach(source IN ITEMS
        ../apps/desktop/src/editor_layer_style_canvas_controller.cpp
        ../apps/desktop/src/editor_text_dimension_controller.cpp
        ../apps/desktop/src/creative_example_thumbnail_service.cpp
        ../apps/desktop/src/editor_preview_histogram_controller.cpp
        ../apps/desktop/src/editor_professional_tools_controller.cpp
    ../apps/desktop/src/editor_smart_document_controller.cpp
    ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
    ../apps/desktop/src/editor_vector_path_io_controller.cpp
        ../apps/desktop/src/editor_smart_document_controller.cpp
        ../apps/desktop/src/editor_layer_style_controller.cpp
    ../apps/desktop/src/photo_component_workspace_controller.h
    ../apps/desktop/src/photo_component_workspace_controller.cpp
        ../apps/desktop/src/editor_vector_path_io_controller.cpp
        ../apps/desktop/src/photo_smart_object.cpp
        ../apps/desktop/src/photo_smart_document.cpp
        ../apps/desktop/src/photo_smart_document_expansion.cpp
        ../apps/desktop/src/photo_vector_path.cpp)
      list(FIND existing_sources "${source}" source_index)
      if(source_index EQUAL -1)
        target_sources(${target} PRIVATE "${source}")
      endif()
    endforeach()
  endforeach()
  add_executable(gaussian_scene_pipeline_driver gaussian_scene_pipeline_driver.cpp
    ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp
    ../apps/desktop/src/gaussian_capture_preparation.cpp)
  set_target_properties(gaussian_scene_pipeline_driver PROPERTIES AUTOMOC ON)
  target_include_directories(gaussian_scene_pipeline_driver PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(gaussian_scene_pipeline_driver PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(gaussian_scene_pipeline_driver)
  if(Python3_Interpreter_FOUND)
    add_test(NAME gaussian_photo_alignment_comparator_test
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/compare-gaussian-photo-alignment.py --self-test)
    add_test(NAME gaussian_held_out_camera_mapping_test
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/map-gaussian-held-out-cameras.py --self-test)
  endif()
  add_executable(gaussian_timed_capture_test gaussian_timed_capture_test.cpp
    ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp
    ../apps/desktop/src/gaussian_capture_preparation.cpp)
  set_target_properties(gaussian_timed_capture_test PROPERTIES AUTOMOC ON)
  target_include_directories(gaussian_timed_capture_test PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(gaussian_timed_capture_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(gaussian_timed_capture_test)
  add_test(NAME gaussian_timed_capture_test COMMAND gaussian_timed_capture_test)
  set_tests_properties(gaussian_timed_capture_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  add_executable(gaussian_capture_color_test gaussian_capture_color_test.cpp ../apps/desktop/src/gaussian_capture_preparation.cpp)
  target_include_directories(gaussian_capture_color_test PRIVATE ../apps/desktop/src ../engine/include)
  target_link_libraries(gaussian_capture_color_test PRIVATE desktop_shell_test_runtime Qt6::Core Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(gaussian_capture_color_test)
  add_test(NAME gaussian_capture_color_test COMMAND gaussian_capture_color_test)
  set_tests_properties(gaussian_capture_color_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  add_executable(gaussian_camera_priors_test gaussian_camera_priors_test.cpp)
  target_include_directories(gaussian_camera_priors_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_camera_priors_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(gaussian_camera_priors_test)
  add_test(NAME gaussian_camera_priors_test COMMAND gaussian_camera_priors_test)
  add_executable(gaussian_image_quality_test gaussian_image_quality_test.cpp)
  add_executable(gaussian_scene_cleanup_test gaussian_scene_cleanup_test.cpp ../apps/desktop/src/gaussian_scene_data.cpp)
  target_include_directories(gaussian_scene_cleanup_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_scene_cleanup_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_scene_cleanup_test)
  add_test(NAME gaussian_scene_cleanup_test COMMAND gaussian_scene_cleanup_test)
  add_executable(gaussian_video_capture_test gaussian_video_capture_test.cpp ../apps/desktop/src/gaussian_video_capture.cpp)
  target_include_directories(gaussian_video_capture_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_video_capture_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_video_capture_test)
  add_test(NAME gaussian_video_capture_test COMMAND gaussian_video_capture_test)
  set_tests_properties(gaussian_video_capture_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
  add_executable(gaussian_focus_pick_test gaussian_focus_pick_test.cpp)
  target_include_directories(gaussian_focus_pick_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_focus_pick_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_focus_pick_test)
  add_test(NAME gaussian_focus_pick_test COMMAND gaussian_focus_pick_test)
  add_executable(gaussian_camera_navigation_test gaussian_camera_navigation_test.cpp
    ../apps/desktop/src/gaussian_scene_data.cpp)
  target_include_directories(gaussian_camera_navigation_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_camera_navigation_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_camera_navigation_test)
  add_test(NAME gaussian_camera_navigation_test COMMAND gaussian_camera_navigation_test)
  add_executable(gaussian_physical_camera_test gaussian_physical_camera_test.cpp
    ../apps/desktop/src/gaussian_scene_data.cpp)
  target_include_directories(gaussian_physical_camera_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_physical_camera_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_physical_camera_test)
  add_test(NAME gaussian_physical_camera_test COMMAND gaussian_physical_camera_test)
  target_link_libraries(gaussian_image_quality_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_image_quality_test)
  add_test(NAME gaussian_image_quality_test COMMAND gaussian_image_quality_test)
  add_test(NAME gaussian_image_quality_24mp_test COMMAND gaussian_image_quality_test --large-image-test 6000 4000)
  set_tests_properties(gaussian_image_quality_24mp_test PROPERTIES TIMEOUT 300 LABELS "gaussian;image-quality;large-image")
  add_executable(gaussian_training_schedule_test gaussian_training_schedule_test.cpp)
  target_include_directories(gaussian_training_schedule_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_training_schedule_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(gaussian_training_schedule_test)
  add_test(NAME gaussian_training_schedule_test COMMAND gaussian_training_schedule_test)
  add_executable(legacy_preview_locality_driver legacy_preview_locality_driver.cpp)
  target_include_directories(legacy_preview_locality_driver PRIVATE ../apps/desktop/src ../engine/src/pipeline)
  target_link_libraries(legacy_preview_locality_driver PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(legacy_preview_locality_driver)
  add_executable(editor_warm_asset_loading_test editor_warm_asset_loading_test.cpp)
  target_include_directories(editor_warm_asset_loading_test PRIVATE ../apps/desktop/src)
  target_link_libraries(editor_warm_asset_loading_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(editor_warm_asset_loading_test)
  add_test(NAME editor_warm_asset_loading_test COMMAND editor_warm_asset_loading_test)
  set_tests_properties(editor_warm_asset_loading_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;preview;loading" TIMEOUT 180)
  if(TARGET photo_editor_customer_access AND OPENSSL_CRYPTO_LIBRARY)
    add_executable(customer_expiry_workflow_test customer_expiry_workflow_test.cpp)
    target_include_directories(customer_expiry_workflow_test PRIVATE ../apps/desktop/src)
    target_link_libraries(customer_expiry_workflow_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Concurrent photo_engine)
    add_test(NAME customer_expiry_workflow_test COMMAND customer_expiry_workflow_test)
    set_tests_properties(customer_expiry_workflow_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 150)
    add_executable(customer_lease_test customer_lease_test.cpp)
    target_link_libraries(customer_lease_test PRIVATE photo_editor_customer_access)
    target_compile_definitions(customer_lease_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME customer_lease_test COMMAND customer_lease_test)
    add_executable(customer_access_controller_test customer_access_controller_test.cpp)
    target_link_libraries(customer_access_controller_test PRIVATE photo_editor_customer_access)
    target_compile_definitions(customer_access_controller_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME customer_access_controller_test COMMAND customer_access_controller_test)
    set_tests_properties(customer_access_controller_test PROPERTIES TIMEOUT 45)
  endif()

  # The panel uses a synthetic QML controller in this test and remains part of
  # the application resources even when the optional customer service is off.
  add_executable(customer_account_panel_test customer_account_panel_test.cpp)
  target_link_libraries(customer_account_panel_test PRIVATE Qt6::Quick Qt6::Qml Qt6::QuickControls2)
  target_compile_definitions(customer_account_panel_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME customer_account_panel_test COMMAND customer_account_panel_test)
  set_tests_properties(customer_account_panel_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(preview_corrupt_cache_test preview_corrupt_cache_test.cpp)
  target_include_directories(preview_corrupt_cache_test PRIVATE ../apps/desktop/src)
  target_link_libraries(preview_corrupt_cache_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(preview_corrupt_cache_test)
  add_test(NAME preview_corrupt_cache_test COMMAND preview_corrupt_cache_test)
  set_tests_properties(preview_corrupt_cache_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;preview;loading;recovery" TIMEOUT 180)
  add_executable(global_slider_mask_fidelity_test global_slider_mask_fidelity_test.cpp)
  target_include_directories(global_slider_mask_fidelity_test PRIVATE ../apps/desktop/src)
  target_link_libraries(global_slider_mask_fidelity_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(global_slider_mask_fidelity_test)
  add_test(NAME global_slider_mask_fidelity_test COMMAND global_slider_mask_fidelity_test)
  set_tests_properties(global_slider_mask_fidelity_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;preview;color;mask" TIMEOUT 90)
  add_executable(png_export_encoding_test png_export_encoding_test.cpp)
  target_include_directories(png_export_encoding_test PRIVATE ../apps/desktop/src)
  target_link_libraries(png_export_encoding_test PRIVATE Qt6::Gui)
  photo_editor_apply_warnings(png_export_encoding_test)
  add_test(NAME png_export_encoding_test COMMAND png_export_encoding_test)
  set_tests_properties(png_export_encoding_test PROPERTIES LABELS "desktop;export;encoding" TIMEOUT 60)

  add_executable(photo_export_recovery_test photo_export_recovery_test.cpp)
  add_executable(native_canvas_export_driver native_canvas_export_driver.cpp)
  add_executable(native_brush_score_driver native_brush_score_driver.cpp)
  target_include_directories(native_brush_score_driver PRIVATE ../apps/desktop/src)
  target_link_libraries(native_brush_score_driver PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(native_brush_score_driver)
  target_include_directories(native_canvas_export_driver PRIVATE ../apps/desktop/src)
  target_link_libraries(native_canvas_export_driver PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(native_canvas_export_driver)
  add_executable(photo_layer_snapshot_test photo_layer_snapshot_test.cpp)
  add_executable(export_recipe_handoff_test export_recipe_handoff_test.cpp)
  add_executable(catalog_recipe_reopen_test catalog_recipe_reopen_test.cpp)
  target_include_directories(catalog_recipe_reopen_test PRIVATE ../apps/desktop/src)
  target_link_libraries(catalog_recipe_reopen_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(catalog_recipe_reopen_test)
  add_test(NAME catalog_recipe_reopen_test COMMAND catalog_recipe_reopen_test)
  set_tests_properties(catalog_recipe_reopen_test PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;integration;catalog;recipe;reopen" TIMEOUT 180)
  add_executable(agent_recipe_persistence_native_contract agent_recipe_persistence_native_contract.cpp)
  target_include_directories(agent_recipe_persistence_native_contract PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_recipe_persistence_native_contract PRIVATE desktop_shell_test_runtime
    photo_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::Widgets Qt6::Network)
  photo_editor_apply_warnings(agent_recipe_persistence_native_contract)
  add_test(NAME agent_recipe_persistence_native_contract COMMAND agent_recipe_persistence_native_contract)
  set_tests_properties(agent_recipe_persistence_native_contract PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;integration;agent;recipe;revert"
    RUN_SERIAL TRUE TIMEOUT 300)
  add_executable(agent_photo_output_native_contract agent_photo_output_native_contract.cpp)
  target_include_directories(agent_photo_output_native_contract PRIVATE ../apps/desktop/src)
  target_link_libraries(agent_photo_output_native_contract PRIVATE desktop_shell_test_runtime
    photo_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent Qt6::Widgets Qt6::Network)
  photo_editor_apply_warnings(agent_photo_output_native_contract)
  add_test(NAME agent_photo_output_native_contract COMMAND agent_photo_output_native_contract)
  set_tests_properties(agent_photo_output_native_contract PROPERTIES SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;integration;agent;output;revert"
    RUN_SERIAL TRUE TIMEOUT 300)
  target_compile_definitions(export_recipe_handoff_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_include_directories(export_recipe_handoff_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_recipe_handoff_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(export_recipe_handoff_test)
  add_test(NAME export_recipe_handoff_test COMMAND export_recipe_handoff_test)
  add_test(NAME export_session_guard_test COMMAND export_recipe_handoff_test --export-session-guard-only)
  set_tests_properties(export_session_guard_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 30 LABELS "desktop;authoring;export;workflow")
  set_tests_properties(export_recipe_handoff_test PROPERTIES TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;export;recipe;color")
  target_compile_definitions(photo_layer_snapshot_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_include_directories(photo_layer_snapshot_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_layer_snapshot_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(photo_layer_snapshot_test)
  add_test(NAME photo_layer_snapshot_test COMMAND photo_layer_snapshot_test)
  set_tests_properties(photo_layer_snapshot_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;qml;layers;history")
  target_include_directories(photo_export_recovery_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_export_recovery_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(photo_export_recovery_test)
  add_test(NAME photo_export_recovery_test COMMAND photo_export_recovery_test)
  set_tests_properties(photo_export_recovery_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;export;recovery;color" TIMEOUT 90)
  add_executable(raw_decode_prewarm_queue_test raw_decode_prewarm_queue_test.cpp)
  target_include_directories(raw_decode_prewarm_queue_test PRIVATE ../apps/desktop/src)
  target_link_libraries(raw_decode_prewarm_queue_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  photo_editor_apply_warnings(raw_decode_prewarm_queue_test)
  add_test(NAME raw_decode_prewarm_queue_test COMMAND raw_decode_prewarm_queue_test)
  set_tests_properties(raw_decode_prewarm_queue_test PROPERTIES LABELS "desktop;preview;loading" TIMEOUT 30)

  add_executable(round10_raw_switch_workload_test round10_raw_switch_workload_test.cpp
    ../apps/desktop/src/observability.cpp)
  target_include_directories(round10_raw_switch_workload_test PRIVATE . ../apps/desktop/src)
  target_link_libraries(round10_raw_switch_workload_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Gui Qt6::Concurrent)
  target_compile_definitions(round10_raw_switch_workload_test PRIVATE
    PHOTO_EDITOR_ROUND10_PHOTO_FIXTURE="${CMAKE_SOURCE_DIR}/apps/desktop/assets/presets/mountain-preview.jpg"
    PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
  photo_editor_apply_warnings(round10_raw_switch_workload_test)
  add_test(NAME round10_raw_switch_workload_test COMMAND round10_raw_switch_workload_test)
  set_tests_properties(round10_raw_switch_workload_test PROPERTIES LABELS "desktop;preview;loading;performance" TIMEOUT 90)

  if(APPLE AND Python3_Interpreter_FOUND)
    add_executable(round10_real_import_mixed_workload_test
      round10_real_import_mixed_workload_test.cpp
      ../apps/desktop/src/observability.cpp)
    target_include_directories(round10_real_import_mixed_workload_test PRIVATE . ../apps/desktop/src)
    target_link_libraries(round10_real_import_mixed_workload_test PRIVATE
      desktop_shell_test_runtime photo_engine Qt6::Core Qt6::Gui Qt6::Concurrent)
    target_compile_definitions(round10_real_import_mixed_workload_test PRIVATE
      PHOTO_EDITOR_ROUND10_REAL_PHOTO_FIXTURE="${CMAKE_SOURCE_DIR}/apps/desktop/assets/presets/mountain-preview.jpg"
      PHOTO_EDITOR_ENABLE_DEV_OBSERVABILITY=1)
    photo_editor_apply_warnings(round10_real_import_mixed_workload_test)
    add_test(NAME round10_real_import_mixed_workload_test
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/perf/run-round10-real-import-mixed-workload.py"
        --driver "$<TARGET_FILE:round10_real_import_mixed_workload_test>"
        --repo-root "${CMAKE_SOURCE_DIR}"
        --catalog-bin-dir "${CMAKE_SOURCE_DIR}/rust/target/debug"
        --photo-fixture "${CMAKE_SOURCE_DIR}/apps/desktop/assets/presets/mountain-preview.jpg")
    set_tests_properties(round10_real_import_mixed_workload_test PROPERTIES
      TIMEOUT 360 SKIP_RETURN_CODE 77 LABELS "desktop;catalog;import;preview;export;performance;qualification")
  endif()

  add_test(NAME preview_reference_workflow_test COMMAND legacy_preview_locality_driver)
  set_tests_properties(preview_reference_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;color;preview;comparison" TIMEOUT 60)
  add_test(NAME preview_reference_aces2_workflow_test COMMAND legacy_preview_locality_driver --self-test-aces2)
  set_tests_properties(preview_reference_aces2_workflow_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;color;preview;comparison;aces2" TIMEOUT 60)
  add_executable(gaussian_interaction_proof_test gaussian_interaction_proof_test.cpp)
  target_include_directories(gaussian_interaction_proof_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_interaction_proof_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(gaussian_interaction_proof_test)
  add_test(NAME gaussian_interaction_proof_test COMMAND gaussian_interaction_proof_test)
  set_tests_properties(gaussian_interaction_proof_test PROPERTIES LABELS "desktop;gaussian;performance")
  if(PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE)
    add_test(NAME workspace_switch_comparison_test COMMAND ${PHOTO_EDITOR_TEST_PYTHON3_EXECUTABLE}
      -B ${CMAKE_SOURCE_DIR}/scripts/perf/test-workspace-switch-comparison.py)
    set_tests_properties(workspace_switch_comparison_test PROPERTIES LABELS "desktop;gaussian;performance" TIMEOUT 10)
  endif()

  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    add_executable(gaussian_viewport_handoff_test gaussian_viewport_handoff_test.cpp
      ../apps/desktop/src/gaussian_scene_viewport.h ../apps/desktop/src/gaussian_scene_viewport.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm ../apps/desktop/src/gaussian_spacetime_artifact.cpp)
    set_target_properties(gaussian_viewport_handoff_test PROPERTIES AUTOMOC ON)
    set_source_files_properties(../apps/desktop/src/gaussian_scene_viewport.cpp PROPERTIES LANGUAGE OBJCXX)
    target_include_directories(gaussian_viewport_handoff_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_viewport_handoff_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_viewport_handoff_test)
    add_test(NAME gaussian_viewport_handoff_test COMMAND gaussian_viewport_handoff_test)
    set_tests_properties(gaussian_viewport_handoff_test PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(gaussian_scene_cleanup_ui_test gaussian_scene_cleanup_ui_test.cpp
      ../apps/desktop/src/scene_video_recorder.cpp ../apps/desktop/src/scene_video_recorder.h ../apps/desktop/src/scene_library_controller.cpp ../apps/desktop/src/scene_library_controller.h
      ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp
      ../apps/desktop/src/gaussian_scene_viewport.h ../apps/desktop/src/gaussian_scene_viewport.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm ../apps/desktop/src/gaussian_spacetime_artifact.cpp)
    set_target_properties(gaussian_scene_cleanup_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(gaussian_scene_cleanup_ui_test PRIVATE ../apps/desktop/src)
    target_compile_definitions(gaussian_scene_cleanup_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(gaussian_scene_cleanup_ui_test PRIVATE native_window_test_input Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent Qt6::Test "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_scene_cleanup_ui_test)
    qt_add_shaders(gaussian_scene_cleanup_ui_test "gaussian_cleanup_ui_shaders"
      BATCHABLE PREFIX "/photo-editor/shaders" BASE "../apps/desktop/shaders"
      FILES ../apps/desktop/shaders/optical_light.vert ../apps/desktop/shaders/optical_light.frag)
    add_test(NAME gaussian_scene_cleanup_ui_test COMMAND gaussian_scene_cleanup_ui_test)
    add_test(NAME gaussian_scene_lasso_ui_test COMMAND gaussian_scene_cleanup_ui_test --lasso-cleanup)
    add_test(NAME gaussian_scene_depth_cleanup_ui_test COMMAND gaussian_scene_cleanup_ui_test --depth-cleanup)
    add_test(NAME gaussian_haze_ui_test COMMAND gaussian_scene_cleanup_ui_test --haze-shadows)
    add_test(NAME gaussian_shadow_receiver_ui_test COMMAND gaussian_scene_cleanup_ui_test --shadow-receiver)
    set_tests_properties(gaussian_shadow_receiver_ui_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_haze_ui_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scene_depth_cleanup_ui_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scene_lasso_ui_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    add_test(NAME gaussian_scene_objects_ui_test COMMAND gaussian_scene_cleanup_ui_test --objects)
    add_test(NAME gaussian_scene_studio_ui_test COMMAND gaussian_scene_cleanup_ui_test --studio-camera)
    set_tests_properties(gaussian_scene_studio_ui_test PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian;studio")
    add_test(NAME gaussian_focus_controls_ui_test COMMAND gaussian_scene_cleanup_ui_test --studio-camera --focus-control-proof)
    set_tests_properties(gaussian_focus_controls_ui_test PROPERTIES TIMEOUT 120 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian;studio")
    add_test(NAME gaussian_spatial_reflections_ui_test COMMAND gaussian_scene_cleanup_ui_test --spatial-reflections)
    add_test(NAME gaussian_spatial_diffuse_ui_test COMMAND gaussian_scene_cleanup_ui_test --spatial-diffuse)
    add_test(NAME gaussian_surface_bounces_ui_test COMMAND gaussian_scene_cleanup_ui_test --surface-bounces)
    add_test(NAME gaussian_progressive_lighting_ui_test COMMAND gaussian_scene_cleanup_ui_test --progressive-lighting)
    add_test(NAME gaussian_video_focus_ui_test COMMAND gaussian_scene_cleanup_ui_test --video-focus-selection)
    add_test(NAME gaussian_photo_capture_options_ui_test COMMAND gaussian_scene_cleanup_ui_test --photo-capture-options)
    set_tests_properties(gaussian_photo_capture_options_ui_test PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    add_test(NAME gaussian_capture_emission_ui_test COMMAND gaussian_scene_cleanup_ui_test --capture-emission)
    add_test(NAME gaussian_capture_lighting_cache_ui_test COMMAND gaussian_scene_cleanup_ui_test --capture-lighting-cache --offscreen-emitter)
    set_tests_properties(gaussian_capture_lighting_cache_ui_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_capture_emission_ui_test PROPERTIES TIMEOUT 180 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_video_focus_ui_test PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_progressive_lighting_ui_test PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE RESOURCE_LOCK photo_editor_desktop_runtime ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_surface_bounces_ui_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_spatial_diffuse_ui_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_spatial_reflections_ui_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    add_test(NAME gaussian_scan_shadows_ui_test COMMAND gaussian_scene_cleanup_ui_test --scan-shadows)
    set_tests_properties(gaussian_scan_shadows_ui_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    add_test(NAME gaussian_environment_ui_test COMMAND gaussian_scene_cleanup_ui_test --environment)
    add_test(NAME gaussian_alpha_model_ui_test COMMAND gaussian_scene_cleanup_ui_test --alpha-model)
    add_test(NAME gaussian_visible_emitters_ui_test COMMAND gaussian_scene_cleanup_ui_test --emitters)
    add_test(NAME gaussian_frame_sequence_ui_test COMMAND gaussian_scene_cleanup_ui_test --frame-sequence)
    set_tests_properties(gaussian_frame_sequence_ui_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian;film")
    set_tests_properties(gaussian_visible_emitters_ui_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_alpha_model_ui_test PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_environment_ui_test PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scene_cleanup_ui_test PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scene_objects_ui_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QSG_RHI_BACKEND=metal" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_spacetime_workspace_test gaussian_spacetime_workspace_test.cpp
      ../apps/desktop/src/gaussian_scene_controller.h ../apps/desktop/src/gaussian_scene_controller.cpp
      ../apps/desktop/src/gaussian_scene_viewport.h ../apps/desktop/src/gaussian_scene_viewport.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm ../apps/desktop/src/gaussian_spacetime_artifact.cpp)
    set_target_properties(gaussian_spacetime_workspace_test PROPERTIES AUTOMOC ON)
    target_include_directories(gaussian_spacetime_workspace_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_spacetime_workspace_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Concurrent "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_spacetime_workspace_test)
    add_test(NAME gaussian_spacetime_workspace_test COMMAND gaussian_spacetime_workspace_test)
    set_tests_properties(gaussian_spacetime_workspace_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_executable(gaussian_scene_render_test gaussian_scene_render_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    set_source_files_properties(../apps/desktop/src/gaussian_scene_renderer_metal.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    set_source_files_properties(gaussian_photo_readback.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_include_directories(gaussian_scene_render_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_scene_render_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_scene_render_test)
    add_test(NAME gaussian_scene_render_test COMMAND gaussian_scene_render_test)
    add_test(NAME gaussian_ray_camera_test COMMAND gaussian_scene_render_test --ray-camera-proof)
    set_tests_properties(gaussian_ray_camera_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180 LABELS "desktop;macos;metal;gaussian;optics")
    add_executable(gaussian_ray_scene_test gaussian_ray_scene_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_ray_scene_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_ray_scene_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_ray_scene_test)
    add_test(NAME gaussian_ray_scene_test COMMAND gaussian_ray_scene_test)
    set_tests_properties(gaussian_ray_scene_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180 LABELS "desktop;macos;metal;gaussian;optics")
    add_test(NAME gaussian_creative_camera_test COMMAND gaussian_scene_render_test --creative-camera-proof)
    set_tests_properties(gaussian_creative_camera_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scene_render_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_soft_lights_test gaussian_soft_lights_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_soft_lights_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_soft_lights_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_soft_lights_test)
    add_test(NAME gaussian_soft_lights_test COMMAND gaussian_soft_lights_test)
    set_tests_properties(gaussian_soft_lights_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_materials_test gaussian_materials_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_materials_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_materials_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_materials_test)
    add_test(NAME gaussian_materials_test COMMAND gaussian_materials_test)
    set_tests_properties(gaussian_materials_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_scene_objects_test gaussian_scene_objects_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_scene_objects_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_scene_objects_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_scene_objects_test)
    add_test(NAME gaussian_scene_objects_test COMMAND gaussian_scene_objects_test)
    set_tests_properties(gaussian_scene_objects_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_shadow_receiver_test gaussian_shadow_receiver_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_shadow_receiver_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_shadow_receiver_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_shadow_receiver_test)
    add_test(NAME gaussian_shadow_receiver_test COMMAND gaussian_shadow_receiver_test)
    add_test(NAME gaussian_shadow_receiver_software_test COMMAND gaussian_shadow_receiver_test)
    set_tests_properties(gaussian_shadow_receiver_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_shadow_receiver_software_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_scan_shadows_test gaussian_scan_shadows_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_scan_shadows_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_scan_shadows_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_scan_shadows_test)
    add_test(NAME gaussian_scan_shadows_test COMMAND gaussian_scan_shadows_test)
    add_test(NAME gaussian_scan_shadows_software_test COMMAND gaussian_scan_shadows_test)
    set_tests_properties(gaussian_scan_shadows_software_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_scan_shadows_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_haze_test gaussian_haze_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_haze_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_haze_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_haze_test)
    add_test(NAME gaussian_haze_test COMMAND gaussian_haze_test)
    add_test(NAME gaussian_haze_software_test COMMAND gaussian_haze_test)
    add_test(NAME gaussian_haze_field_bounds_test COMMAND gaussian_haze_test --field-bounds)
    set_tests_properties(gaussian_haze_field_bounds_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_haze_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_haze_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_spatial_reflections_test gaussian_spatial_reflections_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_spatial_reflections_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_spatial_reflections_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_spatial_reflections_test)
    add_test(NAME gaussian_spatial_reflections_test COMMAND gaussian_spatial_reflections_test)
    add_test(NAME gaussian_spatial_reflections_software_test COMMAND gaussian_spatial_reflections_test)
    set_tests_properties(gaussian_spatial_reflections_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_spatial_reflections_software_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_spatial_diffuse_test gaussian_spatial_diffuse_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_spatial_diffuse_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_spatial_diffuse_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_spatial_diffuse_test)
    add_test(NAME gaussian_spatial_diffuse_test COMMAND gaussian_spatial_diffuse_test)
    add_test(NAME gaussian_spatial_diffuse_software_test COMMAND gaussian_spatial_diffuse_test)
    set_tests_properties(gaussian_spatial_diffuse_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_spatial_diffuse_software_test PROPERTIES TIMEOUT 120 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_progressive_lighting_test gaussian_progressive_lighting_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_progressive_lighting_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_progressive_lighting_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_progressive_lighting_test)
    add_test(NAME gaussian_progressive_lighting_test COMMAND gaussian_progressive_lighting_test)
    add_test(NAME gaussian_progressive_lighting_software_test COMMAND gaussian_progressive_lighting_test)
    set_tests_properties(gaussian_progressive_lighting_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_progressive_lighting_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_capture_emission_test gaussian_capture_emission_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_capture_emission_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_capture_emission_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_capture_emission_test)
    add_test(NAME gaussian_capture_emission_test COMMAND gaussian_capture_emission_test)
    add_test(NAME gaussian_capture_emission_software_test COMMAND gaussian_capture_emission_test)
    add_test(NAME gaussian_capture_emission_importance_test COMMAND gaussian_capture_emission_test --importance)
    add_test(NAME gaussian_capture_emission_importance_software_test COMMAND gaussian_capture_emission_test --importance)
    set_tests_properties(gaussian_capture_emission_importance_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_capture_emission_importance_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_capture_emission_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_capture_emission_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_capture_lighting_cache_test gaussian_capture_lighting_cache_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_capture_lighting_cache_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_capture_lighting_cache_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_capture_lighting_cache_test)
    add_test(NAME gaussian_capture_lighting_cache_test COMMAND gaussian_capture_lighting_cache_test)
    add_test(NAME gaussian_capture_lighting_cache_software_test COMMAND gaussian_capture_lighting_cache_test)
    set_tests_properties(gaussian_capture_lighting_cache_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_capture_lighting_cache_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_surface_bounces_test gaussian_surface_bounces_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_surface_bounces_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_surface_bounces_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_surface_bounces_test)
    add_test(NAME gaussian_surface_bounces_test COMMAND gaussian_surface_bounces_test)
    add_test(NAME gaussian_surface_bounces_software_test COMMAND gaussian_surface_bounces_test)
    set_tests_properties(gaussian_surface_bounces_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    set_tests_properties(gaussian_surface_bounces_software_test PROPERTIES TIMEOUT 180 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LOOKSY_GAUSSIAN_SCAN_SOFTWARE=1" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_environment_lighting_test gaussian_environment_lighting_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_environment_lighting_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_environment_lighting_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_environment_lighting_test)
    add_test(NAME gaussian_environment_lighting_test COMMAND gaussian_environment_lighting_test)
    set_tests_properties(gaussian_environment_lighting_test PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_physical_camera_render_test gaussian_physical_camera_render_test.cpp gaussian_photo_readback.mm
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_physical_camera_render_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_physical_camera_render_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_physical_camera_render_test)
    add_test(NAME gaussian_physical_camera_render_test COMMAND gaussian_physical_camera_render_test)
    set_tests_properties(gaussian_physical_camera_render_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "desktop;macos;metal;gaussian")
    add_executable(gaussian_compound_optics_metal_test gaussian_compound_optics_metal_test.mm)
    target_include_directories(gaussian_compound_optics_metal_test PRIVATE ../apps/desktop/src)
    target_compile_options(gaussian_compound_optics_metal_test PRIVATE -fobjc-arc)
    target_link_libraries(gaussian_compound_optics_metal_test PRIVATE "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_compound_optics_metal_test)
    add_test(NAME gaussian_compound_optics_metal_test COMMAND gaussian_compound_optics_metal_test)
    set_tests_properties(gaussian_compound_optics_metal_test PROPERTIES TIMEOUT 60 LABELS "desktop;macos;metal;gaussian;optics")
    add_test(NAME gaussian_film_plane_render_test COMMAND gaussian_scene_render_test --film-plane-proof
      "${CMAKE_BINARY_DIR}/tests/film-plane-proof")
    set_tests_properties(gaussian_film_plane_render_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "desktop;macos;metal;gaussian;film")
    add_executable(gaussian_film_resolution_test gaussian_film_resolution_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_film_resolution_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_film_resolution_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_film_resolution_test)
    add_test(NAME gaussian_film_resolution_test COMMAND gaussian_film_resolution_test)
    set_tests_properties(gaussian_film_resolution_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180 LABELS "desktop;macos;metal;gaussian;film;large-export")
    add_executable(gaussian_spacetime_render_test gaussian_spacetime_render_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm
      ../apps/desktop/src/gaussian_spacetime_artifact.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp)
    target_include_directories(gaussian_spacetime_render_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_spacetime_render_test PRIVATE Qt6::Core Qt6::Gui "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_spacetime_render_test)
    add_test(NAME gaussian_spacetime_render_test COMMAND gaussian_spacetime_render_test)
    set_tests_properties(gaussian_spacetime_render_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60 LABELS "desktop;macos;metal;gaussian")
  endif()
  add_executable(gaussian_spacetime_test gaussian_spacetime_test.cpp)
  target_include_directories(gaussian_spacetime_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_spacetime_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_spacetime_test)
  add_test(NAME gaussian_spacetime_test COMMAND gaussian_spacetime_test)
  add_executable(gaussian_spacetime_artifact_test gaussian_spacetime_artifact_test.cpp
    ../apps/desktop/src/gaussian_spacetime_artifact.cpp ../apps/desktop/src/gaussian_scene_data.cpp
    ../apps/desktop/src/gaussian_saved_views.cpp ../apps/desktop/src/gaussian_camera_path.cpp)
  target_include_directories(gaussian_spacetime_artifact_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_spacetime_artifact_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_spacetime_artifact_test)
  add_test(NAME gaussian_spacetime_artifact_test COMMAND gaussian_spacetime_artifact_test)
  add_executable(gaussian_saved_views_test gaussian_saved_views_test.cpp
    ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp ../apps/desktop/src/gaussian_camera_path.cpp)
  target_include_directories(gaussian_saved_views_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_saved_views_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_saved_views_test)
  add_test(NAME gaussian_saved_views_test COMMAND gaussian_saved_views_test)
  add_executable(gaussian_camera_path_test gaussian_camera_path_test.cpp
    ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp ../apps/desktop/src/gaussian_camera_path.cpp)
  target_include_directories(gaussian_camera_path_test PRIVATE ../apps/desktop/src)
  target_link_libraries(gaussian_camera_path_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(gaussian_camera_path_test)
  add_test(NAME gaussian_camera_path_test COMMAND gaussian_camera_path_test)
  if(APPLE AND PHOTO_EDITOR_ENABLE_METAL_PREVIEW_BACKEND)
    add_executable(gaussian_film_export_test gaussian_film_export_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm)
    target_include_directories(gaussian_film_export_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_film_export_test PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent "-framework Metal" "-framework Foundation")
    add_executable(gaussian_scene_staging_driver gaussian_scene_staging_driver.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_scene_renderer_metal.mm
      ../apps/desktop/src/gaussian_saved_views.cpp ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp)
    target_include_directories(gaussian_scene_staging_driver PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_scene_staging_driver PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_scene_staging_driver)
    photo_editor_apply_warnings(gaussian_film_export_test)
    add_test(NAME gaussian_film_export_test COMMAND gaussian_film_export_test)
    add_test(NAME gaussian_frame_sequence_test COMMAND gaussian_film_export_test --png-sequence-only)
    set_tests_properties(gaussian_frame_sequence_test PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian;film")
    set_tests_properties(gaussian_film_export_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian;film")
    add_executable(gaussian_film_publication_test gaussian_film_publication_test.cpp)
    add_executable(scene_photo_edit_handoff_test scene_photo_edit_handoff_test.cpp)
    target_compile_definitions(scene_photo_edit_handoff_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(scene_photo_edit_handoff_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
    photo_editor_apply_warnings(scene_photo_edit_handoff_test)
    add_test(NAME scene_photo_edit_handoff_test COMMAND scene_photo_edit_handoff_test)
    set_tests_properties(scene_photo_edit_handoff_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    target_include_directories(gaussian_film_publication_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_film_publication_test PRIVATE Qt6::Core)
    photo_editor_apply_warnings(gaussian_film_publication_test)
    add_test(NAME gaussian_film_publication_test COMMAND gaussian_film_publication_test)
    add_executable(gaussian_spacetime_film_test gaussian_spacetime_film_test.cpp
      ../apps/desktop/src/gaussian_scene_data.cpp ../apps/desktop/src/gaussian_saved_views.cpp
      ../apps/desktop/src/gaussian_camera_path.cpp ../apps/desktop/src/gaussian_film_export.cpp
      ../apps/desktop/src/gaussian_scene_renderer_metal.mm ../apps/desktop/src/gaussian_spacetime_artifact.cpp)
    target_include_directories(gaussian_spacetime_film_test PRIVATE ../apps/desktop/src)
    target_link_libraries(gaussian_spacetime_film_test PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent "-framework Metal" "-framework Foundation")
    photo_editor_apply_warnings(gaussian_spacetime_film_test)
    add_test(NAME gaussian_spacetime_film_test COMMAND gaussian_spacetime_film_test)
    set_tests_properties(gaussian_spacetime_film_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;macos;metal;gaussian;film")
  endif()
endif()

if(TARGET Qt6::Core AND TARGET Qt6::Gui)
  add_executable(timeline_surface_runtime_test timeline_surface_runtime_test.cpp ../apps/desktop/src/timeline_surface_runtime.cpp)
  target_include_directories(timeline_surface_runtime_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_surface_runtime_test PRIVATE Qt6::Core Qt6::Gui)
  target_compile_definitions(timeline_surface_runtime_test PRIVATE
    PHOTO_EDITOR_TEST_PYTHON="${PROJECT_SOURCE_DIR}/.photo-editor/tracking/surface-runtime/venv/bin/python"
    PHOTO_EDITOR_SURFACE_WORKER="${PROJECT_SOURCE_DIR}/scripts/timeline_surface_worker.py")
  photo_editor_apply_warnings(timeline_surface_runtime_test)
  add_test(NAME timeline_surface_runtime_test COMMAND timeline_surface_runtime_test)
  set_tests_properties(timeline_surface_runtime_test PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 150 LABELS "desktop;timeline;surface")
endif()

if(TARGET Qt6::QuickControls2 AND TARGET Qt6::Test)
  add_executable(thumbnail_display_color_test thumbnail_display_color_test.cpp)
  target_compile_definitions(thumbnail_display_color_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(thumbnail_display_color_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  photo_editor_apply_warnings(thumbnail_display_color_test)
  add_test(NAME thumbnail_display_color_test COMMAND thumbnail_display_color_test)
  set_tests_properties(thumbnail_display_color_test PROPERTIES TIMEOUT 30 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  add_executable(crt_signal_surface_test crt_signal_surface_test.cpp)
  target_compile_definitions(crt_signal_surface_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(crt_signal_surface_test PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Test)
  qt_add_shaders(crt_signal_surface_test "crt_signal_test_shaders"
    BATCHABLE PREFIX "/photo-editor/shaders" BASE "../apps/desktop/shaders"
    FILES ../apps/desktop/shaders/agent_crt.vert ../apps/desktop/shaders/agent_crt.frag
      ../apps/desktop/shaders/agent_robot.vert ../apps/desktop/shaders/agent_robot.frag)
  if(APPLE)
    set_target_properties(crt_signal_surface_test PROPERTIES MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "Looksy CRT Preview"
      MACOSX_BUNDLE_GUI_IDENTIFIER "dev.looksy.crt-preview")
  endif()
  photo_editor_apply_warnings(crt_signal_surface_test)
  add_test(NAME crt_signal_surface_test COMMAND crt_signal_surface_test --verify)
  set_tests_properties(crt_signal_surface_test PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;agent;crt;gpu")
endif()

# Timeline controller tests compile the controller directly and therefore need
# the same platform-selected tracking runtime as the desktop application.
set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/timeline-sam2-worker.py"
  PROPERTIES QT_RESOURCE_ALIAS "timeline-sam2-worker.py")
set_source_files_properties("${PROJECT_SOURCE_DIR}/scripts/timeline_surface_worker.py"
  PROPERTIES QT_RESOURCE_ALIAS "timeline-surface-worker.py")
get_property(photo_editor_test_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS photo_editor_test_targets)
  get_target_property(target_sources "${target}" SOURCES)
  if(target_sources AND "../apps/desktop/src/catalog_asset_model.cpp" IN_LIST target_sources
      AND NOT "../apps/desktop/src/photo_layer_adjustment_recipe.cpp" IN_LIST target_sources)
    target_sources("${target}" PRIVATE ../apps/desktop/src/photo_layer_adjustment_recipe.cpp)
  endif()
  if(target_sources AND "../apps/desktop/src/app_bootstrap.cpp" IN_LIST target_sources)
    target_link_libraries("${target}" PRIVATE Qt6::Network)
  endif()
  if(target_sources AND "../apps/desktop/src/timeline_audio_output.cpp" IN_LIST target_sources
      AND NOT "../apps/desktop/src/timeline_voiceover_recorder.cpp" IN_LIST target_sources)
    target_sources("${target}" PRIVATE
      ../apps/desktop/src/timeline_voiceover_recorder.h
      ../apps/desktop/src/timeline_voiceover_recorder.cpp)
  endif()
  if(target_sources AND "../apps/desktop/src/timeline_document_controller.cpp" IN_LIST target_sources)
    if(NOT "../apps/desktop/src/timeline_asset_record_lookup.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_asset_record_lookup.h
        ../apps/desktop/src/timeline_asset_record_lookup.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_sparse_title_roi.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_sparse_title_roi.h
        ../apps/desktop/src/timeline_sparse_title_roi.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_graphics_track_compaction.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_graphics_track_compaction.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_edit_journal_codec.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_edit_journal_codec.h
        ../apps/desktop/src/timeline_edit_journal_codec.cpp)
    endif()
    target_link_libraries("${target}" PRIVATE ZLIB::ZLIB)
    if(NOT "../apps/desktop/src/timeline_frame_quad_warp.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_frame_quad_warp.h
        ../apps/desktop/src/timeline_frame_quad_warp.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_frame_quad_overscan.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_frame_quad_overscan.h
        ../apps/desktop/src/timeline_frame_quad_overscan.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_frame_quad_animation.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_frame_quad_animation.h
        ../apps/desktop/src/timeline_frame_quad_animation.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_fractal_noise_source.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_fractal_noise.h
        ../apps/desktop/src/timeline_fractal_noise.cpp
        ../apps/desktop/src/timeline_fractal_noise_source.h
        ../apps/desktop/src/timeline_fractal_noise_source.cpp)
      target_link_libraries("${target}" PRIVATE Qt6::Concurrent Qt6::Qml Qt6::Quick)
    endif()
    if(NOT "../apps/desktop/src/timeline_frame_shadow.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_frame_shadow.h
        ../apps/desktop/src/timeline_frame_shadow.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_native_generator_frame_cache.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_native_generator_frame_cache.h
        ../apps/desktop/src/timeline_native_generator_frame_cache.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_mask_frame_cache.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_mask_frame_cache.h
        ../apps/desktop/src/timeline_mask_frame_cache.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_bounded_h264_export.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_bounded_h264_export.h
        ../apps/desktop/src/timeline_bounded_h264_export.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_compound_producer_identity.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_compound_producer_identity.h
        ../apps/desktop/src/timeline_compound_producer_identity.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_compound_render.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_compound_render.h
        ../apps/desktop/src/timeline_compound_render.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_source_matte.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_source_matte.h
        ../apps/desktop/src/timeline_source_matte.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_frame_shadow.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_frame_shadow.h
        ../apps/desktop/src/timeline_frame_shadow.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_interchange.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_interchange.h
        ../apps/desktop/src/timeline_interchange.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_delivery.cpp" IN_LIST target_sources)
      target_sources(
        "${target}"
        PRIVATE
          ../apps/desktop/src/timeline_delivery.h
          ../apps/desktop/src/timeline_delivery.cpp)
    endif()
    target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_scene_runtime.cpp ../apps/desktop/src/timeline_surface_runtime.cpp)
    if(NOT "../apps/desktop/src/timeline_sam2_runtime.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_sam2_runtime.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_foreground_matte.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_foreground_matte.cpp)
    endif()
    if(NOT "../apps/desktop/src/timeline_motion_runtime_common.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_motion_runtime_common.cpp)
    endif()
    if(APPLE)
      if(NOT "../apps/desktop/src/timeline_motion_runtime.mm" IN_LIST target_sources)
        target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_motion_runtime.mm)
        set_source_files_properties(
          ../apps/desktop/src/timeline_motion_runtime.mm
          PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
      endif()
      if(NOT "../apps/desktop/src/timeline_desktop_light_field_metal.mm" IN_LIST target_sources)
        target_sources("${target}" PRIVATE
          ../apps/desktop/src/timeline_desktop_light_field_metal.h
          ../apps/desktop/src/timeline_desktop_light_field_metal.mm)
        set_source_files_properties(
          ../apps/desktop/src/timeline_desktop_light_field_metal.mm
          PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
      endif()
      target_link_libraries(
        "${target}"
        PRIVATE "-framework CoreGraphics" "-framework CoreVideo" "-framework Metal" "-framework Vision")
    elseif(NOT "../apps/desktop/src/timeline_motion_runtime_stub.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE ../apps/desktop/src/timeline_motion_runtime_stub.cpp)
    endif()
    if(NOT APPLE AND
       NOT "../apps/desktop/src/timeline_desktop_light_field_metal_stub.cpp" IN_LIST target_sources)
      target_sources("${target}" PRIVATE
        ../apps/desktop/src/timeline_desktop_light_field_metal.h
        ../apps/desktop/src/timeline_desktop_light_field_metal_stub.cpp)
    endif()
    qt_add_resources("${target}" "${target}_timeline_surface_worker" PREFIX "/photo-editor/runtime"
      FILES "${PROJECT_SOURCE_DIR}/scripts/timeline_surface_worker.py")
    qt_add_resources(
      "${target}"
      "${target}_timeline_sam2_worker"
      PREFIX "/photo-editor/runtime"
      FILES "${PROJECT_SOURCE_DIR}/scripts/timeline-sam2-worker.py")
  endif()
endforeach()

add_executable(photo_blend_if_test photo_blend_if_test.cpp)
target_include_directories(photo_blend_if_test PRIVATE ../engine/src/pipeline)
target_link_libraries(photo_blend_if_test PRIVATE photo_engine)
photo_editor_apply_warnings(photo_blend_if_test)
add_test(NAME photo_blend_if_test COMMAND photo_blend_if_test)

if(TARGET Qt6::Core)
  add_executable(timeline_bounded_concat_clock_test timeline_bounded_concat_clock_test.cpp)
  target_include_directories(timeline_bounded_concat_clock_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_bounded_concat_clock_test PRIVATE Qt6::Core)
  photo_editor_apply_warnings(timeline_bounded_concat_clock_test)
  add_test(NAME timeline_bounded_concat_clock_test COMMAND timeline_bounded_concat_clock_test)
  set_tests_properties(timeline_bounded_concat_clock_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

if(TARGET Qt6::Core AND TARGET Qt6::Gui)
  add_executable(
    timeline_native_generator_frame_cache_test
    timeline_native_generator_frame_cache_test.cpp
    ../apps/desktop/src/timeline_native_generator_frame_cache.h
    ../apps/desktop/src/timeline_native_generator_frame_cache.cpp
    ../apps/desktop/src/timeline_desktop_light_field_metal.h
    ../apps/desktop/src/timeline_desktop_light_field_metal_stub.cpp)
  target_compile_definitions(
    timeline_native_generator_frame_cache_test
    PRIVATE PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS)
  target_include_directories(
    timeline_native_generator_frame_cache_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(
    timeline_native_generator_frame_cache_test
    PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_native_generator_frame_cache_test)
  add_test(
    NAME timeline_native_generator_frame_cache_test
    COMMAND timeline_native_generator_frame_cache_test)
  set_tests_properties(
    timeline_native_generator_frame_cache_test
    PROPERTIES TIMEOUT 60 LABELS "timeline;cache;performance")

  add_executable(
    timeline_native_generator_vector_cache_test
    timeline_native_generator_vector_cache_test.cpp
    ../apps/desktop/src/timeline_native_generator_frame_cache.h
    ../apps/desktop/src/timeline_native_generator_frame_cache.cpp
    ../apps/desktop/src/timeline_desktop_light_field_metal.h
    ../apps/desktop/src/timeline_desktop_light_field_metal_stub.cpp)
  target_compile_definitions(
    timeline_native_generator_vector_cache_test
    PRIVATE PHOTO_EDITOR_TIMELINE_NATIVE_GENERATOR_CACHE_TEST_HOOKS)
  target_include_directories(
    timeline_native_generator_vector_cache_test
    PRIVATE ../apps/desktop/src)
  target_link_libraries(
    timeline_native_generator_vector_cache_test
    PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_native_generator_vector_cache_test)
  add_test(
    NAME timeline_native_generator_vector_cache_test
    COMMAND timeline_native_generator_vector_cache_test
      "${PROJECT_SOURCE_DIR}/marketing/native-recreation-v4/polish-pass-2026-10-30/native-projects/looksy-polish-pass15-final-graphics.json")
  set_tests_properties(
    timeline_native_generator_vector_cache_test
    PROPERTIES TIMEOUT 60 LABELS "timeline;cache;performance")

  add_executable(timeline_shared_png_inputs_test timeline_shared_png_inputs_test.cpp)
  target_include_directories(timeline_shared_png_inputs_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_shared_png_inputs_test PRIVATE Qt6::Core Qt6::Gui)
  photo_editor_apply_warnings(timeline_shared_png_inputs_test)
  add_test(NAME timeline_shared_png_inputs_test COMMAND timeline_shared_png_inputs_test)
  set_tests_properties(timeline_shared_png_inputs_test PROPERTIES
    TIMEOUT 120 SKIP_RETURN_CODE 77 LABELS "timeline;export;pixels;performance")
endif()

if(TARGET desktop_shell_test_runtime)
  add_executable(brush_dispatch_projection_test brush_dispatch_projection_test.cpp)
  target_include_directories(brush_dispatch_projection_test PRIVATE ../apps/desktop/src)
  target_link_libraries(brush_dispatch_projection_test PRIVATE desktop_shell_test_runtime Qt6::Widgets Qt6::Quick Qt6::Concurrent photo_engine)
  photo_editor_apply_warnings(brush_dispatch_projection_test)
  add_test(NAME brush_dispatch_projection_test COMMAND brush_dispatch_projection_test)
  set_tests_properties(brush_dispatch_projection_test PROPERTIES TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;brush;performance")
endif()

if(TARGET Qt6::Qml)
  add_executable(timeline_workspace_lifecycle_contract_test timeline_workspace_lifecycle_contract_test.cpp)
  target_link_libraries(timeline_workspace_lifecycle_contract_test PRIVATE Qt6::Qml)
  target_compile_definitions(timeline_workspace_lifecycle_contract_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
  photo_editor_apply_warnings(timeline_workspace_lifecycle_contract_test)
  add_test(NAME timeline_workspace_lifecycle_contract_test COMMAND timeline_workspace_lifecycle_contract_test)
  set_tests_properties(timeline_workspace_lifecycle_contract_test PROPERTIES LABELS "desktop;timeline;contract")
endif()

if(TARGET Qt6::Gui)
  add_executable(brush_projection_perf_test brush_projection_perf_test.cpp)
  target_include_directories(brush_projection_perf_test PRIVATE ../apps/desktop/src)
  target_link_libraries(brush_projection_perf_test PRIVATE Qt6::Gui photo_engine)
  photo_editor_apply_warnings(brush_projection_perf_test)
  add_test(NAME brush_projection_perf_test COMMAND brush_projection_perf_test)
  set_tests_properties(brush_projection_perf_test PROPERTIES TIMEOUT 60 LABELS "desktop;brush;performance")
endif()

if(TARGET Qt6::Qml)
  add_executable(studio_scope_visibility_ui_test studio_scope_visibility_ui_test.cpp)
  target_link_libraries(studio_scope_visibility_ui_test PRIVATE Qt6::Core Qt6::Gui Qt6::Qml)
  target_compile_definitions(studio_scope_visibility_ui_test PRIVATE PHOTO_EDITOR_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  photo_editor_apply_warnings(studio_scope_visibility_ui_test)
  add_test(NAME studio_scope_visibility_ui_test COMMAND studio_scope_visibility_ui_test)
  set_tests_properties(studio_scope_visibility_ui_test PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_DISABLE_DISK_CACHE=1"
    LABELS "desktop;qml;scopes;ui")
endif()

# Missing-media restoration must not repeatedly spawn failed waveform workers.
if(TARGET desktop_shell_test_runtime)
  add_executable(timeline_missing_media_restore_test timeline_missing_media_restore_test.cpp)
  target_include_directories(timeline_missing_media_restore_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_missing_media_restore_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(timeline_missing_media_restore_test)
  add_test(NAME timeline_missing_media_restore_test COMMAND timeline_missing_media_restore_test)
  set_tests_properties(timeline_missing_media_restore_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;recovery;performance")
endif()

if(TARGET desktop_shell_test_runtime)
  if(APPLE)
    add_executable(timeline_prores_first_frame_probe timeline_prores_first_frame_probe.cpp)
  endif()
  add_executable(export_delivery_integrity_failure_test export_delivery_integrity_failure_test.cpp)
  foreach(round13_target IN ITEMS timeline_prores_first_frame_probe export_delivery_integrity_failure_test)
    if(TARGET ${round13_target})
      target_include_directories(${round13_target} PRIVATE ../apps/desktop/src)
      target_link_libraries(${round13_target} PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
      photo_editor_apply_warnings(${round13_target})
    endif()
  endforeach()
  add_test(NAME export_delivery_integrity_failure_test COMMAND export_delivery_integrity_failure_test)
  set_tests_properties(export_delivery_integrity_failure_test PROPERTIES TIMEOUT 180 SKIP_RETURN_CODE 77 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;export;integrity")
endif()

if(APPLE AND TARGET desktop_shell_test_runtime)
  add_executable(export_sustained_batch_test export_sustained_batch_test.cpp)
  target_include_directories(export_sustained_batch_test PRIVATE ../apps/desktop/src)
  target_link_libraries(export_sustained_batch_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(export_sustained_batch_test)
endif()

if(APPLE AND TARGET timeline_prores_first_frame_probe)
  target_sources(timeline_prores_first_frame_probe PRIVATE timeline_memory_autorelease_pool.mm)
  target_link_libraries(timeline_prores_first_frame_probe PRIVATE "-framework CoreFoundation" "-framework Foundation")
endif()

if(UNIX AND TARGET desktop_shell_test_runtime)
  add_executable(timeline_waveform_queue_disappearance_test timeline_waveform_queue_disappearance_test.cpp)
  target_include_directories(timeline_waveform_queue_disappearance_test PRIVATE ../apps/desktop/src)
  target_link_libraries(timeline_waveform_queue_disappearance_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Multimedia Qt6::Concurrent)
  photo_editor_apply_warnings(timeline_waveform_queue_disappearance_test)
  add_test(NAME timeline_waveform_queue_disappearance_test COMMAND timeline_waveform_queue_disappearance_test)
  set_tests_properties(timeline_waveform_queue_disappearance_test PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;timeline;recovery;waveform")
endif()

if(TARGET photo_editor_app)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME packaged_qml_startup_test
      COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check-packaged-qml-startup.py"
        --app "$<TARGET_FILE:photo_editor_app>")
    set_tests_properties(packaged_qml_startup_test PROPERTIES
      TIMEOUT 60 LABELS "desktop;qml;startup;packaging")
  endif()
endif()

if(TARGET desktop_shell_test_runtime)
  add_executable(photo_board_recipe_isolation_test photo_board_recipe_isolation_test.cpp)
  target_include_directories(photo_board_recipe_isolation_test PRIVATE ../apps/desktop/src)
  target_link_libraries(photo_board_recipe_isolation_test PRIVATE desktop_shell_test_runtime photo_engine Qt6::Widgets Qt6::Quick Qt6::Concurrent)
  photo_editor_apply_warnings(photo_board_recipe_isolation_test)
  add_test(NAME photo_board_recipe_isolation_test COMMAND photo_board_recipe_isolation_test)
  set_tests_properties(photo_board_recipe_isolation_test PROPERTIES
    TIMEOUT 60 ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "desktop;photo;board;recovery")
endif()
