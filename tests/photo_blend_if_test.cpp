#include "color_layer_runtime.h"
#include "color_working_space_runtime.h"
#include "photo_editor/pipeline/color_fill_layer_program.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace photo_editor::pipeline;

namespace {

[[nodiscard]] bool near(float actual, double expected, double tolerance = 1.0e-5) {
  return std::abs(static_cast<double>(actual) - expected) <= tolerance;
}

[[nodiscard]] std::vector<ColorPixel> render(
    ColorPixel base,
    const std::vector<PhotoLayer>& layers,
    const PipelineColorContext& context) {
  auto pixels = std::vector<ColorPixel>{base};
  std::string error;
  ColorAdjustmentState state;
  if (!apply_standard_photo_layers(&pixels, 1, 1, context,
          state.color_pipeline.photographic_rendering_model, layers, {}, &error)) {
    std::cerr << "standard layer render failed: " << error << '\n';
    return {};
  }
  return pixels;
}

[[nodiscard]] std::vector<ColorPixel> render(
    ColorPixel base,
    const std::vector<PhotoLayer>& layers) {
  ColorAdjustmentState state;
  return render(base, layers, make_pipeline_color_context(state));
}

[[nodiscard]] bool is_pixel(
    const ColorPixel& actual,
    double red,
    double green,
    double blue,
    double alpha = 1.0,
    double tolerance = 1.0e-5) {
  return near(actual.red, red, tolerance) && near(actual.green, green, tolerance) &&
      near(actual.blue, blue, tolerance) && near(actual.alpha, alpha, tolerance);
}

[[nodiscard]] bool verify_channel(
    BlendIfChannel channel,
    ColorPixel source,
    int component) {
  PhotoLayer layer;
  layer.kind = "pixel";
  layer.pixel_width = 1;
  layer.pixel_height = 1;
  layer.pixels = {source};
  layer.blend_if.enabled = true;
  layer.blend_if.channel = channel;
  layer.blend_if.source_black_low = 0.2;
  layer.blend_if.source_black_high = 0.8;
  const auto result = render({0.0F, 0.0F, 0.0F, 1.0F}, {layer});
  if (result.size() != 1U) return false;
  const auto value = component == 0 ? result[0].red
      : component == 1 ? result[0].green : result[0].blue;
  return near(value, 0.25);
}

}  // namespace

int main() {
  // Disabled means a bit-for-bit identity, even if dormant controls are
  // malformed. The normal layer pipeline still owns alpha and masks.
  {
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.opacity = 0.73;
    layer.pixels = {{0.8F, 0.3F, 0.2F, 0.7F}};
    const auto without_feature = render({0.1F, 0.4F, 0.2F, 0.35F}, {layer});
    layer.blend_if.source_black_low = std::numeric_limits<double>::quiet_NaN();
    layer.blend_if.underlying_white_high = std::numeric_limits<double>::infinity();
    layer.blend_if.enabled = false;
    const auto explicitly_bypassed = render({0.1F, 0.4F, 0.2F, 0.35F}, {layer});
    if (without_feature.size() != 1U || explicitly_bypassed.size() != 1U ||
        !(without_feature.front() == explicitly_bypassed.front())) {
      std::cerr << "disabled Blend If must preserve the existing compositor result exactly\n";
      return 1;
    }
  }

  // Gray and all three RGB channel selectors use independently feathered
  // source ranges. Midpoint samples pass half of the source coverage.
  if (!verify_channel(BlendIfChannel::Gray,
          {0.5F, 0.5F, 0.5F, 1.0F}, 0) ||
      !verify_channel(BlendIfChannel::Red,
          {0.5F, 0.0F, 0.0F, 1.0F}, 0) ||
      !verify_channel(BlendIfChannel::Green,
          {0.0F, 0.5F, 0.0F, 1.0F}, 1) ||
      !verify_channel(BlendIfChannel::Blue,
          {0.0F, 0.0F, 0.5F, 1.0F}, 2)) {
    std::cerr << "gray/red/green/blue Blend If channels must use their feathered source samples\n";
    return 1;
  }
  {
    PhotoLayer source;
    source.kind = "source";
    source.pixel_width = 1;
    source.pixel_height = 1;
    source.pixels = {{0.5F, 0.8F, 0.7F, 1.0F}};
    source.blend_if.enabled = true;
    source.blend_if.channel = BlendIfChannel::Red;
    source.blend_if.source_black_low = 0.2;
    source.blend_if.source_black_high = 0.8;
    const auto result = render({0.0F, 0.2F, 0.1F, 1.0F}, {source});
    if (result.size() != 1U || !is_pixel(result.front(), 0.25, 0.5, 0.4)) {
      std::cerr << "source layers must use the same Blend If compositor as pixel layers\n";
      return 1;
    }
  }

  // Black and white splits feather separately. The source red value is 0.5,
  // halfway through both the black rise and white fall, so each gate is 0.5.
  {
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.pixels = {{0.5F, 0.8F, 0.6F, 1.0F}};
    layer.blend_if.enabled = true;
    layer.blend_if.channel = BlendIfChannel::Red;
    layer.blend_if.source_black_low = 0.2;
    layer.blend_if.source_black_high = 0.8;
    layer.blend_if.source_white_low = 0.2;
    layer.blend_if.source_white_high = 0.8;
    const auto mid = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer});
    if (mid.size() != 1U || !is_pixel(mid.front(), 0.125, 0.35, 0.225)) {
      std::cerr << "black and white split ranges must multiply their feathered ramps\n";
      return 1;
    }
    layer.pixels.front().red = 0.2F;
    const auto black_edge = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer});
    layer.pixels.front().red = 0.8F;
    const auto white_edge = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer});
    if (black_edge.size() != 1U || white_edge.size() != 1U ||
        !is_pixel(black_edge.front(), 0.0, 0.2, 0.1) ||
        !is_pixel(white_edge.front(), 0.0, 0.2, 0.1)) {
      std::cerr << "split ranges must reach zero coverage at both feather endpoints\n";
      return 1;
    }
  }

  // The source and underlying gates are evaluated independently and multiply.
  {
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.pixels = {{0.5F, 0.8F, 0.7F, 1.0F}};
    layer.blend_if.enabled = true;
    layer.blend_if.channel = BlendIfChannel::Red;
    layer.blend_if.source_black_low = 0.2;
    layer.blend_if.source_black_high = 0.8;
    layer.blend_if.underlying_black_low = 0.2;
    layer.blend_if.underlying_black_high = 0.6;
    const auto result = render({0.4F, 0.2F, 0.1F, 1.0F}, {layer});
    if (result.size() != 1U || !is_pixel(result.front(), 0.425, 0.35, 0.25)) {
      std::cerr << "source and underlying ranges must independently feather and multiply\n";
      return 1;
    }

    layer.blend_if.source_black_low = 0.0;
    layer.blend_if.source_black_high = 0.0;
    layer.blend_if.underlying_black_low = 0.0;
    layer.blend_if.underlying_black_high = 0.0;
    layer.blend_if.underlying_black_low = 0.2;
    layer.blend_if.underlying_black_high = 0.6;
    const auto changed_underlying = render({0.6F, 0.2F, 0.1F, 1.0F}, {layer});
    if (changed_underlying.size() != 1U ||
        !is_pixel(changed_underlying.front(), 0.5, 0.8, 0.7)) {
      std::cerr << "underlying Blend If must respond to the current backdrop independently\n";
      return 1;
    }

    layer.blend_if.underlying_black_low = 0.0;
    layer.blend_if.underlying_black_high = 0.0;
    layer.blend_if.underlying_white_low = 0.2;
    layer.blend_if.underlying_white_high = 0.6;
    const auto underlying_white_feather = render({0.4F, 0.2F, 0.1F, 1.0F}, {layer});
    if (underlying_white_feather.size() != 1U ||
        !is_pixel(underlying_white_feather.front(), 0.45, 0.5, 0.4)) {
      std::cerr << "underlying white split ranges must feather independently\n";
      return 1;
    }
  }

  // The same gate runs through fill, adjustment, and isolated group layers.
  {
    PhotoLayer fill;
    fill.kind = "fill";
    fill.fill_color = {0.5F, 0.8F, 0.7F, 1.0F};
    fill.blend_if.enabled = true;
    fill.blend_if.channel = BlendIfChannel::Red;
    fill.blend_if.source_black_low = 0.2;
    fill.blend_if.source_black_high = 0.8;
    auto filled = render({0.0F, 0.2F, 0.1F, 1.0F}, {fill});
    if (filled.size() != 1U || !is_pixel(filled.front(), 0.25, 0.5, 0.4)) {
      std::cerr << "fill layers must apply Blend If in the ordinary compositor\n";
      return 1;
    }

    ColorAdjustmentState state;
    const auto context = make_pipeline_color_context(state);
    PhotoLayer adjustment;
    adjustment.kind = "adjustment";
    adjustment.blend_if.enabled = true;
    adjustment.blend_if.channel = BlendIfChannel::Red;
    adjustment.blend_if.source_black_low = 0.6;
    adjustment.blend_if.source_black_high = 1.0;
    adjustment.blend_if.underlying_black_low = 0.2;
    adjustment.blend_if.underlying_black_high = 0.6;
    std::vector<ColorPixel> adjusted{{0.4F, 0.2F, 0.1F, 1.0F}};
    std::string error;
    if (!apply_photo_layers(&adjusted, 1, 1, context, {adjustment},
            [](ColorPixel* pixel, const PhotoLayer&) { pixel->red = 0.8F; },
            false, false, {}, &error) || adjusted.size() != 1U ||
        !is_pixel(adjusted.front(), 0.5, 0.2, 0.1)) {
      std::cerr << "adjustment layers must gate their adjusted source and original backdrop: "
                << error << '\n';
      return 1;
    }

    PhotoLayer group;
    group.kind = "group";
    group.id = "blend-if-group";
    group.blend_if.enabled = true;
    group.blend_if.channel = BlendIfChannel::Red;
    group.blend_if.source_black_low = 0.2;
    group.blend_if.source_black_high = 0.8;
    group.blend_if.underlying_black_low = 0.2;
    group.blend_if.underlying_black_high = 0.6;
    PhotoLayer child;
    child.kind = "pixel";
    child.parent_id = group.id;
    child.pixel_width = 1;
    child.pixel_height = 1;
    child.pixels = {{0.5F, 0.8F, 0.7F, 1.0F}};
    const auto grouped = render({0.4F, 0.2F, 0.1F, 1.0F}, {group, child});
    if (grouped.size() != 1U || !is_pixel(grouped.front(), 0.425, 0.35, 0.25)) {
      std::cerr << "isolated group output must receive its own source and backdrop gates\n";
      return 1;
    }

    group.opacity = 0.4;
    group.mask_width = 1;
    group.mask_height = 1;
    group.mask_data = {128U};
    group.mask_opacity = 0.5;
    group.blend_if.underlying_black_low = 0.0;
    group.blend_if.underlying_black_high = 0.0;
    child.pixels.front().alpha = 0.5F;
    const auto masked_group = render({0.0F, 0.0F, 0.0F, 0.0F}, {group, child});
    const auto expected_group_alpha = 0.5 * 0.4 * (128.0 / 255.0) * 0.5 * 0.5;
    if (masked_group.size() != 1U ||
        !is_pixel(masked_group.front(), 0.5, 0.8, 0.7, expected_group_alpha, 2.0e-5)) {
      std::cerr << "group alpha, opacity, mask, and Blend If coverage must multiply cleanly\n";
      return 1;
    }
  }

  // Gate coverage composes with content alpha, layer opacity, and mask alpha.
  // RGB stays straight, so a feathered edge does not acquire a dark fringe.
  {
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.opacity = 0.4;
    layer.mask_width = 1;
    layer.mask_height = 1;
    layer.mask_data = {128U};
    layer.mask_opacity = 0.5;
    layer.pixels = {{0.8F, 0.25F, 0.1F, 0.5F}};
    layer.blend_if.enabled = true;
    layer.blend_if.channel = BlendIfChannel::Red;
    layer.blend_if.source_black_low = 0.6;
    layer.blend_if.source_black_high = 1.0;
    const auto result = render({0.0F, 0.0F, 0.0F, 0.0F}, {layer});
    const auto gate_at_source_red = (0.8 - 0.6) / (1.0 - 0.6);
    const auto expected_alpha = 0.5 * 0.4 * (128.0 / 255.0) * 0.5 * gate_at_source_red;
    if (result.size() != 1U || !is_pixel(result.front(), 0.8, 0.25, 0.1,
            expected_alpha, 2.0e-5)) {
      std::cerr << "Blend If soft-alpha mismatch: size=" << result.size()
                << " expectedRGBA=(0.8,0.25,0.1," << expected_alpha << ')';
      if (result.size() == 1U) {
        const auto& actual = result.front();
        std::cerr << " actualRGBA=(" << actual.red << ',' << actual.green << ','
                  << actual.blue << ',' << actual.alpha << ')';
      }
      std::cerr << '\n';
      return 1;
    }
  }

  // The retained fill program has no Blend If kernel yet. Enabled gates must
  // make it decline admission so callers execute the ordinary CPU compositor.
  {
    ColorAdjustmentState state;
    PhotoLayer layer;
    layer.kind = "fill";
    layer.fill_color = {0.8F, 0.25F, 0.1F, 1.0F};
    state.photo_layers.push_back(layer);
    const auto ungated = compile_fill_layer_program(state, 1, 1);
    if (!retained_simple_layers_supported(state) || !ungated.ready()) {
      std::cerr << "identity fill layer should remain eligible for retained-program execution\n";
      return 1;
    }
    state.photo_layers.front().blend_if.enabled = true;
    const auto gated = compile_fill_layer_program(state, 1, 1);
    if (retained_simple_layers_supported(state) || gated.ready()) {
      std::cerr << "retained fill program must reject active Blend If gates\n";
      return 1;
    }
  }

  // Blend If sees normalized linear working values. This profile uses a
  // nominal range of 0..4: negative values become black and values above 4
  // become white before the split ranges are evaluated.
  {
    ColorAdjustmentState state;
    auto context = make_pipeline_color_context(state);
    context.layer_blend_headroom = 4.0;
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.pixels = {{2.0F, 0.8F, 0.6F, 1.0F}};
    layer.blend_if.enabled = true;
    layer.blend_if.channel = BlendIfChannel::Red;
    layer.blend_if.source_black_low = 0.25;
    layer.blend_if.source_black_high = 0.75;
    const auto hdr = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer}, context);
    if (hdr.size() != 1U || !is_pixel(hdr.front(), 1.0, 0.5, 0.35)) {
      std::cerr << "HDR working values must normalize by layer blend headroom\n";
      return 1;
    }
    layer.pixels.front().red = -1.0F;
    const auto negative = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer}, context);
    if (negative.size() != 1U || !is_pixel(negative.front(), 0.0, 0.2, 0.1)) {
      std::cerr << "negative working channels must map to normalized black\n";
      return 1;
    }
    layer.pixels.front().red = 8.0F;
    const auto above_headroom = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer}, context);
    if (above_headroom.size() != 1U || !is_pixel(above_headroom.front(), 8.0, 0.8, 0.6)) {
      std::cerr << "values above the reference headroom must map to normalized white\n";
      return 1;
    }
    context.layer_blend_headroom = std::numeric_limits<double>::quiet_NaN();
    layer.pixels.front().red = 0.5F;
    const auto invalid_headroom = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer}, context);
    if (invalid_headroom.size() != 1U ||
        !is_pixel(invalid_headroom.front(), 0.25, 0.5, 0.35)) {
      std::cerr << "nonfinite blend headroom must fall back to the nominal SDR range\n";
      return 1;
    }
  }

  // Invalid persisted controls fall back to the documented identity
  // thresholds instead of introducing NaNs into coverage.
  {
    PhotoLayer layer;
    layer.kind = "pixel";
    layer.pixel_width = 1;
    layer.pixel_height = 1;
    layer.pixels = {{0.4F, 0.8F, 0.6F, 1.0F}};
    layer.blend_if.enabled = true;
    layer.blend_if.channel = BlendIfChannel::Red;
    layer.blend_if.source_black_low = std::numeric_limits<double>::quiet_NaN();
    layer.blend_if.source_black_high = 0.8;
    layer.blend_if.source_white_low = std::numeric_limits<double>::quiet_NaN();
    layer.blend_if.source_white_high = std::numeric_limits<double>::infinity();
    layer.blend_if.underlying_black_low = std::numeric_limits<double>::quiet_NaN();
    layer.blend_if.underlying_black_high = std::numeric_limits<double>::quiet_NaN();
    const auto result = render({0.0F, 0.2F, 0.1F, 1.0F}, {layer});
    if (result.size() != 1U || !std::isfinite(result.front().red) ||
        !is_pixel(result.front(), 0.2, 0.5, 0.35)) {
      std::cerr << "nonfinite thresholds must use safe defaults and finite coverage\n";
      return 1;
    }
  }

  return 0;
}
