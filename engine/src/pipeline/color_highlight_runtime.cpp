#include "color_highlight_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace photo_editor::pipeline {

struct HighlightReconstructionReferenceGrid final {
  struct Cell final {
    std::array<double, 3> chrominance{};
    double confidence = 0.0;
  };
  int width = 0;
  int height = 0;
  double source_area_x = 0.0;
  double source_area_y = 0.0;
  double source_area_width = 1.0;
  double source_area_height = 1.0;
  std::vector<Cell> cells;
};

namespace {

constexpr double kNormalizedClipThreshold = 0.995;
constexpr double kMaximumRecoveryGain = 1.25;
constexpr int kReferenceGridLongAxisCells = 64;

[[nodiscard]] bool finite_rgb(const ColorPixel& pixel) {
  return std::isfinite(pixel.red) &&
         std::isfinite(pixel.green) &&
         std::isfinite(pixel.blue);
}

[[nodiscard]] std::array<double, 3> rgb(const ColorPixel& pixel) {
  return {
      static_cast<double>(pixel.red),
      static_cast<double>(pixel.green),
      static_cast<double>(pixel.blue),
  };
}

[[nodiscard]] double opposed_channel_estimate(
    const std::array<double, 3>& channels,
    std::size_t channel) {
  const auto first = (channel + 1U) % 3U;
  const auto second = (channel + 2U) % 3U;
  // sqrt(a) * sqrt(b) avoids overflowing for finite HDR float samples.
  return std::sqrt(std::max(0.0, channels[first])) *
         std::sqrt(std::max(0.0, channels[second]));
}

[[nodiscard]] ColorPixel normalized_camera_pixel(
    const ColorPixel& pixel,
    const InputTransformSettings& settings) {
  const auto black = std::clamp(settings.raw_black_level, 0.0, 0.95);
  const auto white = std::clamp(settings.raw_white_level, black + 0.01, 4.0);
  const auto normalize = [&](float channel) {
    return static_cast<float>(std::max(
        0.0, (static_cast<double>(channel) - black) / (white - black)));
  };
  return {normalize(pixel.red), normalize(pixel.green), normalize(pixel.blue), pixel.alpha};
}

[[nodiscard]] bool valid_source_area(const InputTransformSettings& settings) {
  return std::isfinite(settings.source_area_x) &&
         std::isfinite(settings.source_area_y) &&
         std::isfinite(settings.source_area_width) &&
         std::isfinite(settings.source_area_height) &&
         settings.source_area_x >= 0.0 && settings.source_area_y >= 0.0 &&
         settings.source_area_width > 0.0 && settings.source_area_height > 0.0 &&
         settings.source_area_x + settings.source_area_width <= 1.0 + 1.0e-9 &&
         settings.source_area_y + settings.source_area_height <= 1.0 + 1.0e-9;
}

[[nodiscard]] std::array<double, 3> local_chrominance(
    const HighlightReconstructionAnalysis& analysis,
    double source_x,
    double source_y) {
  const auto& grid = analysis.local_reference_grid;
  if (!grid || !std::isfinite(source_x) || !std::isfinite(source_y)) return {};
  const auto x = (source_x - grid->source_area_x) / grid->source_area_width;
  const auto y = (source_y - grid->source_area_y) / grid->source_area_height;
  if (x < 0.0 || x > 1.0 || y < 0.0 || y > 1.0) return {};
  const auto gx = x * static_cast<double>(grid->width) - 0.5;
  const auto gy = y * static_cast<double>(grid->height) - 0.5;
  const auto left = static_cast<int>(std::floor(gx));
  const auto top = static_cast<int>(std::floor(gy));
  auto result = std::array<double, 3>{};
  auto squared_result = std::array<double, 3>{};
  auto weight_sum = 0.0;
  // Bilinear evidence uses at most four cells. No search grows with image size.
  for (int cy = top; cy <= top + 1; ++cy) {
    for (int cx = left; cx <= left + 1; ++cx) {
      if (cx < 0 || cx >= grid->width || cy < 0 || cy >= grid->height) continue;
      const auto& cell = grid->cells[static_cast<std::size_t>(cy) *
                                    static_cast<std::size_t>(grid->width) +
                                    static_cast<std::size_t>(cx)];
      const auto weight = (1.0 - std::abs(gx - static_cast<double>(cx))) *
                          (1.0 - std::abs(gy - static_cast<double>(cy))) *
                          cell.confidence;
      if (weight <= 0.0) continue;
      for (std::size_t channel = 0U; channel < result.size(); ++channel) {
        result[channel] += weight * cell.chrominance[channel];
        squared_result[channel] += weight * cell.chrominance[channel] * cell.chrominance[channel];
      }
      weight_sum += weight;
    }
  }
  // Weak/isolated evidence must not be amplified by renormalizing its weight.
  // A source coordinate reached through crop mapping may differ by one ULP
  // from the same full-source coordinate. Reserve only accumulated double
  // roundoff here, so an exact quarter-support boundary cannot flip recovery.
  constexpr auto kEvidenceRoundoff = 64.0 * std::numeric_limits<double>::epsilon();
  if (weight_sum + kEvidenceRoundoff < 0.25) return {};
  for (std::size_t channel = 0U; channel < result.size(); ++channel) {
    result[channel] /= weight_sum;
    if (squared_result[channel] / weight_sum - result[channel] * result[channel] >
        0.04 * 0.04 + kEvidenceRoundoff)
      return {};
  }
  return result;
}

}  // namespace

ColorPixel reconstruct_camera_neutral_highlight(
    const ColorPixel& input,
    const std::array<double, 3>& camera_neutral,
    double strength) {
  if (!finite_rgb(input) || !std::isfinite(strength) || strength <= 0.0 ||
      !std::all_of(camera_neutral.begin(), camera_neutral.end(), [](double value) {
        return std::isfinite(value) && value > 1.0e-6;
      })) return input;
  const auto channels = rgb(input);
  auto clipped = std::size_t{0};
  auto clipped_count = 0;
  for (std::size_t channel = 0; channel < channels.size(); ++channel) {
    if (channels[channel] < 0.0) return input;
    if (channels[channel] >= kNormalizedClipThreshold) {
      clipped = channel;
      ++clipped_count;
    }
  }
  // Camera-linear floats may also carry genuine HDR energy. Only a sample at
  // the normalized sensor boundary qualifies, never an above-range HDR value.
  if (clipped_count != 1 || channels[clipped] > 1.0 + 1.0e-6) return input;
  const auto first = (clipped + 1U) % 3U;
  const auto second = (clipped + 2U) % 3U;
  const auto a = channels[first] / camera_neutral[first];
  const auto b = channels[second] / camera_neutral[second];
  const auto lower = std::min(a, b);
  const auto upper = std::max(a, b);
  const auto clip_exposure = channels[clipped] / camera_neutral[clipped];
  // A saturated object cannot qualify merely by having one large channel.
  // Both independent unclipped channels must exceed that sensor's neutral
  // clipping exposure, and agree in white-balanced intensity within 20%.
  if (lower <= clip_exposure || upper <= 0.0 || lower / upper <= 0.80) return input;
  const auto confidence = std::clamp((lower / upper - 0.80) / 0.15, 0.0, 1.0);
  // The lower anchor retains the surviving blue/yellow cast. No distant image
  // color or invented scene-wide chrominance participates in this estimate.
  const auto estimate = std::min(lower * camera_neutral[clipped],
      channels[clipped] * 2.0);
  auto output = input;
  const auto value = static_cast<float>(channels[clipped] +
      (estimate - channels[clipped]) * std::clamp(strength, 0.0, 1.0) * confidence);
  if (clipped == 0U) output.red = value;
  else if (clipped == 1U) output.green = value;
  else output.blue = value;
  return output;
}

double highlight_reconstruction_strength(const ColorAdjustmentState& state) {
  if (!state.input_transform.raw_input_transform_enabled ||
      !state.input_transform.highlight_recovery_enabled ||
      state.input_transform.highlight_reconstruction_mode == "Disabled") {
    return 0.0;
  }
  return state.input_transform.highlight_reconstruction_mode == "Camera Default"
             ? 0.35
             : 0.70;
}

bool camera_linear_highlight_reconstruction_enabled(
    const InputTransformSettings& settings) {
  return settings.raw_input_transform_enabled &&
         settings.highlight_recovery_enabled &&
         settings.highlight_reconstruction_mode != "Disabled" &&
         settings.highlight_reconstruction_model == "camera-linear-opposed-v2";
}

double camera_linear_highlight_reconstruction_strength(
    const InputTransformSettings& settings,
    double highlight_headroom) {
  if (!camera_linear_highlight_reconstruction_enabled(settings) ||
      !std::isfinite(highlight_headroom)) return 0.0;
  const auto mode_strength = settings.highlight_reconstruction_mode == "Camera Default" ? 0.60 : 1.0;
  return mode_strength * std::clamp(1.50 - highlight_headroom, 0.50, 1.0);
}

HighlightReconstructionAnalysis analyze_highlight_reconstruction(
    std::span<const ColorPixel> pixels,
    int width,
    int height,
    const InputTransformSettings& settings,
    double highlight_headroom) {
  auto analysis = HighlightReconstructionAnalysis{};
  analysis.model = settings.highlight_reconstruction_model;
  analysis.active =
      camera_linear_highlight_reconstruction_enabled(settings) &&
      width > 0 && height > 0 &&
      static_cast<std::size_t>(width) <=
          std::numeric_limits<std::size_t>::max() /
              static_cast<std::size_t>(height) &&
      pixels.size() ==
          static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  if (!analysis.active) {
    return analysis;
  }

  auto correction_sums = std::array<double, 3>{0.0, 0.0, 0.0};
  for (const auto& pixel : pixels) {
    if (!finite_rgb(pixel)) {
      continue;
    }
    const auto channels = rgb(normalized_camera_pixel(pixel, settings));
    auto clipped = std::array<bool, 3>{false, false, false};
    auto clipped_count = 0U;
    for (auto channel = std::size_t{0U}; channel < channels.size(); ++channel) {
      clipped[channel] = channels[channel] >= kNormalizedClipThreshold;
      if (clipped[channel]) {
        ++analysis.clipped_channel_samples[channel];
        ++clipped_count;
      }
    }
    if (clipped_count > 0U) {
      ++analysis.clipped_pixels;
    }
    if (clipped_count >= 2U) {
      ++analysis.multi_channel_clipped_pixels;
    }
    if (clipped_count == 3U) {
      ++analysis.fully_clipped_pixels;
    }

    const auto positive_sum =
        std::max(0.0, channels[0]) +
        std::max(0.0, channels[1]) +
        std::max(0.0, channels[2]);
    if (clipped_count == 0U && positive_sum > 1.0e-9) {
      for (auto channel = std::size_t{0U}; channel < channels.size(); ++channel) {
        correction_sums[channel] +=
            std::max(0.0, channels[channel]) / positive_sum;
        ++analysis.correction_reference_samples[channel];
      }
    }
  }

  for (auto channel = std::size_t{0U}; channel < correction_sums.size(); ++channel) {
    const auto samples = analysis.correction_reference_samples[channel];
    analysis.chrominance_correction[channel] =
        samples == 0U
            ? 0.0
            : correction_sums[channel] / static_cast<double>(samples);
  }

  if (analysis.multi_channel_clipped_pixels == 0U) return analysis;
  // Spatial evidence is useful only when a pixel retains an unclipped channel.
  // Fully clipped samples have no trustworthy chromatic anchor; no reference
  // color, local or global, is allowed to manufacture one for them.
  if (analysis.multi_channel_clipped_pixels > analysis.fully_clipped_pixels) {
    auto grid = std::make_shared<HighlightReconstructionReferenceGrid>();
    if (valid_source_area(settings)) {
      grid->source_area_x = settings.source_area_x;
      grid->source_area_y = settings.source_area_y;
      grid->source_area_width = settings.source_area_width;
      grid->source_area_height = settings.source_area_height;
    }
    // Use one normalized lattice at every decode resolution. A pixel-sized
    // cell rule would let draft previews borrow color from a much wider part
    // of the photograph than full-resolution renders.
    // Cross-multiply the aspect ratio instead of dividing by a crop extent;
    // valid subnormal extents must not create inf/inf and an invalid int cast.
    const auto source_width = static_cast<double>(width) * grid->source_area_height;
    const auto source_height = static_cast<double>(height) * grid->source_area_width;
    const auto long_axis = std::max(source_width, source_height);
    grid->width = std::max(1, static_cast<int>(std::ceil(
        static_cast<double>(kReferenceGridLongAxisCells) * (source_width / long_axis) * grid->source_area_width)));
    grid->height = std::max(1, static_cast<int>(std::ceil(
        static_cast<double>(kReferenceGridLongAxisCells) * (source_height / long_axis) * grid->source_area_height)));
    const auto grid_size = static_cast<std::size_t>(grid->width) *
                           static_cast<std::size_t>(grid->height);
    struct ReferenceAccumulator final {
      std::array<double, 3> sum{};
      std::array<double, 3> squared_sum{};
      std::size_t samples = 0U;
    };
    auto accumulators = std::vector<ReferenceAccumulator>(grid_size);
    grid->cells.resize(grid_size);
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const auto index = static_cast<std::size_t>(y) *
                               static_cast<std::size_t>(width) +
                           static_cast<std::size_t>(x);
        const auto& source = pixels[index];
        if (!finite_rgb(source) || !std::isfinite(source.alpha) || source.alpha < 0.99F)
          continue;
        const auto channels = rgb(normalized_camera_pixel(source, settings));
        const auto low = std::min({channels[0], channels[1], channels[2]});
        const auto high = std::max({channels[0], channels[1], channels[2]});
        // Dark noise and nearly clipped samples cannot establish reliable
        // channel ratios. Keep only bright unsaturated camera evidence.
        if (low < 0.03 || high < 0.70 || high > 0.98) continue;
        const auto sum = channels[0] + channels[1] + channels[2];
        const auto cx = std::min(grid->width - 1,
            static_cast<int>((static_cast<double>(x) + 0.5) *
                             static_cast<double>(grid->width) / static_cast<double>(width)));
        const auto cy = std::min(grid->height - 1,
            static_cast<int>((static_cast<double>(y) + 0.5) *
                             static_cast<double>(grid->height) / static_cast<double>(height)));
        auto& cell = accumulators[static_cast<std::size_t>(cy) *
                                     static_cast<std::size_t>(grid->width) +
                                 static_cast<std::size_t>(cx)];
        for (std::size_t channel = 0U; channel < channels.size(); ++channel) {
          const auto chrominance = channels[channel] / sum;
          cell.sum[channel] += chrominance;
          cell.squared_sum[channel] += chrominance * chrominance;
        }
        ++cell.samples;
      }
    }
    for (std::size_t index = 0U; index < grid_size; ++index) {
      const auto& samples = accumulators[index];
      if (samples.samples < 4U) continue;
      auto& cell = grid->cells[index];
      auto maximum_variance = 0.0;
      for (std::size_t channel = 0U; channel < cell.chrominance.size(); ++channel) {
        const auto count = static_cast<double>(samples.samples);
        cell.chrominance[channel] = samples.sum[channel] / count;
        maximum_variance = std::max(maximum_variance,
            samples.squared_sum[channel] / count -
                cell.chrominance[channel] * cell.chrominance[channel]);
      }
      // Conflicting colors in one neighborhood reduce confidence to zero.
      cell.confidence = std::clamp(1.0 - maximum_variance / (0.04 * 0.04), 0.0, 1.0);
    }
    analysis.local_reference_grid = std::move(grid);
  }

  // Count the exact bounded operator after its evidence is complete. The old
  // one-pass count ran before chrominance correction was populated and could
  // report zero even while executing a substantial color change.
  const auto has_source_area = valid_source_area(settings);
  const auto area_x = has_source_area ? settings.source_area_x : 0.0;
  const auto area_y = has_source_area ? settings.source_area_y : 0.0;
  const auto area_width = has_source_area ? settings.source_area_width : 1.0;
  const auto area_height = has_source_area ? settings.source_area_height : 1.0;
  const auto reconstruction_strength = camera_linear_highlight_reconstruction_strength(settings, highlight_headroom);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto index = static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
      if (!finite_rgb(pixels[index])) continue;
      const auto input = normalized_camera_pixel(pixels[index], settings);
      const auto output = apply_camera_linear_highlight_reconstruction(
          input, reconstruction_strength, analysis,
          area_x + (static_cast<double>(x) + 0.5) / static_cast<double>(width) * area_width,
          area_y + (static_cast<double>(y) + 0.5) / static_cast<double>(height) * area_height);
      const auto before = rgb(input);
      const auto after = rgb(output);
      for (std::size_t channel = 0U; channel < before.size(); ++channel) {
        if (after[channel] > before[channel] + 1.0e-9)
          ++analysis.reconstructed_channel_samples;
      }
    }
  }
  return analysis;
}

ColorPixel apply_camera_linear_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const HighlightReconstructionAnalysis& analysis,
    double normalized_source_x,
    double normalized_source_y) {
  if (!analysis.active || !std::isfinite(strength) || strength <= 1.0e-9 || !finite_rgb(input)) {
    return input;
  }
  auto channels = rgb(input);
  auto clipped = std::array<bool, 3>{false, false, false};
  auto clipped_count = 0U;
  for (auto channel = std::size_t{0U}; channel < channels.size(); ++channel) {
    clipped[channel] = channels[channel] >= kNormalizedClipThreshold;
    clipped_count += clipped[channel] ? 1U : 0U;
  }
  // A lone high channel is valid saturated scene color. Reconstruction is
  // admitted only when two or more sensor channels reach the clip boundary.
  if (clipped_count < 2U) {
    return input;
  }

  const auto blend = std::clamp(strength, 0.0, 1.0);
  const auto chrominance = clipped_count == 2U
      ? local_chrominance(analysis, normalized_source_x, normalized_source_y)
      : std::array<double, 3>{};
  auto anchor = std::size_t{0U};
  if (clipped_count == 2U) {
    while (clipped[anchor]) ++anchor;
  }
  auto output = input;
  auto output_channels = std::array<float*, 3>{
      &output.red,
      &output.green,
      &output.blue,
  };
  for (auto channel = std::size_t{0U}; channel < channels.size(); ++channel) {
    if (!clipped[channel]) {
      continue;
    }
    auto estimate = opposed_channel_estimate(channels, channel);
    if (clipped_count == 2U && chrominance[anchor] > 1.0e-6) {
      estimate = std::max(estimate,
          channels[anchor] * chrominance[channel] / chrominance[anchor]);
    }
    estimate = std::min({estimate, channels[channel] * kMaximumRecoveryGain,
                         static_cast<double>(std::numeric_limits<float>::max())});
    if (estimate <= channels[channel]) {
      continue;
    }
    *output_channels[channel] = static_cast<float>(
        channels[channel] + (estimate - channels[channel]) * blend);
  }
  return output;
}

}  // namespace photo_editor::pipeline
