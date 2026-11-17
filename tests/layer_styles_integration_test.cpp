#include "color_layer_runtime.h"
#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace photo_editor::pipeline;

namespace {

bool near(float actual, double expected, double tolerance = 1.0e-5) {
  return std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

LayerStyleOperation stroke(std::string id, LayerStyleColor color, double width = 1.0) {
  LayerStyleStroke parameters;
  parameters.width = width;
  parameters.alignment = LayerStyleStrokeAlignment::Outside;
  parameters.fill = color;
  LayerStyleOperation operation;
  operation.id = std::move(id);
  operation.type_id = "stroke";
  operation.parameters = std::move(parameters);
  operation.plane = LayerStylePlane::BelowContent;
  return operation;
}

bool render(std::vector<ColorPixel>* pixels, int width, int height,
    const std::vector<PhotoLayer>& layers, std::string* error) {
  ColorAdjustmentState state;
  return apply_standard_photo_layers(pixels, width, height, make_pipeline_color_context(state),
      state.color_pipeline.photographic_rendering_model, layers, {}, error);
}

bool pixel_style_expansion_obeys_layer_opacity_and_mask() {
  std::vector<ColorPixel> canvas(25, ColorPixel{0, 0, 0, 0});
  PhotoLayer layer;
  layer.id = "styled-pixel";
  layer.kind = "pixel";
  layer.x = 2;
  layer.y = 2;
  layer.opacity = 0.5;
  layer.mask_width = 1;
  layer.mask_height = 1;
  layer.mask_data = {255};
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 0.5F}};
  layer.style_stack.operations = {stroke("outer-red", {1, 0, 0, 1})};

  std::string error;
  if (!render(&canvas, 5, 5, {layer}, &error)) {
    std::cerr << "pixel style render failed: " << error << '\n';
    return false;
  }
  const auto ring = canvas[2U * 5U + 1U];
  const auto center = canvas[2U * 5U + 2U];
  return near(ring.red, 1.0) && near(ring.alpha, 0.25) &&
      near(center.red, 1.0) && near(center.alpha, 0.25);
}

bool style_document_scale_and_global_light_are_explicit() {
  std::vector<ColorPixel> canvas(16, ColorPixel{0, 0, 0, 0});
  PhotoLayer layer;
  layer.kind = "pixel";
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 1}};
  layer.x = 2;
  layer.y = 1;
  layer.style_scale_x = 2.0;
  layer.style_scale_y = 1.0;
  layer.style_stack.operations = {stroke("scaled", {1, 0, 0, 1})};
  std::string error;
  if (!render(&canvas, 4, 4, {layer}, &error)) {
    std::cerr << "scaled layer style render failed: " << error << '\n';
    return false;
  }
  const auto scaled_ring = canvas[0];
  if (!near(scaled_ring.alpha, 1.0)) return false;

  canvas.assign(16, ColorPixel{0, 0, 0, 0});
  layer.x = 0;
  layer.y = 0;
  layer.style_scale_x = 1.0;
  layer.style_scale_y = 1.0;
  layer.style_stack.operations.clear();
  LayerStyleDropShadow shadow;
  shadow.color = {0, 0, 0, 0.6};
  shadow.distance = 1.0;
  shadow.angle_degrees = 0.0;
  shadow.blur = 0.0;
  shadow.use_global_light = true;
  LayerStyleOperation shadow_operation;
  shadow_operation.id = "global-shadow";
  shadow_operation.type_id = "drop_shadow";
  shadow_operation.parameters = shadow;
  shadow_operation.plane = LayerStylePlane::BelowContent;
  layer.style_global_light_angle = 90.0;
  layer.style_stack.operations = {shadow_operation};
  if (!render(&canvas, 4, 4, {layer}, &error)) {
    std::cerr << "globally lit layer style render failed: " << error << '\n';
    return false;
  }
  return near(canvas[0].alpha, 1.0) && near(canvas[4].alpha, 0.6);
}

bool style_masks_resolve_and_missing_masks_fail_closed() {
  PhotoLayer layer;
  layer.kind = "pixel";
  layer.x = 1;
  layer.y = 1;
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 1}};
  auto outer = stroke("masked-stroke", {1, 0, 0, 1});
  outer.mask_ref = "style-mask";
  layer.style_stack.operations = {outer};
  layer.effect_masks = {{"style-mask", {0.0F}}};
  std::vector<ColorPixel> canvas(9, ColorPixel{0, 0, 0, 0});
  std::string error;
  if (!render(&canvas, 3, 3, {layer}, &error)) {
    std::cerr << "resolved style mask render failed: " << error << '\n';
    return false;
  }
  if (!near(canvas[1].alpha, 0.0) || !near(canvas[4].alpha, 1.0)) return false;

  layer.effect_masks.clear();
  const std::vector<ColorPixel> original{{0.2F, 0.3F, 0.4F, 1.0F}};
  canvas = original;
  if (render(&canvas, 1, 1, {layer}, &error)) return false;
  return canvas == original && !error.empty();
}

bool group_styles_apply_to_private_child_composite() {
  std::vector<ColorPixel> canvas(25, ColorPixel{0, 0, 0, 0});
  PhotoLayer group;
  group.id = "group";
  group.kind = "group";
  group.style_stack.operations = {stroke("group-stroke", {0, 1, 0, 1})};
  PhotoLayer child;
  child.id = "child";
  child.parent_id = group.id;
  child.kind = "pixel";
  child.x = 2;
  child.y = 2;
  child.pixel_width = 1;
  child.pixel_height = 1;
  child.pixels = {{1, 1, 1, 1}};
  std::string error;
  if (!render(&canvas, 5, 5, {group, child}, &error)) {
    std::cerr << "group layer style render failed: " << error << '\n';
    return false;
  }
  const auto ring = canvas[2U * 5U + 1U];
  const auto center = canvas[2U * 5U + 2U];
  return near(ring.green, 1.0) && near(ring.alpha, 1.0) &&
      near(center.red, 1.0) && near(center.green, 1.0);
}

bool styles_derive_from_filtered_content() {
  std::vector<ColorPixel> canvas(49, ColorPixel{0, 0, 0, 0});
  PhotoLayer layer;
  layer.kind = "source";
  layer.x = 3;
  layer.y = 3;
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 1}};
  PhotoEffectInstance filter;
  filter.id = "source-blur";
  filter.type_id = "box_blur";
  filter.box.radius = 1;
  filter.box.edge_mode = PhotoEffectEdgeMode::Transparent;
  layer.effects = {filter};
  layer.style_stack.operations = {stroke("after-blur", {1, 0, 0, 1})};
  std::string error;
  if (!render(&canvas, 7, 7, {layer}, &error)) {
    std::cerr << "post-filter layer style render failed: " << error << '\n';
    return false;
  }
  const auto outer = canvas[3U * 7U + 1U];
  return outer.alpha > 0.0F && outer.red > 0.9F;
}

bool active_styles_admit_cpu_and_reject_retained_programs() {
  ColorAdjustmentState state;
  PhotoLayer layer;
  layer.kind = "pixel";
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 1}};
  layer.style_stack.operations = {stroke("cpu-style", {1, 0, 0, 1})};
  state.photo_layers = {layer};
  return metal_color_pipeline_incompatibility_reason(state) == "layer_styles_require_cpu" &&
      !retained_simple_layers_supported(state) &&
      !compile_fill_layer_program(state, 1, 1).ready();
}

bool unsupported_styles_fail_without_partial_composite() {
  std::vector<ColorPixel> canvas{{0.2F, 0.3F, 0.4F, 1.0F}};
  const auto original = canvas;
  PhotoLayer layer;
  layer.kind = "pixel";
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {{1, 1, 1, 1}};
  LayerStyleOperation unsupported;
  unsupported.id = "unsupported";
  unsupported.type_id = "future_style";
  unsupported.algorithm_version = 8;
  unsupported.parameters = std::monostate{};
  layer.style_stack.operations = {unsupported};
  std::string error;
  return !render(&canvas, 1, 1, {layer}, &error) && canvas == original && !error.empty();
}

}  // namespace

int main() {
  if (!pixel_style_expansion_obeys_layer_opacity_and_mask()) return 1;
  if (!style_document_scale_and_global_light_are_explicit()) return 1;
  if (!style_masks_resolve_and_missing_masks_fail_closed()) return 1;
  if (!group_styles_apply_to_private_child_composite()) return 1;
  if (!styles_derive_from_filtered_content()) return 1;
  if (!active_styles_admit_cpu_and_reject_retained_programs()) return 1;
  if (!unsupported_styles_fail_without_partial_composite()) return 1;
  return 0;
}
