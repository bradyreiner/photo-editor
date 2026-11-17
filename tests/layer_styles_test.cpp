#include "photo_editor/pipeline/layer_styles.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

using namespace photo_editor::pipeline;

namespace {

bool expect(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool near(float left, float right, float tolerance = 1e-5F) {
  return std::abs(left - right) <= tolerance;
}

LayerStyleOperation operation(std::string id, std::string type, LayerStyleParameters parameters,
    LayerStylePlane plane = LayerStylePlane::BelowContent) {
  LayerStyleOperation value;
  value.id = std::move(id);
  value.type_id = std::move(type);
  value.parameters = std::move(parameters);
  value.plane = plane;
  return value;
}

LayerStyleColor color(double red, double green, double blue, double alpha = 1.0) {
  return {red, green, blue, alpha};
}

bool stack_codec_contract() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 37.5;
  gradient.scale = 1.25;
  gradient.reverse = true;
  gradient.stops = {{0.0, color(1, 0, 0)}, {0.4, color(0, 0.5, 0)}, {1.0, color(0, 0, 1)}};

  LayerStyleStack stack;
  stack.operations.push_back(operation("stroke-1", "stroke",
      LayerStyleStroke{2.75, LayerStyleStrokeAlignment::Center, gradient}));
  stack.operations.push_back(operation("drop-1", "drop_shadow",
      LayerStyleDropShadow{color(0, 0, 0, 0.7), 18, -32, 7, 1.5, false}));
  stack.operations.push_back(operation("inner-shadow-1", "inner_shadow",
      LayerStyleInnerShadow{color(0.1, 0.1, 0.1, 0.8), 3, 90, 4, 0.5, true},
      LayerStylePlane::OnContent));
  stack.operations.push_back(operation("inner-glow-1", "inner_glow",
      LayerStyleInnerGlow{color(1, 0.8, 0.2, 0.6), 5, 1}, LayerStylePlane::OnContent));
  stack.operations.push_back(operation("outer-glow-1", "outer_glow",
      LayerStyleOuterGlow{color(0.2, 0.4, 1, 0.7), 8, 2}));
  stack.operations.push_back(operation("color-overlay-1", "color_overlay",
      LayerStyleColorOverlay{color(0.25, 0.5, 0.75)}, LayerStylePlane::OnContent));
  stack.operations.push_back(operation("gradient-overlay-1", "gradient_overlay",
      LayerStyleGradientOverlay{gradient}, LayerStylePlane::OnContent));
  stack.operations.push_back(operation("bevel-1", "bevel_emboss",
      LayerStyleBevelEmboss{3, 1, 1.4, -45, true, LayerStyleBevelMode::Emboss,
          color(1, 1, 1, 0.8), color(0, 0, 0, 0.7)}, LayerStylePlane::OnContent));
  stack.operations[2].blend_mode = LayerStyleBlendMode::Multiply;
  stack.operations[0].mask_ref = "stroke-mask";
  stack.operations[1].opacity = 0.73;

  std::string error;
  const auto encoded = serialize_layer_style_stack(stack, &error);
  if (!expect(encoded.has_value(), "A valid full style stack should encode.")) return false;
  const auto decoded = deserialize_layer_style_stack(*encoded, &error);
  if (!expect(decoded.has_value(), "The versioned style stack should decode.")) return false;
  if (!expect(*decoded == stack, "Style stack codec should preserve typed values, order, masks and policies.")) return false;

  LayerStyleOperation future;
  future.id = "future-style";
  future.type_id = "future_style_operator";
  future.algorithm_version = 7;
  future.enabled = false;
  future.opaque_parameters = "future fields with spaces\nand a newline";
  stack.operations.push_back(future);
  const auto future_encoded = serialize_layer_style_stack(stack, &error);
  if (!expect(future_encoded.has_value(), "Unknown disabled styles should remain serializable.")) return false;
  const auto future_decoded = deserialize_layer_style_stack(*future_encoded, &error);
  if (!expect(future_decoded.has_value() && future_decoded->operations.back() == future,
          "Unknown style payloads should round-trip exactly.")) return false;
  future.enabled = true;
  auto unsupported = operation("future-style", "future_style_operator", std::monostate{});
  unsupported.algorithm_version = 7;
  unsupported.opaque_parameters = future.opaque_parameters;
  const LayerStyleStack unsupported_stack{1, true, {unsupported}};
  const auto rendered = render_layer_styles(std::vector<ColorPixel>{{0.2F, 0.3F, 0.4F, 1.0F}},
      1, 1, unsupported_stack);
  return expect(rendered.status == LayerStyleRenderStatus::Unsupported,
      "Enabled unknown style operators must fail closed at render time.");
}

bool invalid_parameter_contract() {
  auto stroke = operation("bad", "stroke", LayerStyleStroke{});
  std::get<LayerStyleStroke>(stroke.parameters).width = std::numeric_limits<double>::quiet_NaN();
  std::string error;
  return expect(!serialize_layer_style_stack(LayerStyleStack{1, true, {stroke}}, &error).has_value(),
      "Non-finite style geometry must be rejected by the codec.");
}

bool outside_stroke_soft_alpha_contract() {
  LayerStyleStroke stroke;
  stroke.width = 1.0;
  stroke.alignment = LayerStyleStrokeAlignment::Outside;
  stroke.fill = color(1, 0, 0);
  auto style = operation("outer-stroke", "stroke", stroke);
  const LayerStyleStack stack{1, true, {style}};
  const std::vector<ColorPixel> source{{1.0F, 1.0F, 1.0F, 0.5F}};
  const auto result = render_layer_styles(source, 1, 1, stack);
  if (!expect(result.ok(), "A soft-alpha outer stroke should render.")) return false;
  if (!expect(result.width == 3 && result.height == 3 && result.canvas_offset_x == -1 &&
          result.canvas_offset_y == -1, "An outside stroke should return sufficient padded bounds.")) return false;
  const auto ring = result.pixels[1U * 3U + 0U];
  const auto center = result.pixels[1U * 3U + 1U];
  if (!near(ring.red, 1.0F) || !near(ring.alpha, 0.5F)) {
    std::cerr << "Outside stroke ring pixel red=" << ring.red << " alpha=" << ring.alpha
              << " center red=" << center.red << " alpha=" << center.alpha << '\n';
    return expect(false, "The outside stroke should retain the source's partial alpha coverage.");
  }
  return expect(near(center.red, 1.0F) && near(center.alpha, 0.5F),
      "The unstyled source pixel should retain its original alpha and color.");
}

bool stroke_width_subpixel_coverage_contract() {
  const std::vector<ColorPixel> source(25U, {0.5F, 0.5F, 0.5F, 1.0F});
  const auto render_stroke = [&](double width, LayerStyleStrokeAlignment alignment) {
    LayerStyleStroke stroke;
    stroke.width = width;
    stroke.alignment = alignment;
    stroke.fill = color(1, 0, 0);
    auto style = operation("width", "stroke", stroke,
        alignment == LayerStyleStrokeAlignment::Inside ? LayerStylePlane::OnContent :
        alignment == LayerStyleStrokeAlignment::Outside ? LayerStylePlane::BelowContent :
            LayerStylePlane::AboveContent);
    return render_layer_styles(source, 5, 5, LayerStyleStack{1, true, {style}});
  };
  for (const auto& [small, large, alignment] : {
           std::tuple{0.25, 1.0, LayerStyleStrokeAlignment::Inside},
           std::tuple{0.25, 1.0, LayerStyleStrokeAlignment::Outside},
           std::tuple{0.5, 1.0, LayerStyleStrokeAlignment::Center}}) {
    const auto thin = render_stroke(small, alignment);
    const auto thick = render_stroke(large, alignment);
    if (!expect(thin.ok() && thick.ok(), "Stroke width variants should render.")) return false;
    if (!expect(thin.width == thick.width && thin.height == thick.height,
            "Subpixel stroke width variants should have comparable bounds.")) return false;
    if (!expect(thin.pixels != thick.pixels,
            "Stroke widths below one pixel must not quantize to the same full-pixel stroke.")) return false;

    if (alignment == LayerStyleStrokeAlignment::Inside) {
      const auto thin_edge = thin.pixels[2U];
      const auto thick_edge = thick.pixels[2U];
      if (!expect(near(thin_edge.red, 0.625F) && near(thick_edge.red, 1.0F),
              "Fractional inside strokes should interpolate edge coverage by authored width.")) return false;
    } else if (alignment == LayerStyleStrokeAlignment::Outside) {
      const auto thin_ring = thin.pixels[3U * static_cast<std::size_t>(thin.width)];
      const auto thick_ring = thick.pixels[3U * static_cast<std::size_t>(thick.width)];
      if (!expect(near(thin_ring.alpha, 0.25F) && near(thick_ring.alpha, 1.0F),
              "Fractional outside strokes should retain proportional soft coverage.")) return false;
    } else {
      const auto thin_edge = thin.pixels[1U * static_cast<std::size_t>(thin.width) + 3U];
      const auto thick_edge = thick.pixels[1U * static_cast<std::size_t>(thick.width) + 3U];
      if (!expect(near(thin_edge.red, 0.625F) && near(thick_edge.red, 0.75F),
              "Fractional centered strokes should scale coverage on both sides of the edge.")) return false;
    }
  }
  return true;
}

bool directional_shadow_contract() {
  constexpr int width = 5;
  constexpr int height = 5;
  std::vector<ColorPixel> source(static_cast<std::size_t>(width * height), {0, 0, 0, 0});
  source[2U * width + 2U] = {1, 1, 1, 1};
  LayerStyleDropShadow shadow;
  shadow.color = color(0, 0, 0, 0.6);
  shadow.distance = 1.0;
  shadow.angle_degrees = 0.0;
  shadow.blur = 0.0;
  shadow.use_global_light = false;
  const LayerStyleStack stack{1, true,
      {operation("shadow", "drop_shadow", shadow, LayerStylePlane::BelowContent)}};
  const auto result = render_layer_styles(source, width, height, stack);
  if (!expect(result.ok(), "A directional shadow should render.")) return false;
  if (!expect(result.canvas_offset_x == 0 && result.canvas_offset_y == 0 && result.width == 6,
          "A rightward shadow should expand only the right bound.")) return false;
  const auto shadow_pixel = result.pixels[2U * static_cast<std::size_t>(result.width) + 3U];
  const auto source_pixel = result.pixels[2U * static_cast<std::size_t>(result.width) + 2U];
  if (!expect(near(shadow_pixel.alpha, 0.6F), "A zero-blur shadow should preserve its opacity.")) return false;
  return expect(near(source_pixel.red, 1.0F) && near(source_pixel.alpha, 1.0F),
      "The original layer content should remain above its drop shadow.");
}

bool gradient_and_mask_alpha_contract() {
  LayerStyleGradient gradient;
  gradient.angle_degrees = 0.0;
  gradient.stops = {{0.0, color(1, 0, 0)}, {1.0, color(0, 0, 1)}};
  auto overlay = operation("gradient", "gradient_overlay",
      LayerStyleGradientOverlay{gradient}, LayerStylePlane::OnContent);
  overlay.mask_ref = "coverage";
  const LayerStyleStack stack{1, true, {overlay}};
  const std::vector<ColorPixel> source{{0.1F, 0.2F, 0.3F, 0.25F}, {0.1F, 0.2F, 0.3F, 0.75F}};
  const std::vector<float> coverage{0.0F, 1.0F};
  const std::vector<LayerStyleMaskBinding> masks{{"coverage", coverage}};
  const auto result = render_layer_styles(source, 2, 1, stack, {}, masks);
  if (!expect(result.ok(), "A masked gradient overlay should render.")) return false;
  const auto left = result.pixels[0];
  const auto right = result.pixels[1];
  if (!expect(near(left.red, source[0].red) && near(left.blue, source[0].blue),
          "Zero style-mask coverage should leave the overlay target unchanged.")) return false;
  if (!expect(near(right.red, 0.0F) && near(right.blue, 1.0F),
          "The gradient should sample exact endpoint colors at its raster endpoints.")) return false;
  if (!expect(near(left.alpha, 0.25F) && near(right.alpha, 0.75F),
          "Color and gradient overlays must preserve partial source alpha.")) return false;

  auto color_overlay_style = operation("solid", "color_overlay",
      LayerStyleColorOverlay{color(0, 1, 0)}, LayerStylePlane::OnContent);
  const LayerStyleStack solid_stack{1, true, {color_overlay_style}};
  const std::vector<ColorPixel> transparent{{0, 0, 0, 0}};
  const auto transparent_result = render_layer_styles(transparent, 1, 1, solid_stack);
  return expect(transparent_result.ok() && near(transparent_result.pixels[0].alpha, 0.0F) &&
          std::isfinite(transparent_result.pixels[0].red),
      "Overlay math should be stable for fully transparent content.");
}

bool style_order_and_bevel_contract() {
  const std::vector<ColorPixel> source{{0.25F, 0.25F, 0.25F, 1.0F}};
  const LayerStyleStack ordered{1, true, {
      operation("red", "color_overlay", LayerStyleColorOverlay{color(1, 0, 0)}, LayerStylePlane::OnContent),
      operation("blue", "color_overlay", LayerStyleColorOverlay{color(0, 0, 1)}, LayerStylePlane::OnContent)}};
  const auto overlay_result = render_layer_styles(source, 1, 1, ordered);
  if (!expect(overlay_result.ok() && near(overlay_result.pixels[0].blue, 1.0F) &&
          near(overlay_result.pixels[0].red, 0.0F),
          "Layer-style operations should apply in declared order.")) return false;

  constexpr int width = 5;
  constexpr int height = 5;
  std::vector<ColorPixel> shape(static_cast<std::size_t>(width * height), {0, 0, 0, 0});
  for (int y = 1; y < 4; ++y)
    for (int x = 1; x < 4; ++x) shape[static_cast<std::size_t>(y * width + x)] = {0.5F, 0.5F, 0.5F, 1.0F};
  LayerStyleBevelEmboss bevel;
  bevel.size = 1.0;
  bevel.softness = 0.0;
  bevel.depth = 1.0;
  bevel.angle_degrees = 180.0;
  bevel.use_global_light = false;
  bevel.highlight = color(1, 1, 1, 0.8);
  bevel.shadow = color(0, 0, 0, 0.8);
  const LayerStyleStack bevel_stack{1, true,
      {operation("bevel", "bevel_emboss", bevel, LayerStylePlane::OnContent)}};
  const auto bevel_result = render_layer_styles(shape, width, height, bevel_stack);
  if (!expect(bevel_result.ok(), "Alpha-derived bevel/emboss should render.")) return false;
  const auto edge = bevel_result.pixels[2U * static_cast<std::size_t>(width) + 1U];
  if (!expect(std::isfinite(edge.red) && std::isfinite(edge.green) && std::isfinite(edge.blue) &&
          near(edge.alpha, 1.0F) && edge.red > 0.5F,
      "Bevel shading should remain finite and preserve source alpha on soft edges.")) return false;

  LayerStyleRenderOptions wide_x;
  wide_x.document_scale_x = 2.0;
  wide_x.document_scale_y = 1.0;
  LayerStyleRenderOptions wide_y;
  wide_y.document_scale_x = 1.0;
  wide_y.document_scale_y = 2.0;
  const auto x_scaled = render_layer_styles(shape, width, height, bevel_stack, wide_x);
  const auto y_scaled = render_layer_styles(shape, width, height, bevel_stack, wide_y);
  return expect(x_scaled.ok() && y_scaled.ok() && x_scaled.pixels != y_scaled.pixels,
      "Bevel smoothing and normals should respond independently to horizontal and vertical scale.");
}

bool cancellation_fast_path_contract() {
  std::atomic_bool cancellation_requested{true};
  LayerStyleRenderOptions options;
  options.cancellation.cancellation_requested = &cancellation_requested;
  const auto result = render_layer_styles(std::vector<ColorPixel>{{0.5F, 0.5F, 0.5F, 1.0F}},
      1, 1, LayerStyleStack{}, options);
  if (!expect(result.status == LayerStyleRenderStatus::Cancelled,
          "A cancelled request must not return the unstyled identity surface.")) return false;
  const auto preflight = preflight_layer_style_stack(1, 1, LayerStyleStack{}, options);
  return expect(preflight.status == LayerStyleRenderStatus::Cancelled,
      "A cancelled authoring preflight must not accept the layer-style stack.");
}

bool preflight_resource_and_unknown_contract() {
  LayerStyleOuterGlow oversized_glow;
  oversized_glow.blur = 4096.0;
  const LayerStyleStack oversized{1, true,
      {operation("large-glow", "outer_glow", oversized_glow)}};
  const auto oversized_result = preflight_layer_style_stack(1, 1, oversized);
  if (!expect(oversized_result.status == LayerStyleRenderStatus::ResourceLimit,
          "Preflight must reject valid parameters whose render radius exceeds the padding limit.")) return false;
  if (!expect(render_layer_styles(std::vector<ColorPixel>{{1, 1, 1, 1}}, 1, 1, oversized).status ==
          oversized_result.status,
          "Rendering and authoring preflight must share the same padding admission result.")) return false;

  LayerStyleStroke stroke;
  stroke.width = 2.0;
  stroke.alignment = LayerStyleStrokeAlignment::Outside;
  const LayerStyleStack expanded{1, true,
      {operation("bounded-stroke", "stroke", stroke)}};
  LayerStyleRenderOptions small_pixel_budget;
  small_pixel_budget.maximum_output_pixels = 24U;
  const auto expanded_result = preflight_layer_style_stack(1, 1, expanded, small_pixel_budget);
  if (!expect(expanded_result.status == LayerStyleRenderStatus::ResourceLimit &&
          expanded_result.output_width == 5 && expanded_result.output_height == 5 &&
          expanded_result.padding_left == 2 && expanded_result.padding_right == 2,
          "Preflight must report expanded bounds while rejecting an output-pixel budget overflow.")) return false;

  const LayerStyleStack on_content{1, true,
      {operation("overlay", "color_overlay", LayerStyleColorOverlay{color(1, 0, 0)},
          LayerStylePlane::OnContent)}};
  LayerStyleRenderOptions small_scratch_budget;
  small_scratch_budget.maximum_scratch_bytes = sizeof(ColorPixel) + sizeof(float) * 4U - 1U;
  if (!expect(preflight_layer_style_stack(1, 1, on_content, small_scratch_budget).status ==
          LayerStyleRenderStatus::ResourceLimit,
          "Preflight must include on-content and alpha scratch in its memory budget.")) return false;

  LayerStyleOperation unknown = operation("future", "future_effect", std::monostate{});
  unknown.opaque_parameters = "future-payload";
  const LayerStyleStack enabled_unknown{1, true, {unknown}};
  if (!expect(preflight_layer_style_stack(1, 1, enabled_unknown).status ==
          LayerStyleRenderStatus::Unsupported,
          "Preflight must reject enabled operation types the renderer cannot execute.")) return false;

  unknown.enabled = false;
  const LayerStyleStack disabled_unknown{1, true, {unknown}};
  const auto disabled_result = preflight_layer_style_stack(1, 1, disabled_unknown);
  if (!expect(disabled_result.status == LayerStyleRenderStatus::Identity &&
          disabled_unknown.operations.front() == unknown,
          "Preflight must preserve disabled unknown operation payloads as inert stack data.")) return false;

  LayerStyleBevelEmboss bevel;
  const LayerStyleStack global_light_stack{1, true,
      {operation("global-bevel", "bevel_emboss", bevel)}};
  LayerStyleRenderOptions authored_light;
  authored_light.global_light_angle_degrees = 361.0;
  const auto light_result = preflight_layer_style_stack(1, 1, global_light_stack, authored_light);
  return expect(light_result.status == LayerStyleRenderStatus::Applied,
      "Global-light angles within the persisted ±3600-degree range must remain renderable.");
}

bool resource_limit_contract() {
  LayerStyleDropShadow shadow;
  shadow.distance = 900.0;
  shadow.blur = 0;
  const LayerStyleStack stack{1, true, {operation("wide-shadow", "drop_shadow", shadow)}};
  LayerStyleRenderOptions options;
  options.maximum_padding = 32;
  const auto result = render_layer_styles(std::vector<ColorPixel>{{1, 1, 1, 1}}, 1, 1, stack, options);
  return expect(result.status == LayerStyleRenderStatus::ResourceLimit,
      "Style operations that exceed configured padding must fail with a resource-limit result.");
}

}  // namespace

int main() {
  if (!stack_codec_contract()) return 1;
  if (!invalid_parameter_contract()) return 1;
  if (!outside_stroke_soft_alpha_contract()) return 1;
  if (!stroke_width_subpixel_coverage_contract()) return 1;
  if (!directional_shadow_contract()) return 1;
  if (!gradient_and_mask_alpha_contract()) return 1;
  if (!style_order_and_bevel_contract()) return 1;
  if (!cancellation_fast_path_contract()) return 1;
  if (!preflight_resource_and_unknown_contract()) return 1;
  if (!resource_limit_contract()) return 1;
  return 0;
}
