#include "photo_editor/pipeline/video_color_transform.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {
namespace {

// Sony technical summary:
// https://pro.sony/s3/cms-static-content/uploadfile/06/1237494271406.pdf
// S-Log3 is evaluated against 10-bit code values normalized to [0, 1].
constexpr double kVideoInputCodeMaximum = 1023.0;
// Sony lists S-Log2 CV anchors 90, 347, and 582 for 0%, 18%, and 90% reflection.
// The legal signal range follows the ACES/OCIO Sony transform definition,
// developed in collaboration with Sony.
constexpr double kSLog3LinearBreakpoint = 0.01125;
constexpr double kSLog3EncodedBreakpoint = 171.2102946929;
constexpr double kSLog3BlackCode = 95.0;
constexpr double kSLog3MiddleGrayCode = 420.0;
constexpr double kSLog3LogSlope = 261.5;
constexpr double kSLog3MiddleGrayReflection = 0.18;
constexpr double kSLog3LogOffset = 0.01;
constexpr double kSLog2LegalBlack = 64.0;
constexpr double kSLog2LegalWhite = 940.0;
constexpr double kSLog2BlackCode = 90.0;
constexpr double kSLog2LogSlope = 0.432699;
constexpr double kSLog2LogOffset = 0.616596 + 0.03;
constexpr double kSLog2LinearOffset = 0.030001222851889303;
constexpr double kSLog2LinearSlope = 3.53881278538813;
constexpr double kSLog2InputScale = 155.0 / (0.9 * 219.0);
constexpr std::size_t kVideoTransferCurveSize = 16385U;

[[nodiscard]] const std::vector<double>& slog3_decode_curve() {
  static const auto curve = [] {
    std::vector<double> values(kVideoTransferCurveSize);
    for (std::size_t index = 0U; index < values.size(); ++index) {
      const auto normalized_code = static_cast<double>(index) /
                                   static_cast<double>(values.size() - 1U);
      values[index] = decode_sony_slog3(normalized_code);
    }
    return values;
  }();
  return curve;
}

[[nodiscard]] const std::vector<double>& slog2_decode_curve() {
  static const auto curve = [] {
    std::vector<double> values(kVideoTransferCurveSize);
    for (std::size_t index = 0U; index < values.size(); ++index) {
      const auto normalized_code = static_cast<double>(index) /
                                   static_cast<double>(values.size() - 1U);
      values[index] = decode_sony_slog2(normalized_code);
    }
    return values;
  }();
  return curve;
}

[[nodiscard]] const std::vector<double>& rec709_encode_curve() {
  static const auto curve = [] {
    std::vector<double> values(kVideoTransferCurveSize);
    for (std::size_t index = 0U; index < values.size(); ++index) {
      const auto linear_light = static_cast<double>(index) /
                                static_cast<double>(values.size() - 1U);
      values[index] = encode_rec709_oetf(linear_light);
    }
    return values;
  }();
  return curve;
}

[[nodiscard]] double interpolate_unit_interval_curve(
    const std::vector<double>& curve,
    double value) {
  const auto position = value * static_cast<double>(curve.size() - 1U);
  const auto lower_index = static_cast<std::size_t>(position);
  const auto upper_index = std::min(lower_index + 1U, curve.size() - 1U);
  const auto fraction = position - static_cast<double>(lower_index);
  return std::lerp(curve[lower_index], curve[upper_index], fraction);
}

[[nodiscard]] double decode_sony_slog3_for_frame(double code_value) {
  if (!std::isfinite(code_value)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (code_value < 0.0 || code_value > 1.0) {
    return decode_sony_slog3(code_value);
  }
  return interpolate_unit_interval_curve(slog3_decode_curve(), code_value);
}

[[nodiscard]] double decode_sony_slog2_for_frame(double code_value) {
  if (!std::isfinite(code_value)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (code_value < 0.0 || code_value > 1.0) {
    return decode_sony_slog2(code_value);
  }
  return interpolate_unit_interval_curve(slog2_decode_curve(), code_value);
}

[[nodiscard]] double decode_video_input_for_frame(
    VideoInputTransformProfile profile,
    double code_value) {
  return profile == VideoInputTransformProfile::SonySLog2SGamut
             ? decode_sony_slog2_for_frame(code_value)
             : decode_sony_slog3_for_frame(code_value);
}

[[nodiscard]] double decode_video_input(
    VideoInputTransformProfile profile,
    double code_value) {
  return profile == VideoInputTransformProfile::SonySLog2SGamut
             ? decode_sony_slog2(code_value)
             : decode_sony_slog3(code_value);
}

[[nodiscard]] bool valid_video_input_profile(
    VideoInputTransformProfile profile) {
  return profile == VideoInputTransformProfile::SonySLog2SGamut ||
         profile == VideoInputTransformProfile::SonySLog3SGamut3 ||
         profile == VideoInputTransformProfile::SonySLog3SGamut3Cine;
}

[[nodiscard]] std::string normalized_signal_token(std::string value) {
  const auto first = std::find_if_not(
      value.begin(), value.end(), [](unsigned char character) {
        return std::isspace(character) != 0;
      });
  const auto last = std::find_if_not(
      value.rbegin(), value.rend(), [](unsigned char character) {
        return std::isspace(character) != 0;
      }).base();
  if (first >= last) {
    return {};
  }
  value = std::string(first, last);
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

template <std::size_t Size>
[[nodiscard]] bool contains_signal_token(
    const std::array<std::string_view, Size>& accepted,
    const std::string& value) {
  return std::find(accepted.cbegin(), accepted.cend(), value) !=
         accepted.cend();
}

[[nodiscard]] std::string_view video_input_transform_name(
    VideoInputTransformProfile profile) {
  switch (profile) {
    case VideoInputTransformProfile::SonySLog2SGamut:
      return "Sony S-Gamut/S-Log2";
    case VideoInputTransformProfile::SonySLog3SGamut3:
      return "Sony S-Gamut3/S-Log3";
    case VideoInputTransformProfile::SonySLog3SGamut3Cine:
      return "Sony S-Gamut3.Cine/S-Log3";
  }
  return "Sony S-Log";
}

[[nodiscard]] bool sony_slog2_curve_matches_reference_anchors() {
  constexpr std::array<std::array<double, 2>, 3> anchors = {{
      {0.0, 90.0},
      {0.18, 347.0},
      {0.90, 582.0},
  }};
  return std::all_of(anchors.cbegin(), anchors.cend(), [](const auto& anchor) {
    const auto reflection = anchor[0];
    const auto code_value = anchor[1];
    const auto normalized_code = code_value / kVideoInputCodeMaximum;
    return std::round(
               encode_sony_slog2(reflection) * kVideoInputCodeMaximum) ==
               code_value &&
           std::abs(decode_sony_slog2(normalized_code) - reflection) <= 0.002;
  });
}

[[nodiscard]] double encode_rec709_oetf_for_frame(double linear_light) {
  if (!std::isfinite(linear_light)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (linear_light < 0.0 || linear_light > 1.0) {
    return encode_rec709_oetf(linear_light);
  }
  return interpolate_unit_interval_curve(
      rec709_encode_curve(), linear_light);
}

}  // namespace

double decode_sony_slog3(double code_value) {
  if (!std::isfinite(code_value)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const auto code = code_value * kVideoInputCodeMaximum;
  if (code >= kSLog3EncodedBreakpoint) {
    return std::pow(
               10.0,
               (code - kSLog3MiddleGrayCode) / kSLog3LogSlope) *
               (kSLog3MiddleGrayReflection + kSLog3LogOffset) -
           kSLog3LogOffset;
  }
  return (code - kSLog3BlackCode) * kSLog3LinearBreakpoint /
         (kSLog3EncodedBreakpoint - kSLog3BlackCode);
}

double encode_sony_slog3(double scene_linear_reflection) {
  if (!std::isfinite(scene_linear_reflection)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (scene_linear_reflection >= kSLog3LinearBreakpoint) {
    return (kSLog3MiddleGrayCode +
            std::log10(
                (scene_linear_reflection + kSLog3LogOffset) /
                (kSLog3MiddleGrayReflection + kSLog3LogOffset)) *
                kSLog3LogSlope) /
           kVideoInputCodeMaximum;
  }
  return (scene_linear_reflection *
              (kSLog3EncodedBreakpoint - kSLog3BlackCode) /
              kSLog3LinearBreakpoint +
          kSLog3BlackCode) /
         kVideoInputCodeMaximum;
}

double decode_sony_slog2(double code_value) {
  if (!std::isfinite(code_value)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const auto code = code_value * kVideoInputCodeMaximum;
  const auto normalized_signal =
      (code - kSLog2LegalBlack) / (kSLog2LegalWhite - kSLog2LegalBlack);
  if (code >= kSLog2BlackCode) {
    return (std::pow(
                10.0,
                (normalized_signal - kSLog2LogOffset) / kSLog2LogSlope) -
            0.037584) /
           kSLog2InputScale;
  }
  return (normalized_signal - kSLog2LinearOffset) /
         kSLog2LinearSlope * 0.9;
}

double encode_sony_slog2(double scene_linear_reflection) {
  if (!std::isfinite(scene_linear_reflection)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const auto normalized_signal = scene_linear_reflection >=
                                         decode_sony_slog2(
                                             kSLog2BlackCode /
                                             kVideoInputCodeMaximum)
                                     ? kSLog2LogSlope * std::log10(
                                           kSLog2InputScale *
                                               scene_linear_reflection +
                                           0.037584) +
                                           kSLog2LogOffset
                                     : kSLog2LinearSlope *
                                               (scene_linear_reflection / 0.9) +
                                           kSLog2LinearOffset;
  return (kSLog2LegalBlack +
          (kSLog2LegalWhite - kSLog2LegalBlack) * normalized_signal) /
         kVideoInputCodeMaximum;
}

double encode_rec709_oetf(double linear_light) {
  if (!std::isfinite(linear_light)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (linear_light <= 0.018) {
    return 4.5 * linear_light;
  }
  return 1.099 * std::pow(linear_light, 0.45) - 0.099;
}

ResolvedVideoSourceSignalMetadata resolve_video_source_signal_metadata(
    const VideoSourceSignalMetadata& metadata,
    bool camera_input_transform_selected) {
  auto result = ResolvedVideoSourceSignalMetadata{};
  auto matrix = normalized_signal_token(metadata.matrix);
  const auto source_height = metadata.frame_height;
  constexpr std::array<std::string_view, 9> recognized_matrices = {
      "bt709", "bt470bg", "smpte170m", "smpte240m", "bt2020nc",
      "bt2020c", "fcc", "ycgco", "ictcp"};
  if (!contains_signal_token(recognized_matrices, matrix)) {
    matrix = source_height > 576 ? "bt709" : "smpte170m";
  }

  const auto range = normalized_signal_token(metadata.range);
  const auto full_range = range == "pc" || range == "jpeg" ||
                          range == "full" ||
                          normalized_signal_token(metadata.matrix) == "adobe-rgb";
  auto transfer = normalized_signal_token(metadata.transfer);
  constexpr std::array<std::string_view, 11> recognized_transfers = {
      "bt709", "smpte170m", "smpte240m", "linear", "log100", "log316",
      "iec61966-2-1", "bt2020-10", "bt2020-12", "smpte2084",
      "arib-std-b67"};
  if (!contains_signal_token(recognized_transfers, transfer)) {
    transfer = matrix == "bt470bg" || matrix == "smpte170m" ||
                       matrix == "smpte240m"
                   ? "smpte170m"
                   : "bt709";
  }

  auto primaries = normalized_signal_token(metadata.primaries);
  constexpr std::array<std::string_view, 11> recognized_primaries = {
      "bt709", "bt470m", "bt470bg", "smpte170m", "smpte240m", "film",
      "bt2020", "smpte428", "smpte431", "smpte432", "jedec-p22"};
  if (!contains_signal_token(recognized_primaries, primaries)) {
    if (matrix == "bt2020nc" || matrix == "bt2020c") {
      primaries = "bt2020";
    } else if (matrix == "bt470bg" || matrix == "smpte170m") {
      primaries = matrix;
    } else {
      primaries = "bt709";
    }
  }

  result.matrix = std::move(matrix);
  result.range = full_range ? "full" : "limited";
  result.transfer = std::move(transfer);
  result.primaries = std::move(primaries);
  result.source_is_hdr = result.transfer == "smpte2084" ||
                         result.transfer == "arib-std-b67";
  result.source_is_rec709 = result.matrix == "bt709" &&
                            result.transfer == "bt709" &&
                            result.primaries == "bt709";
  result.standard_matrix = result.matrix == "bt709" ||
                           result.matrix == "smpte170m" ||
                           result.matrix == "bt470bg" ||
                           result.matrix == "smpte240m";
  result.restore_limited_camera_code_values =
      camera_input_transform_selected && result.range == "limited";
  result.output_transfer = camera_input_transform_selected || result.source_is_hdr
                               ? result.transfer
                               : "bt709";
  result.output_primaries =
      camera_input_transform_selected || result.source_is_hdr
          ? result.primaries
          : "bt709";

  if (result.matrix == "bt709") {
    result.ycbcr_kr = 0.2126;
    result.ycbcr_kb = 0.0722;
  } else if (result.matrix == "bt2020nc") {
    result.ycbcr_kr = 0.2627;
    result.ycbcr_kb = 0.0593;
    result.metadata_aware_yuv_supported = true;
  } else if (result.matrix == "smpte240m") {
    result.ycbcr_kr = 0.212;
    result.ycbcr_kb = 0.087;
  } else if (result.matrix == "fcc") {
    result.ycbcr_kr = 0.30;
    result.ycbcr_kb = 0.11;
  } else if (result.matrix == "bt2020c" || result.matrix == "ycgco" ||
             result.matrix == "ictcp") {
    // These matrices use non-constant-luminance or opponent transforms that
    // cannot be represented by the simple Kr/Kb conversion used by this CPU
    // sampler. Let the platform decoder handle them and report the fallback.
    result.metadata_aware_yuv_supported = false;
  }
  return result;
}

std::optional<CompiledVideoInputTransform> compile_video_input_transform(
    VideoInputTransformProfile profile,
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method) {
  if (!valid_video_input_profile(profile) ||
      working_space.transfer_function != TransferFunction::Linear ||
      !working_space.scene_referred) {
    return std::nullopt;
  }
  const auto source_space = [&]() {
    if (profile == VideoInputTransformProfile::SonySLog3SGamut3Cine) {
      return make_linear_sony_s_gamut3_cine_color_space();
    }
    // Sony specifies S-Gamut and S-Gamut3 with the same RGB primaries and D65
    // white. S-Log2 remains a distinct transfer curve and profile selection.
    return make_linear_sony_s_gamut3_color_space();
  }();
  const auto transform = compile_builtin_color_transform(
      source_space,
      working_space,
      chromatic_adaptation_method);
  return CompiledVideoInputTransform{
      .profile = profile,
      .working_space = working_space,
      .linear_source_to_working = transform.matrix,
      .chromatic_adaptation_method = std::string(chromatic_adaptation_method),
  };
}

ColorPixel apply_video_input_transform(
    const CompiledVideoInputTransform& transform,
    const ColorPixel& encoded_pixel) {
  const auto camera_linear = std::array{
      decode_video_input_for_frame(
          transform.profile, static_cast<double>(encoded_pixel.red)),
      decode_video_input_for_frame(
          transform.profile, static_cast<double>(encoded_pixel.green)),
      decode_video_input_for_frame(
          transform.profile, static_cast<double>(encoded_pixel.blue)),
  };
  const auto working_linear = multiply_matrix3_vector(
      transform.linear_source_to_working,
      camera_linear);
  return ColorPixel{
      .red = static_cast<float>(working_linear[0]),
      .green = static_cast<float>(working_linear[1]),
      .blue = static_cast<float>(working_linear[2]),
      .alpha = encoded_pixel.alpha,
  };
}

void apply_video_input_transform(
    const CompiledVideoInputTransform& transform,
    std::span<ColorPixel> encoded_pixels) {
  for (auto& pixel : encoded_pixels) {
    pixel = apply_video_input_transform(transform, pixel);
  }
}

std::optional<CompiledVideoRec709OutputTransform>
compile_video_rec709_output_transform(
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method) {
  if (working_space.transfer_function != TransferFunction::Linear ||
      !working_space.scene_referred) {
    return std::nullopt;
  }
  const auto transform = compile_builtin_color_transform(
      working_space,
      make_linear_rec709_color_space(),
      chromatic_adaptation_method);
  return CompiledVideoRec709OutputTransform{
      .working_space = working_space,
      .linear_working_to_rec709 = transform.matrix,
      .chromatic_adaptation_method = std::string(chromatic_adaptation_method),
  };
}

ColorPixel apply_video_rec709_output_transform(
    const CompiledVideoRec709OutputTransform& transform,
    const ColorPixel& working_pixel) {
  const auto rec709_linear = multiply_matrix3_vector(
      transform.linear_working_to_rec709,
      {static_cast<double>(working_pixel.red),
       static_cast<double>(working_pixel.green),
       static_cast<double>(working_pixel.blue)});
  return ColorPixel{
      .red = static_cast<float>(encode_rec709_oetf_for_frame(rec709_linear[0])),
      .green = static_cast<float>(encode_rec709_oetf_for_frame(rec709_linear[1])),
      .blue = static_cast<float>(encode_rec709_oetf_for_frame(rec709_linear[2])),
      .alpha = working_pixel.alpha,
  };
}

void apply_video_rec709_output_transform(
    const CompiledVideoRec709OutputTransform& transform,
    std::span<ColorPixel> working_pixels) {
  for (auto& pixel : working_pixels) {
    pixel = apply_video_rec709_output_transform(transform, pixel);
  }
}

VideoTransformLutBuildResult build_sony_video_input_linear_rec709_lut(
    VideoInputTransformProfile profile,
    VideoTransformLutSettings settings) {
  VideoTransformLutBuildResult result;
  if (!valid_video_input_profile(profile) ||
      settings.cube_size < 2 || settings.cube_size > 128 ||
      settings.validation_grid_size < 3 ||
      settings.validation_grid_size > 65 ||
      !std::isfinite(settings.p99_absolute_error_limit) ||
      settings.p99_absolute_error_limit < 0.0 ||
      !std::isfinite(settings.maximum_absolute_error_limit) ||
      settings.maximum_absolute_error_limit < 0.0) {
    result.failure_reason = "Invalid Sony camera input transform LUT settings or profile.";
    return result;
  }
  if (profile == VideoInputTransformProfile::SonySLog2SGamut &&
      !sony_slog2_curve_matches_reference_anchors()) {
    result.failure_reason =
        "The S-Log2 curve does not match Sony's published 10-bit code anchors.";
    return result;
  }

  const auto input_transform = compile_video_input_transform(
      profile,
      make_linear_rec709_color_space());
  const auto output_transform = compile_video_rec709_output_transform(
      make_linear_rec709_color_space());
  if (!input_transform.has_value() || !output_transform.has_value()) {
    result.failure_reason = "Unable to compile the video color transform.";
    return result;
  }
  const auto source_domain_min = decode_video_input(profile, 0.0);
  const auto source_domain_max = decode_video_input(profile, 1.0);
  auto linear_output_min = std::numeric_limits<double>::infinity();
  auto linear_output_max = -std::numeric_limits<double>::infinity();
  for (const auto red : {source_domain_min, source_domain_max}) {
    for (const auto green : {source_domain_min, source_domain_max}) {
      for (const auto blue : {source_domain_min, source_domain_max}) {
        const auto working_linear = multiply_matrix3_vector(
            input_transform->linear_source_to_working, {red, green, blue});
        const auto linear_rec709 = multiply_matrix3_vector(
            output_transform->linear_working_to_rec709, working_linear);
        for (const auto channel : linear_rec709) {
          linear_output_min = std::min(linear_output_min, channel);
          linear_output_max = std::max(linear_output_max, channel);
        }
      }
    }
  }
  if (!std::isfinite(linear_output_min) ||
      !std::isfinite(linear_output_max) ||
      linear_output_max <= linear_output_min) {
    result.failure_reason = "Unable to determine the Rec.709 transfer LUT range.";
    return result;
  }
  result.linear_rec709_output_range = {linear_output_min, linear_output_max};
  const auto exact_transform = [&input_transform, &output_transform](
                                   const std::array<double, 3>& camera_linear) {
    const auto working_linear = multiply_matrix3_vector(
        input_transform->linear_source_to_working,
        camera_linear);
    const auto rec709_linear = multiply_matrix3_vector(
        output_transform->linear_working_to_rec709,
        working_linear);
    return ColorPixel{
        .red = static_cast<float>(rec709_linear[0]),
        .green = static_cast<float>(rec709_linear[1]),
        .blue = static_cast<float>(rec709_linear[2]),
        .alpha = 1.0F,
    };
  };

  auto& lut = result.lut;
  lut.size = settings.cube_size;
  lut.input_color_space = std::string(video_input_transform_name(profile));
  lut.output_color_space = "Linear Rec.709";
  const auto shaper_domain_min = source_domain_min;
  const auto shaper_domain_max = source_domain_max;
  const auto shaper_domain_width = shaper_domain_max - shaper_domain_min;
  lut.domain_min = {0.0, 0.0, 0.0};
  lut.domain_max = {1.0, 1.0, 1.0};
  lut.shaper_size = static_cast<int>(kVideoTransferCurveSize);
  lut.shaper_input_range = {0.0, 1.0};
  lut.shaper_table.reserve(kVideoTransferCurveSize);
  for (std::size_t index = 0U; index < kVideoTransferCurveSize; ++index) {
    const auto code_value = static_cast<double>(index) /
                            static_cast<double>(kVideoTransferCurveSize - 1U);
    const auto normalized_reflection = static_cast<float>(std::clamp(
        (decode_video_input(profile, code_value) - shaper_domain_min) /
            shaper_domain_width,
        0.0,
        1.0));
    lut.shaper_table.push_back(ColorPixel{
        .red = normalized_reflection,
        .green = normalized_reflection,
        .blue = normalized_reflection,
        .alpha = 1.0F,
    });
  }
  const auto lut_size = static_cast<std::size_t>(lut.size);
  lut.table.reserve(lut_size * lut_size * lut_size);
  for (int red_index = 0; red_index < lut.size; ++red_index) {
    const auto red = static_cast<double>(red_index) /
                     static_cast<double>(lut.size - 1);
    for (int green_index = 0; green_index < lut.size; ++green_index) {
      const auto green = static_cast<double>(green_index) /
                         static_cast<double>(lut.size - 1);
      for (int blue_index = 0; blue_index < lut.size; ++blue_index) {
        const auto blue = static_cast<double>(blue_index) /
                          static_cast<double>(lut.size - 1);
        lut.table.push_back(exact_transform({
            shaper_domain_min + shaper_domain_width * red,
            shaper_domain_min + shaper_domain_width * green,
            shaper_domain_min + shaper_domain_width * blue,
        }));
      }
    }
  }
  lut.cache_identity = 0x5654520000000000ULL ^
                       (static_cast<std::uint64_t>(profile) << 32U) ^
                       static_cast<std::uint64_t>(lut.size);
  lut.valid = true;

  const auto grid_size = settings.validation_grid_size;
  const auto sample_count = static_cast<std::size_t>(grid_size) *
                            static_cast<std::size_t>(grid_size) *
                            static_cast<std::size_t>(grid_size);
  std::vector<double> errors;
  errors.reserve(sample_count);
  for (int red_index = 0; red_index < grid_size; ++red_index) {
    const auto red = static_cast<float>(red_index) /
                     static_cast<float>(grid_size - 1);
    for (int green_index = 0; green_index < grid_size; ++green_index) {
      const auto green = static_cast<float>(green_index) /
                         static_cast<float>(grid_size - 1);
      for (int blue_index = 0; blue_index < grid_size; ++blue_index) {
        const auto blue = static_cast<float>(blue_index) /
                          static_cast<float>(grid_size - 1);
        const auto source = ColorPixel{red, green, blue, 1.0F};
        const auto camera_linear = std::array{
            decode_video_input(profile, static_cast<double>(source.red)),
            decode_video_input(profile, static_cast<double>(source.green)),
            decode_video_input(profile, static_cast<double>(source.blue)),
        };
        const auto reference = exact_transform(camera_linear);
        const auto actual = apply_lut(lut, source);
        errors.push_back(std::max({
            std::abs(static_cast<double>(reference.red) - actual.red),
            std::abs(static_cast<double>(reference.green) - actual.green),
            std::abs(static_cast<double>(reference.blue) - actual.blue),
        }));
      }
    }
  }
  std::sort(errors.begin(), errors.end());
  result.validation_sample_count = errors.size();
  result.measured_maximum_absolute_error = errors.back();
  const auto p99_index = static_cast<std::size_t>(std::ceil(
      0.99 * static_cast<double>(errors.size()))) - 1U;
  result.measured_p99_absolute_error = errors[p99_index];
  if (result.measured_p99_absolute_error >
          settings.p99_absolute_error_limit ||
      result.measured_maximum_absolute_error >
          settings.maximum_absolute_error_limit) {
    lut.valid = false;
    result.failure_reason = "The generated LUT exceeds its interpolation error limits.";
  }
  return result;
}

VideoRec709LutBuildResult build_sony_video_input_rec709_lut(
    VideoInputTransformProfile profile,
    VideoRec709LutBuildSettings settings) {
  VideoRec709LutBuildResult result;
  if (!valid_video_input_profile(profile) ||
      settings.cube_size < 2 || settings.cube_size > 128 ||
      settings.validation_grid_size < 3 ||
      settings.validation_grid_size > 65 ||
      !std::isfinite(settings.p99_absolute_error_limit) ||
      settings.p99_absolute_error_limit < 0.0 ||
      !std::isfinite(settings.maximum_absolute_error_limit) ||
      settings.maximum_absolute_error_limit < 0.0) {
    result.failure_reason = "Invalid Sony-to-Rec.709 LUT settings or profile.";
    return result;
  }
  if (profile == VideoInputTransformProfile::SonySLog2SGamut &&
      !sony_slog2_curve_matches_reference_anchors()) {
    result.failure_reason =
        "The S-Log2 curve does not match Sony's published 10-bit code anchors.";
    return result;
  }

  const auto working_space = make_linear_rec709_color_space();
  const auto input_transform = compile_video_input_transform(profile, working_space);
  const auto output_transform = compile_video_rec709_output_transform(working_space);
  if (!input_transform.has_value() || !output_transform.has_value()) {
    result.failure_reason = "Unable to compile the shared video color transforms.";
    return result;
  }
  const auto exact_transform = [&input_transform, &output_transform](
                                   const ColorPixel& encoded_pixel) {
    const auto working = apply_video_input_transform(
        *input_transform, encoded_pixel);
    return apply_video_rec709_output_transform(
        *output_transform, working);
  };

  auto& lut = result.lut;
  lut.size = settings.cube_size;
  lut.input_color_space = std::string(video_input_transform_name(profile));
  lut.output_color_space = "Rec.709";
  lut.domain_min = {0.0, 0.0, 0.0};
  lut.domain_max = {1.0, 1.0, 1.0};
  const auto lut_size = static_cast<std::size_t>(lut.size);
  lut.table.reserve(lut_size * lut_size * lut_size);
  for (int red_index = 0; red_index < lut.size; ++red_index) {
    const auto red = static_cast<float>(red_index) /
                     static_cast<float>(lut.size - 1);
    for (int green_index = 0; green_index < lut.size; ++green_index) {
      const auto green = static_cast<float>(green_index) /
                         static_cast<float>(lut.size - 1);
      for (int blue_index = 0; blue_index < lut.size; ++blue_index) {
        const auto blue = static_cast<float>(blue_index) /
                          static_cast<float>(lut.size - 1);
        lut.table.push_back(exact_transform(ColorPixel{
            .red = red,
            .green = green,
            .blue = blue,
            .alpha = 1.0F,
        }));
      }
    }
  }
  lut.cache_identity = 0x5652340000000000ULL ^
                       (static_cast<std::uint64_t>(profile) << 32U) ^
                       static_cast<std::uint64_t>(lut.size);
  lut.valid = true;

  const auto grid_size = settings.validation_grid_size;
  const auto sample_count = static_cast<std::size_t>(grid_size) *
                            static_cast<std::size_t>(grid_size) *
                            static_cast<std::size_t>(grid_size);
  std::vector<double> errors;
  errors.reserve(sample_count);
  for (int red_index = 0; red_index < grid_size; ++red_index) {
    const auto red = static_cast<float>(red_index) /
                     static_cast<float>(grid_size - 1);
    for (int green_index = 0; green_index < grid_size; ++green_index) {
      const auto green = static_cast<float>(green_index) /
                         static_cast<float>(grid_size - 1);
      for (int blue_index = 0; blue_index < grid_size; ++blue_index) {
        const auto blue = static_cast<float>(blue_index) /
                          static_cast<float>(grid_size - 1);
        const auto source = ColorPixel{red, green, blue, 1.0F};
        const auto reference = exact_transform(source);
        const auto actual = apply_lut_tetrahedral(lut, source);
        errors.push_back(std::max({
            std::abs(static_cast<double>(reference.red) - actual.red),
            std::abs(static_cast<double>(reference.green) - actual.green),
            std::abs(static_cast<double>(reference.blue) - actual.blue),
        }));
      }
    }
  }
  std::sort(errors.begin(), errors.end());
  result.validation_sample_count = errors.size();
  result.measured_maximum_absolute_error = errors.back();
  const auto p99_index = static_cast<std::size_t>(std::ceil(
      0.99 * static_cast<double>(errors.size()))) - 1U;
  result.measured_p99_absolute_error = errors[p99_index];
  if (result.measured_p99_absolute_error >
          settings.p99_absolute_error_limit ||
      result.measured_maximum_absolute_error >
          settings.maximum_absolute_error_limit) {
    lut.valid = false;
    result.failure_reason =
        "The direct Rec.709 cube exceeds its interpolation error limits.";
  }
  return result;
}

const VideoRec709LutBuildResult& cached_sony_video_input_rec709_lut(
    VideoInputTransformProfile profile) {
  static std::array<std::once_flag, 3> initialized;
  static std::array<VideoRec709LutBuildResult, 3> results;
  const auto index = profile == VideoInputTransformProfile::SonySLog2SGamut
                         ? std::size_t{0}
                         : profile == VideoInputTransformProfile::SonySLog3SGamut3
                               ? std::size_t{1}
                               : std::size_t{2};
  std::call_once(initialized[index], [profile, index]() {
    auto result = build_sony_video_input_rec709_lut(
        profile,
        VideoRec709LutBuildSettings{
            .cube_size = 65,
            .validation_grid_size = 23,
            .p99_absolute_error_limit = 0.0015,
            .maximum_absolute_error_limit = 0.005,
        });
    if (!result.ok()) {
      result.lut.table.clear();
      result.lut.table.shrink_to_fit();
    }
    results[index] = std::move(result);
  });
  return results[index];
}

VideoTransformLutBuildResult build_sony_slog3_linear_rec709_lut(
    VideoInputTransformProfile profile,
    VideoTransformLutSettings settings) {
  if (profile == VideoInputTransformProfile::SonySLog2SGamut) {
    VideoTransformLutBuildResult result;
    result.failure_reason = "Use the Sony camera input transform LUT builder for S-Log2.";
    return result;
  }
  return build_sony_video_input_linear_rec709_lut(profile, settings);
}

}  // namespace photo_editor::pipeline
