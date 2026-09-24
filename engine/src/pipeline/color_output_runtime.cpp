#include "color_output_runtime.h"

#include "color_adjustment_scheduler.h"
#include "color_math_runtime.h"
#include "color_working_space_runtime.h"
#include "photo_editor/pipeline/color_transform.h"
#include "photo_editor/pipeline/output_rendering.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace photo_editor::pipeline {

namespace {

template <typename Work>
void parallel_for(int count, Work&& work) {
  run_color_adjustment_parallel_for(
      count, std::function<void(int)>(std::forward<Work>(work)));
}

[[nodiscard]] double sanitized_black_point(
    double value,
    double output_headroom) {
  constexpr auto kBlackPointMargin = 1.0e-6;
  return std::clamp(
      value,
      0.0,
      std::max(0.0, output_headroom - kBlackPointMargin));
}

}  // namespace

[[nodiscard]] bool black_point_compensation_affects_output(
    const ColorPipelineConfig& config) {
  if (!config.black_point_compensation_enabled) {
    return false;
  }

  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto source_black = sanitized_black_point(config.source_black_point, headroom);
  const auto output_black = sanitized_black_point(config.output_black_point, headroom);
  return std::abs(source_black - output_black) > 1.0e-9;
}

[[nodiscard]] ColorPixel apply_black_point_compensation(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config) {
  if (!black_point_compensation_affects_output(config)) {
    return pixel;
  }

  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto source_black = sanitized_black_point(config.source_black_point, headroom);
  const auto output_black = sanitized_black_point(config.output_black_point, headroom);
  const auto source_span = std::max(1.0e-6, headroom - source_black);
  const auto output_span = std::max(0.0, headroom - output_black);
  const auto remap = [&](float channel) {
    const auto value = static_cast<double>(channel);
    if (value <= source_black) {
      return static_cast<float>(output_black);
    }
    const auto normalized = (value - source_black) / source_span;
    return static_cast<float>(std::clamp(
        output_black + normalized * output_span,
        0.0,
        headroom));
  };
  return ColorPixel{
      .red = remap(pixel.red),
      .green = remap(pixel.green),
      .blue = remap(pixel.blue),
      .alpha = pixel.alpha,
  };
}

[[nodiscard]] const OutputColorProfile& output_profile_for_target(
    const ColorPipelineConfig& config,
    GamutWarningTarget target) {
  return target == GamutWarningTarget::PreviewDisplay ? config.preview_output_profile
                                                      : config.export_output_profile;
}

[[nodiscard]] const DisplayTarget& display_target_for_target(
    const ColorPipelineConfig& config,
    GamutWarningTarget target) {
  return target == GamutWarningTarget::PreviewDisplay
             ? config.preview_display_target
             : config.export_display_target;
}

[[nodiscard]] bool use_aces2_photographic_renderer(
    const ColorPipelineConfig& config) {
  return photographic_rendering_model_uses_aces2(
             config.photographic_rendering_model) &&
         config.render_intent == RenderIntent::Perceptual;
}

[[nodiscard]] Aces2OutputImplementation aces2_output_implementation(
    const ColorPipelineConfig& config) {
  return photographic_rendering_model_uses_official_aces2(
             config.photographic_rendering_model)
             ? Aces2OutputImplementation::OfficialWhenAvailable
             : Aces2OutputImplementation::NativeFallback;
}

[[nodiscard]] Matrix3 working_rgb_to_output_rgb_matrix(
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  const auto& target_space = output_profile_for_target(config, target).color_space;
  return compile_builtin_color_transform(
             config.working_profile.color_space,
             target_space,
             chromatic_adaptation_method)
      .matrix;
}

[[nodiscard]] double output_transform_excursion(
    const ColorPixel& pixel,
    double output_headroom) {
  const auto headroom = std::clamp(output_headroom, 1.0, 16.0);
  auto max_excursion = 0.0;
  for (const auto channel : {pixel.red, pixel.green, pixel.blue}) {
    if (channel < 0.0F) {
      max_excursion = std::max(max_excursion, -static_cast<double>(channel));
    } else if (static_cast<double>(channel) > headroom) {
      max_excursion = std::max(max_excursion, static_cast<double>(channel) - headroom);
    }
  }
  return max_excursion;
}

OutputTransformResult apply_output_transform(
    const ColorPixel& pixel,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  const auto& output_profile = output_profile_for_target(config, target);
  const auto output_matrix =
      working_rgb_to_output_rgb_matrix(config, target, chromatic_adaptation_method);
  auto transformed = apply_rgb_matrix(pixel, output_matrix);
  const auto headroom =
      use_aces2_photographic_renderer(config)
          ? display_target_linear_headroom(display_target_for_target(config, target))
          : std::clamp(config.output_headroom, 1.0, 16.0);
  const auto max_excursion = output_transform_excursion(transformed, headroom);
  if (use_aces2_photographic_renderer(config)) {
    const auto processor = compile_aces2_output_processor(
        config.working_profile.color_space,
        display_target_for_target(config, target),
        chromatic_adaptation_method,
        aces2_output_implementation(config), config.renderer_transition_look);
    const auto rendered = aces2_render_to_linear_display_rgb(
        {pixel.red, pixel.green, pixel.blue},
        processor->parameters);
    transformed = ColorPixel{
        .red = static_cast<float>(rendered[0]),
        .green = static_cast<float>(rendered[1]),
        .blue = static_cast<float>(rendered[2]),
        .alpha = pixel.alpha,
    };
    transformed = apply_black_point_compensation(transformed, config);
    transformed = clip_pixel_to_output_headroom(transformed, headroom);
    transformed.alpha = static_cast<float>(clamp_unit(transformed.alpha));
    return OutputTransformResult{
        .pixel = transformed,
        .source_out_of_gamut = max_excursion > 1.0e-6,
        .max_channel_excursion = max_excursion,
        .output_profile_name = output_profile.profile_name,
    };
  }
  transformed = apply_black_point_compensation(transformed, config);
  const auto should_clip =
      !config.output_gamut_mapping_enabled ||
      config.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      config.render_intent == RenderIntent::RelativeColorimetric;
  if (should_clip) {
    transformed = clip_pixel_to_output_headroom(transformed, headroom);
  } else if (output_transform_excursion(transformed, headroom) > 1.0e-6) {
    // Gamut-map in the destination color space. The old path merely removed
    // negative channels and scaled the peak RGB value, which is not a
    // perceptual transform and can visibly bend wide-gamut hues. OKLCH keeps
    // the source hue/lightness intent while reducing only the chroma needed to
    // enter the selected output gamut.
    const auto peak_channel = std::max({
        static_cast<double>(transformed.red),
        static_cast<double>(transformed.green),
        static_cast<double>(transformed.blue),
    });
    if (peak_channel > headroom) {
      const auto peak_scale = headroom / peak_channel;
      transformed.red = static_cast<float>(static_cast<double>(transformed.red) * peak_scale);
      transformed.green = static_cast<float>(static_cast<double>(transformed.green) * peak_scale);
      transformed.blue = static_cast<float>(static_cast<double>(transformed.blue) * peak_scale);
    }
    ColorAdjustmentState output_space_state;
    output_space_state.color_pipeline = config;
    output_space_state.color_pipeline.working_profile.color_space =
        output_profile.color_space;
    output_space_state.input_transform.chromatic_adaptation_method =
        std::string(chromatic_adaptation_method);
    const auto output_context = make_pipeline_color_context(output_space_state);
    const auto output_oklab = linear_rgb_to_oklab(transformed, output_context);
    transformed = gamut_fit_oklab(
        output_oklab.lightness,
        output_oklab.a,
        output_oklab.b,
        transformed.alpha,
        output_context);
  }
  transformed.alpha = static_cast<float>(clamp_unit(transformed.alpha));
  return OutputTransformResult{
      .pixel = transformed,
      .source_out_of_gamut = max_excursion > 1.0e-6,
      .max_channel_excursion = max_excursion,
      .output_profile_name = output_profile.profile_name,
  };
}

const char* output_inverse_status_name(OutputInverseStatus status) {
  switch (status) {
    case OutputInverseStatus::Ready:
      return "ready";
    case OutputInverseStatus::InvalidDisplayTarget:
      return "invalid-display-target";
    case OutputInverseStatus::NonInvertiblePhotographicRendering:
      return "non-invertible-photographic-rendering";
    case OutputInverseStatus::NonInvertibleSoftProof:
      return "non-invertible-soft-proof";
    case OutputInverseStatus::NonInvertibleCustomIcc:
      return "non-invertible-custom-icc";
    case OutputInverseStatus::NonInvertibleBlackPointCompensation:
      return "non-invertible-black-point-compensation";
    case OutputInverseStatus::NonInvertibleClippedOrMappedSample:
      return "non-invertible-clipped-or-mapped-sample";
    case OutputInverseStatus::NonInvertibleAlphaClamp:
      return "non-invertible-alpha-clamp";
    case OutputInverseStatus::OutputProfileMismatch:
      return "output-profile-mismatch";
    case OutputInverseStatus::EncodedSignalOutsideInvertibleDomain:
      return "encoded-signal-outside-invertible-domain";
    case OutputInverseStatus::NonFiniteSample:
      return "non-finite-sample";
  }
  return "invalid-display-target";
}

OutputSampleProvenance output_sample_provenance(
    const ColorPixel& source_pixel,
    const OutputTransformResult& output) {
  return {
      .rgb_was_clipped_or_mapped = output.source_out_of_gamut,
      .alpha_was_clamped = source_pixel.alpha < 0.0F || source_pixel.alpha > 1.0F,
      .output_profile_name = output.output_profile_name,
  };
}

CompiledOutputInverseTransform compile_output_inverse_transform(
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  const auto failed = [](OutputInverseStatus status) {
    return CompiledOutputInverseTransform{
        .status = status,
        .failure_reason = output_inverse_status_name(status),
    };
  };
  const auto& display_target = display_target_for_target(config, target);
  if (!display_target_valid(display_target)) {
    return failed(OutputInverseStatus::InvalidDisplayTarget);
  }
  if (config.soft_proof_enabled) {
    return failed(OutputInverseStatus::NonInvertibleSoftProof);
  }
  const auto& icc_asset =
      target == GamutWarningTarget::PreviewDisplay
          ? config.preview_icc_profile_asset
          : config.export_icc_profile_asset;
  if (icc_asset.status != IccProfileAssetStatus::Unconfigured) {
    return failed(OutputInverseStatus::NonInvertibleCustomIcc);
  }
  if (use_aces2_photographic_renderer(config)) {
    return failed(OutputInverseStatus::NonInvertiblePhotographicRendering);
  }
  if (black_point_compensation_affects_output(config)) {
    return failed(OutputInverseStatus::NonInvertibleBlackPointCompensation);
  }

  const auto& output_profile = output_profile_for_target(config, target);
  const auto forward = working_rgb_to_output_rgb_matrix(
      config,
      target,
      chromatic_adaptation_method);
  return CompiledOutputInverseTransform{
      .status = OutputInverseStatus::Ready,
      .output_to_working = invert_matrix3(forward),
      .display_target = display_target,
      .output_profile_name = output_profile.profile_name,
      .provenance =
          std::string("inverse-output-matrix:v1|source:") +
          output_profile.color_space.name + "|destination:" +
          config.working_profile.color_space.name + "|cat:" +
          std::string(chromatic_adaptation_method) + "|eotf:" +
          display_eotf_name(display_target.eotf),
      .failure_reason = {},
      .requires_unmapped_sample = true,
      .supports_display_encoded_samples = true,
  };
}

OutputInverseResult apply_output_inverse_transform(
    const CompiledOutputInverseTransform& transform,
    const ColorPixel& output_pixel,
    OutputSampleEncoding encoding,
    const OutputSampleProvenance& provenance) {
  const auto failed = [&output_pixel](OutputInverseStatus status) {
    return OutputInverseResult{
        .pixel = output_pixel,
        .status = status,
        .exact = false,
        .failure_reason = output_inverse_status_name(status),
    };
  };
  if (!transform.ready()) {
    return failed(transform.status);
  }
  if (provenance.output_profile_name != transform.output_profile_name) {
    return failed(OutputInverseStatus::OutputProfileMismatch);
  }
  if (provenance.rgb_was_clipped_or_mapped) {
    return failed(OutputInverseStatus::NonInvertibleClippedOrMappedSample);
  }
  if (provenance.alpha_was_clamped) {
    return failed(OutputInverseStatus::NonInvertibleAlphaClamp);
  }
  if (!std::isfinite(output_pixel.red) || !std::isfinite(output_pixel.green) ||
      !std::isfinite(output_pixel.blue) || !std::isfinite(output_pixel.alpha)) {
    return failed(OutputInverseStatus::NonFiniteSample);
  }

  auto linear_display = std::array<double, 3>{
      output_pixel.red,
      output_pixel.green,
      output_pixel.blue,
  };
  if (encoding == OutputSampleEncoding::DisplayEncoded) {
    if (!transform.supports_display_encoded_samples ||
        std::any_of(
            linear_display.cbegin(),
            linear_display.cend(),
            [&transform](double channel) {
              return channel < 0.0 ||
                     (transform.display_target.eotf == DisplayEotf::Pq &&
                      channel > 1.0);
            })) {
      return failed(OutputInverseStatus::EncodedSignalOutsideInvertibleDomain);
    }
    for (auto& channel : linear_display) {
      channel = decode_display_eotf(channel, transform.display_target);
    }
  }
  const auto working = multiply_matrix3_vector(
      transform.output_to_working,
      linear_display);
  if (!std::isfinite(working[0]) || !std::isfinite(working[1]) ||
      !std::isfinite(working[2])) {
    return failed(OutputInverseStatus::NonFiniteSample);
  }
  return OutputInverseResult{
      .pixel = {
          .red = static_cast<float>(working[0]),
          .green = static_cast<float>(working[1]),
          .blue = static_cast<float>(working[2]),
          .alpha = output_pixel.alpha,
      },
      .status = OutputInverseStatus::Ready,
      .exact = true,
      .failure_reason = {},
  };
}

void apply_output_transform(
    std::span<ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  if (use_aces2_photographic_renderer(config)) {
    const auto processor = compile_aces2_output_processor(
        config.working_profile.color_space,
        display_target_for_target(config, target),
        chromatic_adaptation_method,
        aces2_output_implementation(config), config.renderer_transition_look);
    const auto headroom = display_target_linear_headroom(
        display_target_for_target(config, target));
    parallel_for(static_cast<int>(pixels.size()), [&](int index) {
      auto& pixel = pixels[static_cast<std::size_t>(index)];
      const auto rendered = aces2_render_to_linear_display_rgb(
          {pixel.red, pixel.green, pixel.blue},
          processor->parameters);
      auto output = apply_black_point_compensation(
          ColorPixel{
              .red = static_cast<float>(rendered[0]),
              .green = static_cast<float>(rendered[1]),
              .blue = static_cast<float>(rendered[2]),
              .alpha = pixel.alpha,
          },
          config);
      pixel = clip_pixel_to_output_headroom(output, headroom);
      pixel.alpha = static_cast<float>(clamp_unit(pixel.alpha));
    });
    return;
  }
  if (pixels.empty()) return;
  // The display transform is constant over this batch. The scalar API also
  // returns per-sample diagnostics; rebuilding its matrices and gamut context
  // for every display pixel dominates large legacy previews and exports.
  const auto output_matrix =
      working_rgb_to_output_rgb_matrix(config, target, chromatic_adaptation_method);
  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto should_clip =
      !config.output_gamut_mapping_enabled ||
      config.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      config.render_intent == RenderIntent::RelativeColorimetric;
  ColorAdjustmentState output_space_state;
  output_space_state.color_pipeline = config;
  output_space_state.color_pipeline.working_profile.color_space =
      output_profile_for_target(config, target).color_space;
  output_space_state.input_transform.chromatic_adaptation_method =
      std::string(chromatic_adaptation_method);
  const auto output_context = make_pipeline_color_context(output_space_state);

  parallel_for(static_cast<int>(pixels.size()), [&](int index) {
    auto& pixel = pixels[static_cast<std::size_t>(index)];
    auto transformed = apply_black_point_compensation(
        apply_rgb_matrix(pixel, output_matrix), config);
    if (should_clip) {
      transformed = clip_pixel_to_output_headroom(transformed, headroom);
    } else if (output_transform_excursion(transformed, headroom) > 1.0e-6) {
      const auto peak_channel = std::max({
          static_cast<double>(transformed.red),
          static_cast<double>(transformed.green),
          static_cast<double>(transformed.blue),
      });
      if (peak_channel > headroom) {
        const auto peak_scale = headroom / peak_channel;
        transformed.red = static_cast<float>(static_cast<double>(transformed.red) * peak_scale);
        transformed.green = static_cast<float>(static_cast<double>(transformed.green) * peak_scale);
        transformed.blue = static_cast<float>(static_cast<double>(transformed.blue) * peak_scale);
      }
      const auto output_oklab = linear_rgb_to_oklab(transformed, output_context);
      transformed = gamut_fit_oklab(output_oklab.lightness, output_oklab.a,
          output_oklab.b, transformed.alpha, output_context);
    }
    transformed.alpha = static_cast<float>(clamp_unit(transformed.alpha));
    pixel = transformed;
  });
}

void apply_output_gamut_mapping_in_working_space(
    std::span<ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  if (pixels.empty()) {
    return;
  }

  const auto& output_profile = output_profile_for_target(config, target);
  const auto output_matrix =
      working_rgb_to_output_rgb_matrix(config, target, chromatic_adaptation_method);
  const auto output_to_working_matrix = invert_matrix3(output_matrix);
  const auto headroom = std::clamp(config.output_headroom, 1.0, 16.0);
  const auto should_clip =
      !config.output_gamut_mapping_enabled ||
      config.output_gamut_mapping_policy == GamutMappingPolicy::Clip ||
      config.render_intent == RenderIntent::RelativeColorimetric;

  if (use_aces2_photographic_renderer(config)) {
    const auto& display_target = display_target_for_target(config, target);
    const auto processor = compile_aces2_output_processor(
        config.working_profile.color_space,
        display_target,
        chromatic_adaptation_method,
        aces2_output_implementation(config), config.renderer_transition_look);
    const auto target_to_working = compile_builtin_color_transform(
                                       display_target.color_space,
                                       config.working_profile.color_space,
                                       chromatic_adaptation_method)
                                       .matrix;
    const auto headroom = display_target_linear_headroom(display_target);
    parallel_for(static_cast<int>(pixels.size()), [&](int index) {
      auto& pixel = pixels[static_cast<std::size_t>(index)];
      const auto rendered = aces2_render_to_linear_display_rgb(
          {pixel.red, pixel.green, pixel.blue},
          processor->parameters);
      auto target_pixel = apply_black_point_compensation(
          ColorPixel{
              .red = static_cast<float>(rendered[0]),
              .green = static_cast<float>(rendered[1]),
              .blue = static_cast<float>(rendered[2]),
              .alpha = pixel.alpha,
          },
          config);
      target_pixel = clip_pixel_to_output_headroom(target_pixel, headroom);
      pixel = apply_rgb_matrix(target_pixel, target_to_working);
      pixel.alpha = static_cast<float>(clamp_unit(target_pixel.alpha));
    });
    return;
  }

  ColorAdjustmentState output_space_state;
  output_space_state.color_pipeline = config;
  output_space_state.color_pipeline.working_profile.color_space = output_profile.color_space;
  output_space_state.input_transform.chromatic_adaptation_method =
      std::string(chromatic_adaptation_method);
  const auto output_context = make_pipeline_color_context(output_space_state);

  parallel_for(static_cast<int>(pixels.size()), [&](int index) {
    auto target_pixel = apply_rgb_matrix(
        pixels[static_cast<std::size_t>(index)],
        output_matrix);
    if (should_clip) {
      target_pixel = clip_pixel_to_output_headroom(target_pixel, headroom);
    } else if (output_transform_excursion(target_pixel, headroom) > 1.0e-6) {
      const auto peak_channel = std::max({
          static_cast<double>(target_pixel.red),
          static_cast<double>(target_pixel.green),
          static_cast<double>(target_pixel.blue),
      });
      if (peak_channel > headroom) {
        const auto peak_scale = headroom / peak_channel;
        target_pixel.red =
            static_cast<float>(static_cast<double>(target_pixel.red) * peak_scale);
        target_pixel.green =
            static_cast<float>(static_cast<double>(target_pixel.green) * peak_scale);
        target_pixel.blue =
            static_cast<float>(static_cast<double>(target_pixel.blue) * peak_scale);
      }
      const auto output_oklab = linear_rgb_to_oklab(target_pixel, output_context);
      target_pixel = gamut_fit_oklab(
          output_oklab.lightness,
          output_oklab.a,
          output_oklab.b,
          target_pixel.alpha,
          output_context);
    }
    auto working_pixel = apply_rgb_matrix(target_pixel, output_to_working_matrix);
    working_pixel.alpha = static_cast<float>(clamp_unit(target_pixel.alpha));
    pixels[static_cast<std::size_t>(index)] = working_pixel;
  });
}

CompiledAces2OutputLutApplication compile_aces2_output_lut_application(
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    Aces2OutputLutQuality quality,
    std::string_view chromatic_adaptation_method) {
  CompiledAces2OutputLutApplication application;
  if (!photographic_rendering_model_uses_official_aces2(
          config.photographic_rendering_model) ||
      config.render_intent != RenderIntent::Perceptual) {
    return application;
  }
  const auto& display_target = display_target_for_target(config, target);
  application.lut = compile_aces2_output_lut(
      config.working_profile.color_space,
      display_target,
      aces2_output_lut_settings(quality),
      chromatic_adaptation_method, config.renderer_transition_look);
  if (application.lut == nullptr) {
    return application;
  }
  application.target_to_working = compile_builtin_color_transform(
                                      display_target.color_space,
                                      config.working_profile.color_space,
                                      chromatic_adaptation_method)
                                      .matrix;
  application.black_point_headroom =
      std::clamp(config.output_headroom, 1.0, 16.0);
  application.source_black_point = sanitized_black_point(
      config.source_black_point,
      application.black_point_headroom);
  application.output_black_point = sanitized_black_point(
      config.output_black_point,
      application.black_point_headroom);
  application.black_point_compensation_enabled =
      black_point_compensation_affects_output(config);
  application.display_headroom = display_target_linear_headroom(display_target);
  return application;
}

ColorPixel apply_aces2_output_lut_target_application(
    const CompiledAces2OutputLutApplication& application,
    const ColorPixel& pixel) {
  if (!application.ready()) {
    return pixel;
  }
  const auto rendered = aces2_output_lut_apply(
      *application.lut,
      {pixel.red, pixel.green, pixel.blue});
  auto target = ColorPixel{
      .red = static_cast<float>(rendered[0]),
      .green = static_cast<float>(rendered[1]),
      .blue = static_cast<float>(rendered[2]),
      .alpha = pixel.alpha,
  };
  if (application.black_point_compensation_enabled) {
    const auto source_span = std::max(
        1.0e-6,
        application.black_point_headroom - application.source_black_point);
    const auto output_span = std::max(
        0.0,
        application.black_point_headroom - application.output_black_point);
    const auto remap = [&](float channel) {
      const auto value = static_cast<double>(channel);
      if (value <= application.source_black_point) {
        return static_cast<float>(application.output_black_point);
      }
      const auto normalized =
          (value - application.source_black_point) / source_span;
      return static_cast<float>(std::clamp(
          application.output_black_point + normalized * output_span,
          0.0,
          application.black_point_headroom));
    };
    target.red = remap(target.red);
    target.green = remap(target.green);
    target.blue = remap(target.blue);
  }
  target = clip_pixel_to_output_headroom(
      target,
      application.display_headroom);
  target.alpha =
      static_cast<float>(clamp_unit(pixel.alpha));
  return target;
}

ColorPixel apply_aces2_output_lut_application(
    const CompiledAces2OutputLutApplication& application,
    const ColorPixel& pixel) {
  if (!application.ready()) {
    return pixel;
  }
  const auto target =
      apply_aces2_output_lut_target_application(
          application,
          pixel);
  auto working = apply_rgb_matrix(target, application.target_to_working);
  working.alpha = static_cast<float>(clamp_unit(pixel.alpha));
  return working;
}

void apply_aces2_output_lut_application(
    std::span<ColorPixel> pixels,
    const CompiledAces2OutputLutApplication& application) {
  if (!application.ready() || pixels.empty()) {
    return;
  }
  parallel_for(static_cast<int>(pixels.size()), [&](int index) {
    auto& pixel = pixels[static_cast<std::size_t>(index)];
    pixel = apply_aces2_output_lut_application(application, pixel);
  });
}

void apply_aces2_output_lut_target_application(
    std::span<ColorPixel> pixels,
    const CompiledAces2OutputLutApplication& application) {
  if (!application.ready() || pixels.empty()) {
    return;
  }
  parallel_for(static_cast<int>(pixels.size()), [&](int index) {
    auto& pixel = pixels[static_cast<std::size_t>(index)];
    pixel =
        apply_aces2_output_lut_target_application(
            application,
            pixel);
  });
}

GamutWarningAnalysis analyze_gamut_warnings(
    std::span<const ColorPixel> pixels,
    const ColorPipelineConfig& config,
    GamutWarningTarget target,
    std::string_view chromatic_adaptation_method) {
  GamutWarningAnalysis analysis;
  if (pixels.empty()) {
    return analysis;
  }

  const auto& custom_profile =
      target == GamutWarningTarget::PreviewDisplay
          ? config.preview_icc_profile_asset
          : config.export_icc_profile_asset;
  if (custom_profile.status != IccProfileAssetStatus::Unconfigured) {
    const auto aces2_composition = use_aces2_photographic_renderer(config);
    analysis.processor =
        aces2_composition
            ? std::string(photographic_rendering_model_name(
                  config.photographic_rendering_model)) +
                  " + Little CMS"
            : "Little CMS";
    analysis.profile_asset_id = custom_profile.reference.asset_id;
    analysis.profile_payload_sha256 = custom_profile.reference.payload_sha256;
    if (!custom_profile.ready()) {
      analysis.transform_ready = false;
      analysis.failure_reason = custom_profile.failure_reason.empty()
                                    ? "icc_profile_asset_not_ready"
                                    : custom_profile.failure_reason;
      return analysis;
    }
    auto interchange_target = display_target_for_target(config, target);
    if (aces2_composition) {
      interchange_target.target_name =
          "ACES 2 linear Rec.2020 ICC interchange";
      interchange_target.color_space = make_linear_rec2020_color_space();
    }
    const auto aces2_processor =
        aces2_composition
            ? compile_aces2_output_processor(
                  config.working_profile.color_space,
                  interchange_target,
                  chromatic_adaptation_method,
                  aces2_output_implementation(config),
                  config.renderer_transition_look)
            : std::shared_ptr<const Aces2OutputProcessor>{};
    const auto transform = compile_builtin_to_icc_transform(
        aces2_composition ? make_linear_rec2020_color_space()
                          : config.working_profile.color_space,
        custom_profile.payload_bytes(),
        IccTransformOptions{
            .rendering_intent = config.render_intent == RenderIntent::RelativeColorimetric
                                    ? IccRenderingIntent::RelativeColorimetric
                                    : IccRenderingIntent::Perceptual,
            .black_point_compensation = config.black_point_compensation_enabled,
            .pixel_format = IccPixelFormat::RgbaFloat32,
            .target = target == GamutWarningTarget::PreviewDisplay
                          ? (aces2_composition
                                 ? "preview-output:aces2-rec2020-interchange"
                                 : "preview-output")
                          : (aces2_composition
                                 ? "export-output:aces2-rec2020-interchange"
                                 : "export-output"),
        });
    analysis.transform_cache_key =
        aces2_processor != nullptr
            ? aces2_processor->cache_key + "->" +
                  transform.description().cache_key
            : transform.description().cache_key;
    if (!transform.ready()) {
      analysis.transform_ready = false;
      analysis.failure_reason = transform.description().failure_reason;
      return analysis;
    }
    std::vector<IccRgbaFloat> transformed;
    transformed.reserve(pixels.size());
    for (const auto& pixel : pixels) {
      const auto rendered =
          aces2_processor != nullptr
              ? aces2_render_to_linear_display_rgb(
                    {pixel.red, pixel.green, pixel.blue},
                    aces2_processor->parameters)
              : std::array<double, 3>{
                    static_cast<double>(pixel.red),
                    static_cast<double>(pixel.green),
                    static_cast<double>(pixel.blue),
                };
      transformed.push_back(IccRgbaFloat{
          .red = static_cast<float>(rendered[0]),
          .green = static_cast<float>(rendered[1]),
          .blue = static_cast<float>(rendered[2]),
          .alpha = pixel.alpha,
      });
    }
    if (!transform.apply(transformed)) {
      analysis.transform_ready = false;
      analysis.failure_reason = "icc_gamut_analysis_transform_apply_failed";
      return analysis;
    }
    for (const auto& pixel : transformed) {
      if (pixel.alpha <= 1.0e-6F) {
        continue;
      }
      ++analysis.tested_pixels;
      const auto excursion = output_transform_excursion(
          ColorPixel{
              .red = pixel.red,
              .green = pixel.green,
              .blue = pixel.blue,
              .alpha = pixel.alpha,
          },
          1.0);
      if (excursion > 1.0e-6) {
        ++analysis.out_of_gamut_pixels;
        analysis.max_channel_excursion =
            std::max(analysis.max_channel_excursion, excursion);
      }
    }
    if (analysis.tested_pixels > 0) {
      analysis.out_of_gamut_fraction =
          static_cast<double>(analysis.out_of_gamut_pixels) /
          static_cast<double>(analysis.tested_pixels);
    }
    return analysis;
  }

  analysis.processor = "builtin-matrix:v1";

  if (use_aces2_photographic_renderer(config)) {
    analysis.processor = photographic_rendering_model_name(
        config.photographic_rendering_model);
    const auto output_matrix = working_rgb_to_output_rgb_matrix(
        config,
        target,
        chromatic_adaptation_method);
    const auto headroom = display_target_linear_headroom(
        display_target_for_target(config, target));
    for (const auto& pixel : pixels) {
      if (pixel.alpha <= 1.0e-6F) {
        continue;
      }
      ++analysis.tested_pixels;
      const auto excursion = output_transform_excursion(
          apply_rgb_matrix(pixel, output_matrix),
          headroom);
      if (excursion > 1.0e-6) {
        ++analysis.out_of_gamut_pixels;
        analysis.max_channel_excursion = std::max(
            analysis.max_channel_excursion,
            excursion);
      }
    }
    if (analysis.tested_pixels > 0) {
      analysis.out_of_gamut_fraction =
          static_cast<double>(analysis.out_of_gamut_pixels) /
          static_cast<double>(analysis.tested_pixels);
    }
    return analysis;
  }

  for (const auto& pixel : pixels) {
    if (pixel.alpha <= 1.0e-6F) {
      continue;
    }
    ++analysis.tested_pixels;
    const auto output = apply_output_transform(pixel, config, target, chromatic_adaptation_method);
    if (output.source_out_of_gamut) {
      ++analysis.out_of_gamut_pixels;
      analysis.max_channel_excursion =
          std::max(analysis.max_channel_excursion, output.max_channel_excursion);
    }
  }

  if (analysis.tested_pixels > 0) {
    analysis.out_of_gamut_fraction =
        static_cast<double>(analysis.out_of_gamut_pixels) /
        static_cast<double>(analysis.tested_pixels);
  }
  return analysis;
}

}  // namespace photo_editor::pipeline
