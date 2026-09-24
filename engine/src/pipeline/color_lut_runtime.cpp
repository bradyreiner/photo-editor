#include "color_lut_runtime.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace photo_editor::pipeline {
namespace {

[[nodiscard]] bool color_lut_shape_valid(const ColorLut3D& lut) {
  if (lut.size < 2) {
    return false;
  }
  const auto size = static_cast<std::size_t>(lut.size);
  if (size > std::numeric_limits<std::size_t>::max() / size ||
      size * size > std::numeric_limits<std::size_t>::max() / size) {
    return false;
  }
  return lut.table.size() == size * size * size;
}

[[nodiscard]] double signed_power(double value, double exponent) {
  return std::copysign(std::pow(std::abs(value), exponent), value);
}

[[nodiscard]] double decode_lut_transfer(
    double value,
    TransferFunction transfer) {
  switch (transfer) {
    case TransferFunction::Linear:
      return value;
    case TransferFunction::Srgb: {
      const auto magnitude = std::abs(value);
      const auto decoded = magnitude <= 0.04045
                               ? magnitude / 12.92
                               : std::pow((magnitude + 0.055) / 1.055, 2.4);
      return std::copysign(decoded, value);
    }
    case TransferFunction::Gamma18:
      return signed_power(value, 1.8);
    case TransferFunction::Gamma22:
      return signed_power(value, 2.2);
    case TransferFunction::Gamma24:
      return signed_power(value, 2.4);
    case TransferFunction::Bt2020: {
      constexpr auto alpha = 1.09929682680944;
      constexpr auto beta = 0.018053968510807;
      const auto magnitude = std::abs(value);
      const auto decoded = magnitude < 4.5 * beta
                               ? magnitude / 4.5
                               : std::pow(
                                     (magnitude + alpha - 1.0) / alpha,
                                     1.0 / 0.45);
      return std::copysign(decoded, value);
    }
    case TransferFunction::Acescct:
      return acescct_to_linear_ap1(value);
    case TransferFunction::Unknown:
      return value;
  }
  return value;
}

[[nodiscard]] double encode_lut_transfer(
    double value,
    TransferFunction transfer) {
  switch (transfer) {
    case TransferFunction::Linear:
      return value;
    case TransferFunction::Srgb: {
      const auto magnitude = std::abs(value);
      const auto encoded = magnitude <= 0.0031308
                               ? magnitude * 12.92
                               : 1.055 * std::pow(magnitude, 1.0 / 2.4) - 0.055;
      return std::copysign(encoded, value);
    }
    case TransferFunction::Gamma18:
      return signed_power(value, 1.0 / 1.8);
    case TransferFunction::Gamma22:
      return signed_power(value, 1.0 / 2.2);
    case TransferFunction::Gamma24:
      return signed_power(value, 1.0 / 2.4);
    case TransferFunction::Bt2020: {
      constexpr auto alpha = 1.09929682680944;
      constexpr auto beta = 0.018053968510807;
      const auto magnitude = std::abs(value);
      const auto encoded = magnitude < beta
                               ? 4.5 * magnitude
                               : alpha * std::pow(magnitude, 0.45) -
                                     (alpha - 1.0);
      return std::copysign(encoded, value);
    }
    case TransferFunction::Acescct:
      return linear_ap1_to_acescct(value);
    case TransferFunction::Unknown:
      return value;
  }
  return value;
}

[[nodiscard]] ColorPixel apply_lut_rgb_matrix(
    const ColorPixel& pixel,
    const ColorMatrix3& matrix) {
  const auto transformed = multiply_matrix3_vector(
      matrix,
      {
          static_cast<double>(pixel.red),
          static_cast<double>(pixel.green),
          static_cast<double>(pixel.blue),
      });
  return ColorPixel{
      .red = static_cast<float>(transformed[0]),
      .green = static_cast<float>(transformed[1]),
      .blue = static_cast<float>(transformed[2]),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] bool lut_has_unit_domain(const ColorLut3D& lut) {
  return std::abs(lut.domain_min[0]) <= 1e-12 &&
         std::abs(lut.domain_min[1]) <= 1e-12 &&
         std::abs(lut.domain_min[2]) <= 1e-12 &&
         std::abs(lut.domain_max[0] - 1.0) <= 1e-12 &&
         std::abs(lut.domain_max[1] - 1.0) <= 1e-12 &&
         std::abs(lut.domain_max[2] - 1.0) <= 1e-12;
}

[[nodiscard]] ColorPixel apply_lut_unchecked(
    const ColorLut3D& lut,
    const ColorPixel& pixel,
    bool unit_domain) {
  const auto size = lut.size;
  const auto size_minus_1 = static_cast<double>(size - 1);
  const auto normalize_channel = [&](double value, int channel) {
    if (unit_domain) {
      return clamp_unit(value);
    }
    const auto min_value = lut.domain_min[static_cast<std::size_t>(channel)];
    const auto max_value = lut.domain_max[static_cast<std::size_t>(channel)];
    if (!std::isfinite(min_value) || !std::isfinite(max_value) || max_value <= min_value) {
      return clamp_unit(value);
    }
    return clamp_unit((value - min_value) / (max_value - min_value));
  };

  const auto rf = normalize_channel(pixel.red, 0) * size_minus_1;
  const auto gf = normalize_channel(pixel.green, 1) * size_minus_1;
  const auto bf = normalize_channel(pixel.blue, 2) * size_minus_1;
  const auto ri = std::min(static_cast<int>(rf), size - 2);
  const auto gi = std::min(static_cast<int>(gf), size - 2);
  const auto bi = std::min(static_cast<int>(bf), size - 2);
  const auto fr = rf - static_cast<double>(ri);
  const auto fg = gf - static_cast<double>(gi);
  const auto fb = bf - static_cast<double>(bi);

  const auto size_t_size = static_cast<std::size_t>(size);
  const auto row_stride = size_t_size * size_t_size;
  const auto plane_offset = static_cast<std::size_t>(ri) * row_stride +
                            static_cast<std::size_t>(gi) * size_t_size +
                            static_cast<std::size_t>(bi);
  const auto next_r = row_stride;
  const auto next_g = size_t_size;
  const auto next_b = std::size_t{1};
  const auto* table = lut.table.data() + plane_offset;

  const auto& c000 = table[0];
  const auto& c001 = table[next_b];
  const auto& c010 = table[next_g];
  const auto& c011 = table[next_g + next_b];
  const auto& c100 = table[next_r];
  const auto& c101 = table[next_r + next_b];
  const auto& c110 = table[next_r + next_g];
  const auto& c111 = table[next_r + next_g + next_b];

  const auto lerp3 = [&](auto ColorPixel::*channel) {
    const auto c00 = c000.*channel * (1.0 - fb) + c001.*channel * fb;
    const auto c01 = c010.*channel * (1.0 - fb) + c011.*channel * fb;
    const auto c10 = c100.*channel * (1.0 - fb) + c101.*channel * fb;
    const auto c11 = c110.*channel * (1.0 - fb) + c111.*channel * fb;
    const auto c0 = c00 * (1.0 - fg) + c01 * fg;
    const auto c1 = c10 * (1.0 - fg) + c11 * fg;
    return c0 * (1.0 - fr) + c1 * fr;
  };

  auto result = ColorPixel{
      .red = static_cast<float>(lerp3(&ColorPixel::red)),
      .green = static_cast<float>(lerp3(&ColorPixel::green)),
      .blue = static_cast<float>(lerp3(&ColorPixel::blue)),
      .alpha = pixel.alpha,
  };

  const auto red_green_diff = std::abs(pixel.red - pixel.green);
  const auto green_blue_diff = std::abs(pixel.green - pixel.blue);
  const auto red_blue_diff = std::abs(pixel.red - pixel.blue);
  const auto max_channel_diff =
      std::max(red_green_diff, std::max(green_blue_diff, red_blue_diff));
  if (max_channel_diff < 1e-9) {
    const auto average = (result.red + result.green + result.blue) / 3.0;
    result.red = static_cast<float>(average);
    result.green = static_cast<float>(average);
    result.blue = static_cast<float>(average);
  }

  return result;
}

}  // namespace

ColorPixel apply_lut(const ColorLut3D& lut, const ColorPixel& pixel) {
  if (!lut.valid || !color_lut_shape_valid(lut)) {
    return pixel;
  }
  return apply_lut_unchecked(lut, pixel, lut_has_unit_domain(lut));
}

ColorPixel apply_user_luts_to_pixel(
    const ColorPixel& input,
    const std::vector<CompiledUserLut>& user_luts) {
  auto current = input;
  for (const auto& runtime_lut : user_luts) {
    const auto source = current;
    auto lut_input = apply_lut_rgb_matrix(source, runtime_lut.working_to_input);
    lut_input.red = static_cast<float>(encode_lut_transfer(
        static_cast<double>(lut_input.red), runtime_lut.input_transfer));
    lut_input.green = static_cast<float>(encode_lut_transfer(
        static_cast<double>(lut_input.green), runtime_lut.input_transfer));
    lut_input.blue = static_cast<float>(encode_lut_transfer(
        static_cast<double>(lut_input.blue), runtime_lut.input_transfer));
    auto lut_output = apply_lut(runtime_lut.lut, lut_input);
    lut_output.red = static_cast<float>(decode_lut_transfer(
        static_cast<double>(lut_output.red), runtime_lut.output_transfer));
    lut_output.green = static_cast<float>(decode_lut_transfer(
        static_cast<double>(lut_output.green), runtime_lut.output_transfer));
    lut_output.blue = static_cast<float>(decode_lut_transfer(
        static_cast<double>(lut_output.blue), runtime_lut.output_transfer));
    const auto graded =
        apply_lut_rgb_matrix(lut_output, runtime_lut.output_to_working);
    current = ColorPixel{
        .red = static_cast<float>(
            std::lerp(source.red, graded.red, runtime_lut.strength)),
        .green = static_cast<float>(
            std::lerp(source.green, graded.green, runtime_lut.strength)),
        .blue = static_cast<float>(
            std::lerp(source.blue, graded.blue, runtime_lut.strength)),
        .alpha = source.alpha,
    };
  }
  return current;
}

}  // namespace photo_editor::pipeline
