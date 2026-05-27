#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "photo_editor/pipeline/render_graph.h"

namespace {

bool nearly_equal(double lhs, double rhs) {
  const auto delta = lhs - rhs;
  return delta < 1.0e-9 && delta > -1.0e-9;
}

bool contains_stage_name(
    const std::vector<photo_editor::pipeline::RenderStage>& stages,
    const std::string& name) {
  for (const auto& stage : stages) {
    if (stage.name == name) {
      return true;
    }
  }
  return false;
}

bool contains_name(const std::vector<std::string>& names, const std::string& expected) {
  for (const auto& name : names) {
    if (name == expected) {
      return true;
    }
  }
  return false;
}

bool contains_text(const std::string& text, const std::string& expected) {
  return text.find(expected) != std::string::npos;
}

}  // namespace

int main() {
  const photo_editor::pipeline::RenderGraph graph;
  const auto& stages = graph.stages();

  if (!graph.valid()) {
    std::cerr << "expected render graph dependencies to form a valid DAG\n";
    return EXIT_FAILURE;
  }

  if (stages.size() < 7) {
    std::cerr << "expected full render graph scaffold\n";
    return EXIT_FAILURE;
  }

  if (stages.front().name != "raw_decode") {
    std::cerr << "unexpected first stage\n";
    return EXIT_FAILURE;
  }

  if (stages[1].name != "input_transform") {
    std::cerr << "unexpected second stage\n";
    return EXIT_FAILURE;
  }

  const auto input_dependencies = graph.dependencies_for("input_transform");
  if (input_dependencies.size() != 1 || input_dependencies.front() != "raw_decode") {
    std::cerr << "expected input_transform to declare raw_decode dependency\n";
    return EXIT_FAILURE;
  }

  const auto raw_dependents = graph.dependents_for("raw_decode");
  if (raw_dependents.size() != 1 || raw_dependents.front() != "input_transform") {
    std::cerr << "expected raw_decode to feed input_transform\n";
    return EXIT_FAILURE;
  }

  const auto* output_stage = graph.stage("output_transform");
  if (output_stage == nullptr || output_stage->dependencies.size() != 1 ||
      output_stage->dependencies.front() != "crop_transform") {
    std::cerr << "expected output_transform to depend on crop_transform\n";
    return EXIT_FAILURE;
  }

  const auto output_plan = graph.execution_plan_for("output_transform");
  if (output_plan.empty() || output_plan.front().name != "raw_decode" ||
      output_plan.back().name != "output_transform") {
    std::cerr << "expected output execution plan to be dependency ordered\n";
    return EXIT_FAILURE;
  }

  const auto export_plan = graph.execution_plan_for("export_encode");
  if (export_plan.size() != stages.size()) {
    std::cerr << "expected export execution plan to include every render stage\n";
    return EXIT_FAILURE;
  }
  if (export_plan.front().name != "raw_decode" || export_plan.back().name != "export_encode") {
    std::cerr << "expected export execution plan to be dependency ordered\n";
    return EXIT_FAILURE;
  }

  const auto preview_settings =
      graph.variant_settings(photo_editor::pipeline::RenderGraphVariant::Preview);
  const auto full_resolution_settings =
      graph.variant_settings(photo_editor::pipeline::RenderGraphVariant::FullResolution);
  const auto export_settings =
      graph.variant_settings(photo_editor::pipeline::RenderGraphVariant::Export);
  if (preview_settings.terminal_stage != "output_transform" ||
      preview_settings.full_resolution ||
      preview_settings.encode_artifact ||
      preview_settings.allow_disk_cache) {
    std::cerr << "expected preview variant to stop at display output without full-res artifact encoding\n";
    return EXIT_FAILURE;
  }
  if (full_resolution_settings.terminal_stage != "output_transform" ||
      !full_resolution_settings.full_resolution ||
      full_resolution_settings.encode_artifact ||
      !full_resolution_settings.allow_disk_cache) {
    std::cerr << "expected full-resolution variant to render full image output without encoding artifact\n";
    return EXIT_FAILURE;
  }
  if (export_settings.terminal_stage != "export_encode" ||
      !export_settings.full_resolution ||
      !export_settings.encode_artifact ||
      !export_settings.allow_disk_cache) {
    std::cerr << "expected export variant to render full-resolution encoded artifact\n";
    return EXIT_FAILURE;
  }

  const auto preview_variant_plan =
      graph.execution_plan_for(photo_editor::pipeline::RenderGraphVariant::Preview);
  const auto full_resolution_variant_plan =
      graph.execution_plan_for(photo_editor::pipeline::RenderGraphVariant::FullResolution);
  const auto export_variant_plan =
      graph.execution_plan_for(photo_editor::pipeline::RenderGraphVariant::Export);
  if (preview_variant_plan.empty() ||
      preview_variant_plan.back().name != "output_transform" ||
      contains_stage_name(preview_variant_plan, "export_encode")) {
    std::cerr << "expected preview variant plan to exclude export encoding\n";
    return EXIT_FAILURE;
  }
  if (full_resolution_variant_plan.empty() ||
      full_resolution_variant_plan.back().name != "output_transform" ||
      contains_stage_name(full_resolution_variant_plan, "export_encode")) {
    std::cerr << "expected full-resolution variant plan to exclude export encoding\n";
    return EXIT_FAILURE;
  }
  if (export_variant_plan.size() != stages.size() ||
      export_variant_plan.back().name != "export_encode") {
    std::cerr << "expected export variant plan to include export encoding\n";
    return EXIT_FAILURE;
  }

  const auto local_plan = graph.execution_plan_for("local_adjustments");
  if (!contains_stage_name(local_plan, "raw_decode") ||
      !contains_stage_name(local_plan, "global_tone_color") ||
      !contains_stage_name(local_plan, "local_adjustments") ||
      contains_stage_name(local_plan, "detail") ||
      contains_stage_name(local_plan, "output_transform")) {
    std::cerr << "expected local adjustment plan to include dependencies but not downstream stages\n";
    return EXIT_FAILURE;
  }

  if (!graph.execution_plan_for("missing_stage").empty()) {
    std::cerr << "expected missing terminal stage to return an empty execution plan\n";
    return EXIT_FAILURE;
  }

  if (!graph.contains_stage("layer_composite")) {
    std::cerr << "expected render graph to expose a layer compositing stage\n";
    return EXIT_FAILURE;
  }
  if (!graph.contains_stage("mask_resolve")) {
    std::cerr << "expected render graph to expose masks as first-class graph nodes\n";
    return EXIT_FAILURE;
  }
  const auto* mask_stage = graph.stage("mask_resolve");
  const auto* local_stage = graph.stage("local_adjustments");
  if (mask_stage == nullptr || mask_stage->dependencies.size() != 1 ||
      mask_stage->dependencies.front() != "global_tone_color" || local_stage == nullptr ||
      !contains_name(local_stage->dependencies, "mask_resolve")) {
    std::cerr << "expected local adjustments to consume reusable resolved mask outputs\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::RenderDirtyRegion partial_region{
      .x = 0.1,
      .y = 0.2,
      .width = 0.3,
      .height = 0.4,
      .full_frame = false,
  };

  const auto invalidation_rules = graph.invalidation_rules();
  if (invalidation_rules.size() != 10) {
    std::cerr << "expected explicit invalidation rules for every dirty reason\n";
    return EXIT_FAILURE;
  }
  const auto find_rule = [&invalidation_rules](photo_editor::pipeline::RenderDirtyReason reason) {
    for (const auto& rule : invalidation_rules) {
      if (rule.reason == reason) {
        return &rule;
      }
    }
    return static_cast<const photo_editor::pipeline::RenderInvalidationRule*>(nullptr);
  };
  for (const auto& rule : invalidation_rules) {
    if (!graph.contains_stage(rule.first_affected_stage)) {
      std::cerr << "expected invalidation rule to reference a real graph stage\n";
      return EXIT_FAILURE;
    }
  }
  const auto* mask_rule = find_rule(photo_editor::pipeline::RenderDirtyReason::MaskEdit);
  const auto* brush_rule = find_rule(photo_editor::pipeline::RenderDirtyReason::BrushStroke);
  const auto* crop_rule = find_rule(photo_editor::pipeline::RenderDirtyReason::CropTransform);
  const auto* layer_rule = find_rule(photo_editor::pipeline::RenderDirtyReason::LayerChange);
  if (mask_rule == nullptr || mask_rule->first_affected_stage != "mask_resolve" ||
      mask_rule->full_frame_required ||
      brush_rule == nullptr || brush_rule->first_affected_stage != "mask_resolve" ||
      brush_rule->full_frame_required ||
      crop_rule == nullptr || crop_rule->first_affected_stage != "crop_transform" ||
      !crop_rule->full_frame_required ||
      layer_rule == nullptr || layer_rule->first_affected_stage != "layer_composite" ||
      layer_rule->full_frame_required) {
    std::cerr << "expected explicit invalidation rules for masks, brushes, crops, and layers\n";
    return EXIT_FAILURE;
  }

  const auto local_invalidation =
      graph.invalidation_for(photo_editor::pipeline::RenderDirtyReason::LocalAdjustment, partial_region);
  if (local_invalidation.region.full_frame ||
      !nearly_equal(local_invalidation.region.x, 0.1) ||
      !nearly_equal(local_invalidation.region.y, 0.2) ||
      !nearly_equal(local_invalidation.region.width, 0.3) ||
      !nearly_equal(local_invalidation.region.height, 0.4) ||
      !contains_name(local_invalidation.affected_stages, "local_adjustments") ||
      !contains_name(local_invalidation.affected_stages, "output_transform") ||
      contains_name(local_invalidation.affected_stages, "global_tone_color")) {
    std::cerr << "expected local edits to preserve dirty region and affect local downstream stages\n";
    return EXIT_FAILURE;
  }

  const auto mask_invalidation =
      graph.invalidation_for(photo_editor::pipeline::RenderDirtyReason::MaskEdit, partial_region);
  if (mask_invalidation.region.full_frame ||
      !contains_name(mask_invalidation.affected_stages, "mask_resolve") ||
      !contains_name(mask_invalidation.affected_stages, "local_adjustments") ||
      !contains_name(mask_invalidation.affected_stages, "crop_transform")) {
    std::cerr << "expected mask edits to track dirty region through downstream render stages\n";
    return EXIT_FAILURE;
  }

  const photo_editor::pipeline::RenderDirtyRegion overflowing_region{
      .x = 0.9,
      .y = 0.8,
      .width = 0.5,
      .height = 0.4,
      .full_frame = false,
  };
  const auto brush_invalidation =
      graph.invalidation_for(photo_editor::pipeline::RenderDirtyReason::BrushStroke, overflowing_region);
  if (brush_invalidation.region.full_frame ||
      !nearly_equal(brush_invalidation.region.x, 0.9) ||
      !nearly_equal(brush_invalidation.region.y, 0.8) ||
      !nearly_equal(brush_invalidation.region.width, 0.1) ||
      !nearly_equal(brush_invalidation.region.height, 0.2)) {
    std::cerr << "expected brush dirty regions to clamp to normalized image bounds\n";
    return EXIT_FAILURE;
  }

  const auto crop_invalidation =
      graph.invalidation_for(photo_editor::pipeline::RenderDirtyReason::CropTransform, partial_region);
  if (!crop_invalidation.region.full_frame ||
      crop_invalidation.affected_stages.size() != 3 ||
      crop_invalidation.affected_stages.front() != "crop_transform" ||
      crop_invalidation.affected_stages.back() != "export_encode") {
    std::cerr << "expected crop changes to invalidate the full frame from crop through export\n";
    return EXIT_FAILURE;
  }

  const auto layer_invalidation =
      graph.invalidation_for(photo_editor::pipeline::RenderDirtyReason::LayerChange, partial_region);
  if (layer_invalidation.region.full_frame ||
      !contains_name(layer_invalidation.affected_stages, "layer_composite") ||
      !contains_name(layer_invalidation.affected_stages, "detail") ||
      contains_name(layer_invalidation.affected_stages, "local_adjustments")) {
    std::cerr << "expected layer changes to track dirty region from layer compositing downstream\n";
    return EXIT_FAILURE;
  }

  const auto cacheable_stages = graph.cacheable_stage_names();
  if (!graph.stage_cacheable("raw_decode") ||
      !graph.stage_cacheable("input_transform") ||
      !graph.stage_cacheable("mask_resolve") ||
      !graph.stage_cacheable("local_adjustments") ||
      !graph.stage_cacheable("detail") ||
      !graph.stage_cacheable("export_encode") ||
      !contains_name(cacheable_stages, "output_transform")) {
    std::cerr << "expected expensive decode, color, mask, detail, and export stages to be cacheable\n";
    return EXIT_FAILURE;
  }

  const auto uncancellable_cacheable_stages = graph.uncancellable_cacheable_stage_names();
  if (!uncancellable_cacheable_stages.empty() ||
      !graph.stage_cancellable("raw_decode") ||
      !graph.stage_cancellable("mask_resolve") ||
      !graph.stage_cancellable("local_adjustments") ||
      !graph.stage_cancellable("detail") ||
      !graph.stage_cancellable("export_encode") ||
      !contains_name(graph.cancellable_stage_names(), "output_transform")) {
    std::cerr << "expected every expensive cached stage to declare cancellation support\n";
    return EXIT_FAILURE;
  }

  const auto* raw_decode_stage = graph.stage("raw_decode");
  const auto* export_stage = graph.stage("export_encode");
  if (raw_decode_stage == nullptr || export_stage == nullptr ||
      raw_decode_stage->cache_policy != photo_editor::pipeline::RenderCachePolicy::MemoryAndDisk ||
      export_stage->cache_policy != photo_editor::pipeline::RenderCachePolicy::Disk) {
    std::cerr << "expected cache policies to distinguish decoded buffers from encoded artifacts\n";
    return EXIT_FAILURE;
  }

  const auto mask_invalidated_cache_nodes =
      graph.invalidated_cache_nodes_for(mask_invalidation);
  if (!contains_name(mask_invalidated_cache_nodes, "mask_resolve") ||
      !contains_name(mask_invalidated_cache_nodes, "local_adjustments") ||
      !contains_name(mask_invalidated_cache_nodes, "detail") ||
      !contains_name(mask_invalidated_cache_nodes, "export_encode") ||
      contains_name(mask_invalidated_cache_nodes, "raw_decode") ||
      contains_name(mask_invalidated_cache_nodes, "input_transform")) {
    std::cerr << "expected mask edits to invalidate downstream cached nodes only\n";
    return EXIT_FAILURE;
  }

  const auto crop_invalidated_cache_nodes =
      graph.invalidated_cache_nodes_for(crop_invalidation);
  if (crop_invalidated_cache_nodes.size() != 3 ||
      crop_invalidated_cache_nodes.front() != "crop_transform" ||
      crop_invalidated_cache_nodes.back() != "export_encode") {
    std::cerr << "expected crop edits to invalidate cached crop, output, and export nodes\n";
    return EXIT_FAILURE;
  }

  photo_editor::pipeline::RenderGraphDiagnostics diagnostics(graph);
  diagnostics.record_stage(photo_editor::pipeline::RenderStageDiagnostics{
      .stage_name = "raw_decode",
      .elapsed_ms = 4.25,
      .memory_bytes = 4096,
      .cache_hit = true,
  });
  diagnostics.record_stage(photo_editor::pipeline::RenderStageDiagnostics{
      .stage_name = "local_adjustments",
      .elapsed_ms = 3.0,
      .memory_bytes = 8192,
      .gpu_elapsed_ms = 1.5,
      .gpu_memory_bytes = 16384,
      .gpu_used = true,
  });
  diagnostics.record_stage(photo_editor::pipeline::RenderStageDiagnostics{
      .stage_name = "output_transform",
      .elapsed_ms = 2.0,
      .memory_bytes = 2048,
      .fallback_reason = "Display profile unavailable",
  });

  const auto* local_diagnostics = diagnostics.stage("local_adjustments");
  if (local_diagnostics == nullptr ||
      !local_diagnostics->gpu_used ||
      !nearly_equal(local_diagnostics->gpu_elapsed_ms, 1.5)) {
    std::cerr << "expected diagnostics to record per-node GPU usage\n";
    return EXIT_FAILURE;
  }
  if (!nearly_equal(diagnostics.total_elapsed_ms(), 9.25) ||
      diagnostics.peak_memory_bytes() != 8192 ||
      diagnostics.peak_gpu_memory_bytes() != 16384) {
    std::cerr << "expected diagnostics to aggregate elapsed time and memory peaks\n";
    return EXIT_FAILURE;
  }
  if (!contains_name(diagnostics.gpu_stage_names(), "local_adjustments") ||
      !contains_name(diagnostics.cache_hit_stage_names(), "raw_decode") ||
      !contains_name(diagnostics.fallback_stage_names(), "output_transform")) {
    std::cerr << "expected diagnostics summaries for GPU, cache hits, and fallbacks\n";
    return EXIT_FAILURE;
  }
  if (!diagnostics.complete_for(preview_variant_plan) ||
      !diagnostics.complete_for(export_variant_plan)) {
    std::cerr << "expected graph diagnostics to be allocated for every planned node\n";
    return EXIT_FAILURE;
  }

  const auto serialized_export_state =
      graph.serialized_state(photo_editor::pipeline::RenderGraphVariant::Export);
  if (!contains_text(serialized_export_state, "\"variant\":\"export\"") ||
      !contains_text(serialized_export_state, "\"terminal_stage\":\"export_encode\"") ||
      !contains_text(serialized_export_state, "\"full_resolution\":true") ||
      !contains_text(serialized_export_state, "\"name\":\"export_encode\"") ||
      !contains_text(serialized_export_state, "\"dependencies\":[\"output_transform\"]") ||
       !contains_text(serialized_export_state, "\"cache_policy\":\"disk\"") ||
       !contains_text(serialized_export_state, "\"cancellable\":true") ||
       !contains_text(serialized_export_state, "\"name\":\"mask_resolve\"") ||
       !contains_text(serialized_export_state, "\"reason\":\"mask_edit\"") ||
       !contains_text(serialized_export_state, "\"first_affected_stage\":\"mask_resolve\"")) {
    std::cerr << "expected serialized graph state to include variant, stages, dependencies, cache, and invalidation rules\n";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
