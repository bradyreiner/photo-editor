#include "color_layer_runtime.h"
#include "color_adjustment_kernel_runtime.h"
#include "color_layer_composite.h"
#include "color_layer_sampling.h"
#include "photo_editor/pipeline/photo_effects.h"

#include "color_adjustment_scheduler.h"
#include "color_blur_runtime.h"
#include "color_detail_runtime.h"
#include "color_curve_evaluator.h"
#include "color_look_runtime.h"
#include "color_lut_runtime.h"
#include "color_math_runtime.h"
#include "color_mixer_runtime.h"
#include "color_spatial_runtime.h"
#include "color_tone_runtime.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <functional>
#include <numbers>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

constexpr double kPi = std::numbers::pi_v<double>;

[[nodiscard]] std::array<double, 3> effect_luminance_weights_for_context(
    const PipelineColorContext& context) {
  // luminance_for_pixel() uses pixel_to_xyz_native(), so effects that need Y
  // must use this same working-space XYZ matrix row.
  return {context.rgb_to_xyz_native.values[3],
      context.rgb_to_xyz_native.values[4], context.rgb_to_xyz_native.values[5]};
}

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count, std::function<void(int)>(std::forward<Work>(work)));
}

template <typename Work>
void composite_rows(int count, int row_width, Work&& work) {
  // Tiny content should not pay for worker admission. Large layers reuse the
  // same bounded workers as other CPU stages, including nested serial fallback.
  if (static_cast<std::int64_t>(count) * row_width < 65536) {
    for (int row = 0; row < count; ++row) work(row);
  } else {
    parallel_for(count, std::forward<Work>(work));
  }
}

}  // namespace

[[nodiscard]] bool has_photo_layers(const std::vector<PhotoLayer>& layers) {
  for (const auto& layer : layers) {
    if (layer.kind != "group" && layer.enabled && layer.opacity > 1e-6) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] double photo_layer_mask_weight(
    const PhotoLayer& layer,
    int x,
    int y,
    int width,
    int height) {
  const auto expected_size =
      static_cast<std::size_t>(layer.mask_width) * static_cast<std::size_t>(layer.mask_height);
  if (layer.mask_width <= 0 || layer.mask_height <= 0 || layer.mask_data.size() != expected_size ||
      width <= 0 || height <= 0) {
    return 1.0;
  }

  const auto normalized_x =
      width > 1 ? static_cast<double>(std::clamp(x, 0, width - 1)) / static_cast<double>(width - 1)
                : 0.0;
  const auto normalized_y =
      height > 1 ? static_cast<double>(std::clamp(y, 0, height - 1)) / static_cast<double>(height - 1)
                 : 0.0;
  const auto mask_x = std::clamp(
      static_cast<int>(std::lround(normalized_x * static_cast<double>(layer.mask_width - 1))),
      0,
      layer.mask_width - 1);
  const auto mask_y = std::clamp(
      static_cast<int>(std::lround(normalized_y * static_cast<double>(layer.mask_height - 1))),
      0,
      layer.mask_height - 1);
  const auto index = static_cast<std::size_t>(mask_y * layer.mask_width + mask_x);
  auto weight = static_cast<double>(layer.mask_data[index]) / 255.0;
  if (layer.mask_inverted) {
    weight = 1.0 - weight;
  }
  weight = std::min(weight, std::clamp(layer.mask_density, 0.0, 1.0));
  return clamp_unit(weight * std::clamp(layer.mask_opacity, 0.0, 1.0));
}

[[nodiscard]] double overlay_channel(double base, double source, double headroom) {
  const auto safe_headroom =
      sanitized_scene_working_headroom(headroom);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  const auto blended = normalized_base <= 0.5
                           ? 2.0 * normalized_base * normalized_source
                           : 1.0 - 2.0 * (1.0 - normalized_base) * (1.0 - normalized_source);
  return blended * safe_headroom;
}

[[nodiscard]] double soft_light_channel(double base, double source, double headroom) {
  const auto safe_headroom =
      sanitized_scene_working_headroom(headroom);
  const auto normalized_base = std::clamp(base / safe_headroom, 0.0, 1.0);
  const auto normalized_source = std::clamp(source / safe_headroom, 0.0, 1.0);
  double blended = normalized_base;
  if (normalized_source <= 0.5) {
    blended = normalized_base -
              (1.0 - 2.0 * normalized_source) * normalized_base * (1.0 - normalized_base);
  } else {
    const auto d = normalized_base <= 0.25
                       ? ((16.0 * normalized_base - 12.0) * normalized_base + 4.0) *
                             normalized_base
                       : std::sqrt(normalized_base);
    blended = normalized_base + (2.0 * normalized_source - 1.0) * (d - normalized_base);
  }
  return blended * safe_headroom;
}

[[nodiscard]] ColorPixel blend_layer_color(
    const ColorPixel& base,
    const ColorPixel& source,
    const std::string& mode,
    const PipelineColorContext& context) {
  // Blend equations use the declared creative/output reference range. The
  // much wider scene working headroom is a storage contract, not the neutral
  // for Screen, Overlay, Soft Light, or Linear Light.
  const auto headroom =
      context.layer_blend_headroom;
  const auto base_red = std::max(0.0, static_cast<double>(base.red));
  const auto base_green = std::max(0.0, static_cast<double>(base.green));
  const auto base_blue = std::max(0.0, static_cast<double>(base.blue));
  const auto source_red = std::max(0.0, static_cast<double>(source.red));
  const auto source_green = std::max(0.0, static_cast<double>(source.green));
  const auto source_blue = std::max(0.0, static_cast<double>(source.blue));

  if (mode == "multiply") {
    return ColorPixel{
        static_cast<float>(base_red * source_red),
        static_cast<float>(base_green * source_green),
        static_cast<float>(base_blue * source_blue),
        source.alpha};
  }
  if (mode == "screen") {
    return ColorPixel{
        static_cast<float>(headroom - ((headroom - base_red) * (headroom - source_red) / headroom)),
        static_cast<float>(headroom - ((headroom - base_green) * (headroom - source_green) / headroom)),
        static_cast<float>(headroom - ((headroom - base_blue) * (headroom - source_blue) / headroom)),
        source.alpha};
  }
  if (mode == "overlay") {
    return ColorPixel{
        static_cast<float>(overlay_channel(base_red, source_red, headroom)),
        static_cast<float>(overlay_channel(base_green, source_green, headroom)),
        static_cast<float>(overlay_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "soft light" || mode == "soft-light") {
    return ColorPixel{
        static_cast<float>(soft_light_channel(base_red, source_red, headroom)),
        static_cast<float>(soft_light_channel(base_green, source_green, headroom)),
        static_cast<float>(soft_light_channel(base_blue, source_blue, headroom)),
        source.alpha};
  }
  if (mode == "hard light" || mode == "hard-light") {
    return ColorPixel{
        static_cast<float>(overlay_channel(source_red, base_red, headroom)),
        static_cast<float>(overlay_channel(source_green, base_green, headroom)),
        static_cast<float>(overlay_channel(source_blue, base_blue, headroom)),
        source.alpha};
  }
  if (mode == "difference") {
    return ColorPixel{
        static_cast<float>(std::abs(base_red - source_red)),
        static_cast<float>(std::abs(base_green - source_green)),
        static_cast<float>(std::abs(base_blue - source_blue)),
        source.alpha};
  }
  if (mode == "lighten") {
    return ColorPixel{
        static_cast<float>(std::max(base_red, source_red)),
        static_cast<float>(std::max(base_green, source_green)),
        static_cast<float>(std::max(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "darken") {
    return ColorPixel{
        static_cast<float>(std::min(base_red, source_red)),
        static_cast<float>(std::min(base_green, source_green)),
        static_cast<float>(std::min(base_blue, source_blue)),
        source.alpha};
  }
  if (mode == "linear light" || mode == "linear-light") {
    return ColorPixel{
        static_cast<float>(std::clamp(base_red + 2.0 * source_red - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_green + 2.0 * source_green - headroom, 0.0, headroom)),
        static_cast<float>(std::clamp(base_blue + 2.0 * source_blue - headroom, 0.0, headroom)),
        source.alpha};
  }
  if (mode == "hue" || mode == "saturation" || mode == "color" || mode == "luminosity") {
    auto blend_context = context;
    blend_context.output_headroom = headroom;
    const auto base_oklab = linear_rgb_to_oklab(base, context);
    const auto source_oklab = linear_rgb_to_oklab(source, context);
    auto lightness =
        clamp_oklab_lightness(
            base_oklab.lightness,
            blend_context);
    auto chroma = std::hypot(base_oklab.a, base_oklab.b);
    auto hue = wrap_radians(std::atan2(base_oklab.b, base_oklab.a));
    const auto source_chroma = std::hypot(source_oklab.a, source_oklab.b);
    const auto source_hue = source_chroma > 1.0e-6
                                ? wrap_radians(std::atan2(source_oklab.b, source_oklab.a))
                                : hue;
    if (mode == "hue") {
      hue = source_hue;
    } else if (mode == "saturation") {
      chroma = source_chroma;
    } else if (mode == "color") {
      hue = source_hue;
      chroma = source_chroma;
    } else {
      lightness =
          clamp_oklab_lightness(
              source_oklab.lightness,
              blend_context);
    }
    auto blended =
        gamut_fit_oklch(
            lightness,
            chroma,
            hue,
            source.alpha,
            blend_context);
    blended.alpha = source.alpha;
    return blended;
  }
  return source;
}

void composite_layer_pixel(
    ColorPixel* target,
    const ColorPixel& source,
    double alpha,
    const std::string& mode,
    const PipelineColorContext& context, bool erase_base_image = false) {
  if (target == nullptr || alpha <= 1e-6) {
    return;
  }
  if (erase_base_image) {
    target->alpha = static_cast<float>(static_cast<double>(target->alpha) * (1.0 - clamp_unit(alpha)));
    if (target->alpha <= 1e-6F) *target = ColorPixel{0.0F, 0.0F, 0.0F, 0.0F};
    return;
  }
  const auto blended = blend_layer_color(*target, source, mode, context);
  *target = composite_layer_source_over(*target, source, blended, alpha);
}

struct TextMaterialBackdrop final {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  std::vector<ColorPixel> pixels;

  [[nodiscard]] ColorPixel sample(double document_x, double document_y) const {
    if (pixels.empty() || width <= 0 || height <= 0) return {};
    const auto local_x = std::clamp(document_x - static_cast<double>(x), 0.0, static_cast<double>(width - 1));
    const auto local_y = std::clamp(document_y - static_cast<double>(y), 0.0, static_cast<double>(height - 1));
    const auto x0 = static_cast<int>(std::floor(local_x));
    const auto y0 = static_cast<int>(std::floor(local_y));
    const auto x1 = std::min(x0 + 1, width - 1);
    const auto y1 = std::min(y0 + 1, height - 1);
    const auto tx = local_x - x0;
    const auto ty = local_y - y0;
    const auto& p00 = pixels[static_cast<std::size_t>(y0 * width + x0)];
    const auto& p10 = pixels[static_cast<std::size_t>(y0 * width + x1)];
    const auto& p01 = pixels[static_cast<std::size_t>(y1 * width + x0)];
    const auto& p11 = pixels[static_cast<std::size_t>(y1 * width + x1)];
    const auto interpolate = [tx, ty](float a, float b, float c, float d) {
      const auto top = static_cast<double>(a) + (static_cast<double>(b) - a) * tx;
      const auto bottom = static_cast<double>(c) + (static_cast<double>(d) - c) * tx;
      return static_cast<float>(top + (bottom - top) * ty);
    };
    return {interpolate(p00.red, p10.red, p01.red, p11.red),
        interpolate(p00.green, p10.green, p01.green, p11.green),
        interpolate(p00.blue, p10.blue, p01.blue, p11.blue),
        interpolate(p00.alpha, p10.alpha, p01.alpha, p11.alpha)};
  }

  [[nodiscard]] ColorPixel sample(int document_x, int document_y) const {
    return sample(static_cast<double>(document_x), static_cast<double>(document_y));
  }
};

[[nodiscard]] TextMaterialBackdrop make_text_material_backdrop(
    const std::vector<ColorPixel>& lower_stack, int canvas_width, int canvas_height,
    const PhotoLayer& layer, int blur_radius, int sample_margin, CancellationToken cancellation) {
  TextMaterialBackdrop result;
  const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
  const auto radians = layer.transform_rotation * kPi / 180.0;
  const auto half_width = layer.pixel_width * scale * 0.5;
  const auto half_height = layer.pixel_height * scale * 0.5;
  const auto extent_x = std::abs(std::cos(radians)) * half_width + std::abs(std::sin(radians)) * half_height;
  const auto extent_y = std::abs(std::sin(radians)) * half_width + std::abs(std::cos(radians)) * half_height;
  const auto center_x = layer.x - layer.effect_padding_x * scale + half_width;
  const auto center_y = layer.y - layer.effect_padding_y * scale + half_height;
  result.x = std::max(0, static_cast<int>(std::floor(center_x - extent_x)) - sample_margin);
  result.y = std::max(0, static_cast<int>(std::floor(center_y - extent_y)) - sample_margin);
  const auto right = std::min(canvas_width, static_cast<int>(std::ceil(center_x + extent_x)) + sample_margin);
  const auto bottom = std::min(canvas_height, static_cast<int>(std::ceil(center_y + extent_y)) + sample_margin);
  result.width = right - result.x;
  result.height = bottom - result.y;
  if (result.width <= 0 || result.height <= 0 || cancellation.cancelled()) return {};
  const auto count = static_cast<std::size_t>(result.width) * static_cast<std::size_t>(result.height);
  result.pixels.resize(count);
  for (int y = 0; y < result.height; ++y) {
    const auto source_offset = static_cast<std::size_t>((result.y + y) * canvas_width + result.x);
    const auto target_offset = static_cast<std::size_t>(y * result.width);
    std::copy_n(lower_stack.begin() + static_cast<std::ptrdiff_t>(source_offset), result.width,
        result.pixels.begin() + static_cast<std::ptrdiff_t>(target_offset));
  }
  if (blur_radius <= 0) return result;

  std::vector<float> input(count), horizontal(count), output(count);
  for (int channel = 0; channel < 3; ++channel) {
    if (cancellation.cancelled()) return {};
    for (std::size_t index = 0; index < count; ++index) {
      const auto& pixel = result.pixels[index];
      input[index] = channel == 0 ? pixel.red : channel == 1 ? pixel.green : pixel.blue;
    }
    box_blur_into(input, result.width, result.height, blur_radius, &horizontal, &output, cancellation);
    if (cancellation.cancelled()) return {};
    for (std::size_t index = 0; index < count; ++index) {
      auto& pixel = result.pixels[index];
      if (channel == 0) pixel.red = output[index];
      else if (channel == 1) pixel.green = output[index];
      else pixel.blue = output[index];
    }
  }
  return result;
}

struct MaterialVector final {
  double x = 0.0;
  double y = 0.0;
  double z = 1.0;
};

[[nodiscard]] double material_dot(const MaterialVector& left, const MaterialVector& right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] MaterialVector material_cross(const MaterialVector& left, const MaterialVector& right) {
  return {left.y * right.z - left.z * right.y,
      left.z * right.x - left.x * right.z,
      left.x * right.y - left.y * right.x};
}

[[nodiscard]] MaterialVector material_normalize(const MaterialVector& value) {
  const auto length = std::sqrt(std::max(0.0, material_dot(value, value)));
  if (length <= 1e-12) return {};
  return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] MaterialVector material_add(const MaterialVector& left, const MaterialVector& right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] MaterialVector material_scale(const MaterialVector& value, double scale) {
  return {value.x * scale, value.y * scale, value.z * scale};
}

struct TextSurfaceNormal final {
  MaterialVector value;
  double edge_coverage = 0.0;
  double outward_x = 0.0;
  double outward_y = 0.0;
};

[[nodiscard]] TextSurfaceNormal text_surface_normal(
    const PhotoLayer& layer, double alpha_dx, double alpha_dy, double transform_scale,
    double text_x, double text_y) {
  const auto gradient_length = std::hypot(alpha_dx, alpha_dy);
  const auto min_dimension = static_cast<double>(std::max(1, std::min(layer.pixel_width, layer.pixel_height)));
  const auto depth = (std::isfinite(layer.text_material_depth)
      ? std::clamp(layer.text_material_depth, 0.0, 32.0) : 4.0) * transform_scale;
  const auto softness = (std::isfinite(layer.text_material_edge_softness)
      ? std::clamp(layer.text_material_edge_softness, 0.0, 16.0) : 1.0) * transform_scale;
  // Alpha derivatives are measured per raster pixel. Normalize by glyph size
  // so the same edge has the same coverage when its raster is enlarged.
  const auto edge_coverage = std::clamp(gradient_length * min_dimension, 0.0, 1.0);
  const auto normalized_x = layer.pixel_width > 1
      ? std::clamp(text_x / static_cast<double>(layer.pixel_width), 0.0, 1.0) : 0.5;
  const auto normalized_y = layer.pixel_height > 1
      ? std::clamp(text_y / static_cast<double>(layer.pixel_height), 0.0, 1.0) : 0.5;
  // A shallow crown gives the 2D lettering a plausible front-face normal for
  // reflected strips. Its curvature is inferred from the existing depth field
  // and normalized by glyph size; it does not alter the editable glyph mask.
  const auto crown = std::clamp(depth / (min_dimension * transform_scale) * 1.5, 0.0, 0.24);
  const auto crown_normal = MaterialVector{
      -(normalized_x - 0.5) * 2.0 * crown,
      -(normalized_y - 0.5) * 2.0 * crown, 1.0};
  if (gradient_length <= 1e-8 || edge_coverage <= 1e-8 || depth <= 0.0) {
    return {material_normalize(crown_normal), edge_coverage, 0.0, 0.0};
  }
  const auto outward_x = -alpha_dx / gradient_length;
  const auto outward_y = -alpha_dy / gradient_length;
  const auto slope = std::clamp(depth / std::max(softness, 0.01), 0.0, 8.0);
  return {material_normalize({crown_normal.x + outward_x * slope * edge_coverage,
      crown_normal.y + outward_y * slope * edge_coverage, crown_normal.z}),
      edge_coverage, outward_x, outward_y};
}

[[nodiscard]] double dielectric_fresnel(double cosine_incident, double ior) {
  const auto cos_i = std::clamp(std::abs(cosine_incident), 0.0, 1.0);
  const auto sin_t_squared = (1.0 - cos_i * cos_i) / (ior * ior);
  if (sin_t_squared >= 1.0) return 1.0;
  const auto cos_t = std::sqrt(std::max(0.0, 1.0 - sin_t_squared));
  const auto rs_denominator = cos_i + ior * cos_t;
  const auto rp_denominator = ior * cos_i + cos_t;
  if (rs_denominator <= 1e-12 || rp_denominator <= 1e-12) return 1.0;
  const auto rs = (cos_i - ior * cos_t) / rs_denominator;
  const auto rp = (ior * cos_i - cos_t) / rp_denominator;
  return std::clamp(0.5 * (rs * rs + rp * rp), 0.0, 1.0);
}

[[nodiscard]] double schlick_fresnel(double f0, double cosine) {
  const auto grazing = std::pow(std::clamp(1.0 - cosine, 0.0, 1.0), 5.0);
  return f0 + (1.0 - f0) * grazing;
}

struct MetalTextProfile final {
  // Conductor F0 values expressed in canonical linear sRGB. Filament's metal
  // table specifies sRGB specular colors; convert those values to linear before
  // using them in the Fresnel term. Chrome comes from Hoffman's linear table.
  std::array<double, 3> f0_linear_srgb{};
  double roughness = 0.2;
  double anisotropy = 0.0;
};

[[nodiscard]] double srgb_eotf(double encoded) {
  return encoded <= 0.04045 ? encoded / 12.92
                            : std::pow((encoded + 0.055) / 1.055, 2.4);
}

[[nodiscard]] std::array<double, 3> linearize_srgb_f0(
    std::array<double, 3> encoded_srgb) {
  return {srgb_eotf(encoded_srgb[0]), srgb_eotf(encoded_srgb[1]), srgb_eotf(encoded_srgb[2])};
}

[[nodiscard]] MetalTextProfile metal_text_profile(std::string_view material) {
  if (material == "polished_chrome") return {{0.549, 0.556, 0.554}, 0.085, 0.0};
  if (material == "brushed_silver") return {linearize_srgb_f0({0.97, 0.96, 0.91}), 0.26, 0.82};
  if (material == "warm_gold") return {linearize_srgb_f0({1.00, 0.85, 0.57}), 0.18, 0.08};
  if (material == "copper") return {linearize_srgb_f0({0.97, 0.74, 0.62}), 0.25, 0.2};
  if (material == "dark_steel") return {linearize_srgb_f0({0.77, 0.78, 0.78}), 0.19, 0.0};
  if (material == "champagne_gold") return {{0.84, 0.75, 0.56}, 0.29, 0.3};
  if (material == "rose_gold") return {{0.9, 0.64, 0.57}, 0.23, 0.24};
  if (material == "antique_brass") return {{0.73, 0.58, 0.36}, 0.37, 0.65};
  if (material == "titanium") return {linearize_srgb_f0({0.76, 0.73, 0.69}), 0.27, 0.5};
  if (material == "black_chrome") return {{0.12, 0.14, 0.18}, 0.11, 0.0};
  return {{0.55, 0.55, 0.55}, 0.2, 0.0};
}

[[nodiscard]] double ggx_distribution(
    const MaterialVector& normal, const MaterialVector& half_vector,
    const MaterialVector& tangent, const MaterialVector& bitangent,
    double alpha_x, double alpha_y) {
  const auto n_dot_h = std::max(0.0, material_dot(normal, half_vector));
  if (n_dot_h <= 0.0) return 0.0;
  const auto h_x = material_dot(half_vector, tangent);
  const auto h_y = material_dot(half_vector, bitangent);
  const auto denominator = (h_x / alpha_x) * (h_x / alpha_x) +
      (h_y / alpha_y) * (h_y / alpha_y) + n_dot_h * n_dot_h;
  return 1.0 / (kPi * alpha_x * alpha_y * denominator * denominator);
}

[[nodiscard]] double ggx_lambda(
    const MaterialVector& normal, const MaterialVector& direction,
    const MaterialVector& tangent, const MaterialVector& bitangent,
    double alpha_x, double alpha_y) {
  const auto n_dot_direction = std::clamp(material_dot(normal, direction), 1e-6, 1.0);
  const auto sin_squared = std::max(0.0, 1.0 - n_dot_direction * n_dot_direction);
  if (sin_squared <= 1e-12) return 0.0;
  const auto tangent_length = std::sqrt(sin_squared);
  const auto x = material_dot(direction, tangent) / tangent_length;
  const auto y = material_dot(direction, bitangent) / tangent_length;
  const auto alpha_squared = x * x * alpha_x * alpha_x + y * y * alpha_y * alpha_y;
  const auto tan_squared = sin_squared / (n_dot_direction * n_dot_direction);
  return 0.5 * (std::sqrt(1.0 + alpha_squared * tan_squared) - 1.0);
}

[[nodiscard]] ColorPixel material_text_source(
    const PhotoLayer& layer, ColorPixel glyph, const ColorPixel& lower,
    const TextMaterialBackdrop* backdrop, const ColorPixel& material_tint_linear_srgb,
    const ColorMatrix3& linear_srgb_to_working, double document_x, double document_y,
    double alpha_dx, double alpha_dy, double text_x, double text_y) {
  const auto transform_scale = std::clamp(layer.transform_scale, 0.05, 32.0);
  const auto surface = text_surface_normal(
      layer, alpha_dx, alpha_dy, transform_scale, text_x, text_y);
  if (layer.text_material == "frosted_glass" || layer.text_material == "smoked_plastic") {
    const auto transmission = std::isfinite(layer.text_material_transmission)
        ? std::clamp(layer.text_material_transmission, 0.0, 1.0) : 0.68;
    const auto edge_softness = std::isfinite(layer.text_material_edge_softness)
        ? std::clamp(layer.text_material_edge_softness, 0.0, 16.0) : 1.0;
    const auto ior = layer.text_material == "frosted_glass" ? 1.5 : 1.49;
    const auto fresnel = dielectric_fresnel(surface.value.z, ior);
    const auto depth = (std::isfinite(layer.text_material_depth)
        ? std::clamp(layer.text_material_depth, 0.0, 32.0) : 4.0) * transform_scale;
    const auto offset = depth * (1.0 - 1.0 / ior) * surface.edge_coverage;
    const auto refracted = backdrop != nullptr && !backdrop->pixels.empty()
        ? backdrop->sample(document_x + surface.outward_x * offset,
            document_y + surface.outward_y * offset)
        : lower;
    const auto light_angle = std::isfinite(layer.text_material_light_angle)
        ? std::clamp(layer.text_material_light_angle, -180.0, 180.0) : -45.0;
    const auto angle = light_angle * kPi / 180.0;
    const MaterialVector view{0.0, 0.0, 1.0};
    const MaterialVector light = material_normalize({0.32 * std::cos(angle),
        0.32 * std::sin(angle), 0.947});
    const auto reflected_view = material_normalize(material_add(
        material_scale(surface.value, 2.0 * material_dot(surface.value, view)), material_scale(view, -1.0)));
    const auto reflected_alignment = std::clamp(material_dot(reflected_view, light), 0.0, 1.0);
    const auto reflection_roughness = std::clamp(0.08 + edge_softness * 0.035, 0.04, 0.82);
    const auto reflection_radiance = 0.08 + 0.92 * std::pow(reflected_alignment,
        std::max(1.0, 18.0 * (1.0 - reflection_roughness)));
    const auto transmitted = (1.0 - fresnel) * transmission;
    const auto absorbed = (1.0 - fresnel) * (1.0 - transmission);
    glyph.red = static_cast<float>(transmitted * refracted.red + absorbed * layer.text_material_tint.red +
        fresnel * reflection_radiance);
    glyph.green = static_cast<float>(transmitted * refracted.green + absorbed * layer.text_material_tint.green +
        fresnel * reflection_radiance);
    glyph.blue = static_cast<float>(transmitted * refracted.blue + absorbed * layer.text_material_tint.blue +
        fresnel * reflection_radiance);
    return glyph;
  }
  if (layer.text_material == "soft_emboss") {
    const auto light_angle = std::isfinite(layer.text_material_light_angle)
        ? std::clamp(layer.text_material_light_angle, -180.0, 180.0) : -45.0;
    const auto edge_softness = std::isfinite(layer.text_material_edge_softness)
        ? std::clamp(layer.text_material_edge_softness, 0.0, 16.0) : 1.0;
    const auto angle = light_angle * kPi / 180.0;
    const MaterialVector light = material_normalize({0.72 * std::cos(angle),
        0.72 * std::sin(angle), 0.694});
    const MaterialVector view{0.0, 0.0, 1.0};
    auto tangent = MaterialVector{std::cos(angle), std::sin(angle), 0.0};
    tangent = material_normalize(material_add(tangent,
        material_scale(surface.value, -material_dot(tangent, surface.value))));
    if (material_dot(tangent, tangent) <= 1e-10) tangent = material_normalize({1.0, 0.0, 0.0});
    const auto bitangent = material_normalize(material_cross(surface.value, tangent));
    const auto n_dot_l = std::max(0.0, material_dot(surface.value, light));
    const auto n_dot_v = std::max(1e-4, material_dot(surface.value, view));
    const auto half_vector = material_normalize(material_add(view, light));
    const auto n_dot_h = std::max(0.0, material_dot(surface.value, half_vector));
    const auto alpha = std::pow(std::clamp(0.24 + edge_softness * 0.025, 0.1, 0.82), 2.0);
    const auto distribution = ggx_distribution(
        surface.value, half_vector, tangent, bitangent, alpha, alpha);
    const auto geometry = 1.0 / (1.0 + ggx_lambda(surface.value, view, tangent, bitangent, alpha, alpha) +
        ggx_lambda(surface.value, light, tangent, bitangent, alpha, alpha));
    const auto fresnel = dielectric_fresnel(material_dot(view, half_vector), 1.5);
    const auto direct_specular = n_dot_l > 1e-6 && n_dot_h > 0.0
        ? distribution * geometry * fresnel / (4.0 * n_dot_v * n_dot_l) * n_dot_l * 0.12 : 0.0;
    const auto diffuse = (1.0 - fresnel) * (0.18 + 0.82 * n_dot_l);
    const auto specular = fresnel * 0.08 + direct_specular * surface.edge_coverage;
    glyph.red = static_cast<float>(layer.text_material_tint.red * diffuse + specular);
    glyph.green = static_cast<float>(layer.text_material_tint.green * diffuse + specular);
    glyph.blue = static_cast<float>(layer.text_material_tint.blue * diffuse + specular);
    return glyph;
  }
  const auto metal_material = std::string_view(layer.text_material);
  const bool is_metal = metal_material == "polished_chrome" || metal_material == "brushed_silver" ||
      metal_material == "warm_gold" || metal_material == "copper" || metal_material == "dark_steel" ||
      metal_material == "champagne_gold" || metal_material == "rose_gold" ||
      metal_material == "antique_brass" || metal_material == "titanium" || metal_material == "black_chrome";
  if (!is_metal) return glyph;

  const auto material_angle = std::isfinite(layer.text_material_light_angle)
      ? std::clamp(layer.text_material_light_angle, -180.0, 180.0) : -45.0;
  const auto angle = material_angle * kPi / 180.0;
  const auto reflectivity = std::isfinite(layer.text_material_transmission)
      ? std::clamp(layer.text_material_transmission, 0.0, 1.0) : 0.78;
  const auto profile = metal_text_profile(metal_material);
  const auto normal = surface.value;
  const auto view = MaterialVector{0.0, 0.0, 1.0};
  const auto n_dot_v = std::max(1e-4, material_dot(normal, view));
  auto tangent = MaterialVector{std::cos(angle), std::sin(angle), 0.0};
  tangent = material_normalize(material_add(tangent, material_scale(normal, -material_dot(tangent, normal))));
  if (material_dot(tangent, tangent) <= 1e-10) tangent = material_normalize({1.0, 0.0, 0.0});
  const auto bitangent = material_normalize(material_cross(normal, tangent));
  const auto alpha = profile.roughness * profile.roughness;
  const auto alpha_x = std::max(0.008, alpha * (1.0 - profile.anisotropy * 0.55));
  const auto alpha_y = std::max(0.008, alpha * (1.0 + profile.anisotropy * 0.8));
  std::array<double, 3> reflected{};
  for (std::size_t channel = 0; channel < reflected.size(); ++channel) {
    const auto tint = std::clamp(static_cast<double>(channel == 0 ? material_tint_linear_srgb.red :
        channel == 1 ? material_tint_linear_srgb.green : material_tint_linear_srgb.blue), 0.0, 1.0);
    const auto f0 = std::clamp(profile.f0_linear_srgb[channel] * (0.65 + 0.35 * tint), 0.0, 1.0);
    // Preintegrated low-frequency neutral studio fill. Its scene-linear
    // radiance is 0.28; the remaining directional response comes from the
    // explicit softbox samples below.
    reflected[channel] = schlick_fresnel(f0, n_dot_v) * 0.28;
  }

  // Three neutral D65 softboxes are sampled in the same canonical linear sRGB
  // reference as the conductor F0 values. Each angular radius broadens the
  // GGX response as a bounded approximation of integrating over that source's
  // area; weights represent source radiance times its angular quadrature area.
  // The broad frontal fill plus these key, fill, and rim panels give the flat
  // face and inferred bevel/crown normals a readable reflection sweep.
  struct StudioSoftboxSample final {
    double tilt;
    double azimuth_offset;
    double integrated_weight;
    double angular_radius;
  };
  constexpr std::array<StudioSoftboxSample, 3> softboxes{{
      {0.08, -0.72, 0.14, 0.19},
      {0.28, 0.00, 0.18, 0.08},
      {0.52, 0.78, 0.13, 0.16},
  }};
  for (const auto& softbox : softboxes) {
    const auto azimuth = angle + softbox.azimuth_offset;
    const auto tilt = softbox.tilt;
    const MaterialVector light = material_normalize({
        std::sin(tilt) * std::cos(azimuth),
        std::sin(tilt) * std::sin(azimuth), std::cos(tilt)});
    const auto n_dot_l = std::max(0.0, material_dot(normal, light));
    if (n_dot_l <= 1e-6) continue;
    const auto half_vector = material_normalize(material_add(view, light));
    const auto n_dot_h = std::max(0.0, material_dot(normal, half_vector));
    const auto v_dot_h = std::clamp(material_dot(view, half_vector), 0.0, 1.0);
    const auto source_alpha_x = std::hypot(alpha_x, softbox.angular_radius);
    const auto source_alpha_y = std::hypot(alpha_y, softbox.angular_radius);
    const auto distribution = ggx_distribution(
        normal, half_vector, tangent, bitangent, source_alpha_x, source_alpha_y);
    const auto geometry = 1.0 / (1.0 + ggx_lambda(
        normal, view, tangent, bitangent, source_alpha_x, source_alpha_y) +
        ggx_lambda(normal, light, tangent, bitangent, source_alpha_x, source_alpha_y));
    if (n_dot_h <= 0.0 || n_dot_v <= 0.0) continue;
    const auto brdf_scale = distribution * geometry / (4.0 * n_dot_v * n_dot_l);
    for (std::size_t channel = 0; channel < reflected.size(); ++channel) {
      const auto tint = std::clamp(static_cast<double>(channel == 0 ? material_tint_linear_srgb.red :
          channel == 1 ? material_tint_linear_srgb.green : material_tint_linear_srgb.blue), 0.0, 1.0);
      const auto f0 = std::clamp(profile.f0_linear_srgb[channel] * (0.65 + 0.35 * tint), 0.0, 1.0);
      reflected[channel] += brdf_scale * schlick_fresnel(f0, v_dot_h) * n_dot_l *
          softbox.integrated_weight;
    }
  }
  const ColorPixel reflected_linear_srgb{
      static_cast<float>(reflected[0] * reflectivity),
      static_cast<float>(reflected[1] * reflectivity),
      static_cast<float>(reflected[2] * reflectivity), glyph.alpha};
  const auto reflected_working = apply_rgb_matrix(reflected_linear_srgb, linear_srgb_to_working);
  // Preserve signed and extended working-space values for later compositing and
  // output-gamut handling; clipping here would make the material depend on the
  // chosen working gamut.
  glyph.red = reflected_working.red;
  glyph.green = reflected_working.green;
  glyph.blue = reflected_working.blue;
  return glyph;
}

[[nodiscard]] bool photo_layer_uses_normal_blend(const PhotoLayer& layer) {
  return layer.blend_mode.empty() || layer.blend_mode == "normal";
}

[[nodiscard]] bool has_photo_layer_transform(const PhotoLayer& layer) {
  const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
  return std::abs(scale - 1.0) > 1e-6 || std::abs(layer.transform_rotation) > 1e-6;
}

[[nodiscard]] bool has_photo_layer_curve_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.curve_shadows) > 1e-9 || std::abs(layer.curve_darks) > 1e-9 ||
         std::abs(layer.curve_lights) > 1e-9 || std::abs(layer.curve_highlights) > 1e-9 ||
         has_point_curve(layer.master_curve_points) || has_point_curve(layer.luma_curve_points) ||
         has_point_curve(layer.red_curve_points) || has_point_curve(layer.green_curve_points) ||
         has_point_curve(layer.blue_curve_points);
}

[[nodiscard]] bool has_photo_layer_advanced_tone_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.brightness) > 1e-9 || std::abs(layer.filmic_shoulder) > 1e-9 ||
         std::abs(layer.filmic_toe) > 1e-9 || std::abs(layer.adaptive_contrast) > 1e-9 ||
         std::abs(layer.levels_black_point) > 1e-9 || std::abs(layer.levels_gamma) > 1e-9 ||
         std::abs(layer.levels_white_point) > 1e-9 ||
         std::abs(layer.zone_deep_shadows) > 1e-9 ||
         std::abs(layer.zone_shadows) > 1e-9 ||
         std::abs(layer.zone_midtones) > 1e-9 ||
         std::abs(layer.zone_lights) > 1e-9 ||
         std::abs(layer.zone_highlights) > 1e-9;
}

[[nodiscard]] double apply_layer_perceptual_point_curve(double value, const CurveEvaluationData& curve) {
  return apply_perceptual_point_curve(value, curve);
}

[[nodiscard]] ColorPixel apply_layer_luminance_point_curve(
    const ColorPixel& pixel,
    const CurveEvaluationData& curve,
    double output_headroom,
    const PipelineColorContext& context) {
  if (curve.points.size() < 2) {
    return pixel;
  }
  const auto source_luminance = luminance_for_pixel(pixel, context);
  const auto target_luminance = apply_layer_perceptual_point_curve(source_luminance, curve);
  return scale_pixel_to_luminance_with_headroom(
      pixel,
      target_luminance,
      output_headroom,
      context);
}

[[nodiscard]] ColorPixel interpolate_adjustment_pixel(
    const ColorPixel& base,
    const ColorPixel& adjusted,
    double weight) {
  const auto clamped_weight = clamp_unit(weight);
  return ColorPixel{
      .red = static_cast<float>(
          std::lerp(static_cast<double>(base.red), static_cast<double>(adjusted.red), clamped_weight)),
      .green = static_cast<float>(
          std::lerp(static_cast<double>(base.green), static_cast<double>(adjusted.green), clamped_weight)),
      .blue = static_cast<float>(
          std::lerp(static_cast<double>(base.blue), static_cast<double>(adjusted.blue), clamped_weight)),
      .alpha = base.alpha,
  };
}

ColorPixel interpolate_effect_pixel(const ColorPixel& base, const ColorPixel& adjusted, double weight) {
  weight = clamp_unit(weight);
  if (weight == 0) return base;
  const auto alpha = base.alpha * (1 - weight) + adjusted.alpha * weight;
  if (alpha <= 0) return {0, 0, 0, 0};
  const auto channel = [&](float a, float b) {
    return static_cast<float>((a * base.alpha * (1 - weight) + b * adjusted.alpha * weight) / alpha);
  };
  return {channel(base.red, adjusted.red), channel(base.green, adjusted.green),
      channel(base.blue, adjusted.blue), static_cast<float>(alpha)};
}

[[nodiscard]] ColorAdjustmentState compile_photo_layer_advanced_tone(const PhotoLayer& layer) {
  ColorAdjustmentState tone_state;
  tone_state.brightness = layer.brightness;
  tone_state.filmic_shoulder = layer.filmic_shoulder;
  tone_state.filmic_toe = layer.filmic_toe;
  tone_state.adaptive_contrast = layer.adaptive_contrast;
  tone_state.levels_black_point = layer.levels_black_point;
  tone_state.levels_gamma = layer.levels_gamma;
  tone_state.levels_white_point = layer.levels_white_point;
  tone_state.zone_deep_shadows = layer.zone_deep_shadows;
  tone_state.zone_shadows = layer.zone_shadows;
  tone_state.zone_midtones = layer.zone_midtones;
  tone_state.zone_lights = layer.zone_lights;
  tone_state.zone_highlights = layer.zone_highlights;
  return tone_state;
}

[[nodiscard]] ColorPixel apply_photo_layer_advanced_tone_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const ColorAdjustmentState* prepared_tone) {
  // Preserve the scalar callback path as the oracle; standard layers share
  // this immutable state across all pixels and spatial-preparation workers.
  if (prepared_tone == nullptr) {
    const auto tone = compile_photo_layer_advanced_tone(layer);
    return apply_photo_layer_advanced_tone_adjustments(input, context, layer, &tone);
  }
  const auto& tone_state = *prepared_tone;
  auto target_luminance = apply_global_tone(luminance_for_pixel(input, context), tone_state);
  const auto layer_output_headroom = context.output_headroom;
  if (layer.filmic_shoulder != 0.0) {
    const auto shoulder_threshold = shoulder_threshold_for_max_luminance(
        target_luminance,
        layer_output_headroom,
        layer.filmic_shoulder);
    if (shoulder_threshold < layer_output_headroom) {
      target_luminance = apply_highlight_shoulder(
          target_luminance,
          shoulder_threshold,
          layer_output_headroom,
          layer.filmic_shoulder);
    }
  }
  auto pixel = scale_pixel_for_global_tone(
      input,
      target_luminance,
      layer_output_headroom,
      context);
  pixel.alpha = input.alpha;
  return pixel;
}

struct PhotoLayerCurveRuntime final {
  CurveEvaluationData parametric;
  CurveEvaluationData master;
  CurveEvaluationData luma;
  CurveEvaluationData red;
  CurveEvaluationData green;
  CurveEvaluationData blue;
};

[[nodiscard]] PhotoLayerCurveRuntime compile_photo_layer_curves(const PhotoLayer& layer) {
  ColorAdjustmentState curve_state;
  curve_state.curve_shadows = layer.curve_shadows;
  curve_state.curve_darks = layer.curve_darks;
  curve_state.curve_lights = layer.curve_lights;
  curve_state.curve_highlights = layer.curve_highlights;
  curve_state.master_curve_points = layer.master_curve_points;
  return {build_parametric_curve_data(curve_state), build_curve_data(layer.master_curve_points),
      build_curve_data(layer.luma_curve_points), build_curve_data(layer.red_curve_points),
      build_curve_data(layer.green_curve_points), build_curve_data(layer.blue_curve_points)};
}

[[nodiscard]] ColorPixel apply_photo_layer_curve_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const PhotoLayerCurveRuntime* compiled = nullptr) {
  // The fallback remains available to scalar callers and parity tests.
  const auto fallback = compiled == nullptr
      ? std::optional{compile_photo_layer_curves(layer)} : std::nullopt;
  const auto& curves = compiled != nullptr ? *compiled : *fallback;
  const auto layer_output_headroom = context.output_headroom;
  auto pixel = input;
  const auto& parametric_curve = curves.parametric;
  const auto curve_target = evaluate_tone_curve_target(
      luminance_for_pixel(pixel, context),
      parametric_curve,
      false,
      layer_output_headroom);
  pixel = scale_pixel_to_luminance_with_headroom(
      pixel,
      curve_target,
      layer_output_headroom,
      context);

  const auto& master_curve = curves.master;
  if (master_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, master_curve));
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, master_curve));
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, master_curve));
  }

  const auto& luma_curve = curves.luma;
  if (luma_curve.points.size() >= 2) {
    pixel = apply_layer_luminance_point_curve(
        pixel,
        luma_curve,
        layer_output_headroom,
        context);
  }

  const auto& red_curve = curves.red;
  if (red_curve.points.size() >= 2) {
    pixel.red = static_cast<float>(apply_layer_perceptual_point_curve(pixel.red, red_curve));
  }
  const auto& green_curve = curves.green;
  if (green_curve.points.size() >= 2) {
    pixel.green = static_cast<float>(apply_layer_perceptual_point_curve(pixel.green, green_curve));
  }
  const auto& blue_curve = curves.blue;
  if (blue_curve.points.size() >= 2) {
    pixel.blue = static_cast<float>(apply_layer_perceptual_point_curve(pixel.blue, blue_curve));
  }

  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] ColorAdjustmentState photo_layer_color_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState color_state;
  color_state.color_mixer_model = layer.color_mixer_model;
  color_state.color_mixer_channel = layer.color_mixer_channel;
  color_state.hue_adjustment = layer.hue_adjustment;
  color_state.saturation_adjustment = layer.saturation_adjustment;
  color_state.luminance_adjustment = layer.luminance_adjustment;
  color_state.color_mixer_channels = layer.color_mixer_channels;
  color_state.rgb_channel_mixer = layer.rgb_channel_mixer;
  color_state.black_and_white_enabled = layer.black_and_white_enabled;
  color_state.black_and_white_mixer = layer.black_and_white_mixer;
  color_state.color_replacement_enabled = layer.color_replacement_enabled;
  color_state.color_replacement_source_hue = layer.color_replacement_source_hue;
  color_state.color_replacement_target_hue = layer.color_replacement_target_hue;
  color_state.color_replacement_range = layer.color_replacement_range;
  color_state.color_replacement_softness = layer.color_replacement_softness;
  color_state.color_replacement_strength = layer.color_replacement_strength;
  color_state.color_replacement_preserve_luminance = layer.color_replacement_preserve_luminance;
  color_state.color_replacement_preserve_saturation = layer.color_replacement_preserve_saturation;
  color_state.skin_tone_protection_enabled = layer.skin_tone_protection_enabled;
  color_state.skin_tone_line_hue = layer.skin_tone_line_hue;
  color_state.skin_tone_protection_range = layer.skin_tone_protection_range;
  color_state.skin_tone_protection_strength = layer.skin_tone_protection_strength;
  color_state.color_harmony_enabled = layer.color_harmony_enabled;
  color_state.color_harmony_mode = layer.color_harmony_mode;
  color_state.color_harmony_base_hue = layer.color_harmony_base_hue;
  color_state.color_harmony_strength = layer.color_harmony_strength;
  color_state.lut_enabled = layer.lut_enabled;
  color_state.lut_strength = layer.lut_strength;
  color_state.lut_name = layer.lut_name;
  color_state.lut_table = layer.lut_table;
  color_state.lut_size = layer.lut_size;
  color_state.lut_domain_min = layer.lut_domain_min;
  color_state.lut_domain_max = layer.lut_domain_max;
  color_state.lut_input_color_space = layer.lut_input_color_space;
  color_state.lut_output_color_space = layer.lut_output_color_space;
  color_state.lut_stack = layer.lut_stack;
  color_state.lut_shaper_table = layer.lut_shaper_table;
  color_state.lut_shaper_size = layer.lut_shaper_size;
  color_state.lut_shaper_input_range = layer.lut_shaper_input_range;
  color_state.grading_shadows_hue = layer.grading_shadows_hue;
  color_state.grading_shadows_saturation = layer.grading_shadows_saturation;
  color_state.grading_shadows_luminance = layer.grading_shadows_luminance;
  color_state.grading_midtones_hue = layer.grading_midtones_hue;
  color_state.grading_midtones_saturation = layer.grading_midtones_saturation;
  color_state.grading_midtones_luminance = layer.grading_midtones_luminance;
  color_state.grading_highlights_hue = layer.grading_highlights_hue;
  color_state.grading_highlights_saturation = layer.grading_highlights_saturation;
  color_state.grading_highlights_luminance = layer.grading_highlights_luminance;
  color_state.grading_amount = layer.grading_amount;
  color_state.grading_balance = layer.grading_balance;
  return color_state;
}

[[nodiscard]] bool has_photo_layer_color_adjustments(
    const ColorAdjustmentState& color_state,
    const std::vector<CompiledUserLut>& user_luts) {
  return has_rgb_channel_mixer_adjustments(color_state) || color_state.black_and_white_enabled ||
         has_post_tone_color_adjustments(color_state, 0.0, 0.0) || !user_luts.empty();
}

bool photo_layer_requires_gamut_fit(
    const PhotoLayer& layer,
    const PipelineColorContext& context) {
  const auto layer_state = photo_layer_color_adjustment_state(layer);
  return has_photo_layer_color_adjustments(
      layer_state,
      compile_user_lut_runtimes(
          layer_state,
          context.working_space,
          context.chromatic_adaptation_method));
}


[[nodiscard]] ColorPixel apply_photo_layer_color_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const ColorAdjustmentState& color_state,
    const std::vector<CompiledUserLut>& user_luts) {
  auto pixel = input;
  if (has_rgb_channel_mixer_adjustments(color_state)) {
    pixel = apply_rgb_channel_mixer(pixel, color_state.rgb_channel_mixer);
  }
  if (color_state.black_and_white_enabled) {
    pixel = apply_black_and_white_mixer(
        pixel,
        pixel_to_linear_srgb_reference(pixel, context),
        color_state,
        luminance_for_pixel(pixel, context));
  }
  pixel = apply_post_tone_color_adjustments(
      pixel,
      context,
      color_state,
      0.0,
      0.0,
      0.0,
      context.output_headroom);
  if (!user_luts.empty()) {
    pixel = apply_user_luts_to_pixel(pixel, user_luts);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

[[nodiscard]] constexpr std::string_view photo_layer_effect_detail_order_contract() {
  return "photo-layer-effect-detail-order:v1|layer-recipe:tone->curves->color->effects-detail|effects-detail:before-mask-opacity-blend|mask-opacity-density:alpha-only|global-spatial-detail:after-layer-composite";
}

[[nodiscard]] bool has_photo_layer_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.dehaze) > 1e-9 || std::abs(layer.texture) > 1e-9 ||
         std::abs(layer.clarity) > 1e-9 || std::abs(layer.sharpening) > 1e-9 ||
         std::abs(layer.luminance_nr) > 1e-9 || std::abs(layer.color_nr) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_spatial_effect_detail_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9;
}

[[nodiscard]] bool has_photo_layer_optical_adjustments(const PhotoLayer& layer) {
  return std::abs(layer.lens_distortion) > 1e-9 ||
         std::abs(layer.lens_vignette) > 1e-9 ||
         std::abs(layer.chromatic_aberration) > 1e-9 ||
         std::abs(layer.perspective_vertical) > 1e-9 ||
         std::abs(layer.perspective_horizontal) > 1e-9 ||
         std::abs(layer.global_transform_rotation) > 1e-9 ||
         std::abs(layer.global_transform_scale - 1.0) > 1e-9 ||
         std::abs(layer.transform_skew_x) > 1e-9 ||
         std::abs(layer.transform_skew_y) > 1e-9 ||
         std::abs(layer.liquify_strength) > 1e-9;
}

[[nodiscard]] ColorAdjustmentState photo_layer_optical_adjustment_state(const PhotoLayer& layer) {
  ColorAdjustmentState state;
  state.lens_distortion = layer.lens_distortion;
  state.lens_vignette = layer.lens_vignette;
  state.chromatic_aberration = layer.chromatic_aberration;
  state.perspective_vertical = layer.perspective_vertical;
  state.perspective_horizontal = layer.perspective_horizontal;
  state.transform_rotation = layer.global_transform_rotation;
  state.transform_scale = layer.global_transform_scale;
  state.transform_skew_x = layer.transform_skew_x;
  state.transform_skew_y = layer.transform_skew_y;
  state.liquify_center_x = layer.liquify_center_x;
  state.liquify_center_y = layer.liquify_center_y;
  state.liquify_radius = layer.liquify_radius;
  state.liquify_strength = layer.liquify_strength;
  state.content_aware_crop_fill = layer.content_aware_crop_fill;
  return state;
}

struct PhotoLayerEffectDetailContext final {
  const std::vector<float>* luminance = nullptr;
  const std::vector<float>* texture_blur = nullptr;
  const std::vector<float>* clarity_blur = nullptr;
  int width = 0;
  int height = 0;
};

[[nodiscard]] bool valid_photo_layer_effect_detail_context(
    const PhotoLayerEffectDetailContext* detail_context,
    std::size_t index) {
  return detail_context != nullptr &&
         detail_context->luminance != nullptr &&
         detail_context->width > 0 &&
         detail_context->height > 0 &&
         index < detail_context->luminance->size();
}

[[nodiscard]] ColorPixel apply_photo_layer_effect_detail_adjustments(
    const ColorPixel& input,
    const PipelineColorContext& context,
    const PhotoLayer& layer,
    const PhotoLayerEffectDetailContext* detail_context = nullptr,
    std::size_t index = 0) {
  auto pixel = input;
  const auto dehaze_strength = clamp_signed_percent(layer.dehaze) / 100.0;
  if (std::abs(dehaze_strength) > 1e-9) {
    auto lab = linear_rgb_to_oklab(pixel, context);
    const auto chroma = std::hypot(lab.a, lab.b);
    if (chroma > 1e-6) {
      const auto scale = std::max(0.0, 1.0 + dehaze_strength * 0.10);
      lab.a *= scale;
      lab.b *= scale;
      pixel = clamp_rgb_to_output_headroom(oklab_to_linear_rgb(lab, pixel.alpha, context), context);
    }
  }

  const auto texture_strength = clamp_signed_percent(layer.texture) / 100.0;
  const auto clarity_strength = clamp_signed_percent(layer.clarity) / 100.0;
  if ((std::abs(texture_strength) > 1e-9 || std::abs(clarity_strength) > 1e-9) &&
      valid_photo_layer_effect_detail_context(detail_context, index)) {
    const auto& luminance_buffer = *detail_context->luminance;
    const auto safe_width = static_cast<std::size_t>(detail_context->width);
    const auto safe_height = static_cast<std::size_t>(detail_context->height);
    const auto x = index % safe_width;
    const auto y = index / safe_width;
    const auto luminance = static_cast<double>(luminance_buffer[index]);
    auto local_activity = 0.0;
    if (x > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - 1])));
    }
    if (x + 1 < safe_width) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + 1])));
    }
    if (y > 0) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index - safe_width])));
    }
    if (y + 1 < safe_height) {
      local_activity = std::max(
          local_activity,
          std::abs(luminance - static_cast<double>(luminance_buffer[index + safe_width])));
    }

    const auto midtone_weight = bell_weight(luminance, 0.5, 0.42);
    const auto shadow_guard = smoothstep(0.025, 0.12, luminance);
    const auto highlight_headroom = 1.0 - smoothstep(0.74, 0.98, luminance);
    const auto tone_guard = shadow_guard * highlight_headroom;
    auto target_luminance = luminance;

    if (std::abs(texture_strength) > 1e-9 &&
        detail_context->texture_blur != nullptr &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->texture_blur->size() &&
        index < detail_context->clarity_blur->size()) {
      const auto texture_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->texture_blur)[index]);
      const auto broad_edge_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto broad_edge_guard =
          1.0 - smoothstep(0.08, 0.36, std::abs(broad_edge_signal)) * 0.72;
      const auto texture_gain = texture_strength >= 0.0 ? 1.30 : 0.48;
      target_luminance +=
          texture_strength * texture_signal * (0.46 + midtone_weight * 0.70) *
          broad_edge_guard * tone_guard * texture_gain;
    }

    if (std::abs(clarity_strength) > 1e-9 &&
        detail_context->clarity_blur != nullptr &&
        index < detail_context->clarity_blur->size()) {
      const auto clarity_signal =
          static_cast<double>(luminance_buffer[index] - (*detail_context->clarity_blur)[index]);
      const auto edge_limit =
          1.0 - smoothstep(0.24, 0.58, std::abs(clarity_signal)) * 0.28;
      const auto structure_gate = smoothstep(0.003, 0.035, local_activity);
      const auto clarity_gain = clarity_strength >= 0.0 ? 1.08 : 0.46;
      target_luminance +=
          clarity_strength * clarity_signal * (0.36 + midtone_weight * 0.82) *
          tone_guard * edge_limit * structure_gate * clarity_gain;
    }

    pixel = scale_pixel_to_luminance_with_headroom(
        pixel,
        target_luminance,
        context.output_headroom,
        context);
  }
  pixel.alpha = input.alpha;
  return pixel;
}

void apply_photo_layer_sharpen_noise_adjustments(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const PhotoLayer& layer) {
  ColorAdjustmentState detail;
  detail.sharpening = layer.sharpening;
  detail.sharpen_radius = layer.sharpen_radius;
  detail.sharpen_detail = layer.sharpen_detail;
  detail.sharpen_masking = layer.sharpen_masking;
  detail.luminance_nr = layer.luminance_nr;
  detail.color_nr = layer.color_nr;
  apply_fine_detail_effects_in_context(pixels, width, height, detail, context, {}, nullptr);
}

namespace {
bool run_layer_effects(std::vector<ColorPixel>* pixels, int width, int height,
    const PhotoLayer& layer, CancellationToken cancellation, std::string* error) {
  std::vector<PhotoEffectMaskBinding> masks;
  for (const auto& mask : layer.effect_masks) masks.push_back({mask.id, mask.coverage});
  // This content raster belongs to prepared_layer, never to the caller's live
  // layer. The enclosing compositor discards it and its output on any failure.
  const auto result = apply_photo_effect_stack_to_private_raster(pixels, width, height, layer.effects,
      {.scale_x = layer.effect_scale_x, .scale_y = layer.effect_scale_y,
       .luminance_weights = layer.effect_luminance_weights, .cancellation = cancellation}, masks);
  if (!result.ok() && error != nullptr) *error = "Layer '" + layer.name + "': " + result.error;
  return result.ok();
}

bool final_content_padding_is_clipped(const PhotoLayer& layer, int width, int height) {
  // Untransformed pixel sampling cannot see outside the canvas. Text materials
  // can sample neighboring alpha, and later effects can read an earlier halo;
  // those paths must retain their expanded content.
  if ((layer.kind != "pixel" && layer.kind != "source") ||
      has_photo_layer_transform(layer) || layer.effects.size() != 1) return false;
  const auto& effect = layer.effects.front();
  const bool transparent_kernel =
      (effect.type_id == "gaussian_blur" && effect.gaussian.edge_mode == PhotoEffectEdgeMode::Transparent) ||
      (effect.type_id == "box_blur" && effect.box.edge_mode == PhotoEffectEdgeMode::Transparent) ||
      (effect.type_id == "unsharp_mask" && effect.unsharp.edge_mode == PhotoEffectEdgeMode::Transparent) ||
      (effect.type_id == "motion_blur" && effect.motion.edge_mode == PhotoEffectEdgeMode::Transparent);
  if (!transparent_kernel) return false;
  const auto left = static_cast<std::int64_t>(layer.x) - layer.effect_padding_x;
  const auto top = static_cast<std::int64_t>(layer.y) - layer.effect_padding_y;
  return left <= 0 && top <= 0 && left + layer.pixel_width >= width && top + layer.pixel_height >= height;
}

bool prepare_single_effect_content(PhotoLayer* layer, int width, int height,
    bool final_effect, CancellationToken cancellation, std::string* error) {
  if (layer->kind == "fill") {
    layer->kind = "pixel";
    layer->pixel_width = width;
    layer->pixel_height = height;
    layer->x = 0;
    layer->y = 0;
    layer->transform_scale = 1;
    layer->transform_rotation = 0;
    layer->pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), layer->fill_color);
  }
  if (layer->pixel_width <= 0 || layer->pixel_height <= 0 ||
      layer->pixels.size() != static_cast<std::size_t>(layer->pixel_width) * static_cast<std::size_t>(layer->pixel_height)) {
    if (error) *error = "Photo effect layer has no valid raster content.";
    return false;
  }
  PhotoEffectSupport support;
  const auto admission = photo_effect_stack_support(layer->effects,
      {.scale_x = layer->effect_scale_x, .scale_y = layer->effect_scale_y}, &support);
  if (!admission.ok()) { if (error) *error = admission.error; return false; }
  // Extent expansion is for transparent boundaries. Extending/reflecting image
  // edges should not create an infinite opaque rectangle around a cutout.
  const auto transparent = std::any_of(layer->effects.begin(), layer->effects.end(), [](const auto& effect) {
    const auto edge = effect.type_id == "box_blur" ? effect.box.edge_mode : effect.gaussian.edge_mode;
    return effect.enabled && effect.amount > 0 && edge == PhotoEffectEdgeMode::Transparent;
  });
  if (!transparent || (support.x == 0 && support.y == 0))
    return run_layer_effects(&layer->pixels, layer->pixel_width, layer->pixel_height, *layer, cancellation, error);
  if (final_effect && final_content_padding_is_clipped(*layer, width, height))
    return run_layer_effects(&layer->pixels, layer->pixel_width, layer->pixel_height, *layer, cancellation, error);
  const auto expanded_width = static_cast<std::int64_t>(layer->pixel_width) + 2LL * support.x;
  const auto expanded_height = static_cast<std::int64_t>(layer->pixel_height) + 2LL * support.y;
  constexpr std::int64_t max_pixels = 512LL * 1024 * 1024 / sizeof(ColorPixel);
  if (expanded_width <= 0 || expanded_height <= 0 || expanded_width > max_pixels / expanded_height) {
    if (error) *error = "Expanded effect content exceeds the raster budget.";
    return false;
  }
  const auto new_width = static_cast<int>(expanded_width), new_height = static_cast<int>(expanded_height);
  const auto count = static_cast<std::size_t>(new_width) * static_cast<std::size_t>(new_height);
  std::vector<ColorPixel> expanded(count, ColorPixel{0, 0, 0, 0});
  for (int y = 0; y < layer->pixel_height; ++y) {
    if (cancellation.cancelled()) return false;
    std::copy_n(layer->pixels.begin() + static_cast<std::ptrdiff_t>(y) * layer->pixel_width, layer->pixel_width,
        expanded.begin() + static_cast<std::ptrdiff_t>(y + support.y) * new_width + support.x);
  }
  for (auto& mask : layer->effect_masks) {
    if (mask.coverage.size() != layer->pixels.size()) { if (error) *error = "Content effect mask size mismatch."; return false; }
    std::vector<float> padded(count, 0);
    for (int y = 0; y < layer->pixel_height; ++y)
      std::copy_n(mask.coverage.begin() + static_cast<std::ptrdiff_t>(y) * layer->pixel_width, layer->pixel_width,
          padded.begin() + static_cast<std::ptrdiff_t>(y + support.y) * new_width + support.x);
    mask.coverage = std::move(padded);
  }
  layer->pixel_width = new_width;
  layer->pixel_height = new_height;
  layer->effect_padding_x += support.x;
  layer->effect_padding_y += support.y;
  layer->pixels = std::move(expanded);
  return run_layer_effects(&layer->pixels, new_width, new_height, *layer, cancellation, error);
}

bool prepare_effect_content(PhotoLayer* layer, int width, int height,
    CancellationToken cancellation, std::string* error) {
  auto effects = std::move(layer->effects);
  for (auto effect_it = effects.begin(); effect_it != effects.end(); ++effect_it) {
    const auto& effect = *effect_it;
    if (!effect.enabled || effect.amount == 0) continue;
    layer->effects = {effect};
    const bool final_effect = std::none_of(std::next(effect_it), effects.end(), [](const auto& next) {
      return next.enabled && next.amount != 0;
    });
    if (!prepare_single_effect_content(layer, width, height, final_effect, cancellation, error)) return false;
  }
  layer->effects = std::move(effects);
  return true;
}
}  // namespace

static bool apply_photo_layers_impl(
    std::vector<ColorPixel>* pixels,
    int width,
    int height,
    const PipelineColorContext& context,
    const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    bool common_adjustment_uses_layer_fields,
    bool common_adjustment_thread_safe, CancellationToken cancellation, std::string* error,
    std::optional<PhotographicRenderingModel> standard_rendering_model,
    std::string_view parent_id = {}) {
  (void)photo_layer_effect_detail_order_contract();
  if (pixels == nullptr || width <= 0 || height <= 0 ||
      !apply_common_adjustment || pixels->size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
    return false;
  }

  const auto participates = [](const PhotoLayer& layer) {
    if (!layer.enabled || layer.opacity <= 1e-6) return false;
    if (layer.kind == "group") return true;
    if (layer.kind == "adjustment" || layer.kind == "fill") return true;
    return (layer.kind == "pixel" || layer.kind == "text" || layer.kind == "source") &&
        layer.pixel_width > 0 && layer.pixel_height > 0 &&
        layer.pixels.size() == static_cast<std::size_t>(layer.pixel_width * layer.pixel_height);
  };
  const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  // Metal profile F0 and studio radiance are authored in linear sRGB. This
  // inverse uses the same D65/XYZ adaptation represented by the working-space
  // context's forward matrix, and is intentionally not gamut-clamped.
  const auto linear_srgb_to_working = invert_matrix3(context.rgb_to_linear_srgb);
  std::vector<double> previous_layer_alpha(common_adjustment_uses_layer_fields ? 0 : pixel_count, 1.0);
  for (auto layer_it = layers.begin(); layer_it != layers.end(); ++layer_it) {
    if (cancellation.cancelled()) return false;
    const auto& original_layer = *layer_it;
    if (original_layer.parent_id != parent_id) continue;
    if (!original_layer.enabled || original_layer.opacity <= 1e-6) continue;
    std::optional<PhotoLayer> prepared_layer;
    if ((original_layer.kind == "pixel" || original_layer.kind == "source") &&
        !original_layer.erase_base_image && participates(original_layer)) {
      const std::array common_values{original_layer.exposure, original_layer.contrast,
          original_layer.highlights, original_layer.shadows, original_layer.whites,
          original_layer.blacks, original_layer.temperature, original_layer.tint,
          original_layer.vibrance, original_layer.saturation, original_layer.hue_shift,
          original_layer.saturation_shift, original_layer.colorize_strength};
      const auto needs_content_adjustments =
          std::any_of(common_values.begin(), common_values.end(),
              [](double value) { return std::abs(value) > 1e-6; }) ||
          has_photo_layer_advanced_tone_adjustments(original_layer) ||
          has_photo_layer_curve_adjustments(original_layer) ||
          photo_layer_requires_gamut_fit(original_layer, context) ||
          has_photo_layer_effect_detail_adjustments(original_layer) ||
          has_photo_layer_optical_adjustments(original_layer);
      if (needs_content_adjustments) {
        // Reuse the canonical layer recipe on this layer's content, before its
        // effects, placement, opacity and cutout coverage are composited.
        // Moving the pixels avoids copying a full raster into the recipe row.
        prepared_layer = original_layer;
        prepared_layer->effect_luminance_weights = effect_luminance_weights_for_context(context);
        auto content_pixels = std::move(prepared_layer->pixels);
        auto content_recipe = *prepared_layer;
        content_recipe.kind = "adjustment";
        content_recipe.parent_id.clear();
        content_recipe.copy_base_image = false;
        content_recipe.opacity = 1.0;
        content_recipe.blend_mode = "normal";
        content_recipe.clipping_mask = false;
        content_recipe.mask_data.clear();
        content_recipe.mask_width = 0;
        content_recipe.mask_height = 0;
        content_recipe.mask_inverted = false;
        content_recipe.mask_opacity = 1.0;
        content_recipe.mask_density = 1.0;
        content_recipe.effects.clear();
        const std::vector<PhotoLayer> content_stack{std::move(content_recipe)};
        if (!apply_photo_layers_impl(&content_pixels, original_layer.pixel_width,
                original_layer.pixel_height, context, content_stack,
                apply_common_adjustment, common_adjustment_uses_layer_fields,
                common_adjustment_thread_safe, cancellation, error, standard_rendering_model)) {
          return false;
        }
        prepared_layer->pixels = std::move(content_pixels);
      }
    }
    if (original_layer.kind != "adjustment" && original_layer.kind != "group" &&
        photo_layer_has_active_effects(original_layer)) {
      if (!prepared_layer) prepared_layer = original_layer;
      prepared_layer->effect_luminance_weights = effect_luminance_weights_for_context(context);
      if (!prepare_effect_content(&*prepared_layer, width, height, cancellation, error)) return false;
    }
    const auto& layer = prepared_layer ? *prepared_layer : original_layer;
    const auto material_tint_linear_srgb = layer.kind == "text"
        ? apply_rgb_matrix(layer.text_material_tint, context.rgb_to_linear_srgb)
        : ColorPixel{};
    const auto next_layer = std::find_if(std::next(layer_it), layers.end(), [&](const auto& candidate) {
      return candidate.parent_id == parent_id && participates(candidate);
    });
    const auto retain_alpha = !common_adjustment_uses_layer_fields ||
        (next_layer != layers.end() && next_layer->clipping_mask);
    std::vector<double> current_layer_alpha(retain_alpha ? pixel_count : 0, 0.0);
    const auto opacity = clamp_unit(layer.opacity);
    if (layer.kind == "group") {
      std::vector<ColorPixel> group_pixels(pixel_count, ColorPixel{0.0F, 0.0F, 0.0F, 0.0F});
      if (!apply_photo_layers_impl(&group_pixels, width, height, context, layers,
              apply_common_adjustment, common_adjustment_uses_layer_fields,
              common_adjustment_thread_safe, cancellation, error, standard_rendering_model, layer.id)) {
        return false;
      }
      auto group_adjustment = layer;
      group_adjustment.kind = "adjustment";
      group_adjustment.parent_id.clear();
      group_adjustment.opacity = 1.0;
      group_adjustment.clipping_mask = false;
      group_adjustment.mask_data.clear();
      group_adjustment.mask_width = 0;
      group_adjustment.mask_height = 0;
      group_adjustment.mask_inverted = false;
      group_adjustment.mask_opacity = 1.0;
      group_adjustment.mask_density = 1.0;
      group_adjustment.effects.clear();
      group_adjustment.copy_base_image = false;
      const std::vector<PhotoLayer> group_adjustment_stack{group_adjustment};
      const auto apply_group_adjustment = [&](ColorPixel* pixel, const PhotoLayer&) {
        apply_common_adjustment(pixel, layer);
      };
      if (!apply_photo_layers_impl(&group_pixels, width, height, context,
              group_adjustment_stack, apply_group_adjustment,
              common_adjustment_uses_layer_fields, common_adjustment_thread_safe,
              cancellation, error, standard_rendering_model)) {
        return false;
      }
      auto group_layer = layer;
      group_layer.pixels = std::move(group_pixels);
      group_layer.pixel_width = width;
      group_layer.pixel_height = height;
      group_layer.effect_luminance_weights = effect_luminance_weights_for_context(context);
      if (photo_layer_has_active_effects(layer)) {
        if (!prepare_effect_content(&group_layer, width, height, cancellation, error)) return false;
      }
      if (!has_photo_layer_transform(group_layer) && group_layer.x == 0 &&
          group_layer.y == 0) {
        for (int group_y = 0; group_y < group_layer.pixel_height; ++group_y) {
          const auto target_y = group_y - group_layer.effect_padding_y;
          if (target_y < 0 || target_y >= height) continue;
          for (int group_x = 0; group_x < group_layer.pixel_width; ++group_x) {
            const auto target_x = group_x - group_layer.effect_padding_x;
            if (target_x < 0 || target_x >= width) continue;
            const auto source_index =
                static_cast<std::size_t>(group_y * group_layer.pixel_width + group_x);
            const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
            const auto& source = group_layer.pixels[source_index];
            const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty()
                ? previous_layer_alpha[target_index] : 1.0;
            const auto alpha = clamp_unit(
                static_cast<double>(source.alpha) * opacity * clip_weight *
                photo_layer_mask_weight(layer, target_x, target_y, width, height));
            if (retain_alpha) {
              current_layer_alpha[target_index] =
                  std::max(current_layer_alpha[target_index], alpha);
            }
            if (alpha > 1e-6) {
              composite_layer_pixel(
                  &(*pixels)[target_index], source, alpha, layer.blend_mode, context);
            }
          }
        }
      } else {
        const auto scale = std::clamp(group_layer.transform_scale, 0.05, 32.0);
        const auto radians = -group_layer.transform_rotation * kPi / 180.0;
        const auto cos_rotation = std::cos(radians);
        const auto sin_rotation = std::sin(radians);
        const auto center_x = static_cast<double>(group_layer.x) -
                              group_layer.effect_padding_x * scale +
                              (static_cast<double>(group_layer.pixel_width) * scale * 0.5);
        const auto center_y = static_cast<double>(group_layer.y) -
                              group_layer.effect_padding_y * scale +
                              (static_cast<double>(group_layer.pixel_height) * scale * 0.5);
        for (int target_y = 0; target_y < height; ++target_y) {
          for (int target_x = 0; target_x < width; ++target_x) {
            const auto dx = static_cast<double>(target_x) + 0.5 - center_x;
            const auto dy = static_cast<double>(target_y) + 0.5 - center_y;
            const auto local_x =
                ((dx * cos_rotation - dy * sin_rotation) / scale) +
                (static_cast<double>(group_layer.pixel_width) * 0.5) - 0.5;
            const auto local_y =
                ((dx * sin_rotation + dy * cos_rotation) / scale) +
                (static_cast<double>(group_layer.pixel_height) * 0.5) - 0.5;
            const auto source = sample_layer_filtered(
                group_layer.pixels, group_layer.pixel_width, group_layer.pixel_height,
                local_x, local_y, scale);
            const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
            const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty()
                ? previous_layer_alpha[target_index] : 1.0;
            const auto alpha = clamp_unit(
                static_cast<double>(source.alpha) * opacity * clip_weight *
                photo_layer_mask_weight(layer, target_x, target_y, width, height));
            if (retain_alpha) {
              current_layer_alpha[target_index] =
                  std::max(current_layer_alpha[target_index], alpha);
            }
            if (alpha > 1e-6) {
              composite_layer_pixel(
                  &(*pixels)[target_index], source, alpha, layer.blend_mode, context);
            }
          }
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if (layer.kind == "adjustment") {
      const std::array common_values{layer.exposure, layer.contrast, layer.highlights,
          layer.shadows, layer.whites, layer.blacks, layer.temperature, layer.tint,
          layer.vibrance, layer.saturation, layer.hue_shift, layer.saturation_shift,
          layer.colorize_strength};
      const auto has_common_adjustments = !common_adjustment_uses_layer_fields ||
          std::any_of(common_values.begin(), common_values.end(),
              [](double value) { return std::abs(value) > 1e-6; });
      // Prepare from the layer being rendered. Linked base copies and groups
      // can supply copied/synthesized layers outside the caller's vector.
      const auto common_adjustment = standard_rendering_model && has_common_adjustments
          ? compile_local_adjustment(context, *standard_rendering_model, LocalAdjustmentSettings{
              .exposure = layer.exposure, .contrast = layer.contrast,
              .highlights = layer.highlights, .shadows = layer.shadows,
              .whites = layer.whites, .blacks = layer.blacks,
              .temperature = layer.temperature, .tint = layer.tint,
              .vibrance = layer.vibrance, .saturation = layer.saturation,
              .hue_shift = layer.hue_shift, .saturation_shift = layer.saturation_shift,
              .colorize_hue = layer.colorize_hue, .colorize_strength = layer.colorize_strength})
          : std::nullopt;
      const auto has_advanced_tone_adjustments = has_photo_layer_advanced_tone_adjustments(layer);
      const auto layer_tone = common_adjustment_uses_layer_fields && has_advanced_tone_adjustments
          ? std::optional{compile_photo_layer_advanced_tone(layer)} : std::nullopt;
      const auto has_curve_adjustments = has_photo_layer_curve_adjustments(layer);
      const auto layer_curves = common_adjustment_uses_layer_fields && has_curve_adjustments
          ? std::optional{compile_photo_layer_curves(layer)} : std::nullopt;
      const auto layer_color_state = photo_layer_color_adjustment_state(layer);
      const auto layer_user_luts = compile_user_lut_runtimes(
          layer_color_state,
          context.working_space,
          context.chromatic_adaptation_method);
      const auto has_color_adjustments =
          has_photo_layer_color_adjustments(layer_color_state, layer_user_luts);
      const auto has_effect_detail_adjustments = has_photo_layer_effect_detail_adjustments(layer);
      const auto has_spatial_effect_detail_adjustments =
          has_photo_layer_spatial_effect_detail_adjustments(layer);
      const auto has_optical_adjustments = has_photo_layer_optical_adjustments(layer);
      const auto has_creative_effects = photo_layer_has_active_effects(layer);
      const auto precompute_adjusted_layer =
          has_optical_adjustments || has_effect_detail_adjustments || has_creative_effects;
      const auto neutral_normal_layer = !has_common_adjustments &&
          !has_advanced_tone_adjustments && !has_curve_adjustments && !has_color_adjustments &&
          !precompute_adjusted_layer && photo_layer_uses_normal_blend(layer) &&
          !layer.clipping_mask && std::isfinite(layer.opacity) &&
          std::isfinite(layer.mask_opacity) && std::isfinite(layer.mask_density) &&
          std::all_of(pixels->begin(), pixels->end(), [](const ColorPixel& pixel) {
            return std::isfinite(pixel.red) && std::isfinite(pixel.green) && std::isfinite(pixel.blue);
          });
      if (neutral_normal_layer) {
        // A neutral normal adjustment leaves finite RGB and alpha unchanged.
        // Its coverage still matters if the next participating layer clips to it.
        if (retain_alpha) {
          for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
              current_layer_alpha[static_cast<std::size_t>(y * width + x)] =
                  opacity * photo_layer_mask_weight(layer, x, y, width, height);
        }
        previous_layer_alpha = std::move(current_layer_alpha);
        continue;
      }
      std::vector<ColorPixel> effect_detail_input_pixels;
      std::vector<float> effect_detail_luminance;
      std::vector<float> effect_detail_texture_blur;
      std::vector<float> effect_detail_clarity_blur;
      PhotoLayerEffectDetailContext effect_detail_context;
      const auto apply_layer_recipe_without_effect_detail =
          [&](const ColorPixel& source_pixel) {
            auto adjusted = source_pixel;
            if (has_common_adjustments) {
              if (standard_rendering_model) {
                apply_local_adjustment_mask(&adjusted, context, *standard_rendering_model, 1.0,
                    layer.exposure, layer.contrast, layer.highlights, layer.shadows,
                    layer.whites, layer.blacks, layer.temperature, layer.tint,
                    layer.vibrance, layer.saturation, layer.hue_shift, layer.saturation_shift,
                    layer.colorize_hue, layer.colorize_strength,
                    common_adjustment ? &*common_adjustment : nullptr);
              } else {
                apply_common_adjustment(&adjusted, layer);
              }
            }
            if (has_advanced_tone_adjustments) {
              adjusted = apply_photo_layer_advanced_tone_adjustments(
                  adjusted, context, layer, layer_tone ? &*layer_tone : nullptr);
            }
            if (has_curve_adjustments) {
              adjusted = apply_photo_layer_curve_adjustments(
                  adjusted, context, layer, layer_curves ? &*layer_curves : nullptr);
            }
            if (has_color_adjustments) {
              adjusted = apply_photo_layer_color_adjustments(
                  adjusted,
                  context,
                  layer_color_state,
                  layer_user_luts);
            }
            return adjusted;
          };
      if (precompute_adjusted_layer) {
        effect_detail_input_pixels = *pixels;
        const auto prepare_row = [&](int y) {
          for (int x = 0; x < width; ++x) {
            auto& pixel = effect_detail_input_pixels[static_cast<std::size_t>(y * width + x)];
            pixel = apply_layer_recipe_without_effect_detail(pixel);
          }
        };
        if (common_adjustment_uses_layer_fields &&
            (!has_common_adjustments || common_adjustment_thread_safe)) {
          parallel_for(height, prepare_row);
        } else {
          for (int y = 0; y < height; ++y) prepare_row(y);
        }
        if (has_optical_adjustments) {
          auto optical_state = photo_layer_optical_adjustment_state(layer);
          apply_lens_vignette_correction(
              &effect_detail_input_pixels,
              width,
              height,
              optical_state);
          apply_geometry_corrections(
              &effect_detail_input_pixels,
              width,
              height,
              optical_state);
        }
        // Only Texture/Clarity consume this plane. Other prepared adjustments
        // and creative effects need the RGB input but no full-frame luminance.
        if (has_spatial_effect_detail_adjustments) {
          effect_detail_luminance.resize(effect_detail_input_pixels.size());
          for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
            effect_detail_luminance[index] =
                static_cast<float>(luminance_for_pixel(effect_detail_input_pixels[index], context));
          }
        }
        BlurWorkspace effect_detail_blur_workspace;
        if (std::abs(layer.texture) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              2,
              0.00045,
              &effect_detail_texture_blur,
              &effect_detail_blur_workspace);
        }
        if (std::abs(layer.texture) > 1e-9 || std::abs(layer.clarity) > 1e-9) {
          guided_luminance_blur_into(
              effect_detail_luminance,
              width,
              height,
              8,
              0.0018,
              &effect_detail_clarity_blur,
              &effect_detail_blur_workspace);
        }
        effect_detail_context = PhotoLayerEffectDetailContext{
            .luminance = &effect_detail_luminance,
            .texture_blur = effect_detail_texture_blur.empty() ? nullptr : &effect_detail_texture_blur,
            .clarity_blur = effect_detail_clarity_blur.empty() ? nullptr : &effect_detail_clarity_blur,
            .width = width,
            .height = height,
        };
        if (has_effect_detail_adjustments) {
          for (std::size_t index = 0; index < effect_detail_input_pixels.size(); ++index) {
            effect_detail_input_pixels[index] = apply_photo_layer_effect_detail_adjustments(
                effect_detail_input_pixels[index],
                context,
                layer,
                has_spatial_effect_detail_adjustments ? &effect_detail_context : nullptr,
                index);
          }
          apply_photo_layer_sharpen_noise_adjustments(
              &effect_detail_input_pixels,
              width,
              height,
              context,
              layer);
        }
      }
      if (has_creative_effects) {
        std::vector<PhotoEffectMaskBinding> masks;
        for (const auto& mask : layer.effect_masks) masks.push_back({mask.id, mask.coverage});
        // apply_photo_layers owns the enclosing output transaction. Consume
        // each finished tile directly into it, avoiding a second full-frame
        // effect output. On any later failure the enclosing copy is discarded.
        const auto result = render_photo_effect_tiles(effect_detail_input_pixels, width, height,
            layer.effects, [&](const PhotoEffectTileView& tile) {
              parallel_for(tile.height, [&](int row) {
                const int y = tile.y + row;
                for (int column = 0; column < tile.width; ++column) {
                  const int x = tile.x + column;
                  const auto index = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                      static_cast<std::size_t>(x);
                  auto& pixel = (*pixels)[index];
                  const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty()
                      ? previous_layer_alpha[index] : 1.0;
                  const auto layer_weight = opacity * clip_weight * photo_layer_mask_weight(layer, x, y, width, height);
                  if (retain_alpha) current_layer_alpha[index] = layer_weight;
                  if (layer_weight <= 1e-6) continue;
                  const auto before_layer = pixel;
                  const auto adjusted = tile.pixels[static_cast<std::size_t>(row) *
                      static_cast<std::size_t>(tile.stride) + static_cast<std::size_t>(column)];
                  auto blended = photo_layer_uses_normal_blend(layer) ? adjusted :
                      blend_layer_color(before_layer, adjusted, layer.blend_mode, context);
                  blended.alpha = adjusted.alpha;
                  pixel = interpolate_effect_pixel(before_layer, blended, layer_weight);
                }
              });
              return !cancellation.cancelled();
            }, {.scale_x = layer.effect_scale_x, .scale_y = layer.effect_scale_y,
                .luminance_weights = effect_luminance_weights_for_context(context),
                .cancellation = cancellation}, masks);
        if (!result.ok()) {
          if (error) *error = "Layer '" + layer.name + "': " + result.error;
          return false;
        }
        if (result.status == PhotoEffectStatus::Applied) {
          previous_layer_alpha = std::move(current_layer_alpha);
          continue;
        }
      }
      const auto apply_adjustment_row = [&](int y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          auto& pixel = (*pixels)[index];
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[index] : 1.0;
          const auto layer_weight =
              opacity * clip_weight * photo_layer_mask_weight(layer, x, y, width, height);
          if (retain_alpha) current_layer_alpha[index] = layer_weight;
          if (layer_weight <= 1e-6) {
            continue;
          }
          const auto before_layer = pixel;
          auto adjusted = precompute_adjusted_layer
                              ? effect_detail_input_pixels[index]
                              : apply_layer_recipe_without_effect_detail(before_layer);
          if (has_creative_effects) {
            auto blended = photo_layer_uses_normal_blend(layer) ? adjusted :
                blend_layer_color(before_layer, adjusted, layer.blend_mode, context);
            blended.alpha = adjusted.alpha;
            pixel = interpolate_effect_pixel(before_layer, blended, layer_weight);
          } else if (photo_layer_uses_normal_blend(layer)) {
            pixel = interpolate_adjustment_pixel(before_layer, adjusted, layer_weight);
          } else {
            pixel = interpolate_adjustment_pixel(before_layer,
                blend_layer_color(before_layer, adjusted, layer.blend_mode, context), layer_weight);
          }
        }
      };
      // Prepared layer inputs are immutable and rows write disjoint pixels.
      // Custom callbacks stay serial unless the caller explicitly opts in.
      if (common_adjustment_uses_layer_fields &&
          (!has_common_adjustments || common_adjustment_thread_safe)) {
        parallel_for(height, apply_adjustment_row);
      } else {
        for (int y = 0; y < height; ++y) apply_adjustment_row(y);
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
    if (layer.kind == "fill") {
      for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
          const auto index = static_cast<std::size_t>(y * width + x);
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[index] : 1.0;
          const auto alpha = clamp_unit(
              static_cast<double>(layer.fill_color.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, x, y, width, height));
          if (retain_alpha) current_layer_alpha[index] = alpha;
          if (alpha <= 1e-6) {
            continue;
          }
          auto& target = (*pixels)[index];
          composite_layer_pixel(&target, layer.fill_color, alpha, layer.blend_mode, context);
        }
      }
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
  if ((layer.kind != "pixel" && layer.kind != "text" && layer.kind != "source") ||
        layer.pixel_width <= 0 || layer.pixel_height <= 0 ||
        layer.pixels.size() != static_cast<std::size_t>(layer.pixel_width * layer.pixel_height)) {
    continue;
  }
  TextMaterialBackdrop text_material_backdrop;
  if (layer.kind == "text" &&
      (layer.text_material == "frosted_glass" || layer.text_material == "smoked_plastic")) {
    const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
    const auto blur_value = layer.text_material == "frosted_glass" && std::isfinite(layer.text_material_blur)
        ? std::clamp(layer.text_material_blur, 0.0, 4096.0) * scale : 0.0;
    const auto depth = std::isfinite(layer.text_material_depth)
        ? std::clamp(layer.text_material_depth, 0.0, 32.0) * scale : 0.0;
    const auto blur_radius = static_cast<int>(std::ceil(blur_value));
    const auto sample_margin = blur_radius + static_cast<int>(std::ceil(depth));
    text_material_backdrop = make_text_material_backdrop(*pixels, width, height, layer,
        blur_radius, sample_margin, cancellation);
    if (cancellation.cancelled()) return false;
    if (text_material_backdrop.pixels.empty()) {
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }
  }
  if (!has_photo_layer_transform(layer)) {
      composite_rows(layer.pixel_height, layer.pixel_width, [&](int layer_y) {
        if (cancellation.cancelled()) return;
        const auto target_y = layer.y + layer_y - layer.effect_padding_y;
        if (target_y < 0 || target_y >= height) {
          return;
        }
        for (int layer_x = 0; layer_x < layer.pixel_width; ++layer_x) {
          const auto target_x = layer.x + layer_x - layer.effect_padding_x;
          if (target_x < 0 || target_x >= width) {
            continue;
          }
          const auto source_index = static_cast<std::size_t>(layer_y * layer.pixel_width + layer_x);
          const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
          auto source = layer.pixels[source_index];
          auto& target = (*pixels)[target_index];
          const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[target_index] : 1.0;
          if (layer.kind == "text" && !layer.text_material.empty() && layer.text_material != "none") {
            const auto alpha_at = [&](int sx, int sy) {
              if (sx < 0 || sy < 0 || sx >= layer.pixel_width || sy >= layer.pixel_height) return 0.0;
              return static_cast<double>(layer.pixels[static_cast<std::size_t>(sy * layer.pixel_width + sx)].alpha);
            };
            const auto alpha_dx = (alpha_at(layer_x + 1, layer_y) - alpha_at(layer_x - 1, layer_y)) * 0.5;
            const auto alpha_dy = (alpha_at(layer_x, layer_y + 1) - alpha_at(layer_x, layer_y - 1)) * 0.5;
            source = material_text_source(layer, source, target, &text_material_backdrop,
                material_tint_linear_srgb, linear_srgb_to_working,
                static_cast<double>(target_x), static_cast<double>(target_y), alpha_dx, alpha_dy,
                static_cast<double>(layer_x), static_cast<double>(layer_y));
          }
          const auto alpha = clamp_unit(
              static_cast<double>(source.alpha) * opacity * clip_weight *
              photo_layer_mask_weight(layer, target_x, target_y, width, height));
          if (retain_alpha) current_layer_alpha[target_index] = layer.erase_base_image ? target.alpha : std::max(current_layer_alpha[target_index], alpha);
          if (alpha <= 1e-6) {
            continue;
          }
          composite_layer_pixel(&target, source, alpha, layer.blend_mode, context, layer.erase_base_image);
          if (retain_alpha && layer.erase_base_image) current_layer_alpha[target_index] = target.alpha;
        }
      });
      if (cancellation.cancelled()) return false;
      previous_layer_alpha = std::move(current_layer_alpha);
      continue;
    }

    const auto scale = std::clamp(layer.transform_scale, 0.05, 32.0);
    const auto radians = -layer.transform_rotation * kPi / 180.0;
    const auto cos_rotation = std::cos(radians);
    const auto sin_rotation = std::sin(radians);
    const auto center_x = static_cast<double>(layer.x) - layer.effect_padding_x * scale +
                          (static_cast<double>(layer.pixel_width) * scale * 0.5);
    const auto center_y = static_cast<double>(layer.y) - layer.effect_padding_y * scale +
                          (static_cast<double>(layer.pixel_height) * scale * 0.5);
    // Each task owns a distinct destination row. The prepared content, clip
    // coverage, masks and text backdrop are immutable for the entire layer.
    composite_rows(height, width, [&](int target_y) {
      if (cancellation.cancelled()) return;
      for (int target_x = 0; target_x < width; ++target_x) {
        const auto dx = static_cast<double>(target_x) + 0.5 - center_x;
        const auto dy = static_cast<double>(target_y) + 0.5 - center_y;
        const auto local_x =
            ((dx * cos_rotation - dy * sin_rotation) / scale) +
            (static_cast<double>(layer.pixel_width) * 0.5) - 0.5;
        const auto local_y =
            ((dx * sin_rotation + dy * cos_rotation) / scale) +
            (static_cast<double>(layer.pixel_height) * 0.5) - 0.5;
        auto source = sample_layer_filtered(
            layer.pixels, layer.pixel_width, layer.pixel_height, local_x, local_y, scale);
        const auto target_index = static_cast<std::size_t>(target_y * width + target_x);
        auto& target = (*pixels)[target_index];
        const auto clip_weight = layer.clipping_mask && !previous_layer_alpha.empty() ? previous_layer_alpha[target_index] : 1.0;
        if (layer.kind == "text" && !layer.text_material.empty() && layer.text_material != "none") {
          const auto alpha_left = sample_layer_filtered(layer.pixels, layer.pixel_width, layer.pixel_height,
              local_x - 1.0, local_y, scale).alpha;
          const auto alpha_right = sample_layer_filtered(layer.pixels, layer.pixel_width, layer.pixel_height,
              local_x + 1.0, local_y, scale).alpha;
          const auto alpha_up = sample_layer_filtered(layer.pixels, layer.pixel_width, layer.pixel_height,
              local_x, local_y - 1.0, scale).alpha;
          const auto alpha_down = sample_layer_filtered(layer.pixels, layer.pixel_width, layer.pixel_height,
              local_x, local_y + 1.0, scale).alpha;
          const auto alpha_dx = (static_cast<double>(alpha_right) - alpha_left) * 0.5;
          const auto alpha_dy = (static_cast<double>(alpha_down) - alpha_up) * 0.5;
          source = material_text_source(layer, source, target, &text_material_backdrop,
              material_tint_linear_srgb, linear_srgb_to_working,
              static_cast<double>(target_x), static_cast<double>(target_y), alpha_dx, alpha_dy,
              local_x, local_y);
        }
        const auto alpha = clamp_unit(
            static_cast<double>(source.alpha) * opacity * clip_weight *
            photo_layer_mask_weight(layer, target_x, target_y, width, height));
        if (retain_alpha) current_layer_alpha[target_index] = layer.erase_base_image ? target.alpha : std::max(current_layer_alpha[target_index], alpha);
        if (alpha <= 1e-6) {
          continue;
        }
        composite_layer_pixel(&target, source, alpha, layer.blend_mode, context, layer.erase_base_image);
        if (retain_alpha && layer.erase_base_image) current_layer_alpha[target_index] = target.alpha;
      }
    });
    if (cancellation.cancelled()) return false;
    previous_layer_alpha = std::move(current_layer_alpha);
  }
  return !cancellation.cancelled();
}

static bool apply_photo_layers_prepared(std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    bool common_adjustment_uses_layer_fields, bool common_adjustment_thread_safe,
    CancellationToken cancellation, std::string* error,
    std::optional<PhotographicRenderingModel> standard_rendering_model) {
  if (error) error->clear();
  bool effects_active = false;
  for (const auto& layer : layers) {
    if (!layer.enabled || layer.opacity <= 1e-6) continue;
    effects_active = effects_active || photo_layer_has_active_effects(layer);
    if (!layer.effect_error.empty()) { if (error) *error = layer.effect_error; return false; }
    for (const auto& effect : layer.effects) {
      if (!effect.enabled || effect.amount == 0) continue;
      const auto expected = layer.kind == "adjustment" ? PhotoEffectInputScope::CompositeBelow : PhotoEffectInputScope::LayerContent;
      if (effect.input_scope != expected) {
        if (error) *error = "Photo effect input scope is incompatible with its layer.";
        return false;
      }
    }
  }
  if (pixels == nullptr || cancellation.cancelled()) return false;
  try {
    if (std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
          return layer.enabled && layer.copy_base_image;
        })) {
      auto resolved = layers;
      auto developed_base = *pixels;
      std::vector<PhotoLayer> base_edits;
      for (const auto& layer : resolved) if (layer.erase_base_image) base_edits.push_back(layer);
      if (!base_edits.empty() && !apply_photo_layers_impl(&developed_base, width, height, context, base_edits,
            apply_common_adjustment, common_adjustment_uses_layer_fields, common_adjustment_thread_safe,
            cancellation, error, standard_rendering_model)) return false;
      std::erase_if(resolved, [](const auto& layer) { return layer.erase_base_image; });
      for (auto& layer : resolved) {
        if (!layer.copy_base_image) continue;
        layer.copy_base_image = false;
        layer.pixels = developed_base;
        layer.pixel_width = width;
        layer.pixel_height = height;
      }
      if (!apply_photo_layers_prepared(&developed_base, width, height, context, resolved,
          apply_common_adjustment, common_adjustment_uses_layer_fields,
          common_adjustment_thread_safe, cancellation, error, standard_rendering_model)) return false;
      pixels->swap(developed_base);
      return true;
    }
    if (!effects_active) return apply_photo_layers_impl(pixels, width, height, context, layers,
        apply_common_adjustment, common_adjustment_uses_layer_fields, common_adjustment_thread_safe,
        cancellation, error, standard_rendering_model);
    auto output = *pixels;
    if (!apply_photo_layers_impl(&output, width, height, context, layers, apply_common_adjustment,
            common_adjustment_uses_layer_fields, common_adjustment_thread_safe,
            cancellation, error, standard_rendering_model)) return false;
    pixels->swap(output);
    return true;
  } catch (const std::bad_alloc&) {
    if (error) *error = "Could not allocate photo layer effect buffers.";
    return false;
  }
}

bool apply_photo_layers(std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, const std::vector<PhotoLayer>& layers,
    const PhotoLayerCommonAdjustment& apply_common_adjustment,
    bool common_adjustment_uses_layer_fields, bool common_adjustment_thread_safe,
    CancellationToken cancellation, std::string* error) {
  return apply_photo_layers_prepared(pixels, width, height, context, layers,
      apply_common_adjustment, common_adjustment_uses_layer_fields,
      common_adjustment_thread_safe, cancellation, error, std::nullopt);
}

bool apply_standard_photo_layers(
    std::vector<ColorPixel>* pixels, int width, int height,
    const PipelineColorContext& context, PhotographicRenderingModel rendering_model,
    const std::vector<PhotoLayer>& layers, CancellationToken cancellation, std::string* error) {
  return apply_photo_layers_prepared(pixels, width, height, context, layers,
      [&](ColorPixel* pixel, const PhotoLayer& layer) {
        apply_local_adjustment_mask(pixel, context, rendering_model, 1.0,
            layer.exposure, layer.contrast, layer.highlights, layer.shadows,
            layer.whites, layer.blacks, layer.temperature, layer.tint,
            layer.vibrance, layer.saturation, layer.hue_shift, layer.saturation_shift,
            layer.colorize_hue, layer.colorize_strength);
      }, true, true, cancellation, error, rendering_model);
}

}  // namespace photo_editor::pipeline
