#include "color_highlight_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace photo_editor::pipeline {

namespace {

constexpr double kNormalizedClipThreshold = 0.995;

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
    std::size_t channel,
    const HighlightReconstructionAnalysis& analysis) {
  const auto first = (channel + 1U) % 3U;
  const auto second = (channel + 2U) % 3U;
  const auto correction = analysis.chrominance_correction[channel];
  if (analysis.correction_reference_samples[channel] > 0U &&
      correction > 1.0e-6 && correction < 1.0 - 1.0e-6) {
    return (channels[first] + channels[second]) * correction / (1.0 - correction);
  }
  return std::sqrt(
      std::max(0.0, channels[first]) *
      std::max(0.0, channels[second]));
}

}  // namespace

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

HighlightReconstructionAnalysis analyze_highlight_reconstruction(
    std::span<const ColorPixel> pixels,
    int width,
    int height,
    const InputTransformSettings& settings) {
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
    const auto channels = rgb(pixel);
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

    if (clipped_count >= 2U) {
      for (auto channel = std::size_t{0U}; channel < channels.size(); ++channel) {
        if (clipped[channel] &&
            opposed_channel_estimate(channels, channel, analysis) >
                channels[channel] + 1.0e-9) {
          ++analysis.reconstructed_channel_samples;
        }
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
  return analysis;
}

ColorPixel apply_camera_linear_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const HighlightReconstructionAnalysis& analysis) {
  if (!analysis.active || strength <= 1.0e-9 || !finite_rgb(input)) {
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
    const auto estimate = opposed_channel_estimate(channels, channel, analysis);
    if (estimate <= channels[channel]) {
      continue;
    }
    *output_channels[channel] = static_cast<float>(
        channels[channel] + (estimate - channels[channel]) * blend);
  }
  return output;
}

}  // namespace photo_editor::pipeline
