#include "photo_editor/pipeline/render_graph.h"

#include <algorithm>
#include <string_view>
#include <unordered_set>

namespace photo_editor::pipeline {

namespace {

[[nodiscard]] bool contains_name(const std::vector<RenderStage>& stages, std::string_view name) {
  return std::ranges::any_of(stages, [name](const RenderStage& stage) {
    return stage.name == name;
  });
}

[[nodiscard]] const RenderStage* find_stage(
    const std::vector<RenderStage>& stages,
    std::string_view name) {
  const auto iterator = std::ranges::find_if(stages, [name](const RenderStage& stage) {
    return stage.name == name;
  });
  return iterator == stages.cend() ? nullptr : &(*iterator);
}

[[nodiscard]] bool string_vector_contains(
    const std::vector<std::string>& values,
    std::string_view value) {
  return std::ranges::any_of(values, [value](const std::string& candidate) {
    return candidate == value;
  });
}

[[nodiscard]] double clamp_unit_interval(double value) {
  return std::clamp(value, 0.0, 1.0);
}

[[nodiscard]] RenderDirtyRegion normalize_dirty_region(RenderDirtyRegion region) {
  if (region.full_frame || region.width <= 0.0 || region.height <= 0.0) {
    return {};
  }

  const auto left = clamp_unit_interval(region.x);
  const auto top = clamp_unit_interval(region.y);
  const auto right = clamp_unit_interval(region.x + region.width);
  const auto bottom = clamp_unit_interval(region.y + region.height);
  if (right <= left || bottom <= top) {
    return {};
  }

  return RenderDirtyRegion{
      .x = left,
      .y = top,
      .width = right - left,
      .height = bottom - top,
      .full_frame = false,
  };
}

[[nodiscard]] bool visit_stage(
    const std::vector<RenderStage>& stages,
    std::string_view name,
    std::unordered_set<std::string>* visiting,
    std::unordered_set<std::string>* visited) {
  if (visiting == nullptr || visited == nullptr) {
    return false;
  }

  if (visited->contains(std::string{name})) {
    return true;
  }
  if (visiting->contains(std::string{name})) {
    return false;
  }

  const auto* current_stage = find_stage(stages, name);
  if (current_stage == nullptr) {
    return false;
  }

  visiting->insert(current_stage->name);
  for (const auto& dependency : current_stage->dependencies) {
    if (!visit_stage(stages, dependency, visiting, visited)) {
      return false;
    }
  }
  visiting->erase(current_stage->name);
  visited->insert(current_stage->name);
  return true;
}

[[nodiscard]] bool stage_depends_on(
    const std::vector<RenderStage>& stages,
    std::string_view stage_name,
    std::string_view dependency_name) {
  const auto* current_stage = find_stage(stages, stage_name);
  if (current_stage == nullptr) {
    return false;
  }
  for (const auto& dependency : current_stage->dependencies) {
    if (dependency == dependency_name ||
        stage_depends_on(stages, dependency, dependency_name)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool validate_graph(const std::vector<RenderStage>& stages) {
  std::unordered_set<std::string> names;
  for (const auto& stage : stages) {
    if (stage.name.empty() || !names.insert(stage.name).second) {
      return false;
    }
    for (const auto& dependency : stage.dependencies) {
      if (!contains_name(stages, dependency)) {
        return false;
      }
    }
  }

  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  for (const auto& stage : stages) {
    if (!visit_stage(stages, stage.name, &visiting, &visited)) {
      return false;
    }
  }
  return true;
}

void append_execution_dependencies(
    const std::vector<RenderStage>& stages,
    std::string_view name,
    std::unordered_set<std::string>* visited,
    std::vector<RenderStage>* execution_plan) {
  if (visited == nullptr || execution_plan == nullptr || visited->contains(std::string{name})) {
    return;
  }

  const auto* current_stage = find_stage(stages, name);
  if (current_stage == nullptr) {
    return;
  }

  for (const auto& dependency : current_stage->dependencies) {
    append_execution_dependencies(stages, dependency, visited, execution_plan);
  }
  visited->insert(current_stage->name);
  execution_plan->push_back(*current_stage);
}

[[nodiscard]] std::string_view first_dirty_stage_for(RenderDirtyReason reason) {
  switch (reason) {
    case RenderDirtyReason::SourceDecode:
      return "raw_decode";
    case RenderDirtyReason::InputTransform:
      return "input_transform";
    case RenderDirtyReason::GlobalAdjustment:
      return "global_tone_color";
    case RenderDirtyReason::LocalAdjustment:
      return "local_adjustments";
    case RenderDirtyReason::MaskEdit:
    case RenderDirtyReason::BrushStroke:
      return "mask_resolve";
    case RenderDirtyReason::LayerChange:
      return "layer_composite";
    case RenderDirtyReason::DetailAdjustment:
      return "detail";
    case RenderDirtyReason::CropTransform:
      return "crop_transform";
    case RenderDirtyReason::OutputTransform:
      return "output_transform";
  }
  return "raw_decode";
}

[[nodiscard]] bool reason_requires_full_frame(RenderDirtyReason reason) {
  switch (reason) {
    case RenderDirtyReason::SourceDecode:
    case RenderDirtyReason::InputTransform:
    case RenderDirtyReason::GlobalAdjustment:
    case RenderDirtyReason::DetailAdjustment:
    case RenderDirtyReason::CropTransform:
    case RenderDirtyReason::OutputTransform:
      return true;
    case RenderDirtyReason::LocalAdjustment:
    case RenderDirtyReason::MaskEdit:
    case RenderDirtyReason::BrushStroke:
    case RenderDirtyReason::LayerChange:
      return false;
  }
  return true;
}

[[nodiscard]] std::string cache_policy_name(RenderCachePolicy policy) {
  switch (policy) {
    case RenderCachePolicy::None:
      return "none";
    case RenderCachePolicy::Memory:
      return "memory";
    case RenderCachePolicy::Disk:
      return "disk";
    case RenderCachePolicy::MemoryAndDisk:
      return "memory_and_disk";
  }
  return "none";
}

[[nodiscard]] std::string variant_name(RenderGraphVariant variant) {
  switch (variant) {
    case RenderGraphVariant::Preview:
      return "preview";
    case RenderGraphVariant::FullResolution:
      return "full_resolution";
    case RenderGraphVariant::Export:
      return "export";
  }
  return "preview";
}

[[nodiscard]] std::string dirty_reason_name(RenderDirtyReason reason) {
  switch (reason) {
    case RenderDirtyReason::SourceDecode:
      return "source_decode";
    case RenderDirtyReason::InputTransform:
      return "input_transform";
    case RenderDirtyReason::GlobalAdjustment:
      return "global_adjustment";
    case RenderDirtyReason::LocalAdjustment:
      return "local_adjustment";
    case RenderDirtyReason::MaskEdit:
      return "mask_edit";
    case RenderDirtyReason::BrushStroke:
      return "brush_stroke";
    case RenderDirtyReason::DetailAdjustment:
      return "detail_adjustment";
    case RenderDirtyReason::CropTransform:
      return "crop_transform";
    case RenderDirtyReason::LayerChange:
      return "layer_change";
    case RenderDirtyReason::OutputTransform:
      return "output_transform";
  }
  return "source_decode";
}

[[nodiscard]] std::string json_string(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const auto character : value) {
    if (character == '"' || character == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

void append_string_array(std::string* output, const std::vector<std::string>& values) {
  if (output == nullptr) {
    return;
  }
  output->push_back('[');
  for (std::size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      output->push_back(',');
    }
    *output += json_string(values[index]);
  }
  output->push_back(']');
}

}  // namespace

RenderGraph::RenderGraph()
    : stages_({
          {"raw_decode",
           "Decode RAW or raster source into working space.",
           {},
           RenderCachePolicy::MemoryAndDisk,
           true},
          {"input_transform",
           "Apply input profile, white balance adaptation, and source-to-working transform.",
           {"raw_decode"},
           RenderCachePolicy::Memory,
           true},
          {"camera_lens_normalization",
           "Apply lens correction and geometry normalization.",
           {"input_transform"},
           RenderCachePolicy::Memory,
           true},
          {"global_tone_color",
           "Apply tone curve, presence, grading, and color mixer controls.",
           {"camera_lens_normalization"},
           RenderCachePolicy::Memory,
           true},
          {"mask_resolve",
           "Resolve mask stack geometry, bitmap selections, and brush strokes into reusable mask outputs.",
           {"global_tone_color"},
           RenderCachePolicy::Memory,
           true},
          {"local_adjustments",
           "Apply local adjustment stacks using resolved mask outputs.",
           {"global_tone_color", "mask_resolve"},
           RenderCachePolicy::Memory,
           true},
          {"layer_composite",
           "Composite source, generated, and adjustment layers before detail processing.",
           {"local_adjustments"},
           RenderCachePolicy::Memory,
           true},
          {"detail",
           "Apply sharpening, denoise, and fine detail controls.",
           {"layer_composite"},
           RenderCachePolicy::Memory,
           true},
          {"crop_transform",
           "Apply crop and transform after tone and mask edits.",
           {"detail"},
           RenderCachePolicy::Memory,
           true},
          {"output_transform",
           "Convert into display or export target color space.",
           {"crop_transform"},
           RenderCachePolicy::MemoryAndDisk,
           true},
          {"export_encode",
           "Encode preview or export output artifact.",
           {"output_transform"},
           RenderCachePolicy::Disk,
           true},
      }),
      valid_(validate_graph(stages_)) {}

const std::vector<RenderStage>& RenderGraph::stages() const noexcept { return stages_; }

bool RenderGraph::contains_stage(std::string_view name) const noexcept {
  return contains_name(stages_, name);
}

const RenderStage* RenderGraph::stage(std::string_view name) const noexcept {
  return find_stage(stages_, name);
}

std::vector<std::string> RenderGraph::dependencies_for(std::string_view name) const {
  const auto* render_stage = stage(name);
  return render_stage == nullptr ? std::vector<std::string>{} : render_stage->dependencies;
}

std::vector<std::string> RenderGraph::dependents_for(std::string_view name) const {
  std::vector<std::string> dependents;
  for (const auto& render_stage : stages_) {
    if (string_vector_contains(render_stage.dependencies, name)) {
      dependents.push_back(render_stage.name);
    }
  }
  return dependents;
}

std::vector<std::string> RenderGraph::downstream_stages_from(std::string_view name) const {
  std::vector<std::string> downstream_stages;
  if (!valid_ || !contains_stage(name)) {
    return downstream_stages;
  }

  for (const auto& render_stage : stages_) {
    if (render_stage.name == name || stage_depends_on(stages_, render_stage.name, name)) {
      downstream_stages.push_back(render_stage.name);
    }
  }
  return downstream_stages;
}

std::vector<RenderStage> RenderGraph::execution_plan_for(std::string_view terminal_stage) const {
  std::vector<RenderStage> execution_plan;
  if (!valid_ || !contains_stage(terminal_stage)) {
    return execution_plan;
  }

  std::unordered_set<std::string> visited;
  append_execution_dependencies(stages_, terminal_stage, &visited, &execution_plan);
  return execution_plan;
}

RenderGraphVariantSettings RenderGraph::variant_settings(RenderGraphVariant variant) const {
  switch (variant) {
    case RenderGraphVariant::Preview:
      return RenderGraphVariantSettings{
          .variant = variant,
          .terminal_stage = "output_transform",
          .full_resolution = false,
          .encode_artifact = false,
          .allow_disk_cache = false,
      };
    case RenderGraphVariant::FullResolution:
      return RenderGraphVariantSettings{
          .variant = variant,
          .terminal_stage = "output_transform",
          .full_resolution = true,
          .encode_artifact = false,
          .allow_disk_cache = true,
      };
    case RenderGraphVariant::Export:
      return RenderGraphVariantSettings{
          .variant = variant,
          .terminal_stage = "export_encode",
          .full_resolution = true,
          .encode_artifact = true,
          .allow_disk_cache = true,
      };
  }
  return {};
}

std::vector<RenderStage> RenderGraph::execution_plan_for(RenderGraphVariant variant) const {
  return execution_plan_for(variant_settings(variant).terminal_stage);
}

std::string RenderGraph::serialized_state(RenderGraphVariant variant) const {
  const auto settings = variant_settings(variant);
  std::string output;
  output.reserve(4096);
  output += "{\"variant\":";
  output += json_string(variant_name(settings.variant));
  output += ",\"terminal_stage\":";
  output += json_string(settings.terminal_stage);
  output += ",\"full_resolution\":";
  output += settings.full_resolution ? "true" : "false";
  output += ",\"encode_artifact\":";
  output += settings.encode_artifact ? "true" : "false";
  output += ",\"allow_disk_cache\":";
  output += settings.allow_disk_cache ? "true" : "false";
  output += ",\"stages\":[";
  for (std::size_t index = 0; index < stages_.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    const auto& render_stage = stages_[index];
    output += "{\"name\":";
    output += json_string(render_stage.name);
    output += ",\"description\":";
    output += json_string(render_stage.description);
    output += ",\"dependencies\":";
    append_string_array(&output, render_stage.dependencies);
    output += ",\"cache_policy\":";
    output += json_string(cache_policy_name(render_stage.cache_policy));
    output += ",\"cancellable\":";
    output += render_stage.cancellable ? "true" : "false";
    output.push_back('}');
  }
  output += "],\"invalidation_rules\":[";
  const auto rules = invalidation_rules();
  for (std::size_t index = 0; index < rules.size(); ++index) {
    if (index > 0) {
      output.push_back(',');
    }
    const auto& rule = rules[index];
    output += "{\"reason\":";
    output += json_string(dirty_reason_name(rule.reason));
    output += ",\"first_affected_stage\":";
    output += json_string(rule.first_affected_stage);
    output += ",\"full_frame_required\":";
    output += rule.full_frame_required ? "true" : "false";
    output.push_back('}');
  }
  output += "]}";
  return output;
}

RenderInvalidation RenderGraph::invalidation_for(
    RenderDirtyReason reason,
    const RenderDirtyRegion& changed_region) const {
  const auto first_dirty_stage = first_dirty_stage_for(reason);
  return RenderInvalidation{
      .reason = reason,
      .region = reason_requires_full_frame(reason) ? RenderDirtyRegion{}
                                                   : normalize_dirty_region(changed_region),
      .affected_stages = downstream_stages_from(first_dirty_stage),
  };
}

std::vector<RenderInvalidationRule> RenderGraph::invalidation_rules() const {
  const std::vector<RenderDirtyReason> reasons = {
      RenderDirtyReason::SourceDecode,
      RenderDirtyReason::InputTransform,
      RenderDirtyReason::GlobalAdjustment,
      RenderDirtyReason::LocalAdjustment,
      RenderDirtyReason::MaskEdit,
      RenderDirtyReason::BrushStroke,
      RenderDirtyReason::DetailAdjustment,
      RenderDirtyReason::CropTransform,
      RenderDirtyReason::LayerChange,
      RenderDirtyReason::OutputTransform,
  };

  std::vector<RenderInvalidationRule> rules;
  rules.reserve(reasons.size());
  for (const auto reason : reasons) {
    rules.push_back(RenderInvalidationRule{
        .reason = reason,
        .first_affected_stage = std::string{first_dirty_stage_for(reason)},
        .full_frame_required = reason_requires_full_frame(reason),
    });
  }
  return rules;
}

bool RenderGraph::stage_cacheable(std::string_view name) const noexcept {
  const auto* render_stage = stage(name);
  return render_stage != nullptr && render_stage->cache_policy != RenderCachePolicy::None;
}

std::vector<std::string> RenderGraph::cacheable_stage_names() const {
  std::vector<std::string> names;
  for (const auto& render_stage : stages_) {
    if (render_stage.cache_policy != RenderCachePolicy::None) {
      names.push_back(render_stage.name);
    }
  }
  return names;
}

bool RenderGraph::stage_cancellable(std::string_view name) const noexcept {
  const auto* render_stage = stage(name);
  return render_stage != nullptr && render_stage->cancellable;
}

std::vector<std::string> RenderGraph::cancellable_stage_names() const {
  std::vector<std::string> names;
  for (const auto& render_stage : stages_) {
    if (render_stage.cancellable) {
      names.push_back(render_stage.name);
    }
  }
  return names;
}

std::vector<std::string> RenderGraph::uncancellable_cacheable_stage_names() const {
  std::vector<std::string> names;
  for (const auto& render_stage : stages_) {
    if (render_stage.cache_policy != RenderCachePolicy::None && !render_stage.cancellable) {
      names.push_back(render_stage.name);
    }
  }
  return names;
}

std::vector<std::string> RenderGraph::invalidated_cache_nodes_for(
    const RenderInvalidation& invalidation) const {
  std::vector<std::string> invalidated_nodes;
  for (const auto& stage_name : invalidation.affected_stages) {
    if (stage_cacheable(stage_name)) {
      invalidated_nodes.push_back(stage_name);
    }
  }
  return invalidated_nodes;
}

bool RenderGraph::valid() const noexcept { return valid_; }

RenderGraphDiagnostics::RenderGraphDiagnostics(const RenderGraph& graph) {
  stages_.reserve(graph.stages().size());
  for (const auto& render_stage : graph.stages()) {
    stages_.push_back(RenderStageDiagnostics{.stage_name = render_stage.name});
  }
}

void RenderGraphDiagnostics::record_stage(RenderStageDiagnostics diagnostics) {
  const auto iterator = std::ranges::find_if(stages_, [&diagnostics](const auto& existing) {
    return existing.stage_name == diagnostics.stage_name;
  });
  if (iterator == stages_.end()) {
    stages_.push_back(std::move(diagnostics));
    return;
  }
  *iterator = std::move(diagnostics);
}

const std::vector<RenderStageDiagnostics>& RenderGraphDiagnostics::stages() const noexcept {
  return stages_;
}

const RenderStageDiagnostics* RenderGraphDiagnostics::stage(std::string_view name) const noexcept {
  const auto iterator = std::ranges::find_if(stages_, [name](const auto& diagnostics) {
    return diagnostics.stage_name == name;
  });
  return iterator == stages_.cend() ? nullptr : &(*iterator);
}

double RenderGraphDiagnostics::total_elapsed_ms() const noexcept {
  auto total_elapsed = 0.0;
  for (const auto& diagnostics : stages_) {
    total_elapsed += diagnostics.elapsed_ms;
  }
  return total_elapsed;
}

std::uint64_t RenderGraphDiagnostics::peak_memory_bytes() const noexcept {
  std::uint64_t peak_memory = 0;
  for (const auto& diagnostics : stages_) {
    peak_memory = std::max(peak_memory, diagnostics.memory_bytes);
  }
  return peak_memory;
}

std::uint64_t RenderGraphDiagnostics::peak_gpu_memory_bytes() const noexcept {
  std::uint64_t peak_memory = 0;
  for (const auto& diagnostics : stages_) {
    peak_memory = std::max(peak_memory, diagnostics.gpu_memory_bytes);
  }
  return peak_memory;
}

std::vector<std::string> RenderGraphDiagnostics::gpu_stage_names() const {
  std::vector<std::string> names;
  for (const auto& diagnostics : stages_) {
    if (diagnostics.gpu_used) {
      names.push_back(diagnostics.stage_name);
    }
  }
  return names;
}

std::vector<std::string> RenderGraphDiagnostics::cache_hit_stage_names() const {
  std::vector<std::string> names;
  for (const auto& diagnostics : stages_) {
    if (diagnostics.cache_hit) {
      names.push_back(diagnostics.stage_name);
    }
  }
  return names;
}

std::vector<std::string> RenderGraphDiagnostics::fallback_stage_names() const {
  std::vector<std::string> names;
  for (const auto& diagnostics : stages_) {
    if (!diagnostics.fallback_reason.empty()) {
      names.push_back(diagnostics.stage_name);
    }
  }
  return names;
}

bool RenderGraphDiagnostics::complete_for(
    const std::vector<RenderStage>& execution_plan) const noexcept {
  for (const auto& render_stage : execution_plan) {
    const auto* diagnostics = stage(render_stage.name);
    if (diagnostics == nullptr) {
      return false;
    }
  }
  return true;
}

}  // namespace photo_editor::pipeline
