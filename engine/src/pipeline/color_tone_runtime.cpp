#include "color_tone_runtime.h"

#include "color_math_runtime.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <memory>
#include <mutex>
#include <span>

namespace photo_editor::pipeline {

namespace {

constexpr double kMiddleGray = 0.18;

struct ToneAnchor final {
  double input_stops = 0.0;
  double output_stops = 0.0;
};

[[nodiscard]] double gentle_signed_response(double control) {
  const auto clamped = std::clamp(control, -1.0, 1.0);
  return std::copysign(std::pow(std::abs(clamped), 1.18), clamped);
}

[[nodiscard]] double interpolate_tone_anchors(
    double stops,
    std::span<const ToneAnchor> anchors) {
  if (anchors.empty()) {
    return stops;
  }
  if (stops <= anchors.front().input_stops) {
    return stops +
           (anchors.front().output_stops - anchors.front().input_stops);
  }
  if (stops >= anchors.back().input_stops) {
    return stops +
           (anchors.back().output_stops - anchors.back().input_stops) * 0.72;
  }

  for (std::size_t index = 0; index + 1 < anchors.size(); ++index) {
    const auto& left = anchors[index];
    const auto& right = anchors[index + 1];
    if (stops < left.input_stops || stops > right.input_stops) {
      continue;
    }
    const auto span =
        std::max(1.0e-6, right.input_stops - left.input_stops);
    const auto t =
        std::clamp((stops - left.input_stops) / span, 0.0, 1.0);
    const auto eased = t * t * (3.0 - 2.0 * t);
    const auto left_delta = left.output_stops - left.input_stops;
    const auto right_delta = right.output_stops - right.input_stops;
    return stops + std::lerp(left_delta, right_delta, eased);
  }
  return stops;
}

[[nodiscard]] double apply_legacy_anchor_tone_curve(
    double value,
    const ColorAdjustmentState& state) {
  if (std::abs(state.contrast) <= 1.0e-9 &&
      std::abs(state.highlights) <= 1.0e-9 &&
      std::abs(state.shadows) <= 1.0e-9 &&
      std::abs(state.whites) <= 1.0e-9 &&
      std::abs(state.blacks) <= 1.0e-9 &&
      std::abs(state.brightness) <= 1.0e-9 &&
      std::abs(state.zone_deep_shadows) <= 1.0e-9 &&
      std::abs(state.zone_shadows) <= 1.0e-9 &&
      std::abs(state.zone_midtones) <= 1.0e-9 &&
      std::abs(state.zone_lights) <= 1.0e-9 &&
      std::abs(state.zone_highlights) <= 1.0e-9) {
    return value;
  }
  const auto safe_value = std::max(1.0e-8, value);
  const auto input_stops = std::log2(safe_value / kMiddleGray);
  const auto response = [](double control) {
    return gentle_signed_response(clamp_signed_percent(control) / 100.0);
  };
  const auto contrast = response(state.contrast);
  const auto highlights = response(state.highlights);
  const auto shadows = response(state.shadows);
  const auto whites = response(state.whites);
  const auto blacks = response(state.blacks);
  const auto brightness = response(state.brightness);

  constexpr std::array<double, 9> kInputStops = {
      -8.0, -5.25, -3.20, -1.45, 0.0, 1.35, 2.55, 4.25, 6.50};
  std::array<ToneAnchor, 9> anchors{};
  for (std::size_t index = 0; index < anchors.size(); ++index) {
    const auto normalized_stop = kInputStops[index] / 1.50;
    const auto contrast_offset =
        contrast >= 0.0
            ? contrast * 0.65 * std::tanh(kInputStops[index] / 1.55)
            : contrast * 0.85 * std::tanh(kInputStops[index] / 1.20) /
                  (1.0 + normalized_stop * normalized_stop);
    anchors[index] = ToneAnchor{
        .input_stops = kInputStops[index],
        .output_stops = kInputStops[index] + contrast_offset,
    };
  }

  const auto black_endpoint_strength = blacks >= 0.0 ? 1.55 : 2.65;
  const auto black_deep_strength = blacks >= 0.0 ? 1.20 : 2.50;
  const auto black_low_strength = blacks >= 0.0 ? 0.62 : 3.45;
  const auto black_transition_strength = blacks >= 0.0 ? 0.08 : 0.28;
  anchors[0].output_stops += blacks * black_endpoint_strength;
  anchors[1].output_stops += blacks * black_deep_strength;
  anchors[2].output_stops += blacks * black_low_strength + shadows * 0.42;
  anchors[3].output_stops +=
      shadows * 1.14 + blacks * black_transition_strength;
  anchors[4].output_stops += brightness * 0.90;
  anchors[5].output_stops += brightness * 0.65 + highlights * 0.68;
  anchors[6].output_stops += highlights * 1.04 + whites * 0.18;
  anchors[7].output_stops += whites * 0.82 + highlights * 0.16;
  anchors[8].output_stops += whites * 1.12;

  const auto zone_offset = tone_zone_exposure_delta(input_stops, state) * 0.75;
  anchors[1].output_stops += zone_offset * 0.40;
  anchors[2].output_stops += zone_offset * 0.68;
  anchors[3].output_stops += zone_offset * 0.82;
  anchors[4].output_stops += zone_offset * 0.74;
  anchors[5].output_stops += zone_offset * 0.68;
  anchors[6].output_stops += zone_offset * 0.58;
  anchors[7].output_stops += zone_offset * 0.34;

  constexpr auto kMinimumStopSeparation = 0.18;
  for (std::size_t index = 1; index < anchors.size(); ++index) {
    anchors[index].output_stops = std::max(
        anchors[index].output_stops,
        anchors[index - 1].output_stops + kMinimumStopSeparation);
  }
  return kMiddleGray *
         std::pow(2.0, interpolate_tone_anchors(input_stops, anchors));
}

[[nodiscard]] double apply_monotone_log_luminance_tone(
    double value,
    const ColorAdjustmentState& state) {
  if (std::abs(state.contrast) <= 1.0e-9 &&
      std::abs(state.highlights) <= 1.0e-9 &&
      std::abs(state.shadows) <= 1.0e-9 &&
      std::abs(state.whites) <= 1.0e-9 &&
      std::abs(state.blacks) <= 1.0e-9 &&
      std::abs(state.brightness) <= 1.0e-9) {
    return value;
  }

  auto stops = std::log2(std::max(1.0e-8, value) / kMiddleGray);
  const auto response = [](double control) {
    return std::abs(control) <= 1.0e-9
               ? 0.0
               : gentle_signed_response(
                     clamp_signed_percent(control) / 100.0);
  };
  const auto contrast = response(state.contrast);
  const auto shadows = response(state.shadows);
  const auto highlights = response(state.highlights);
  const auto blacks = response(state.blacks);
  const auto whites = response(state.whites);
  const auto brightness = response(state.brightness);

  if (std::abs(contrast) > 1.0e-12) {
    const auto normalized_extreme = stops / 2.0;
    stops += contrast * std::tanh(stops / 1.25) /
             (1.0 + normalized_extreme * normalized_extreme);
  }
  if (std::abs(shadows) > 1.0e-12) {
    stops += shadows * 0.54 * bell_weight(stops, -1.85, 1.85);
  }
  if (std::abs(highlights) > 1.0e-12) {
    stops += highlights * 0.54 * bell_weight(stops, 1.95, 1.85);
  }
  if (std::abs(blacks) > 1.0e-12) {
    stops += blacks * 0.58 * bell_weight(stops, -3.60, 2.75);
  }
  if (std::abs(whites) > 1.0e-12) {
    stops += whites * 0.58 * bell_weight(stops, 3.60, 3.0);
  }
  if (std::abs(brightness) > 1.0e-12) {
    stops += brightness * 0.90 * bell_weight(stops, 0.35, 4.0);
  }
  return kMiddleGray * std::exp2(stops);
}

}  // namespace

bool uses_legacy_fixed_anchor_tone_runtime(
    PhotographicRenderingModel model) noexcept {
  return model == PhotographicRenderingModel::LegacyOklchV1;
}

bool color_tone_stage_is_identity(
    const ColorAdjustmentState& state) noexcept {
  return photographic_rendering_model_uses_aces2(
             state.color_pipeline.photographic_rendering_model) &&
         state.color_pipeline.render_intent == RenderIntent::Perceptual &&
         state.contrast == 0.0 &&
         state.highlights == 0.0 &&
         state.shadows == 0.0 &&
         state.whites == 0.0 &&
         state.blacks == 0.0 &&
         state.brightness == 0.0 &&
         state.hdr_local_tone_mapping == 0.0 &&
         state.filmic_shoulder == 0.0 &&
         state.filmic_toe == 0.0 &&
         state.adaptive_contrast == 0.0 &&
         state.levels_black_point == 0.0 &&
         state.levels_gamma == 0.0 &&
         state.levels_white_point == 0.0 &&
         state.zone_deep_shadows == 0.0 &&
         state.zone_shadows == 0.0 &&
         state.zone_midtones == 0.0 &&
         state.zone_lights == 0.0 &&
         state.zone_highlights == 0.0;
}

double apply_photographic_tone_runtime(
    double value,
    const ColorAdjustmentState& state) {
  if (uses_legacy_fixed_anchor_tone_runtime(
          state.color_pipeline.photographic_rendering_model)) {
    return apply_legacy_anchor_tone_curve(value, state);
  }
  return apply_monotone_log_luminance_tone(value, state);
}

double tone_zone_exposure_delta(
    double stops,
    const ColorAdjustmentState& state) {
  auto delta = 0.0;
  delta += bell_weight(stops, -3.50, 1.45) *
           (clamp_signed_percent(state.zone_deep_shadows) / 100.0) * 0.95;
  delta += bell_weight(stops, -1.70, 1.35) *
           (clamp_signed_percent(state.zone_shadows) / 100.0) * 0.90;
  delta += bell_weight(stops, 0.00, 1.50) *
           (clamp_signed_percent(state.zone_midtones) / 100.0) * 0.82;
  delta += bell_weight(stops, 1.70, 1.35) *
           (clamp_signed_percent(state.zone_lights) / 100.0) * 0.86;
  delta += bell_weight(stops, 3.30, 1.55) *
           (clamp_signed_percent(state.zone_highlights) / 100.0) * 0.90;
  return delta;
}

namespace {

[[nodiscard]] double apply_filmic_toe(double value, double amount) {
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1.0e-6) {
    return value;
  }
  const auto safe_value = std::max(0.0, value);
  const auto shadow_weight =
      1.0 - smoothstep(
                kMiddleGray * 0.35,
                kMiddleGray * 2.0,
                safe_value);
  if (shadow_weight <= 1.0e-6) {
    return safe_value;
  }
  if (control > 0.0) {
    const auto lifted = std::sqrt(safe_value * kMiddleGray);
    return std::lerp(
        safe_value, lifted, control * shadow_weight * 0.72);
  }
  const auto crushed =
      safe_value * (1.0 - (-control) * 0.58 * shadow_weight);
  return std::max(0.0, crushed);
}

[[nodiscard]] double apply_adaptive_contrast(
    double value,
    double amount) {
  const auto control = clamp_signed_percent(amount) / 100.0;
  if (std::abs(control) <= 1.0e-6) {
    return value;
  }
  const auto safe_value = std::max(1.0e-6, value);
  const auto stops = std::log2(safe_value / kMiddleGray);
  const auto midtone_weight =
      1.0 - smoothstep(0.85, 3.35, std::abs(stops));
  if (midtone_weight <= 1.0e-6) {
    return value;
  }
  const auto slope = std::clamp(
      1.0 + control * 0.42 * midtone_weight, 0.58, 1.48);
  const auto contrasted =
      kMiddleGray * std::pow(safe_value / kMiddleGray, slope);
  const auto detail_weight = std::clamp(
      midtone_weight * (0.42 + std::abs(control) * 0.58),
      0.0,
      1.0);
  return std::lerp(safe_value, contrasted, detail_weight);
}

[[nodiscard]] double apply_levels(
    double value,
    const ColorAdjustmentState& state) {
  const auto black_control =
      std::clamp(state.levels_black_point / 100.0, 0.0, 1.0);
  const auto white_control =
      std::clamp(state.levels_white_point / 100.0, 0.0, 1.0);
  const auto gamma_control =
      clamp_signed_percent(state.levels_gamma) / 100.0;
  if (black_control <= 1.0e-6 && white_control <= 1.0e-6 &&
      std::abs(gamma_control) <= 1.0e-6) {
    return value;
  }
  const auto input_black = black_control * 0.95;
  const auto input_white =
      std::max(input_black + 0.01, 1.0 - white_control * 0.95);
  const auto normalized = std::max(
      0.0, (value - input_black) / (input_white - input_black));
  const auto gamma = std::pow(2.0, -gamma_control * 1.45);
  return std::pow(normalized, gamma);
}

[[nodiscard]] double apply_zone_exposure(
    double adjusted_value,
    double source_value,
    const ColorAdjustmentState& state) {
  if (std::abs(state.zone_deep_shadows) <= 1.0e-9 &&
      std::abs(state.zone_shadows) <= 1.0e-9 &&
      std::abs(state.zone_midtones) <= 1.0e-9 &&
      std::abs(state.zone_lights) <= 1.0e-9 &&
      std::abs(state.zone_highlights) <= 1.0e-9) {
    return adjusted_value;
  }
  const auto safe_value = std::max(1.0e-8, source_value);
  const auto stops = std::log2(safe_value / kMiddleGray);
  const auto delta = tone_zone_exposure_delta(stops, state) * 1.15;
  return std::max(
      0.0, adjusted_value * std::pow(2.0, delta));
}

struct ToneLuminanceTableKey final {
  PhotographicRenderingModel rendering_model =
      PhotographicRenderingModel::Aces2OcioV1;
  std::array<double, 16> controls{};

  [[nodiscard]] bool operator==(
      const ToneLuminanceTableKey&) const = default;
};

[[nodiscard]] ToneLuminanceTableKey tone_luminance_table_key(
    const ColorAdjustmentState& state) {
  return ToneLuminanceTableKey{
      .rendering_model =
          state.color_pipeline.photographic_rendering_model,
      .controls = {
          state.contrast,
          state.highlights,
          state.shadows,
          state.whites,
          state.blacks,
          state.brightness,
          state.filmic_toe,
          state.adaptive_contrast,
          state.levels_black_point,
          state.levels_gamma,
          state.levels_white_point,
          state.zone_deep_shadows,
          state.zone_shadows,
          state.zone_midtones,
          state.zone_lights,
          state.zone_highlights,
      },
  };
}

[[nodiscard]] ToneLuminanceTable build_tone_luminance_table(
    const ColorAdjustmentState& state) {
  ToneLuminanceTable table;
  table.samples.resize(
      static_cast<std::size_t>(ToneLuminanceTable::kSampleCount));
  for (int index = 0; index < ToneLuminanceTable::kSampleCount; ++index) {
    const auto stops =
        ToneLuminanceTable::kMinimumStops +
        static_cast<double>(index) * table.stop_step;
    table.samples[static_cast<std::size_t>(index)] =
        apply_global_tone(kMiddleGray * std::exp2(stops), state);
  }
  return table;
}

}  // namespace

double apply_global_tone(
    double value,
    const ColorAdjustmentState& state) {
  const auto safe_value = std::max(0.0, value);
  auto adjusted = apply_photographic_tone_runtime(safe_value, state);
  adjusted = apply_zone_exposure(adjusted, safe_value, state);
  adjusted = apply_adaptive_contrast(adjusted, state.adaptive_contrast);
  adjusted = apply_filmic_toe(adjusted, state.filmic_toe);
  return apply_levels(adjusted, state);
}

std::shared_ptr<const ToneLuminanceTable> acquire_tone_luminance_table(
    const ColorAdjustmentState& state,
    ColorAdjustmentTelemetry* telemetry) {
  struct CacheEntry final {
    ToneLuminanceTableKey key;
    std::shared_ptr<const ToneLuminanceTable> table;
  };
  struct ToneTableCache final {
    std::mutex mutex;
    std::deque<CacheEntry> entries;
  };
  constexpr std::size_t kMaximumCachedToneTables = 4;
  static ToneTableCache cache;

  const auto key = tone_luminance_table_key(state);
  std::lock_guard lock(cache.mutex);
  const auto match = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const CacheEntry& entry) { return entry.key == key; });
  if (match != cache.entries.end()) {
    auto table = match->table;
    if (match != cache.entries.begin()) {
      auto entry = std::move(*match);
      cache.entries.erase(match);
      cache.entries.push_front(std::move(entry));
    }
    if (telemetry != nullptr) {
      telemetry->tone_table_cache_hit_count += 1;
    }
    return table;
  }

  const auto build_started = std::chrono::steady_clock::now();
  auto table = std::make_shared<const ToneLuminanceTable>(
      build_tone_luminance_table(state));
  cache.entries.push_front(CacheEntry{.key = key, .table = table});
  if (cache.entries.size() > kMaximumCachedToneTables) {
    cache.entries.pop_back();
  }
  if (telemetry != nullptr) {
    telemetry->tone_table_cache_miss_count += 1;
    telemetry->tone_table_build_ms +=
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - build_started)
            .count();
  }
  return table;
}

double evaluate_tone_luminance_table(
    const ToneLuminanceTable& table,
    double value,
    const ColorAdjustmentState& state) {
  if (!table.ready() || value <= 0.0) {
    return apply_global_tone(value, state);
  }
  const auto stops =
      std::log2(std::max(1.0e-8, value) / kMiddleGray);
  if (stops <= ToneLuminanceTable::kMinimumStops ||
      stops >= ToneLuminanceTable::kMaximumStops) {
    return apply_global_tone(value, state);
  }
  const auto position =
      (stops - ToneLuminanceTable::kMinimumStops) / table.stop_step;
  const auto lower = static_cast<int>(position);
  const auto fraction = position - static_cast<double>(lower);
  return std::lerp(
      table.samples[static_cast<std::size_t>(lower)],
      table.samples[static_cast<std::size_t>(lower + 1)],
      fraction);
}

}  // namespace photo_editor::pipeline
