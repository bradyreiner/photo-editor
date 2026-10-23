#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "../engine/src/pipeline/color_working_space_runtime.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace photo_editor::pipeline;

constexpr std::array<std::string_view, 7> kProfiles{
    "Linear sRGB", "Linear Display P3", "Linear Adobe RGB",
    "Linear ProPhoto RGB", "Linear Rec.2020", "Linear ACEScg",
    "Linear ACES2065-1"};
constexpr std::array<std::string_view, 4> kMixerModels{
    "OKLab", "HSV", "LCH", "HSL"};
constexpr std::array<double, 8> kHueCenters{
    0.0, 0.09, 0.15, 0.32, 0.50, 0.64, 0.78, 0.88};
constexpr std::array<std::string_view, 8> kChannels{
    "Reds", "Oranges", "Yellows", "Greens", "Aquas", "Blues",
    "Purples", "Magentas"};
constexpr float kAlpha = 0.37F;
constexpr double kGainLimit = 2.0;

[[nodiscard]] bool check(bool condition, std::string_view message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

[[nodiscard]] ColorAdjustmentState state_for(
    std::string_view profile, std::string_view model,
    PhotographicRenderingModel renderer =
        PhotographicRenderingModel::Aces2OcioV1) {
  ColorAdjustmentState state;
  state.input_transform.raw_input_transform_enabled = false;
  state.color_pipeline.working_profile =
      working_color_profile_from_name(profile);
  state.color_pipeline.photographic_rendering_model = renderer;
  state.color_mixer_model = std::string(model);
  return state;
}

[[nodiscard]] ColorPixel convert_space(
    const ColorPixel& pixel, const PipelineColorContext& from,
    const PipelineColorContext& to) {
  return apply_rgb_matrix(
      apply_rgb_matrix(pixel, from.rgb_to_xyz_d65), to.xyz_d65_to_rgb);
}

[[nodiscard]] double srgb_to_linear(double value) {
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

[[nodiscard]] ColorPixel decode_srgb8(
    const std::array<int, 3>& code, float alpha = kAlpha) {
  return {
      static_cast<float>(srgb_to_linear(static_cast<double>(code[0]) / 255.0)),
      static_cast<float>(srgb_to_linear(static_cast<double>(code[1]) / 255.0)),
      static_cast<float>(srgb_to_linear(static_cast<double>(code[2]) / 255.0)),
      alpha};
}

struct Lab final {
  double lightness;
  double a;
  double b;
};

// Ottosson's published OKLab equations for linear sRGB. This oracle is kept
// separate from the engine's working-space and OKLab conversion routines.
[[nodiscard]] Lab published_oklab(const ColorPixel& linear_srgb) {
  const auto red = static_cast<double>(linear_srgb.red);
  const auto green = static_cast<double>(linear_srgb.green);
  const auto blue = static_cast<double>(linear_srgb.blue);
  const auto l = std::cbrt(0.4122214708 * red + 0.5363325363 * green +
                           0.0514459929 * blue);
  const auto m = std::cbrt(0.2119034982 * red + 0.6806995451 * green +
                           0.1073969566 * blue);
  const auto s = std::cbrt(0.0883024619 * red + 0.2817188376 * green +
                           0.6299787005 * blue);
  return {
      0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
      1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
      0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s};
}

[[nodiscard]] double distance(const Lab& left, const Lab& right) {
  const auto dl = left.lightness - right.lightness;
  const auto da = left.a - right.a;
  const auto db = left.b - right.b;
  return std::sqrt(dl * dl + da * da + db * db);
}

[[nodiscard]] double scatter(std::span<const Lab> values) {
  if (values.empty()) return 0.0;
  Lab mean{0.0, 0.0, 0.0};
  for (const auto& value : values) {
    mean.lightness += value.lightness;
    mean.a += value.a;
    mean.b += value.b;
  }
  const auto count = static_cast<double>(values.size());
  mean.lightness /= count;
  mean.a /= count;
  mean.b /= count;
  double sum = 0.0;
  for (const auto& value : values) {
    const auto d = distance(value, mean);
    sum += d * d;
  }
  return std::sqrt(sum / count);
}

[[nodiscard]] std::array<double, 3> hsv_to_rgb(
    double hue, double saturation, double value) {
  const auto h = hue * 6.0;
  const auto chroma = value * saturation;
  const auto x = chroma * (1.0 - std::abs(std::fmod(h, 2.0) - 1.0));
  const auto m = value - chroma;
  std::array<double, 3> rgb{};
  switch (static_cast<int>(h) % 6) {
    case 0: rgb = {chroma, x, 0.0}; break;
    case 1: rgb = {x, chroma, 0.0}; break;
    case 2: rgb = {0.0, chroma, x}; break;
    case 3: rgb = {0.0, x, chroma}; break;
    case 4: rgb = {x, 0.0, chroma}; break;
    default: rgb = {chroma, 0.0, x}; break;
  }
  for (auto& component : rgb) component += m;
  return rgb;
}

[[nodiscard]] ColorPixel display_hsv_to_linear_rgb(
    double hue, double saturation, double value) {
  const auto encoded = hsv_to_rgb(hue, saturation, value);
  return {
      static_cast<float>(srgb_to_linear(encoded[0])),
      static_cast<float>(srgb_to_linear(encoded[1])),
      static_cast<float>(srgb_to_linear(encoded[2])), kAlpha};
}

[[nodiscard]] std::uint32_t next_noise(std::uint32_t* state) {
  *state ^= *state << 13U;
  *state ^= *state >> 17U;
  *state ^= *state << 5U;
  return *state;
}

[[nodiscard]] std::vector<ColorPixel> low_chroma_noise_patch(
    double hue, std::size_t count, const PipelineColorContext& working,
    const PipelineColorContext& srgb) {
  const auto base = hsv_to_rgb(hue, 2.0 / 46.0, 46.0 / 255.0);
  std::array<int, 3> code{};
  for (std::size_t component = 0; component < code.size(); ++component) {
    code[component] = static_cast<int>(std::lround(base[component] * 255.0));
  }
  std::uint32_t random = 0xD00DFEEDU;
  std::vector<ColorPixel> pixels;
  pixels.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    auto noisy = code;
    for (auto& component : noisy) {
      const auto offset = (next_noise(&random) & 1U) == 0U ? -1 : 1;
      component = std::clamp(component + offset, 0, 255);
    }
    pixels.push_back(convert_space(decode_srgb8(noisy), srgb, working));
  }
  return pixels;
}

[[nodiscard]] bool render_reference_and_compiled(
    const ColorAdjustmentState& state,
    const std::vector<ColorPixel>& input,
    std::vector<ColorPixel>* reference,
    std::vector<ColorPixel>* compiled) {
  *reference = input;
  if (!apply_color_look_stage(reference, static_cast<int>(input.size()), 1,
                              state)) {
    return check(false, "CPU reference look stage failed");
  }
  const auto program = compile_look_stage_program(state);
  compiled->clear();
  compiled->reserve(input.size());
  for (std::size_t index = 0; index < input.size(); ++index) {
    const auto actual = apply_compiled_look_stage_program(program, input[index]);
    const auto& expected = (*reference)[index];
    const auto error = std::max({
        std::abs(static_cast<double>(actual.red) - expected.red),
        std::abs(static_cast<double>(actual.green) - expected.green),
        std::abs(static_cast<double>(actual.blue) - expected.blue)});
    if (!std::isfinite(actual.red) || !std::isfinite(actual.green) ||
        !std::isfinite(actual.blue) || actual.alpha != input[index].alpha ||
        expected.alpha != input[index].alpha || error > 4.0e-5) {
      std::cerr << "CPU/reference parity failed at sample " << index
                << " RGB error=" << error << '\n';
      return false;
    }
    compiled->push_back(actual);
  }
  return true;
}

[[nodiscard]] std::vector<Lab> to_reference_lab(
    std::span<const ColorPixel> pixels,
    const PipelineColorContext& working,
    const PipelineColorContext& srgb) {
  std::vector<Lab> result;
  result.reserve(pixels.size());
  for (const auto& pixel : pixels) {
    result.push_back(published_oklab(convert_space(pixel, working, srgb)));
  }
  return result;
}

[[nodiscard]] bool test_two_code_pair(
    std::string_view profile, std::string_view model,
    const PipelineColorContext& srgb, bool include_hue) {
  auto state = state_for(profile, model);
  state.color_mixer_channels[0].luminance = 30.0;
  if (include_hue) state.color_mixer_channels[0].hue = 63.0;
  const auto working = make_pipeline_color_context(state);
  const auto first = decode_srgb8({46, 45, 45});
  const auto second = decode_srgb8({46, 44, 44});
  const auto input = std::vector<ColorPixel>{
      convert_space(first, srgb, working),
      convert_space(second, srgb, working)};
  std::vector<ColorPixel> reference;
  std::vector<ColorPixel> compiled;
  if (!render_reference_and_compiled(state, input, &reference, &compiled)) {
    return false;
  }
  const auto input_lab = to_reference_lab(input, working, srgb);
  const auto output_lab = to_reference_lab(reference, working, srgb);
  const auto input_distance = distance(input_lab[0], input_lab[1]);
  const auto output_distance = distance(output_lab[0], output_lab[1]);
  const auto gain = output_distance / input_distance;
  if (!std::isfinite(gain) || gain > kGainLimit) {
    std::cerr << "two-code noise gain " << gain << " exceeds " << kGainLimit
              << " for " << profile << '/' << model
              << (include_hue ? " H+63/L+30" : " L-only")
              << " (input ΔE_OK=" << input_distance
              << ", output ΔE_OK=" << output_distance << ")\n";
    return false;
  }
  return true;
}

[[nodiscard]] bool test_seeded_noise_matrix(
    const PipelineColorContext& srgb) {
  constexpr std::size_t kPatchSize = 1024U;
  for (const auto profile : kProfiles) {
    for (const auto model : kMixerModels) {
      for (std::size_t channel = 0; channel < kChannels.size(); ++channel) {
        auto state = state_for(profile, model);
        state.color_mixer_channels[channel].hue = 63.0;
        state.color_mixer_channels[channel].luminance = 30.0;
        const auto channel_working = make_pipeline_color_context(state);
        const auto input = low_chroma_noise_patch(
            kHueCenters[channel], kPatchSize, channel_working, srgb);
        std::vector<ColorPixel> reference;
        std::vector<ColorPixel> compiled;
        if (!render_reference_and_compiled(state, input, &reference, &compiled)) {
          return false;
        }
        const auto input_lab = to_reference_lab(input, channel_working, srgb);
        const auto output_lab = to_reference_lab(reference, channel_working, srgb);
        const auto input_scatter = scatter(input_lab);
        const auto output_scatter = scatter(output_lab);
        const auto gain = output_scatter / input_scatter;
        if (!std::isfinite(gain) || gain > kGainLimit) {
          std::cerr << "seeded low-chroma noise gain " << gain << " exceeds "
                    << kGainLimit << " for " << profile << '/' << model << '/'
                    << kChannels[channel] << " (" << input_scatter << " -> "
                    << output_scatter << ")\n";
          return false;
        }
      }
    }
  }
  return true;
}

[[nodiscard]] bool test_saturated_and_hue_response(
    std::string_view profile, std::string_view model, std::size_t channel,
    const PipelineColorContext& srgb) {
  const auto sample = display_hsv_to_linear_rgb(kHueCenters[channel], 1.0, 1.0);
  auto state = state_for(profile, model);
  state.color_mixer_channels[channel].luminance = 30.0;
  auto working = make_pipeline_color_context(state);
  const auto input = convert_space(sample, srgb, working);
  const auto initial_lab = published_oklab(sample);
  std::vector<ColorPixel> reference;
  std::vector<ColorPixel> compiled;
  if (!render_reference_and_compiled(state, {input}, &reference, &compiled)) {
    return false;
  }
  const auto output_srgb = convert_space(reference[0], working, srgb);
  const auto output_lab = published_oklab(output_srgb);
  const auto lightness_delta = output_lab.lightness - initial_lab.lightness;
  const auto initial_chroma = std::hypot(initial_lab.a, initial_lab.b);
  const auto expected_lightness_delta = model == "HSV" ? 0.042 :
      model == "LCH" ? 0.060 : 0.090;
  if (!(initial_chroma >= 0.12) ||
      std::abs(lightness_delta - expected_lightness_delta) > 1.0e-3 ||
      reference[0].alpha != kAlpha) {
    std::cerr << "saturated luminance response was attenuated for " << profile
              << '/' << model << '/' << kChannels[channel]
              << " C=" << initial_chroma << " ΔL=" << lightness_delta
              << " expected=" << expected_lightness_delta << '\n';
    return false;
  }

  state = state_for(profile, model);
  state.color_mixer_channels[channel].hue = 63.0;
  working = make_pipeline_color_context(state);
  const auto hue_input = convert_space(sample, srgb, working);
  if (!render_reference_and_compiled(state, {hue_input}, &reference, &compiled)) {
    return false;
  }
  const auto hue_output_srgb = convert_space(reference[0], working, srgb);
  const auto hue_delta = distance(initial_lab, published_oklab(hue_output_srgb));
  if (!(hue_delta > 0.04) || reference[0].alpha != kAlpha) {
    std::cerr << "saturated hue-only edit stopped responding for " << profile
              << '/' << model << '/' << kChannels[channel]
              << " ΔE_OK=" << hue_delta << '\n';
    return false;
  }
  return true;
}

[[nodiscard]] bool test_faint_red_exposure_homogeneity(
    std::string_view profile, std::string_view model,
    const PipelineColorContext& srgb) {
  auto state = state_for(profile, model);
  state.color_mixer_channels[0].luminance = 30.0;
  const auto working = make_pipeline_color_context(state);
  const auto base = decode_srgb8({46, 34, 34}, 1.0F);
  constexpr std::array<double, 4> target_peaks{1.0e-6, 1.0e-5, 1.0e-4, 1.0e-3};
  std::array<double, target_peaks.size()> relative_deltas{};
  for (std::size_t index = 0; index < target_peaks.size(); ++index) {
    const auto scale = target_peaks[index] / static_cast<double>(base.red);
    const ColorPixel faint{
        static_cast<float>(static_cast<double>(base.red) * scale),
        static_cast<float>(static_cast<double>(base.green) * scale),
        static_cast<float>(static_cast<double>(base.blue) * scale), kAlpha};
    const auto input = convert_space(faint, srgb, working);
    std::vector<ColorPixel> reference;
    std::vector<ColorPixel> compiled;
    if (!render_reference_and_compiled(state, {input}, &reference, &compiled)) {
      return false;
    }
    const auto initial = published_oklab(faint);
    const auto output = published_oklab(convert_space(reference[0], working, srgb));
    const auto delta = output.lightness - initial.lightness;
    relative_deltas[index] = delta / initial.lightness;
    if (!(delta > 0.0) || !std::isfinite(relative_deltas[index]) ||
        reference[0].alpha != kAlpha) {
      std::cerr << "genuine dark-red luminance response vanished at peak "
                << target_peaks[index] << " in " << profile << '/' << model
                << " ΔL=" << delta << '\n';
      return false;
    }
  }
  for (std::size_t index = 1; index < relative_deltas.size(); ++index) {
    const auto ratio = relative_deltas[index] / relative_deltas[0];
    if (std::abs(ratio - 1.0) > 0.035) {
      std::cerr << "faint-red ΔL/L changed with exposure in " << profile << '/'
                << model << " ratio=" << ratio << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_overlap_uses_original_chroma(
    const PipelineColorContext& srgb) {
  auto unsaturated = state_for("Linear sRGB", "OKLab");
  unsaturated.color_mixer_channels[0].luminance = 30.0;
  unsaturated.color_mixer_channels[1].luminance = 30.0;
  auto saturated = unsaturated;
  saturated.color_mixer_channels[0].saturation = -50.0;
  const auto rgb = hsv_to_rgb(0.045, 0.30, 0.24);
  const ColorPixel sample{
      static_cast<float>(srgb_to_linear(rgb[0])),
      static_cast<float>(srgb_to_linear(rgb[1])),
      static_cast<float>(srgb_to_linear(rgb[2])), kAlpha};
  const auto working = make_pipeline_color_context(unsaturated);
  const auto input = convert_space(sample, srgb, working);
  std::vector<ColorPixel> no_saturation;
  std::vector<ColorPixel> with_saturation;
  std::vector<ColorPixel> compiled;
  if (!render_reference_and_compiled(
          unsaturated, {input}, &no_saturation, &compiled) ||
      !render_reference_and_compiled(
          saturated, {input}, &with_saturation, &compiled)) {
    return false;
  }
  const auto before = published_oklab(sample).lightness;
  const auto no_sat_l = published_oklab(
      convert_space(no_saturation[0], working, srgb)).lightness;
  const auto with_sat_l = published_oklab(
      convert_space(with_saturation[0], working, srgb)).lightness;
  const auto no_sat_delta = no_sat_l - before;
  const auto with_sat_delta = with_sat_l - before;
  if (!(no_sat_delta > 0.001) ||
      std::abs(no_sat_delta - with_sat_delta) > 2.0e-5) {
    std::cerr << "overlapping channel luminance changed after saturation: "
              << no_sat_delta << " vs " << with_sat_delta << '\n';
    return false;
  }
  return true;
}

[[nodiscard]] bool test_luminance_direction_and_symmetry(
    std::string_view profile, std::string_view model,
    const PipelineColorContext& srgb) {
  const std::array samples{
      decode_srgb8({46, 45, 45}),
      ColorPixel{1.0F, 0.0F, 0.0F, kAlpha}};
  auto identity = state_for(profile, model);
  auto positive = identity;
  positive.color_mixer_channels[0].luminance = 30.0;
  auto negative = identity;
  negative.color_mixer_channels[0].luminance = -30.0;
  const auto working = make_pipeline_color_context(identity);

  for (std::size_t index = 0; index < samples.size(); ++index) {
    const auto input = convert_space(samples[index], srgb, working);
    std::vector<ColorPixel> baseline;
    std::vector<ColorPixel> raised;
    std::vector<ColorPixel> lowered;
    std::vector<ColorPixel> compiled;
    if (!render_reference_and_compiled(
            identity, {input}, &baseline, &compiled) ||
        !render_reference_and_compiled(
            positive, {input}, &raised, &compiled) ||
        !render_reference_and_compiled(
            negative, {input}, &lowered, &compiled)) {
      return false;
    }
    const auto base_lab = published_oklab(convert_space(baseline[0], working, srgb));
    const auto positive_lab = published_oklab(convert_space(raised[0], working, srgb));
    const auto negative_lab = published_oklab(convert_space(lowered[0], working, srgb));
    const auto positive_delta = positive_lab.lightness - base_lab.lightness;
    const auto negative_delta = base_lab.lightness - negative_lab.lightness;
    const auto positive_chroma = std::hypot(positive_lab.a, positive_lab.b);
    const auto negative_chroma = std::hypot(negative_lab.a, negative_lab.b);
    const auto base_hue = std::atan2(base_lab.b, base_lab.a);
    const auto positive_hue = std::atan2(positive_lab.b, positive_lab.a);
    const auto negative_hue = std::atan2(negative_lab.b, negative_lab.a);
    const auto positive_hue_delta = std::abs(std::remainder(
        positive_hue - base_hue, 2.0 * std::numbers::pi));
    const auto negative_hue_delta = std::abs(std::remainder(
        negative_hue - base_hue, 2.0 * std::numbers::pi));
    const auto base_chroma = std::hypot(base_lab.a, base_lab.b);
    const auto minimum_chroma = std::min(
        {base_chroma, positive_chroma, negative_chroma});
    // The independent a/b oracle has a fixed absolute tolerance of 2e-5 per
    // axis. The corresponding maximum chord error is sqrt(2) times that
    // bound, which implies this hue-angle error at the measured chroma. Keep
    // hue finite and within this geometric bound rather than assigning a
    // dimensionally unrelated fixed radian tolerance.
    constexpr auto kLabAxisTolerance = 2.0e-5;
    const auto maximum_hue_error = std::asin(std::min(
        1.0, std::sqrt(2.0) * kLabAxisTolerance / minimum_chroma));
    if (!(positive_delta > 0.0) || !(negative_delta > 0.0) ||
        !(negative_lab.lightness > 0.0) ||
        std::abs(positive_delta - negative_delta) > 2.0e-5 ||
        !(minimum_chroma > 0.0) || !std::isfinite(positive_hue_delta) ||
        !std::isfinite(negative_hue_delta) ||
        positive_hue_delta > maximum_hue_error ||
        negative_hue_delta > maximum_hue_error ||
        std::abs(positive_lab.a - base_lab.a) > kLabAxisTolerance ||
        std::abs(positive_lab.b - base_lab.b) > kLabAxisTolerance ||
        std::abs(negative_lab.a - base_lab.a) > kLabAxisTolerance ||
        std::abs(negative_lab.b - base_lab.b) > kLabAxisTolerance ||
        std::abs(positive_chroma - base_chroma) > 2.0e-5 ||
        std::abs(negative_chroma - base_chroma) > 2.0e-5 ||
        raised[0].alpha != kAlpha || lowered[0].alpha != kAlpha ||
        !std::isfinite(raised[0].red) || !std::isfinite(raised[0].green) ||
        !std::isfinite(raised[0].blue) || !std::isfinite(lowered[0].red) ||
        !std::isfinite(lowered[0].green) || !std::isfinite(lowered[0].blue)) {
      std::cerr << "positive/negative luminance response is asymmetric or "
                << "changed OKLab chroma in " << profile << '/' << model
                << (index == 0U ? " low-chroma" : " saturated")
                << " ΔL+=" << positive_delta << " ΔL-=" << negative_delta
                << " C+=" << positive_chroma << " C-=" << negative_chroma
                << " hueΔ+=" << positive_hue_delta
                << " hueΔ-=" << negative_hue_delta
                << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool test_legacy_unscaled_and_signed_hdr(
    std::string_view profile, std::string_view model,
    const PipelineColorContext& srgb) {
  const auto low = decode_srgb8({46, 45, 45});
  auto modern = state_for(profile, model);
  modern.color_mixer_channels[0].luminance = 30.0;
  const auto modern_working = make_pipeline_color_context(modern);
  const auto modern_input = convert_space(low, srgb, modern_working);
  std::vector<ColorPixel> modern_output;
  std::vector<ColorPixel> compiled;
  if (!render_reference_and_compiled(
          modern, {modern_input}, &modern_output, &compiled)) {
    return false;
  }
  const auto input_l = published_oklab(low).lightness;
  const auto modern_delta = published_oklab(
      convert_space(modern_output[0], modern_working, srgb)).lightness - input_l;

  const auto red = srgb_to_linear(46.0 / 255.0);
  const auto green = srgb_to_linear(45.0 / 255.0);
  const auto relative_spread = (red - green) / red;
  const auto confidence_t = std::clamp(
      (relative_spread - 0.02) / (0.12 - 0.02), 0.0, 1.0);
  const auto confidence = confidence_t * confidence_t *
                          (3.0 - 2.0 * confidence_t);
  const auto model_luminance_scale = model == "HSV" ? 0.14 :
      model == "LCH" ? 0.20 : 0.30;
  // Encoded [46,45,45] decodes to a display hue exactly at the Reds center,
  // so the independently computed hue weight is one. The historical absolute
  // L offset is confidence * amount * the model's saved response scale.
  const auto expected_legacy_delta = confidence * 0.30 * model_luminance_scale;
  const std::array legacy_renderers{
      PhotographicRenderingModel::LegacyOklchV1,
      PhotographicRenderingModel::Aces2HellwigJmhV1};
  for (const auto renderer : legacy_renderers) {
    auto legacy = state_for(profile, model, renderer);
    legacy.color_mixer_channels[0].luminance = 30.0;
    const auto legacy_working = make_pipeline_color_context(legacy);
    const auto legacy_input = convert_space(low, srgb, legacy_working);
    std::vector<ColorPixel> legacy_output;
    if (!render_reference_and_compiled(
            legacy, {legacy_input}, &legacy_output, &compiled)) {
      return false;
    }
    const auto legacy_delta = published_oklab(
        convert_space(legacy_output[0], legacy_working, srgb)).lightness - input_l;
    const auto tolerance = std::max(5.0e-5, expected_legacy_delta * 0.01);
    if (!(modern_delta > 0.0) ||
        std::abs(legacy_delta - expected_legacy_delta) > tolerance) {
      std::cerr << "legacy mixer L offset differs from independent expected "
                << "value in " << profile << '/' << model << " renderer="
                << photographic_rendering_model_name(renderer)
                << " actual=" << legacy_delta
                << " expected=" << expected_legacy_delta
                << " modern=" << modern_delta << '\n';
      return false;
    }
  }

  auto signed_state = state_for(profile, model);
  signed_state.color_mixer_channels[0].luminance = 1.0;
  const auto signed_working = make_pipeline_color_context(signed_state);
  const ColorPixel signed_srgb{1.0F, -0.01F, -0.01F, kAlpha};
  auto hdr_state = state_for(profile, model);
  hdr_state.color_mixer_channels[0].luminance = 30.0;
  const auto hdr_working = make_pipeline_color_context(hdr_state);
  const ColorPixel hdr_srgb{4.0F, 2.0F, 2.0F, kAlpha};
  const auto signed_input = convert_space(signed_srgb, srgb, signed_working);
  const auto hdr_input = convert_space(hdr_srgb, srgb, hdr_working);
  std::vector<ColorPixel> signed_outputs;
  std::vector<ColorPixel> hdr_outputs;
  if (!render_reference_and_compiled(
          signed_state, {signed_input}, &signed_outputs, &compiled) ||
      !render_reference_and_compiled(
          hdr_state, {hdr_input}, &hdr_outputs, &compiled)) {
    return false;
  }
  const auto signed_output = convert_space(signed_outputs[0], signed_working, srgb);
  const auto hdr_output = convert_space(hdr_outputs[0], hdr_working, srgb);
  if (!(signed_output.green < 0.0F) || !(hdr_output.red > 1.0F) ||
      signed_outputs[0].alpha != kAlpha || hdr_outputs[0].alpha != kAlpha ||
      !std::isfinite(signed_output.green) || !std::isfinite(hdr_output.red)) {
    std::cerr << "signed/HDR mixer samples were clipped or lost alpha in "
              << profile << '/' << model << '\n';
    return false;
  }
  return true;
}

[[nodiscard]] int verify_metal(const PipelineColorContext& srgb) {
  metal::MetalPipelineBackend backend;
  if (!backend.available()) {
    std::cerr << "Metal noise gain test: " << backend.unavailable_reason() << '\n';
    return backend.unavailable_reason() == "no_system_metal_device" ||
                   backend.unavailable_reason() == "metal_not_compiled"
               ? 77
               : EXIT_FAILURE;
  }
  constexpr std::size_t kPatchSize = 256U;
  for (const auto profile : kProfiles) {
    for (const auto model : kMixerModels) {
      auto state = state_for(profile, model);
      for (std::size_t channel = 0; channel < kChannels.size(); ++channel) {
        state.color_mixer_channels[channel].hue = 63.0;
        state.color_mixer_channels[channel].luminance = 30.0;
      }
      const auto working = make_pipeline_color_context(state);
      std::vector<ColorPixel> input;
      input.reserve(kPatchSize * kChannels.size());
      for (const auto hue : kHueCenters) {
        const auto patch = low_chroma_noise_patch(hue, kPatchSize, working, srgb);
        input.insert(input.end(), patch.begin(), patch.end());
      }
      std::vector<ColorPixel> expected = input;
      if (!apply_color_look_stage(&expected, static_cast<int>(input.size()), 1,
                                  state)) {
        return EXIT_FAILURE;
      }
      const auto program = compile_look_stage_program(state);
      const auto descriptor = RenderSurfaceDescriptor{
          .pixel_encoding = RenderPixelEncoding::SceneLinearFloat,
          .color_domain = RenderColorDomain::WorkingScene,
          .range = RenderRangeModel::SceneExtended,
          .alpha = RenderAlphaModel::Straight,
          .color_space_identity = std::string(profile),
          .transfer_identity = "linear",
          .width = static_cast<std::uint32_t>(input.size()),
          .height = 1U};
      ColorAdjustmentTelemetry telemetry;
      const auto surface = backend.publish_masks_and_color_lut_surface(
          input, static_cast<int>(input.size()), 1, state, {}, descriptor,
          &telemetry, {}, 1.0, false, nullptr, nullptr, &program);
      std::vector<ColorPixel> actual;
      if (!surface || !surface->copy_pixels(&actual) ||
          actual.size() != expected.size() ||
          telemetry.gpu_kernel_dispatch_count < 1U) {
        std::cerr << "Metal noise gain dispatch failed for " << profile << '/'
                  << model << ": " << telemetry.gpu_fallback_reason << '\n';
        return EXIT_FAILURE;
      }
      for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto error = std::max({
            std::abs(static_cast<double>(actual[index].red) - expected[index].red),
            std::abs(static_cast<double>(actual[index].green) - expected[index].green),
            std::abs(static_cast<double>(actual[index].blue) - expected[index].blue)});
        if (!std::isfinite(actual[index].red) ||
            !std::isfinite(actual[index].green) ||
            !std::isfinite(actual[index].blue) || error > 8.0e-5 ||
            actual[index].alpha != input[index].alpha) {
          std::cerr << "Metal/reference noise parity failed for " << profile
                    << '/' << model << " sample=" << index
                    << " RGB error=" << error << '\n';
          return EXIT_FAILURE;
        }
      }
      for (std::size_t channel = 0; channel < kChannels.size(); ++channel) {
        const auto offset = channel * kPatchSize;
        const auto source_lab = to_reference_lab(
            std::span<const ColorPixel>(input).subspan(offset, kPatchSize),
            working, srgb);
        const auto output_lab = to_reference_lab(
            std::span<const ColorPixel>(actual).subspan(offset, kPatchSize),
            working, srgb);
        const auto gain = scatter(output_lab) / scatter(source_lab);
        if (!std::isfinite(gain) || gain > kGainLimit) {
          std::cerr << "Metal seeded noise gain " << gain << " exceeds "
                    << kGainLimit << " for " << profile << '/' << model << '/'
                    << kChannels[channel] << '\n';
          return EXIT_FAILURE;
        }
      }
    }
  }
  std::cout << "Metal noise gain: 28 actual dispatches; 7 profiles, 4 models, "
               "8 hue patches each; RGB tolerance 8e-5\n";
  return EXIT_SUCCESS;
}

}  // namespace

int main(int argc, char** argv) {
  auto srgb_state = state_for("Linear sRGB", "OKLab");
  const auto srgb = make_pipeline_color_context(srgb_state);
  if (argc == 2 && std::string_view(argv[1]) == "--metal") {
    return verify_metal(srgb);
  }
  if (!check(argc == 1, "usage: color_mixer_noise_gain_test [--metal]")) {
    return EXIT_FAILURE;
  }

  for (const auto profile : kProfiles) {
    for (const auto model : kMixerModels) {
      if (!test_two_code_pair(profile, model, srgb, false) ||
          !test_two_code_pair(profile, model, srgb, true)) {
        return EXIT_FAILURE;
      }
      for (std::size_t channel = 0; channel < kChannels.size(); ++channel) {
        if (!test_saturated_and_hue_response(profile, model, channel, srgb)) {
          return EXIT_FAILURE;
        }
      }
      if (!test_faint_red_exposure_homogeneity(profile, model, srgb) ||
          !test_luminance_direction_and_symmetry(profile, model, srgb) ||
          !test_legacy_unscaled_and_signed_hdr(profile, model, srgb)) {
        return EXIT_FAILURE;
      }
    }
  }
  if (!test_seeded_noise_matrix(srgb) ||
      !test_overlap_uses_original_chroma(srgb)) {
    return EXIT_FAILURE;
  }
  std::cout << "CPU color mixer noise gain: 7 profiles, 4 models, 8 selector "
               "centers; pairwise/scatter gain <=2, faint response retained, "
               "saturated response unchanged\n";
  return EXIT_SUCCESS;
}
