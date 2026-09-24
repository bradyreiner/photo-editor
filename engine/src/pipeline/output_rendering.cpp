#include "photo_editor/pipeline/output_rendering.h"
#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <sstream>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

namespace photo_editor::pipeline {

struct Aces2OfficialOutputTransform final {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
  OCIO::ConstCPUProcessorRcPtr cpu_processor;
#endif
  bool processor_output_is_srgb_encoded = false;
  bool processor_input_is_working = false;
  std::string implementation_id = "ocio-fixed-function-aces-output-transform-20";

  [[nodiscard]] static float decode_srgb_display(float encoded) {
    const auto value = std::clamp(encoded, 0.0F, 1.0F);
    if (value <= 0.04045F) {
      return value / 12.92F;
    }
    return std::pow((value + 0.055F) / 1.055F, 2.4F);
  }

  [[nodiscard]] std::array<double, 3> apply(
      const std::array<double, 3>& aces2065_1) const {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
    if (cpu_processor) {
      auto safe_input = aces2065_1;
      for (int attempt = 0; attempt < 17; ++attempt) {
        float pixel[3] = {
            static_cast<float>(safe_input[0]),
            static_cast<float>(safe_input[1]),
            static_cast<float>(safe_input[2]),
        };
        cpu_processor->applyRGB(pixel);
        if (std::isfinite(pixel[0]) && std::isfinite(pixel[1]) &&
            std::isfinite(pixel[2])) {
          if (processor_output_is_srgb_encoded) {
            for (auto& channel : pixel) {
              channel = decode_srgb_display(channel);
            }
          }
          return {
              static_cast<double>(pixel[0]),
              static_cast<double>(pixel[1]),
              static_cast<double>(pixel[2]),
          };
        }
        for (auto& channel : safe_input) {
          channel *= 0.5;
        }
      }
      return {0.0, 0.0, 0.0};
    }
#endif
    return aces2065_1;
  }

  [[nodiscard]] bool apply_rgba(
      std::span<std::array<float, 4>> pixels) const {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
    if (cpu_processor && !pixels.empty()) {
      try {
        constexpr std::size_t kMaximumBatchPixels = 65'536U;
        for (std::size_t offset = 0U; offset < pixels.size();
             offset += kMaximumBatchPixels) {
          const auto count = std::min(
              kMaximumBatchPixels,
              pixels.size() - offset);
          OCIO::PackedImageDesc image(
              pixels.data() + static_cast<std::ptrdiff_t>(offset),
              static_cast<long>(count),
              1L,
              4L);
          cpu_processor->apply(image);
        }
        if (processor_output_is_srgb_encoded) {
          for (auto& pixel : pixels) {
            pixel[0] = decode_srgb_display(pixel[0]);
            pixel[1] = decode_srgb_display(pixel[1]);
            pixel[2] = decode_srgb_display(pixel[2]);
          }
        }
        return true;
      } catch (const OCIO::Exception&) {
        return false;
      }
    }
#else
    (void)pixels;
#endif
    return pixels.empty();
  }
};

namespace {

constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 1.0 - 4.0 * kHlgA;
const double kHlgC = 0.5 - kHlgA * std::log(4.0 * kHlgA);

constexpr double kAcesReferenceLuminance = 100.0;
constexpr double kAcesAdaptingLuminance = 100.0;
constexpr double kAcesBackgroundLuminance = 20.0;
constexpr double kAcesColorfulnessSurround = 0.9;
constexpr double kCamNonlinearOffset = 0.2713 * kAcesReferenceLuminance;
constexpr double kCamNonlinearScale = 4.0 * kAcesReferenceLuminance;
constexpr double kHellwigLightnessScale = 100.0;
constexpr std::size_t kCompiledGamutLightnessSamples = 129U;
constexpr std::size_t kCompiledGamutHueSamples = 360U;
constexpr std::size_t kOutputProcessorCacheCapacity = 8U;
constexpr std::size_t kOutputLutCacheCapacity = 2U;
constexpr std::size_t kOutputLutCacheMaximumBytes =
    32U * 1024U * 1024U;

struct OutputProcessorCacheEntry final {
  RgbColorSpace working_color_space;
  DisplayTarget display_target;
  std::string chromatic_adaptation_method;
  Aces2OutputImplementation implementation =
      Aces2OutputImplementation::OfficialWhenAvailable;
  RendererTransitionLook transition_look =
      RendererTransitionLook::None;
  std::shared_ptr<const Aces2OutputProcessor> processor;
  std::uint64_t last_use = 0U;
};

struct OutputProcessorCache final {
  std::mutex mutex;
  std::vector<OutputProcessorCacheEntry> entries;
  Aces2OutputProcessorCacheStats stats;
  std::uint64_t use_counter = 0U;
};

struct OutputLutCacheEntry final {
  std::string key;
  std::shared_ptr<const Aces2OutputLut3D> lut;
  std::uint64_t last_use = 0U;
};

struct OutputLutCache final {
  std::mutex mutex;
  std::vector<OutputLutCacheEntry> entries;
  Aces2OutputLutCacheStats stats;
  std::uint64_t use_counter = 0U;
};

[[nodiscard]] OutputProcessorCache& output_processor_cache() {
  static OutputProcessorCache cache;
  return cache;
}

[[nodiscard]] OutputLutCache& output_lut_cache() {
  static OutputLutCache cache;
  return cache;
}

[[nodiscard]] bool same_color_space(
    const RgbColorSpace& left,
    const RgbColorSpace& right) {
  return left.name == right.name && left.primaries == right.primaries &&
         left.transfer_function == right.transfer_function &&
         left.white_point == right.white_point &&
         left.scene_referred == right.scene_referred &&
         left.wide_gamut == right.wide_gamut;
}

[[nodiscard]] bool same_display_target(
    const DisplayTarget& left,
    const DisplayTarget& right) {
  return left.target_name == right.target_name &&
         same_color_space(left.color_space, right.color_space) &&
         left.eotf == right.eotf &&
         left.peak_luminance_nits == right.peak_luminance_nits &&
         left.black_luminance_nits == right.black_luminance_nits &&
         left.reference_white_nits == right.reference_white_nits &&
         left.surround_luminance_nits == right.surround_luminance_nits &&
         left.surround == right.surround &&
         left.mastering_mode == right.mastering_mode;
}

[[nodiscard]] double official_aces2_input_limit(double peak_luminance_nits) {
  const auto peak = std::clamp(peak_luminance_nits, 1.0, 10000.0);
  return std::max(
      0.0,
      8.0 *
          (128.0 +
           768.0 * (std::log(peak / 100.0) / std::log(10000.0 / 100.0))));
}

[[nodiscard]] bool output_lut_settings_valid(
    const Aces2OutputLutSettings& settings) {
  return settings.cube_size >= 2 && settings.cube_size <= 129 &&
         std::isfinite(settings.shaper_offset) &&
         settings.shaper_offset > 0.0 && settings.shaper_offset <= 1.0;
}

[[nodiscard]] double decode_log_shaper(
    double coordinate,
    double maximum,
    double offset) {
  return offset *
         std::expm1(
             std::clamp(coordinate, 0.0, 1.0) *
             std::log1p(maximum / offset));
}

[[nodiscard]] double encode_log_shaper(
    double value,
    double maximum,
    double offset) {
  return std::log1p(std::clamp(value, 0.0, maximum) / offset) /
         std::log1p(maximum / offset);
}

[[nodiscard]] std::size_t output_lut_table_index(
    int size,
    int red,
    int green,
    int blue) {
  const auto safe_size = static_cast<std::size_t>(size);
  return (static_cast<std::size_t>(red) * safe_size +
          static_cast<std::size_t>(green)) *
             safe_size +
         static_cast<std::size_t>(blue);
}

[[nodiscard]] std::array<double, 3> tetrahedral_lut_sample(
    const Aces2OutputLut3D& lut,
    const std::array<double, 3>& coordinates) {
  const auto size = lut.settings.cube_size;
  const auto scale = static_cast<double>(size - 1);
  std::array<int, 3> lower{};
  std::array<double, 3> fraction{};
  for (std::size_t channel = 0U; channel < coordinates.size(); ++channel) {
    const auto position = std::clamp(coordinates[channel], 0.0, 1.0) * scale;
    lower[channel] = std::min(static_cast<int>(position), size - 2);
    fraction[channel] = position - static_cast<double>(lower[channel]);
  }
  const auto sample = [&](int red, int green, int blue) {
    const auto& value = lut.table[output_lut_table_index(
        size,
        lower[0] + red,
        lower[1] + green,
        lower[2] + blue)];
    return std::array<double, 3>{value[0], value[1], value[2]};
  };
  const auto c000 = sample(0, 0, 0);
  const auto c111 = sample(1, 1, 1);
  std::array<std::array<double, 3>, 4> vertices{};
  std::array<double, 4> weights{};
  const auto red = fraction[0];
  const auto green = fraction[1];
  const auto blue = fraction[2];
  if (red >= green) {
    if (green >= blue) {
      vertices = {c000, sample(1, 0, 0), sample(1, 1, 0), c111};
      weights = {1.0 - red, red - green, green - blue, blue};
    } else if (red >= blue) {
      vertices = {c000, sample(1, 0, 0), sample(1, 0, 1), c111};
      weights = {1.0 - red, red - blue, blue - green, green};
    } else {
      vertices = {c000, sample(0, 0, 1), sample(1, 0, 1), c111};
      weights = {1.0 - blue, blue - red, red - green, green};
    }
  } else if (red >= blue) {
    vertices = {c000, sample(0, 1, 0), sample(1, 1, 0), c111};
    weights = {1.0 - green, green - red, red - blue, blue};
  } else if (green >= blue) {
    vertices = {c000, sample(0, 1, 0), sample(0, 1, 1), c111};
    weights = {1.0 - green, green - blue, blue - red, red};
  } else {
    vertices = {c000, sample(0, 0, 1), sample(0, 1, 1), c111};
    weights = {1.0 - blue, blue - green, green - red, red};
  }
  std::array<double, 3> output{};
  for (std::size_t vertex = 0U; vertex < vertices.size(); ++vertex) {
    for (std::size_t channel = 0U; channel < output.size(); ++channel) {
      output[channel] += vertices[vertex][channel] * weights[vertex];
    }
  }
  return output;
}

[[nodiscard]] std::string output_processor_cache_key(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    std::string_view chromatic_adaptation_method,
    std::string_view official_transform_id,
    RendererTransitionLook transition_look) {
  std::ostringstream stream;
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << "aces2-output-processor:v4|"
         << (!official_transform_id.empty() ? "OpenColorIO ACES 2"
                                            : "native ACES 2 fallback")
         << '|'
         << (!official_transform_id.empty()
                 ? std::string(aces2_output_renderer_version()) + ":" +
                       std::string(official_transform_id)
                 : std::string{"aces2-hellwig-jmh-compiled-boundary:v1"})
         << '|'
         << working_color_space.name << '|'
         << static_cast<int>(working_color_space.primaries) << '|'
         << static_cast<int>(working_color_space.transfer_function) << '|'
         << static_cast<int>(working_color_space.white_point) << '|'
         << working_color_space.scene_referred << '|'
         << working_color_space.wide_gamut << '|'
         << display_target.target_name << '|'
         << display_target.color_space.name << '|'
         << static_cast<int>(display_target.color_space.primaries) << '|'
         << static_cast<int>(display_target.color_space.transfer_function) << '|'
         << static_cast<int>(display_target.color_space.white_point) << '|'
         << display_target.color_space.scene_referred << '|'
         << display_target.color_space.wide_gamut << '|'
         << static_cast<int>(display_target.eotf) << '|'
         << display_target.peak_luminance_nits << '|'
         << display_target.black_luminance_nits << '|'
         << display_target.reference_white_nits << '|'
         << display_target.surround_luminance_nits << '|'
         << static_cast<int>(display_target.surround) << '|'
         << static_cast<int>(display_target.mastering_mode) << '|'
         << chromatic_adaptation_method << '|'
         << static_cast<int>(transition_look);
  return stream.str();
}

[[nodiscard]] std::string output_lut_cache_key(
    const Aces2OutputProcessor& processor,
    const Aces2OutputLutSettings& settings) {
  std::ostringstream stream;
  stream.precision(std::numeric_limits<double>::max_digits10);
  stream << "aces2-output-lut:v2|tetrahedral|ap1-log2|"
         << settings.cube_size << '|' << settings.shaper_offset << '|'
         << processor.cache_key;
  return stream.str();
}

[[nodiscard]] std::shared_ptr<const Aces2OfficialOutputTransform>
compile_official_aces2_output_transform(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target) {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
  const auto rounded_peak = std::round(display_target.peak_luminance_nits);
  if (!display_target_valid(display_target) ||
      std::abs(display_target.peak_luminance_nits - rounded_peak) > 1.0e-9 ||
      rounded_peak < 1.0 || rounded_peak > 10000.0 ||
      display_target.color_space.primaries == ColorPrimaries::Unknown ||
      display_target.color_space.white_point == WhitePoint::Unknown) {
    return {};
  }
  const auto primaries = rgb_primaries(display_target.color_space.primaries);
  const auto white =
      white_point_chromaticity(display_target.color_space.white_point);
  const std::array<double, 9> parameters = {
      rounded_peak,
      primaries[0].x,
      primaries[0].y,
      primaries[1].x,
      primaries[1].y,
      primaries[2].x,
      primaries[2].y,
      white.x,
      white.y,
  };
  try {
    const auto standard_srgb_100_nit_target =
        display_target.color_space.primaries == ColorPrimaries::Srgb &&
        display_target.color_space.white_point == WhitePoint::D65 &&
        display_target.eotf == DisplayEotf::Srgb &&
        std::abs(display_target.peak_luminance_nits - 100.0) <= 1.0e-9 &&
        std::abs(display_target.black_luminance_nits - 0.005) <= 1.0e-9 &&
        std::abs(display_target.reference_white_nits - 100.0) <= 1.0e-9 &&
        std::abs(display_target.surround_luminance_nits - 5.0) <= 1.0e-9 &&
        display_target.surround == DisplaySurround::Dim &&
        display_target.mastering_mode == DisplayMasteringMode::Sdr;
    if (standard_srgb_100_nit_target) {
      constexpr auto kBuiltinConfig =
          "cg-config-v4.0.0_aces-v2.0_ocio-v2.5";
      constexpr auto kDisplay = "sRGB - Display";
      constexpr auto kView = "ACES 2.0 - SDR 100 nits (Rec.709)";
      const auto source_name = [&working_color_space]() -> const char* {
        if (working_color_space.transfer_function !=
                TransferFunction::Linear ||
            working_color_space.white_point != WhitePoint::D65) {
          return nullptr;
        }
        switch (working_color_space.primaries) {
          case ColorPrimaries::Srgb:
            return "Linear Rec.709 (sRGB)";
          case ColorPrimaries::DisplayP3:
            return "Linear P3-D65";
          case ColorPrimaries::Rec2020:
            return "Linear Rec.2020";
          default:
            return nullptr;
        }
      }();
      if (source_name != nullptr) {
        const auto config =
            OCIO::Config::CreateFromBuiltinConfig(kBuiltinConfig);
        const auto processor = config->getProcessor(
            source_name,
            kDisplay,
            kView,
            OCIO::TRANSFORM_DIR_FORWARD);
        const auto cpu_processor = processor->getDefaultCPUProcessor();
        if (cpu_processor) {
          return std::make_shared<const Aces2OfficialOutputTransform>(
              Aces2OfficialOutputTransform{
                  .cpu_processor = cpu_processor,
                  .processor_output_is_srgb_encoded = true,
                  .processor_input_is_working = true,
                  .implementation_id =
                      std::string("ocio-builtin-cg-v4-aces-v2-srgb-display-view:") +
                      source_name,
              });
        }
      }
    }
    const auto transform = OCIO::FixedFunctionTransform::Create(
        OCIO::FIXED_FUNCTION_ACES_OUTPUT_TRANSFORM_20,
        parameters.data(),
        parameters.size());
    const auto raw_config = OCIO::Config::CreateRaw();
    const auto processor = raw_config->getProcessor(transform);
    const auto cpu_processor = processor->getDefaultCPUProcessor();
    if (!cpu_processor) {
      return {};
    }
    return std::make_shared<const Aces2OfficialOutputTransform>(
        Aces2OfficialOutputTransform{
            .cpu_processor = cpu_processor,
            .processor_output_is_srgb_encoded = false,
            .processor_input_is_working = false,
            .implementation_id =
                "ocio-fixed-function-aces-output-transform-20",
        });
  } catch (const OCIO::Exception&) {
    return {};
  }
#else
  (void)display_target;
  return {};
#endif
}

[[nodiscard]] double post_adaptation_response_forward(double value) {
  const auto absolute = std::abs(value);
  const auto powered = std::pow(absolute, 0.42);
  const auto response = powered / (kCamNonlinearOffset + powered);
  return std::copysign(response, value);
}

[[nodiscard]] double post_adaptation_response_inverse(double value) {
  const auto response = std::min(std::abs(value), 0.99);
  const auto powered = kCamNonlinearOffset * response / (1.0 - response);
  return std::copysign(std::pow(powered, 1.0 / 0.42), value);
}

[[nodiscard]] double wrap_degrees(double value) {
  auto wrapped = std::fmod(value, 360.0);
  if (wrapped < 0.0) {
    wrapped += 360.0;
  }
  return wrapped;
}

[[nodiscard]] double hellwig_lightness_to_luminance(
    double lightness,
    const Aces2HellwigJmhParameters& parameters) {
  const auto achromatic = std::pow(
      std::abs(lightness) / kHellwigLightnessScale,
      parameters.inverse_lightness_exponent);
  const auto response = parameters.achromatic_white_response * achromatic;
  return post_adaptation_response_inverse(response) /
         parameters.normalized_luminance_adaptation;
}

[[nodiscard]] double luminance_to_hellwig_lightness(
    double luminance,
    const Aces2HellwigJmhParameters& parameters) {
  const auto response = post_adaptation_response_forward(
      std::abs(luminance) * parameters.normalized_luminance_adaptation);
  const auto lightness = kHellwigLightnessScale * std::pow(
      response * parameters.inverse_achromatic_white_response,
      parameters.lightness_exponent);
  return std::copysign(lightness, luminance);
}

[[nodiscard]] double aces2_toe(
    double value,
    double limit,
    double k1_input,
    double k2_input) {
  if (value > limit) {
    return value;
  }
  const auto k2 = std::max(k2_input, 0.001);
  const auto k1 = std::hypot(k1_input, k2);
  const auto k3 = (limit + k1) / (limit + k2);
  const auto minus_b = k3 * value - k1;
  const auto minus_c = k2 * k3 * value;
  return 0.5 *
         (minus_b + std::sqrt(std::max(
                        0.0,
                        minus_b * minus_b + 4.0 * minus_c)));
}

[[nodiscard]] double reach_colorfulness_at_hue(
    double hue_degrees,
    const Aces2ChromaCompressionParameters& parameters) {
  const auto hue = wrap_degrees(hue_degrees);
  const auto lower = static_cast<std::size_t>(hue);
  const auto fraction = hue - static_cast<double>(lower);
  const auto padded_lower = lower + 1U;
  return std::lerp(
      parameters.reach_colorfulness[padded_lower],
      parameters.reach_colorfulness[padded_lower + 1U],
      fraction);
}

[[nodiscard]] double chroma_normalization_at_hue(
    double hue_degrees,
    double scale) {
  const auto radians = hue_degrees * std::numbers::pi / 180.0;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  const auto cosine_2 = cosine * cosine - sine * sine;
  const auto sine_2 = 2.0 * cosine * sine;
  const auto cosine_3 = 4.0 * cosine * cosine * cosine - 3.0 * cosine;
  const auto sine_3 = 3.0 * sine - 4.0 * sine * sine * sine;
  const auto colorfulness =
      11.34072 * cosine + 16.46899 * cosine_2 + 7.88380 * cosine_3 +
      14.66441 * sine - 6.37224 * sine_2 + 9.19364 * sine_3 +
      77.12896;
  return colorfulness * scale;
}

[[nodiscard]] bool rgb_inside_display_cube(
    const std::array<double, 3>& rgb,
    double maximum) {
  constexpr double tolerance = 1.0e-10;
  return std::all_of(
      rgb.begin(),
      rgb.end(),
      [maximum](double channel) {
        return std::isfinite(channel) && channel >= -tolerance &&
               channel <= maximum + tolerance;
      });
}

[[nodiscard]] double signed_power(double value, double exponent) {
  return std::copysign(std::pow(std::abs(value), exponent), value);
}

[[nodiscard]] double exact_target_gamut_boundary(
    double lightness,
    double hue_degrees,
    const Aces2HellwigJmhParameters& output_appearance,
    double maximum_physical_rgb,
    double known_outside_colorfulness = 0.0) {
  if (lightness <= 0.0 || maximum_physical_rgb <= 0.0) {
    return 0.0;
  }

  auto lower = 0.0;
  auto upper = std::max(known_outside_colorfulness, 16.0);
  auto upper_rgb = aces2_hellwig_jmh_to_linear_rgb(
      {.j = lightness, .m = upper, .h_degrees = hue_degrees},
      output_appearance);
  while (rgb_inside_display_cube(upper_rgb, maximum_physical_rgb) &&
         upper < 4096.0) {
    lower = upper;
    upper *= 2.0;
    upper_rgb = aces2_hellwig_jmh_to_linear_rgb(
        {.j = lightness, .m = upper, .h_degrees = hue_degrees},
        output_appearance);
  }
  if (rgb_inside_display_cube(upper_rgb, maximum_physical_rgb)) {
    return upper;
  }

  for (int iteration = 0; iteration < 28; ++iteration) {
    const auto sample = std::midpoint(lower, upper);
    const auto sample_rgb = aces2_hellwig_jmh_to_linear_rgb(
        {.j = lightness, .m = sample, .h_degrees = hue_degrees},
        output_appearance);
    if (rgb_inside_display_cube(sample_rgb, maximum_physical_rgb)) {
      lower = sample;
    } else {
      upper = sample;
    }
  }
  return lower;
}

[[nodiscard]] double compress_colorfulness_to_boundary(
    double colorfulness,
    double boundary) {
  const auto safe_boundary = std::max(0.0, boundary);
  const auto threshold = 0.75 * safe_boundary;
  if (colorfulness > threshold && safe_boundary > threshold) {
    const auto distance =
        (colorfulness - threshold) / (safe_boundary - threshold);
    return threshold +
           (safe_boundary - threshold) * (1.0 - std::exp(-distance));
  }
  return std::min(colorfulness, safe_boundary);
}

[[nodiscard]] std::shared_ptr<const Aces2GamutBoundaryTable>
build_gamut_boundary_table(
    const Aces2HellwigJmhParameters& output_appearance,
    double maximum_physical_rgb) {
  const auto maximum_lightness = aces2_linear_rgb_to_hellwig_jmh(
      {maximum_physical_rgb, maximum_physical_rgb, maximum_physical_rgb},
      output_appearance)
                                     .j;
  auto table = std::make_shared<Aces2GamutBoundaryTable>();
  table->lightness_sample_count = kCompiledGamutLightnessSamples;
  table->hue_sample_count = kCompiledGamutHueSamples;
  table->maximum_lightness = maximum_lightness;
  table->maximum_colorfulness.resize(
      table->lightness_sample_count * table->hue_sample_count,
      0.0);

  std::atomic<std::size_t> next_lightness_index = 0U;
  const auto hardware_threads = std::max(1U, std::thread::hardware_concurrency());
  const auto worker_count = std::min(
      table->lightness_sample_count,
      static_cast<std::size_t>(hardware_threads));
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (std::size_t worker_index = 0U;
       worker_index < worker_count;
       ++worker_index) {
    workers.emplace_back([&, output_appearance, maximum_physical_rgb]() {
      while (true) {
        const auto lightness_index =
            next_lightness_index.fetch_add(1U, std::memory_order_relaxed);
        if (lightness_index >= table->lightness_sample_count) {
          break;
        }
        const auto lightness = maximum_lightness *
                               static_cast<double>(lightness_index) /
                               static_cast<double>(
                                   table->lightness_sample_count - 1U);
        for (std::size_t hue_index = 0U;
             hue_index < table->hue_sample_count;
             ++hue_index) {
          table->maximum_colorfulness[
              lightness_index * table->hue_sample_count + hue_index] =
              exact_target_gamut_boundary(
                  lightness,
                  static_cast<double>(hue_index),
                  output_appearance,
                  maximum_physical_rgb);
        }
      }
    });
  }
  for (auto& worker : workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  return table;
}

[[nodiscard]] bool gamut_boundary_table_valid(
    const Aces2GamutBoundaryTable& table) {
  return table.lightness_sample_count >= 2U &&
         table.hue_sample_count >= 2U &&
         table.maximum_lightness > 0.0 &&
         table.maximum_colorfulness.size() ==
             table.lightness_sample_count * table.hue_sample_count;
}

[[nodiscard]] double interpolated_target_gamut_boundary(
    double lightness,
    double hue_degrees,
    const Aces2GamutBoundaryTable& table) {
  if (!gamut_boundary_table_valid(table)) {
    return 0.0;
  }
  const auto lightness_position =
      std::clamp(lightness / table.maximum_lightness, 0.0, 1.0) *
      static_cast<double>(table.lightness_sample_count - 1U);
  const auto lightness_lower = static_cast<std::size_t>(lightness_position);
  const auto lightness_upper = std::min(
      lightness_lower + 1U,
      table.lightness_sample_count - 1U);
  const auto lightness_fraction =
      lightness_position - static_cast<double>(lightness_lower);

  const auto normalized_hue = wrap_degrees(hue_degrees) *
                              static_cast<double>(table.hue_sample_count) /
                              360.0;
  const auto hue_lower =
      static_cast<std::size_t>(normalized_hue) % table.hue_sample_count;
  const auto hue_upper = (hue_lower + 1U) % table.hue_sample_count;
  const auto hue_fraction =
      normalized_hue - std::floor(normalized_hue);
  const auto sample = [&](std::size_t lightness_sample,
                          std::size_t hue_sample) {
    return table.maximum_colorfulness[
        lightness_sample * table.hue_sample_count + hue_sample];
  };
  const auto lower_boundary = std::lerp(
      sample(lightness_lower, hue_lower),
      sample(lightness_lower, hue_upper),
      hue_fraction);
  const auto upper_boundary = std::lerp(
      sample(lightness_upper, hue_lower),
      sample(lightness_upper, hue_upper),
      hue_fraction);
  return 0.995 * std::lerp(
                     lower_boundary,
                     upper_boundary,
                     lightness_fraction);
}

}  // namespace

Aces2ToneScaleParameters make_aces2_tone_scale(double peak_luminance_nits) {
  const auto peak = std::clamp(peak_luminance_nits, 1.0, 10000.0);
  constexpr double normalized_white = 100.0;
  constexpr double contrast = 1.15;
  constexpr double grey = 0.18;
  constexpr double grey_luminance = 10.013;
  constexpr double grey_change_per_stop = 0.14;
  constexpr double flare = 0.04;
  constexpr double sdr_scene_roof = 128.0;
  constexpr double hdr_scene_roof = 896.0;

  const auto peak_position =
      std::log(peak / normalized_white) / std::log(10000.0 / normalized_white);
  const auto scene_roof =
      sdr_scene_roof + (hdr_scene_roof - sdr_scene_roof) * peak_position;
  const auto peak_scale = peak / normalized_white;
  const auto peak_flare =
      0.5 * (peak_scale + std::sqrt(peak_scale * (peak_scale + 4.0 * flare)));
  const auto roof_response = std::pow(
      (scene_roof / peak_flare) / ((scene_roof / peak_flare) + 1.0),
      contrast);
  const auto output_scale = peak_flare / roof_response;
  const auto peak_stops = std::log(peak / normalized_white) / std::log(2.0);
  const auto grey_target =
      grey_luminance / normalized_white * (1.0 + peak_stops * grey_change_per_stop);
  const auto grey_flare =
      0.5 * (grey_target + std::sqrt(grey_target * (grey_target + 4.0 * flare)));
  const auto grey_intersection =
      -(peak_flare * std::pow(grey_flare / output_scale, 1.0 / contrast)) /
      (std::pow(grey_flare / output_scale, 1.0 / contrast) - 1.0);
  const auto grey_weight = grey / grey_intersection;
  const auto scene_scale = grey_weight * peak_flare;
  const auto output_power = std::pow(
      (scene_roof / peak_flare) / ((scene_roof / peak_flare) + grey_weight),
      contrast);

  return Aces2ToneScaleParameters{
      .peak_luminance_nits = peak,
      .normalized_white_nits = normalized_white,
      .contrast = contrast,
      .flare = flare,
      .scene_scale = scene_scale,
      .output_scale = peak_flare / output_power,
      .output_power = output_power,
      .forward_limit = 8.0 * scene_roof,
      .inverse_limit = peak / (output_power * normalized_white),
  };
}

double aces2_tone_scale_forward(
    double scene_linear,
    const Aces2ToneScaleParameters& parameters) {
  const auto input = std::clamp(scene_linear, 0.0, parameters.forward_limit);
  const auto denominator = input + parameters.scene_scale;
  if (denominator <= 0.0) {
    return 0.0;
  }
  const auto response = parameters.output_scale *
                        std::pow(input / denominator, parameters.contrast);
  const auto flared = response <= 0.0
                          ? 0.0
                          : response * response / (response + parameters.flare);
  return std::clamp(
      flared * parameters.normalized_white_nits,
      0.0,
      parameters.peak_luminance_nits);
}

double aces2_tone_scale_inverse(
    double display_luminance_nits,
    const Aces2ToneScaleParameters& parameters) {
  const auto normalized = std::clamp(
      display_luminance_nits / parameters.normalized_white_nits,
      0.0,
      parameters.inverse_limit);
  if (normalized <= 0.0) {
    return 0.0;
  }
  const auto response =
      0.5 * (normalized + std::sqrt(normalized * (4.0 * parameters.flare + normalized)));
  const auto divisor =
      std::pow(parameters.output_scale / response, 1.0 / parameters.contrast) - 1.0;
  return divisor <= 0.0 ? parameters.forward_limit
                        : parameters.scene_scale / divisor;
}

Aces2HellwigJmhParameters make_aces2_hellwig_jmh_parameters(
    const RgbColorSpace& color_space) {
  // Column-vector form of the ACES 2 MATRIX_16 and Aab matrices. The CTL
  // reference uses row vectors, so its matrices are transposed here.
  const auto xyz_to_cam16 = matrix3_from_rows(
      0.401288, 0.650173, -0.051461,
      -0.250268, 1.204414, 0.045854,
      -0.002079, 0.048952, 0.953127);
  const auto rgb_to_xyz = rgb_to_xyz_matrix(color_space);
  const auto white_xyz = multiply_matrix3_vector(
      rgb_to_xyz,
      {kAcesReferenceLuminance,
       kAcesReferenceLuminance,
       kAcesReferenceLuminance});
  const auto white_cone = multiply_matrix3_vector(xyz_to_cam16, white_xyz);
  const auto y_white = white_xyz[1];

  const auto k = 1.0 / (5.0 * kAcesAdaptingLuminance + 1.0);
  const auto k4 = k * k * k * k;
  const auto luminance_adaptation =
      0.2 * k4 * (5.0 * kAcesAdaptingLuminance) +
      0.1 * std::pow(1.0 - k4, 2.0) *
          std::pow(5.0 * kAcesAdaptingLuminance, 1.0 / 3.0);
  const auto normalized_luminance_adaptation =
      luminance_adaptation / kAcesReferenceLuminance;
  const auto cone_adaptation = matrix3_from_rows(
      normalized_luminance_adaptation * y_white / white_cone[0], 0.0, 0.0,
      0.0, normalized_luminance_adaptation * y_white / white_cone[1], 0.0,
      0.0, 0.0, normalized_luminance_adaptation * y_white / white_cone[2]);
  const auto reference_scale = matrix3_from_rows(
      kAcesReferenceLuminance, 0.0, 0.0,
      0.0, kAcesReferenceLuminance, 0.0,
      0.0, 0.0, kAcesReferenceLuminance);
  const auto rgb_to_adapted_cone = multiply_matrix3(
      cone_adaptation,
      multiply_matrix3(
          reference_scale,
          multiply_matrix3(xyz_to_cam16, rgb_to_xyz)));

  const auto adapted_white = multiply_matrix3_vector(
      rgb_to_adapted_cone,
      {1.0, 1.0, 1.0});
  const std::array<double, 3> compressed_white = {
      post_adaptation_response_forward(adapted_white[0]),
      post_adaptation_response_forward(adapted_white[1]),
      post_adaptation_response_forward(adapted_white[2]),
  };
  const auto base_cone_to_aab = matrix3_from_rows(
      2.0, 1.0, 1.0 / 20.0,
      1.0, -12.0 / 11.0, 1.0 / 11.0,
      1.0 / 9.0, 1.0 / 9.0, -2.0 / 9.0);
  const auto raw_achromatic_white = kCamNonlinearScale *
      multiply_matrix3_vector(base_cone_to_aab, compressed_white)[0];
  const auto cone_to_aab_scale = matrix3_from_rows(
      kCamNonlinearScale / raw_achromatic_white, 0.0, 0.0,
      0.0, kCamNonlinearScale * 43.0 * kAcesColorfulnessSurround, 0.0,
      0.0, 0.0, kCamNonlinearScale * 43.0 * kAcesColorfulnessSurround);
  const auto adapted_cone_to_aab =
      multiply_matrix3(cone_to_aab_scale, base_cone_to_aab);
  const auto lightness_exponent =
      0.59 * (1.48 + std::sqrt(
                         kAcesBackgroundLuminance /
                         kAcesReferenceLuminance));
  const auto achromatic_white_response =
      post_adaptation_response_forward(luminance_adaptation);

  return {
      .color_space = color_space,
      .linear_rgb_to_adapted_cone = rgb_to_adapted_cone,
      .adapted_cone_to_linear_rgb = invert_matrix3(rgb_to_adapted_cone),
      .adapted_cone_to_aab = adapted_cone_to_aab,
      .aab_to_adapted_cone = invert_matrix3(adapted_cone_to_aab),
      .normalized_luminance_adaptation = normalized_luminance_adaptation,
      .lightness_exponent = lightness_exponent,
      .inverse_lightness_exponent = 1.0 / lightness_exponent,
      .achromatic_white_response = achromatic_white_response,
      .inverse_achromatic_white_response = 1.0 / achromatic_white_response,
  };
}

HellwigJmh aces2_linear_rgb_to_hellwig_jmh(
    const std::array<double, 3>& linear_rgb,
    const Aces2HellwigJmhParameters& parameters) {
  const auto adapted_cone = multiply_matrix3_vector(
      parameters.linear_rgb_to_adapted_cone,
      linear_rgb);
  const std::array<double, 3> compressed_cone = {
      post_adaptation_response_forward(adapted_cone[0]),
      post_adaptation_response_forward(adapted_cone[1]),
      post_adaptation_response_forward(adapted_cone[2]),
  };
  const auto aab = multiply_matrix3_vector(
      parameters.adapted_cone_to_aab,
      compressed_cone);
  if (aab[0] <= 0.0) {
    return {};
  }
  const auto lightness = kHellwigLightnessScale * std::pow(
      aab[0],
      parameters.lightness_exponent);
  const auto colorfulness = std::hypot(aab[1], aab[2]);
  const auto hue = wrap_degrees(
      std::atan2(aab[2], aab[1]) * 180.0 / std::numbers::pi);
  return {.j = lightness, .m = colorfulness, .h_degrees = hue};
}

std::array<double, 3> aces2_hellwig_jmh_to_linear_rgb(
    const HellwigJmh& jmh,
    const Aces2HellwigJmhParameters& parameters) {
  const auto hue_radians = jmh.h_degrees * std::numbers::pi / 180.0;
  const std::array<double, 3> aab = {
      std::pow(
          std::max(0.0, jmh.j) / kHellwigLightnessScale,
          parameters.inverse_lightness_exponent),
      std::max(0.0, jmh.m) * std::cos(hue_radians),
      std::max(0.0, jmh.m) * std::sin(hue_radians),
  };
  const auto compressed_cone = multiply_matrix3_vector(
      parameters.aab_to_adapted_cone,
      aab);
  const std::array<double, 3> adapted_cone = {
      post_adaptation_response_inverse(compressed_cone[0]),
      post_adaptation_response_inverse(compressed_cone[1]),
      post_adaptation_response_inverse(compressed_cone[2]),
  };
  return multiply_matrix3_vector(
      parameters.adapted_cone_to_linear_rgb,
      adapted_cone);
}

Aces2ChromaCompressionParameters make_aces2_chroma_compression_parameters(
    double peak_luminance_nits) {
  const auto tone_scale = make_aces2_tone_scale(peak_luminance_nits);
  const auto input_appearance = make_aces2_hellwig_jmh_parameters(
      make_aces2065_1_color_space());
  const auto reach_appearance = make_aces2_hellwig_jmh_parameters(
      make_acescg_color_space());
  const auto maximum_lightness = luminance_to_hellwig_lightness(
      tone_scale.peak_luminance_nits,
      input_appearance);
  std::array<double, kAces2PaddedHueTableSize> reach_colorfulness{};
  for (std::size_t index = 0; index < kAces2HueTableSize; ++index) {
    const auto hue = static_cast<double>(index);
    auto lower = 0.0;
    auto upper = 50.0;
    auto outside = false;
    while (!outside && upper < 1300.0) {
      const auto rgb = aces2_hellwig_jmh_to_linear_rgb(
          {.j = maximum_lightness, .m = upper, .h_degrees = hue},
          reach_appearance);
      outside = rgb[0] < 0.0 || rgb[1] < 0.0 || rgb[2] < 0.0;
      if (!outside) {
        lower = upper;
        upper += 50.0;
      }
    }
    while (upper - lower > 1.0e-2) {
      const auto sample = std::midpoint(lower, upper);
      const auto rgb = aces2_hellwig_jmh_to_linear_rgb(
          {.j = maximum_lightness, .m = sample, .h_degrees = hue},
          reach_appearance);
      if (rgb[0] < 0.0 || rgb[1] < 0.0 || rgb[2] < 0.0) {
        upper = sample;
      } else {
        lower = sample;
      }
    }
    reach_colorfulness[index + 1U] = upper;
  }
  reach_colorfulness[0] = reach_colorfulness[kAces2HueTableSize];
  reach_colorfulness[kAces2HueTableSize + 1U] = reach_colorfulness[1U];

  const auto log_peak = std::log2(
      tone_scale.peak_luminance_nits / tone_scale.normalized_white_nits);
  const auto model_gamma =
      0.59 * (1.48 + std::sqrt(
                         kAcesBackgroundLuminance /
                         kAcesReferenceLuminance));
  return {
      .tone_scale = tone_scale,
      .input_appearance = input_appearance,
      .reach_appearance = reach_appearance,
      .reach_colorfulness = reach_colorfulness,
      .maximum_lightness = maximum_lightness,
      .inverse_model_gamma = 1.0 / model_gamma,
      .saturation_expansion = std::max(
          0.2,
          1.3 - (1.3 * 0.69) * log_peak),
      .saturation_threshold = 0.5 / tone_scale.peak_luminance_nits,
      .highlight_compression = 2.4 + (2.4 * 3.3) * log_peak,
      .hue_normalization_scale =
          std::pow(0.03379 * tone_scale.peak_luminance_nits, 0.30596) -
          0.45135,
  };
}

HellwigJmh aces2_tone_and_chroma_compress(
    const HellwigJmh& jmh,
    const Aces2ChromaCompressionParameters& parameters) {
  const auto scene_linear = hellwig_lightness_to_luminance(
                                jmh.j,
                                parameters.input_appearance) /
                            kAcesReferenceLuminance;
  const auto tonemapped_luminance = aces2_tone_scale_forward(
      scene_linear,
      parameters.tone_scale);
  const auto tonemapped_lightness = luminance_to_hellwig_lightness(
      tonemapped_luminance,
      parameters.input_appearance);
  if (jmh.m == 0.0 || jmh.j <= 0.0 || tonemapped_lightness <= 0.0) {
    return {
        .j = tonemapped_lightness,
        .m = 0.0,
        .h_degrees = wrap_degrees(jmh.h_degrees),
    };
  }

  const auto normalized_lightness = std::clamp(
      tonemapped_lightness / parameters.maximum_lightness,
      0.0,
      1.0);
  const auto inverse_lightness = std::max(0.0, 1.0 - normalized_lightness);
  const auto normalization = chroma_normalization_at_hue(
      jmh.h_degrees,
      parameters.hue_normalization_scale);
  if (normalization <= 0.0) {
    return {
        .j = tonemapped_lightness,
        .m = 0.0,
        .h_degrees = wrap_degrees(jmh.h_degrees),
    };
  }
  const auto limit =
      std::pow(normalized_lightness, parameters.inverse_model_gamma) *
      reach_colorfulness_at_hue(jmh.h_degrees, parameters) /
      normalization;
  const auto toe_limit = limit - 0.001;
  const auto expansion_strength =
      inverse_lightness * parameters.saturation_expansion;
  const auto expansion_threshold = std::sqrt(
      normalized_lightness * normalized_lightness +
      parameters.saturation_threshold);
  const auto compression_strength =
      normalized_lightness * parameters.highlight_compression;

  auto compressed_colorfulness =
      jmh.m * std::pow(
                  tonemapped_lightness / jmh.j,
                  parameters.inverse_model_gamma) /
      normalization;
  compressed_colorfulness =
      limit - aces2_toe(
                  limit - compressed_colorfulness,
                  toe_limit,
                  expansion_strength,
                  expansion_threshold);
  compressed_colorfulness = aces2_toe(
      compressed_colorfulness,
      limit,
      compression_strength,
      inverse_lightness);
  return {
      .j = tonemapped_lightness,
      .m = std::max(0.0, compressed_colorfulness * normalization),
      .h_degrees = wrap_degrees(jmh.h_degrees),
  };
}

Aces2OutputRenderingParameters make_aces2_output_rendering_parameters(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    std::string_view chromatic_adaptation_method,
    Aces2OutputImplementation implementation,
    RendererTransitionLook transition_look) {
  const auto safe_target = display_target_valid(display_target)
                               ? display_target
                               : make_sdr_display_target(
                                     output_color_profile_from_name("sRGB"));
  const auto aces2065_1 = make_aces2065_1_color_space();
  const auto acescg = make_acescg_color_space();
  const auto peak = std::clamp(
      safe_target.peak_luminance_nits,
      1.0,
      10000.0);
  auto parameters = Aces2OutputRenderingParameters{
      .display_target = safe_target,
      .transition_look = transition_look,
      .tone_and_chroma = make_aces2_chroma_compression_parameters(peak),
      .output_appearance = make_aces2_hellwig_jmh_parameters(
          safe_target.color_space),
      .working_to_aces2065_1 = compile_builtin_color_transform(
                                   working_color_space,
                                   aces2065_1,
                                   chromatic_adaptation_method)
                                   .matrix,
      .aces2065_1_to_acescg = compile_builtin_color_transform(
                                  aces2065_1,
                                  acescg,
                                  chromatic_adaptation_method)
                                  .matrix,
      .acescg_to_aces2065_1 = compile_builtin_color_transform(
                                  acescg,
                                  aces2065_1,
                                  chromatic_adaptation_method)
                                  .matrix,
      .acescg_to_official_input = compile_builtin_color_transform(
                                      acescg,
                                      aces2065_1,
                                      chromatic_adaptation_method)
                                      .matrix,
      .maximum_physical_rgb = peak / kAcesReferenceLuminance,
      .physical_to_reference_white =
          kAcesReferenceLuminance / safe_target.reference_white_nits,
  };
  if (implementation == Aces2OutputImplementation::OfficialWhenAvailable) {
    parameters.official_transform =
        compile_official_aces2_output_transform(
            working_color_space, safe_target);
    if (parameters.official_transform != nullptr &&
        parameters.official_transform->processor_input_is_working) {
      parameters.acescg_to_official_input =
          compile_builtin_color_transform(
              acescg,
              working_color_space,
              chromatic_adaptation_method)
              .matrix;
    }
  }
  return parameters;
}

std::shared_ptr<const Aces2OutputProcessor> compile_aces2_output_processor(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    std::string_view chromatic_adaptation_method,
    Aces2OutputImplementation implementation,
    RendererTransitionLook transition_look) {
  auto& cache = output_processor_cache();
  // Processor construction is expensive (the official OCIO route may build a
  // complete transform). Keep lookup and construction under one lock so a
  // thumbnail or tile burst cannot fan one cold miss out into many identical
  // builds. Distinct first-use targets serialize here as well; there are at
  // most eight resident targets, and subsequent calls are lock-short hits.
  const std::scoped_lock lock(cache.mutex);
  ++cache.stats.requests;
  const auto match = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const OutputProcessorCacheEntry& entry) {
        return same_color_space(
                   entry.working_color_space,
                   working_color_space) &&
               same_display_target(entry.display_target, display_target) &&
               entry.chromatic_adaptation_method ==
                   chromatic_adaptation_method &&
               entry.implementation == implementation &&
               entry.transition_look == transition_look;
      });
  if (match != cache.entries.end()) {
    ++cache.stats.hits;
    match->last_use = ++cache.use_counter;
    cache.stats.entries = cache.entries.size();
    return match->processor;
  }
  ++cache.stats.misses;

  auto parameters = make_aces2_output_rendering_parameters(
      working_color_space,
      display_target,
      chromatic_adaptation_method,
      implementation,
      transition_look);
  if (parameters.official_transform == nullptr) {
    parameters.gamut_boundary_table = build_gamut_boundary_table(
        parameters.output_appearance,
        parameters.maximum_physical_rgb);
  }
  const auto processor_key = output_processor_cache_key(
      working_color_space,
      display_target,
      chromatic_adaptation_method,
      parameters.official_transform != nullptr
          ? parameters.official_transform->implementation_id
          : std::string_view{},
      transition_look);
  auto processor = std::make_shared<const Aces2OutputProcessor>(
      Aces2OutputProcessor{
          .parameters = std::move(parameters),
          .cache_key = processor_key,
      });

  if (cache.entries.size() >= kOutputProcessorCacheCapacity) {
    const auto least_recently_used = std::min_element(
        cache.entries.begin(),
        cache.entries.end(),
        [](const OutputProcessorCacheEntry& left,
           const OutputProcessorCacheEntry& right) {
          return left.last_use < right.last_use;
        });
    cache.entries.erase(least_recently_used);
    ++cache.stats.evictions;
  }
  cache.entries.push_back(OutputProcessorCacheEntry{
      .working_color_space = working_color_space,
      .display_target = display_target,
      .chromatic_adaptation_method =
          std::string(chromatic_adaptation_method),
      .implementation = implementation,
      .transition_look = transition_look,
      .processor = processor,
      .last_use = ++cache.use_counter,
  });
  ++cache.stats.builds;
  cache.stats.entries = cache.entries.size();
  return processor;
}

Aces2OutputProcessorCacheStats aces2_output_processor_cache_stats() {
  auto& cache = output_processor_cache();
  const std::scoped_lock lock(cache.mutex);
  auto stats = cache.stats;
  stats.entries = cache.entries.size();
  return stats;
}

void clear_aces2_output_processor_cache_for_testing() {
  auto& cache = output_processor_cache();
  const std::scoped_lock lock(cache.mutex);
  cache.entries.clear();
  cache.stats = {};
  cache.use_counter = 0U;
}

bool Aces2OutputLut3D::valid() const {
  if (!output_lut_settings_valid(settings) ||
      !std::isfinite(shaper_maximum) || shaper_maximum <= 0.0 ||
      !std::isfinite(maximum_physical_rgb) || maximum_physical_rgb <= 0.0 ||
      !std::isfinite(physical_to_reference_white) ||
      physical_to_reference_white <= 0.0 || cache_key.empty()) {
    return false;
  }
  const auto size = static_cast<std::size_t>(settings.cube_size);
  return table.size() == size * size * size;
}

std::size_t Aces2OutputLut3D::byte_size() const {
  return table.size() * sizeof(table.front());
}

Aces2OutputLutSettings aces2_output_lut_settings(
    Aces2OutputLutQuality quality) {
  switch (quality) {
    case Aces2OutputLutQuality::Interactive:
      // The 65-cube tier is intentionally a transient preview approximation.
      // Its looser gate is measured separately from final-output parity.
      return {.cube_size = 65, .shaper_offset = 0.03};
    case Aces2OutputLutQuality::Final:
      // The 0.05 toe allocation is measured against the direct OCIO ACES 2
      // display-view processor. It keeps both p99 and worst-patch error inside
      // the final gate after the standard-target processor was made exact.
      return {.cube_size = 97, .shaper_offset = 0.05};
  }
  return {};
}

namespace {

[[nodiscard]] std::array<double, 3>
apply_renderer_transition_to_aces2065_1(
    std::array<double, 3> aces2065_1,
    const Aces2OutputRenderingParameters& parameters) {
  if (parameters.transition_look !=
      RendererTransitionLook::PreserveLegacyDirectLuminanceV1) {
    return aces2065_1;
  }
  const auto appearance = aces2_linear_rgb_to_hellwig_jmh(
      aces2065_1,
      parameters.tone_and_chroma.input_appearance);
  const auto scene_luminance =
      hellwig_lightness_to_luminance(
          appearance.j,
          parameters.tone_and_chroma.input_appearance) /
      kAcesReferenceLuminance;
  if (!std::isfinite(scene_luminance) || scene_luminance <= 1.0e-12) {
    return aces2065_1;
  }
  const auto desired_display_luminance = std::clamp(
      scene_luminance * parameters.display_target.reference_white_nits,
      0.0,
      parameters.display_target.peak_luminance_nits);
  const auto compensated_scene_luminance = aces2_tone_scale_inverse(
      desired_display_luminance,
      parameters.tone_and_chroma.tone_scale);
  const auto scale = compensated_scene_luminance / scene_luminance;
  if (!std::isfinite(scale) || scale < 0.0) {
    return aces2065_1;
  }
  for (auto& channel : aces2065_1) {
    channel *= scale;
  }
  return aces2065_1;
}

[[nodiscard]] std::array<double, 3> transition_official_lut_input(
    std::array<double, 3> acescg,
    const Aces2OutputRenderingParameters& parameters,
    double shaper_maximum) {
  if (parameters.transition_look ==
      RendererTransitionLook::PreserveLegacyDirectLuminanceV1) {
    const auto aces2065_1 = apply_renderer_transition_to_aces2065_1(
        multiply_matrix3_vector(parameters.acescg_to_aces2065_1, acescg),
        parameters);
    acescg = multiply_matrix3_vector(
        parameters.aces2065_1_to_acescg, aces2065_1);
    for (auto& channel : acescg) {
      channel = std::clamp(channel, 0.0, shaper_maximum);
    }
  }
  return multiply_matrix3_vector(
      parameters.acescg_to_official_input, acescg);
}

}  // namespace

std::shared_ptr<const Aces2OutputLut3D> compile_aces2_output_lut(
    const RgbColorSpace& working_color_space,
    const DisplayTarget& display_target,
    Aces2OutputLutSettings settings,
    std::string_view chromatic_adaptation_method,
    RendererTransitionLook transition_look) {
  if (!output_lut_settings_valid(settings)) {
    auto& failure_cache = output_lut_cache();
    const std::scoped_lock lock(failure_cache.mutex);
    failure_cache.stats.last_failure_reason = "invalid_lut_settings";
    return {};
  }
  const auto processor = compile_aces2_output_processor(
      working_color_space,
      display_target,
      chromatic_adaptation_method,
      Aces2OutputImplementation::OfficialWhenAvailable,
      transition_look);
  if (processor == nullptr ||
      processor->parameters.official_transform == nullptr) {
    auto& failure_cache = output_lut_cache();
    const std::scoped_lock lock(failure_cache.mutex);
    failure_cache.stats.last_failure_reason =
        "official_aces2_processor_unavailable";
    return {};
  }
  const auto key = output_lut_cache_key(*processor, settings);
  auto& cache = output_lut_cache();
  // As with processor compilation, one cold LUT key must have one builder.
  // A 97-cube contains more than 900k samples; allowing every thumbnail worker
  // to build the same cube in parallel creates seconds of avoidable latency
  // and a large transient memory spike.
  const std::scoped_lock lock(cache.mutex);
  ++cache.stats.requests;
  const auto match = std::find_if(
      cache.entries.begin(),
      cache.entries.end(),
      [&](const OutputLutCacheEntry& entry) { return entry.key == key; });
  if (match != cache.entries.end()) {
    ++cache.stats.hits;
    match->last_use = ++cache.use_counter;
    cache.stats.entries = cache.entries.size();
    cache.stats.bytes = 0U;
    for (const auto& entry : cache.entries) {
      cache.stats.bytes += entry.lut->byte_size();
    }
    cache.stats.last_failure_reason.clear();
    return match->lut;
  }
  ++cache.stats.misses;

  const auto size = static_cast<std::size_t>(settings.cube_size);
  const auto table_size = size * size * size;
  auto lut = std::make_shared<Aces2OutputLut3D>();
  lut->settings = settings;
  lut->shaper_maximum = official_aces2_input_limit(
      processor->parameters.display_target.peak_luminance_nits);
  lut->maximum_physical_rgb = processor->parameters.maximum_physical_rgb;
  lut->physical_to_reference_white =
      processor->parameters.physical_to_reference_white;
  lut->working_to_acescg = multiply_matrix3(
      processor->parameters.aces2065_1_to_acescg,
      processor->parameters.working_to_aces2065_1);
  lut->table.resize(table_size);
  lut->cache_key = key;

  std::vector<double> decoded_values(size);
  for (std::size_t index = 0U; index < size; ++index) {
    decoded_values[index] = decode_log_shaper(
        static_cast<double>(index) / static_cast<double>(size - 1U),
        lut->shaper_maximum,
        settings.shaper_offset);
  }
  for (int red = 0; red < settings.cube_size; ++red) {
    for (int green = 0; green < settings.cube_size; ++green) {
      for (int blue = 0; blue < settings.cube_size; ++blue) {
        const auto official_input = transition_official_lut_input(
            {
                decoded_values[static_cast<std::size_t>(red)],
                decoded_values[static_cast<std::size_t>(green)],
                decoded_values[static_cast<std::size_t>(blue)],
            },
            processor->parameters,
            lut->shaper_maximum);
        lut->table[output_lut_table_index(
            settings.cube_size,
            red,
            green,
            blue)] = {
            static_cast<float>(official_input[0]),
            static_cast<float>(official_input[1]),
            static_cast<float>(official_input[2]),
            1.0F,
        };
      }
    }
  }
  if (!processor->parameters.official_transform->apply_rgba(lut->table)) {
    cache.stats.last_failure_reason = "ocio_bulk_apply_failed";
    return {};
  }
  for (int red = 0; red < settings.cube_size; ++red) {
    for (int green = 0; green < settings.cube_size; ++green) {
      for (int blue = 0; blue < settings.cube_size; ++blue) {
        auto& value = lut->table[output_lut_table_index(
            settings.cube_size,
            red,
            green,
            blue)];
        if (std::isfinite(value[0]) && std::isfinite(value[1]) &&
            std::isfinite(value[2]) && std::isfinite(value[3])) {
          continue;
        }
        const auto official_input = transition_official_lut_input(
            {
                decoded_values[static_cast<std::size_t>(red)],
                decoded_values[static_cast<std::size_t>(green)],
                decoded_values[static_cast<std::size_t>(blue)],
            },
            processor->parameters,
            lut->shaper_maximum);
        const auto repaired =
            processor->parameters.official_transform->apply(official_input);
        value = {
            static_cast<float>(repaired[0]),
            static_cast<float>(repaired[1]),
            static_cast<float>(repaired[2]),
            1.0F,
        };
        ++lut->repaired_non_finite_samples;
      }
    }
  }
  const auto non_finite = std::find_if(
      lut->table.cbegin(),
      lut->table.cend(),
      [](const std::array<float, 4>& value) {
        return !std::isfinite(value[0]) || !std::isfinite(value[1]) ||
               !std::isfinite(value[2]) || !std::isfinite(value[3]);
      });
  if (non_finite != lut->table.cend()) {
    const auto index = static_cast<std::size_t>(
        std::distance(lut->table.cbegin(), non_finite));
    cache.stats.last_failure_reason =
        "ocio_output_contains_non_finite_values:index=" +
        std::to_string(index) + ":rgb=" +
        std::to_string((*non_finite)[0]) + "," +
        std::to_string((*non_finite)[1]) + "," +
        std::to_string((*non_finite)[2]);
    return {};
  }
  if (!lut->valid()) {
    cache.stats.last_failure_reason = "compiled_lut_invalid";
    return {};
  }
  if (lut->byte_size() >
      kOutputLutCacheMaximumBytes) {
    cache.stats.last_failure_reason =
        "compiled_lut_exceeds_cache_byte_ceiling";
    return {};
  }

  const auto resident_bytes = [&]() {
    auto bytes = std::size_t{0U};
    for (const auto& entry : cache.entries) {
      bytes += entry.lut->byte_size();
    }
    return bytes;
  };
  while (!cache.entries.empty() &&
         (cache.entries.size() >=
              kOutputLutCacheCapacity ||
          resident_bytes() +
                  lut->byte_size() >
              kOutputLutCacheMaximumBytes)) {
    const auto least_recently_used = std::min_element(
        cache.entries.begin(),
        cache.entries.end(),
        [](const OutputLutCacheEntry& left, const OutputLutCacheEntry& right) {
          return left.last_use < right.last_use;
        });
    cache.entries.erase(least_recently_used);
    ++cache.stats.evictions;
  }
  cache.entries.push_back(OutputLutCacheEntry{
      .key = key,
      .lut = lut,
      .last_use = ++cache.use_counter,
  });
  ++cache.stats.builds;
  cache.stats.entries = cache.entries.size();
  cache.stats.bytes = 0U;
  for (const auto& entry : cache.entries) {
    cache.stats.bytes += entry.lut->byte_size();
  }
  cache.stats.last_failure_reason.clear();
  return lut;
}

std::array<double, 3> aces2_output_lut_apply(
    const Aces2OutputLut3D& lut,
    const std::array<double, 3>& working_linear_rgb) {
  if (!lut.valid()) {
    return working_linear_rgb;
  }
  auto acescg = multiply_matrix3_vector(
      lut.working_to_acescg,
      working_linear_rgb);
  std::array<double, 3> coordinates{};
  for (std::size_t channel = 0U; channel < acescg.size(); ++channel) {
    acescg[channel] = std::clamp(
        acescg[channel],
        0.0,
        lut.shaper_maximum);
    coordinates[channel] = encode_log_shaper(
        acescg[channel],
        lut.shaper_maximum,
        lut.settings.shaper_offset);
  }
  auto output = tetrahedral_lut_sample(lut, coordinates);
  for (auto& channel : output) {
    channel = std::clamp(channel, 0.0, lut.maximum_physical_rgb) *
              lut.physical_to_reference_white;
  }
  return output;
}

Aces2OutputLutCacheStats aces2_output_lut_cache_stats() {
  auto& cache = output_lut_cache();
  const std::scoped_lock lock(cache.mutex);
  auto stats = cache.stats;
  stats.entries = cache.entries.size();
  stats.bytes = 0U;
  for (const auto& entry : cache.entries) {
    stats.bytes += entry.lut->byte_size();
  }
  stats.maximum_bytes =
      kOutputLutCacheMaximumBytes;
  return stats;
}

void clear_aces2_output_lut_cache_for_testing() {
  auto& cache = output_lut_cache();
  const std::scoped_lock lock(cache.mutex);
  cache.entries.clear();
  cache.stats = {};
  cache.use_counter = 0U;
}

bool aces2_official_output_transform_available() {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
  return true;
#else
  return false;
#endif
}

std::string_view aces2_output_renderer_backend_name() {
  return aces2_official_output_transform_available()
             ? std::string_view{"OpenColorIO ACES 2"}
             : std::string_view{"native ACES 2 fallback"};
}

std::string_view aces2_output_renderer_version() {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
  return std::string_view{"ocio-aces2-output-transform:"
                          PHOTO_EDITOR_OCIO_COMPILED_VERSION};
#else
  return std::string_view{"aces2-hellwig-jmh-compiled-boundary:v1"};
#endif
}

std::string_view aces2_output_processor_backend_name(
    const Aces2OutputProcessor& processor) {
  return processor.parameters.official_transform != nullptr
             ? std::string_view{"OpenColorIO ACES 2"}
             : std::string_view{"native ACES 2 fallback"};
}

std::string_view aces2_output_processor_version(
    const Aces2OutputProcessor& processor) {
  if (processor.parameters.official_transform != nullptr) {
#if defined(PHOTO_EDITOR_HAS_OCIO_ACES2) && PHOTO_EDITOR_HAS_OCIO_ACES2
    return std::string_view{"ocio-aces2-output-transform:"
                            PHOTO_EDITOR_OCIO_COMPILED_VERSION};
#endif
  }
  return std::string_view{"aces2-hellwig-jmh-compiled-boundary:v1"};
}

std::array<double, 3> aces2_render_to_linear_display_rgb(
    const std::array<double, 3>& working_linear_rgb,
    const Aces2OutputRenderingParameters& parameters) {
  auto aces2065_1 = multiply_matrix3_vector(
      parameters.working_to_aces2065_1,
      working_linear_rgb);
  aces2065_1 = apply_renderer_transition_to_aces2065_1(
      aces2065_1, parameters);
  auto acescg = multiply_matrix3_vector(
      parameters.aces2065_1_to_acescg,
      aces2065_1);
  const auto official_input_limit = official_aces2_input_limit(
      parameters.display_target.peak_luminance_nits);
  const auto input_limit = parameters.official_transform != nullptr
                               ? official_input_limit
                               : parameters.tone_and_chroma.tone_scale.forward_limit;
  for (auto& channel : acescg) {
    channel = std::clamp(
        channel,
        0.0,
        input_limit);
  }
  aces2065_1 = multiply_matrix3_vector(
      parameters.acescg_to_aces2065_1,
      acescg);
  if (parameters.official_transform != nullptr) {
    const auto official_input = multiply_matrix3_vector(
        parameters.acescg_to_official_input,
        acescg);
    auto official_output =
        parameters.official_transform->apply(official_input);
    for (auto& channel : official_output) {
      channel = std::clamp(
                    channel,
                    0.0,
                    parameters.maximum_physical_rgb) *
                parameters.physical_to_reference_white;
    }
    return official_output;
  }
  const auto scene_jmh = aces2_linear_rgb_to_hellwig_jmh(
      aces2065_1,
      parameters.tone_and_chroma.input_appearance);
  auto rendered_jmh = aces2_tone_and_chroma_compress(
      scene_jmh,
      parameters.tone_and_chroma);
  auto physical_rgb = aces2_hellwig_jmh_to_linear_rgb(
      rendered_jmh,
      parameters.output_appearance);

  if (!rgb_inside_display_cube(
          physical_rgb,
          parameters.maximum_physical_rgb) &&
      rendered_jmh.m > 0.0) {
    // The uncached reference path finds the exact constant-J/h boundary. A
    // compiled processor samples its immutable boundary table, then verifies
    // containment and falls back to the exact search if interpolation was
    // optimistic. Both paths use the same smooth 75-percent onset.
    const auto original_colorfulness = rendered_jmh.m;
    const auto has_compiled_boundary =
        parameters.gamut_boundary_table != nullptr &&
        gamut_boundary_table_valid(*parameters.gamut_boundary_table);
    const auto boundary_estimate =
        has_compiled_boundary
            ? interpolated_target_gamut_boundary(
                  rendered_jmh.j,
                  rendered_jmh.h_degrees,
                  *parameters.gamut_boundary_table)
            : 0.0;
    const auto boundary =
        has_compiled_boundary
            ? boundary_estimate
            : exact_target_gamut_boundary(
                  rendered_jmh.j,
                  rendered_jmh.h_degrees,
                  parameters.output_appearance,
                  parameters.maximum_physical_rgb,
                  original_colorfulness);
    rendered_jmh.m = compress_colorfulness_to_boundary(
        original_colorfulness,
        boundary);
    physical_rgb = aces2_hellwig_jmh_to_linear_rgb(
        rendered_jmh,
        parameters.output_appearance);
    if (!rgb_inside_display_cube(
            physical_rgb,
            parameters.maximum_physical_rgb)) {
      const auto exact_boundary = exact_target_gamut_boundary(
          rendered_jmh.j,
          rendered_jmh.h_degrees,
          parameters.output_appearance,
          parameters.maximum_physical_rgb,
          original_colorfulness);
      rendered_jmh.m = compress_colorfulness_to_boundary(
          original_colorfulness,
          exact_boundary);
      physical_rgb = aces2_hellwig_jmh_to_linear_rgb(
          rendered_jmh,
          parameters.output_appearance);
    }
  }

  for (auto& channel : physical_rgb) {
    channel = std::clamp(
                  channel,
                  0.0,
                  parameters.maximum_physical_rgb) *
              parameters.physical_to_reference_white;
  }
  return physical_rgb;
}

double encode_display_eotf(double linear_display_light, const DisplayTarget& target) {
  const auto linear = std::max(0.0, linear_display_light);
  switch (target.eotf) {
    case DisplayEotf::Pq: {
      constexpr double m1 = 2610.0 / 16384.0;
      constexpr double m2 = 2523.0 / 32.0;
      constexpr double c1 = 3424.0 / 4096.0;
      constexpr double c2 = 2413.0 / 128.0;
      constexpr double c3 = 2392.0 / 128.0;
      const auto luminance = std::clamp(
          linear * target.reference_white_nits / 10000.0,
          0.0,
          1.0);
      const auto powered = std::pow(luminance, m1);
      return std::pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
    }
    case DisplayEotf::Hlg:
      return linear <= (1.0 / 12.0)
                 ? std::sqrt(3.0 * linear)
                 : kHlgA * std::log(12.0 * linear - kHlgB) + kHlgC;
    case DisplayEotf::Gamma18:
      return signed_power(linear, 1.0 / 1.8);
    case DisplayEotf::Gamma22:
      return signed_power(linear, 1.0 / 2.2);
    case DisplayEotf::Gamma24:
      return signed_power(linear, 1.0 / 2.4);
    case DisplayEotf::Linear:
      return linear;
    case DisplayEotf::Srgb:
    default:
      return linear_to_srgb(linear);
  }
}

double decode_display_eotf(double encoded_signal, const DisplayTarget& target) {
  const auto encoded = std::max(0.0, encoded_signal);
  switch (target.eotf) {
    case DisplayEotf::Pq: {
      constexpr double m1 = 2610.0 / 16384.0;
      constexpr double m2 = 2523.0 / 32.0;
      constexpr double c1 = 3424.0 / 4096.0;
      constexpr double c2 = 2413.0 / 128.0;
      constexpr double c3 = 2392.0 / 128.0;
      const auto powered = std::pow(std::clamp(encoded, 0.0, 1.0), 1.0 / m2);
      const auto numerator = std::max(powered - c1, 0.0);
      const auto denominator = c2 - c3 * powered;
      const auto absolute = denominator <= 0.0
                                ? 1.0
                                : std::pow(numerator / denominator, 1.0 / m1);
      return absolute * 10000.0 / target.reference_white_nits;
    }
    case DisplayEotf::Hlg:
      return encoded <= 0.5
                 ? encoded * encoded / 3.0
                 : (std::exp((encoded - kHlgC) / kHlgA) + kHlgB) / 12.0;
    case DisplayEotf::Gamma18:
      return signed_power(encoded, 1.8);
    case DisplayEotf::Gamma22:
      return signed_power(encoded, 2.2);
    case DisplayEotf::Gamma24:
      return signed_power(encoded, 2.4);
    case DisplayEotf::Linear:
      return encoded;
    case DisplayEotf::Srgb:
    default:
      return srgb_to_linear(encoded);
  }
}

}  // namespace photo_editor::pipeline
