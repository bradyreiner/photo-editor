#include "color_input_runtime.h"

#include "color_input_white_balance.h"
#include "color_math_runtime.h"
#include "photo_editor/pipeline/color_transform.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <string_view>

namespace photo_editor::pipeline {

constexpr double kInputPi = std::numbers::pi_v<double>;

[[nodiscard]] Matrix3 calibration_saturation_matrix(double saturation) {
  const auto strength = clamp_signed_percent(saturation) / 100.0;
  const auto scale = std::max(0.0, 1.0 + strength);
  constexpr double kRedLuma = 0.2126;
  constexpr double kGreenLuma = 0.7152;
  constexpr double kBlueLuma = 0.0722;
  return matrix3_from_rows(
      kRedLuma + (1.0 - kRedLuma) * scale,
      kGreenLuma - kGreenLuma * scale,
      kBlueLuma - kBlueLuma * scale,
      kRedLuma - kRedLuma * scale,
      kGreenLuma + (1.0 - kGreenLuma) * scale,
      kBlueLuma - kBlueLuma * scale,
      kRedLuma - kRedLuma * scale,
      kGreenLuma - kGreenLuma * scale,
      kBlueLuma + (1.0 - kBlueLuma) * scale);
}

[[nodiscard]] Matrix3 calibration_hue_matrix(double hue) {
  const auto radians = clamp_signed_percent(hue) / 100.0 * kInputPi;
  const auto cosine = std::cos(radians);
  const auto sine = std::sin(radians);
  return matrix3_from_rows(
      0.213 + cosine * 0.787 - sine * 0.213,
      0.715 - cosine * 0.715 - sine * 0.715,
      0.072 - cosine * 0.072 + sine * 0.928,
      0.213 - cosine * 0.213 + sine * 0.143,
      0.715 + cosine * 0.285 + sine * 0.140,
      0.072 - cosine * 0.072 - sine * 0.283,
      0.213 - cosine * 0.213 - sine * 0.787,
      0.715 - cosine * 0.715 + sine * 0.715,
      0.072 + cosine * 0.928 + sine * 0.072);
}

[[nodiscard]] Matrix3 calibration_primary_matrix(
    double red_hue,
    double red_saturation,
    double green_hue,
    double green_saturation,
    double blue_hue,
    double blue_saturation) {
  const auto signed_unit = [](double value) {
    return clamp_signed_percent(value) / 100.0;
  };
  const auto red_shift = signed_unit(red_hue) * 0.28;
  const auto green_shift = signed_unit(green_hue) * 0.28;
  const auto blue_shift = signed_unit(blue_hue) * 0.28;
  const auto red_gain = std::max(0.0, 1.0 + signed_unit(red_saturation) * 0.60);
  const auto green_gain = std::max(0.0, 1.0 + signed_unit(green_saturation) * 0.60);
  const auto blue_gain = std::max(0.0, 1.0 + signed_unit(blue_saturation) * 0.60);
  return matrix3_from_rows(
      red_gain,
      std::max(0.0, -green_shift),
      std::max(0.0, blue_shift),
      std::max(0.0, red_shift),
      green_gain,
      std::max(0.0, -blue_shift),
      std::max(0.0, -red_shift),
      std::max(0.0, green_shift),
      blue_gain);
}

[[nodiscard]] Matrix3 calibration_matrix(const InputTransformSettings& settings) {
  return multiply_matrix3(
      calibration_hue_matrix(settings.calibration_hue),
      multiply_matrix3(
          calibration_saturation_matrix(settings.calibration_saturation),
          calibration_primary_matrix(
              settings.red_primary_hue,
              settings.red_primary_saturation,
              settings.green_primary_hue,
              settings.green_primary_saturation,
              settings.blue_primary_hue,
               settings.blue_primary_saturation)));
}

[[nodiscard]] Matrix3 camera_profile_matrix(
    const ColorPipelineConfig& config,
    double source_temperature,
    double source_tint,
    const RgbColorSpace& working_space,
    std::string_view chromatic_adaptation_method) {
  if (!config.camera_profile.valid ||
      (config.input_profile.kind != InputTransformKind::CameraDcpProfile &&
       config.input_profile.kind != InputTransformKind::CameraIccProfile &&
       config.camera_profile.execution_model !=
           "libraw-camera-to-xyz-d50:v1" &&
       config.camera_profile.execution_model !=
           "libraw-camera-to-xyz-d65:v1")) {
    return identity_matrix3();
  }
  if ((config.input_profile.kind == InputTransformKind::CameraDcpProfile &&
       config.camera_profile.selection_source !=
           CameraProfileSelectionSource::ExplicitUserDcp) ||
      (config.input_profile.kind == InputTransformKind::CameraIccProfile &&
       config.camera_profile.selection_source !=
           CameraProfileSelectionSource::ExplicitUserIcc)) {
    return identity_matrix3();
  }
  if (config.input_profile.kind == InputTransformKind::CameraDcpProfile &&
      config.camera_dcp_profile_asset.ready()) {
    const auto dcp_transform = dcp_camera_to_xyz_d50(
        *config.camera_dcp_profile_asset.profile,
        source_temperature,
        source_tint);
    if (!dcp_transform.valid) {
      return identity_matrix3();
    }
    if (config.camera_profile_rendering_model ==
        CameraProfileRenderingModel::DngProfileV2) {
      const auto profile_space = make_linear_prophoto_rgb_color_space();
      return multiply_matrix3(
          invert_matrix3(rgb_to_xyz_matrix(profile_space)),
          Matrix3{.values = dcp_transform.camera_to_xyz_d50});
    }
    const auto d50_white = xyz_from_chromaticity(
        white_point_chromaticity(WhitePoint::D50));
    const auto working_white = xyz_from_chromaticity(
        white_point_chromaticity(working_space.white_point));
    const auto pcs_to_working = multiply_matrix3(
        invert_matrix3(rgb_to_xyz_matrix(working_space)),
        chromatic_adaptation_matrix(
            d50_white,
            working_white,
            chromatic_adaptation_method));
    return multiply_matrix3(
        pcs_to_working,
        Matrix3{.values = dcp_transform.camera_to_xyz_d50});
  }
  if (config.input_profile.kind == InputTransformKind::CameraIccProfile) {
    return identity_matrix3();
  }
  if (config.camera_profile.execution_model ==
      "libraw-camera-to-xyz-d50:v1") {
    const auto d50_white = xyz_from_chromaticity(
        white_point_chromaticity(WhitePoint::D50));
    const auto working_white = xyz_from_chromaticity(
        white_point_chromaticity(working_space.white_point));
    const auto xyz_d50_to_working = multiply_matrix3(
        invert_matrix3(rgb_to_xyz_matrix(working_space)),
        chromatic_adaptation_matrix(
            d50_white,
            working_white,
            chromatic_adaptation_method));
    return multiply_matrix3(
        xyz_d50_to_working,
        Matrix3{.values = config.camera_profile.camera_to_xyz_d50});
  }
  if (config.camera_profile.execution_model ==
      "libraw-camera-to-xyz-d65:v1") {
    const auto d65_white = xyz_from_chromaticity(
        white_point_chromaticity(WhitePoint::D65));
    const auto working_white = xyz_from_chromaticity(
        white_point_chromaticity(working_space.white_point));
    const auto xyz_d65_to_working = multiply_matrix3(
        invert_matrix3(rgb_to_xyz_matrix(working_space)),
        chromatic_adaptation_matrix(
            d65_white,
            working_white,
            chromatic_adaptation_method));
    return multiply_matrix3(
        xyz_d65_to_working,
        Matrix3{.values = config.camera_profile.camera_to_xyz_d50});
  }
  if (config.camera_profile.dual_illuminant) {
    const auto blend = dcp_reciprocal_temperature_weight(
        source_temperature,
        config.camera_profile.tungsten_temperature,
        config.camera_profile.daylight_temperature);
    Matrix3 matrix;
    for (std::size_t i = 0; i < matrix.values.size(); ++i) {
      matrix.values[i] = config.camera_profile.camera_to_xyz_d50_tungsten[i] * (1.0 - blend) +
                         config.camera_profile.camera_to_xyz_d50_daylight[i] * blend;
    }
    return matrix;
  }
  return Matrix3{.values = config.camera_profile.camera_to_xyz_d50};
}

[[nodiscard]] Matrix3 camera_matching_look_matrix(std::string_view look) {
  if (look == "Neutral") {
    return calibration_saturation_matrix(-12.0);
  }
  if (look == "Vivid") {
    return multiply_matrix3(
        matrix3_from_rows(
            1.04, -0.01, 0.00,
            -0.01, 1.03, -0.01,
            0.00, -0.01, 1.04),
        calibration_saturation_matrix(18.0));
  }
  if (look == "Portrait") {
    return matrix3_from_rows(
        1.04, 0.01, 0.00,
        0.00, 1.00, 0.00,
        -0.01, 0.00, 0.98);
  }
  if (look == "Landscape") {
    return multiply_matrix3(
        matrix3_from_rows(
            1.00, -0.01, 0.00,
            0.00, 1.05, -0.01,
            -0.01, 0.00, 1.06),
        calibration_saturation_matrix(12.0));
  }
  if (look == "Monochrome") {
    constexpr double kRedLuma = 0.2126;
    constexpr double kGreenLuma = 0.7152;
    constexpr double kBlueLuma = 0.0722;
    return matrix3_from_rows(
        kRedLuma, kGreenLuma, kBlueLuma,
        kRedLuma, kGreenLuma, kBlueLuma,
        kRedLuma, kGreenLuma, kBlueLuma);
  }
  return identity_matrix3();
}


[[nodiscard]] ColorPixel apply_highlight_reconstruction(
    const ColorPixel& input,
    double strength,
    const PipelineColorContext& context) {
  if (strength <= 1e-6) {
    return input;
  }

  const auto peak = std::max({static_cast<double>(input.red),
                              static_cast<double>(input.green),
                              static_cast<double>(input.blue)});
  if (peak <= 1.0 + 1e-6) {
    return input;
  }

  const auto original_luminance = luminance_for_pixel(input, context);
  const auto blend = clamp_unit(smoothstep(1.0, 4.0, peak) * strength);
  auto reconstructed = input;
  auto reconstruct_channel = [&](float channel) {
    const auto current = static_cast<double>(channel);
    const auto deficit = std::max(0.0, peak - current);
    const auto lift = blend * smoothstep(peak * 0.05, peak * 0.85, deficit);
    return static_cast<float>(current + deficit * lift);
  };
  reconstructed.red = reconstruct_channel(reconstructed.red);
  reconstructed.green = reconstruct_channel(reconstructed.green);
  reconstructed.blue = reconstruct_channel(reconstructed.blue);
  return scale_pixel_to_luminance(reconstructed, original_luminance, context);
}

[[nodiscard]] InputStageParameters make_input_stage_parameters(
    const ColorAdjustmentState& state,
    const PipelineColorContext& context,
    const HighlightReconstructionAnalysis* highlight_analysis) {
  InputStageParameters parameters;
  const auto valid_source_area =
      std::isfinite(state.input_transform.source_area_x) &&
      std::isfinite(state.input_transform.source_area_y) &&
      std::isfinite(state.input_transform.source_area_width) &&
      std::isfinite(state.input_transform.source_area_height) &&
      state.input_transform.source_area_x >= 0.0 &&
      state.input_transform.source_area_y >= 0.0 &&
      state.input_transform.source_area_width > 0.0 &&
      state.input_transform.source_area_height > 0.0 &&
      state.input_transform.source_area_x +
              state.input_transform.source_area_width <=
          1.0 + 1.0e-9 &&
      state.input_transform.source_area_y +
              state.input_transform.source_area_height <=
          1.0 + 1.0e-9;
  if (valid_source_area) {
    parameters.source_area_x = state.input_transform.source_area_x;
    parameters.source_area_y = state.input_transform.source_area_y;
    parameters.source_area_width = state.input_transform.source_area_width;
    parameters.source_area_height = state.input_transform.source_area_height;
  }
  const auto input_transform_enabled = state.input_transform.raw_input_transform_enabled;
  parameters.clamp_camera_raw_black_floor = input_transform_enabled;
  const auto& recipe = state.color_pipeline.input_transform_recipe;
  const auto has_source_white_balance =
      input_transform_enabled && recipe.has_source_white_balance;
  const auto source_temperature =
      has_source_white_balance ? recipe.source_temperature : 5500.0;
  const auto source_tint =
      has_source_white_balance ? recipe.source_tint : 0.0;
  const auto dcp_input =
      input_transform_enabled &&
      state.color_pipeline.input_profile.kind ==
          InputTransformKind::CameraDcpProfile;
  const auto engine_characterized_camera_input =
      input_transform_enabled && state.color_pipeline.camera_profile.valid &&
      (dcp_input ||
       state.color_pipeline.camera_profile.execution_model ==
           "libraw-camera-to-xyz-d50:v1" ||
       state.color_pipeline.camera_profile.execution_model ==
           "libraw-camera-to-xyz-d65:v1");
  const auto dcp_white_temperature =
      dcp_input && state.input_transform.raw_white_balance_override_enabled
          ? state.input_transform.raw_white_balance_temperature
          : source_temperature;
  const auto dcp_white_tint =
      dcp_input && state.input_transform.raw_white_balance_override_enabled
          ? state.input_transform.raw_white_balance_tint
          : source_tint;
  parameters.baseline_exposure_gain = std::pow(
      2.0,
      input_transform_enabled ? recipe.baseline_exposure : 0.0);
  parameters.scene_exposure_gain = std::pow(
      2.0,
      effective_input_exposure(state));
  parameters.dcp_baseline_exposure_gain = std::pow(
      2.0,
      input_transform_enabled ? recipe.baseline_exposure : 0.0);
  if (input_transform_enabled) {
    parameters.camera_profile = camera_profile_matrix(
        state.color_pipeline,
        dcp_white_temperature,
        dcp_white_tint,
        context.working_space,
        context.chromatic_adaptation_method);
    parameters.camera_matching_look =
        camera_matching_look_matrix(state.input_transform.camera_matching_look);
    parameters.calibration = calibration_matrix(state.input_transform);
  }
  parameters.execute_dcp_profile_rendering =
      dcp_input &&
      state.color_pipeline.camera_profile.valid &&
      state.color_pipeline.camera_profile.selection_source ==
          CameraProfileSelectionSource::ExplicitUserDcp &&
      state.color_pipeline.camera_profile_rendering_model ==
          CameraProfileRenderingModel::DngProfileV2 &&
      state.color_pipeline.camera_dcp_profile_asset.ready();
  if (parameters.execute_dcp_profile_rendering) {
    parameters.dcp_profile =
        state.color_pipeline.camera_dcp_profile_asset.profile;
    parameters.dcp_render_plan =
        compile_dcp_profile_render_plan(
        *parameters.dcp_profile,
        state.color_pipeline.camera_dcp_profile_asset
            .reference.payload_sha256,
        dcp_white_temperature,
        dcp_white_tint);
    const auto profile_space = make_linear_prophoto_rgb_color_space();
    parameters.dcp_profile_context = pipeline_color_context_for_space(
        context,
        profile_space);
    const auto d50_white = xyz_from_chromaticity(
        white_point_chromaticity(WhitePoint::D50));
    const auto working_white = xyz_from_chromaticity(
        white_point_chromaticity(context.working_space.white_point));
    parameters.dcp_profile_to_working = multiply_matrix3(
        context.xyz_native_to_rgb,
        multiply_matrix3(
            chromatic_adaptation_matrix(
                d50_white,
                working_white,
                context.chromatic_adaptation_method),
            rgb_to_xyz_matrix(profile_space)));
  }
  parameters.execute_camera_linear_highlight_reconstruction =
      camera_linear_highlight_reconstruction_enabled(state.input_transform);
  if (parameters.execute_camera_linear_highlight_reconstruction) {
    const auto mode_strength =
        state.input_transform.highlight_reconstruction_mode == "Camera Default"
            ? 0.60
            : 1.0;
    parameters.highlight_reconstruction_strength =
        mode_strength *
        std::clamp(1.50 - recipe.highlight_headroom, 0.50, 1.0);
  } else {
    parameters.highlight_reconstruction_strength =
        highlight_reconstruction_strength(state) *
        std::clamp(1.15 - recipe.highlight_headroom, 0.35, 1.0);
  }
  if (highlight_analysis != nullptr) {
    parameters.highlight_reconstruction = *highlight_analysis;
  } else {
    parameters.highlight_reconstruction.active =
        parameters.execute_camera_linear_highlight_reconstruction;
    parameters.highlight_reconstruction.model =
        parameters.execute_camera_linear_highlight_reconstruction
            ? "camera-linear-opposed-v2"
            : "legacy-working-rgb-v1";
  }
  if (input_transform_enabled) {
    parameters.raw_black_level = std::clamp(state.input_transform.raw_black_level, 0.0, 0.95);
    parameters.raw_white_level = std::clamp(
        state.input_transform.raw_white_level,
        parameters.raw_black_level + 0.01,
        4.0);
  }

  if (engine_characterized_camera_input ||
      recipe.decoder_white_balance_applied) {
    parameters.white_balance = input_white_balance_matrix(
        effective_input_temperature(state),
        effective_input_tint(state),
        context);
    parameters.white_balance_after_camera_profile = true;
  } else if (input_transform_enabled &&
      state.input_transform.raw_white_balance_override_enabled) {
    parameters.white_balance = input_white_balance_matrix_between_whites(
        source_temperature,
        source_tint,
        state.input_transform.raw_white_balance_temperature,
        state.input_transform.raw_white_balance_tint,
        context);
  } else {
    parameters.white_balance = multiply_matrix3(
        input_white_balance_matrix(
            effective_input_temperature(state),
            effective_input_tint(state),
            context),
        has_source_white_balance
            ? input_white_balance_matrix_between_whites(
                  source_temperature,
                  source_tint,
                  5500.0,
                  0.0,
                  context)
            : identity_matrix3());
  }
  return parameters;
}

[[nodiscard]] ColorPixel apply_normalize_raw_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext&) {
  auto pixel = parameters.white_balance_after_camera_profile
                   ? input
                   : apply_rgb_matrix(input, parameters.white_balance);
  const auto normalize_channel = [&](float channel) {
    const auto normalized = (static_cast<double>(channel) - parameters.raw_black_level) /
                            (parameters.raw_white_level - parameters.raw_black_level);
    // Camera transforms can create small signed excursions. They are not
    // editable scene-linear color; establish the RAW black floor before the
    // general tone pipeline so camera-matrix noise cannot become a visible cast.
    return static_cast<float>(parameters.clamp_camera_raw_black_floor
                                  ? std::max(0.0, normalized)
                                  : normalized);
  };
  pixel.red = normalize_channel(pixel.red);
  pixel.green = normalize_channel(pixel.green);
  pixel.blue = normalize_channel(pixel.blue);
  if (parameters.execute_camera_linear_highlight_reconstruction) {
    pixel = apply_camera_linear_highlight_reconstruction(
        pixel,
        parameters.highlight_reconstruction_strength,
        parameters.highlight_reconstruction);
  }
  return pixel;
}

[[nodiscard]] ColorPixel apply_camera_characterization_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context,
    double normalized_x,
    double normalized_y) {
  auto pixel = input;
  pixel = apply_rgb_matrix(pixel, parameters.camera_profile);
  if (parameters.execute_dcp_profile_rendering && parameters.dcp_profile != nullptr) {
    const auto apply_dcp_rgb = [&](const DcpRgb& rgb) {
      return ColorPixel{
          .red = static_cast<float>(rgb.red),
          .green = static_cast<float>(rgb.green),
          .blue = static_cast<float>(rgb.blue),
          .alpha = pixel.alpha,
      };
    };
    pixel = apply_dcp_rgb(dcp_apply_hue_sat_map(
        *parameters.dcp_profile,
        *parameters.dcp_render_plan,
        DcpRgb{pixel.red, pixel.green, pixel.blue}));
    pixel = apply_dcp_rgb(dcp_apply_profile_gain_table_map(
        *parameters.dcp_profile,
        parameters.source_area_x +
            normalized_x * parameters.source_area_width,
        parameters.source_area_y +
            normalized_y * parameters.source_area_height,
        parameters.dcp_baseline_exposure_gain,
        DcpRgb{pixel.red, pixel.green, pixel.blue}));
    if (!parameters.execute_camera_linear_highlight_reconstruction) {
      pixel = apply_highlight_reconstruction(
          pixel,
          parameters.highlight_reconstruction_strength,
          parameters.dcp_profile_context);
    }
    pixel.red = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                       ? std::max(0.0, static_cast<double>(pixel.red) * parameters.dcp_baseline_exposure_gain)
                                       : static_cast<double>(pixel.red) * parameters.dcp_baseline_exposure_gain);
    pixel.green = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                         ? std::max(0.0, static_cast<double>(pixel.green) * parameters.dcp_baseline_exposure_gain)
                                         : static_cast<double>(pixel.green) * parameters.dcp_baseline_exposure_gain);
    pixel.blue = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                        ? std::max(0.0, static_cast<double>(pixel.blue) * parameters.dcp_baseline_exposure_gain)
                                        : static_cast<double>(pixel.blue) * parameters.dcp_baseline_exposure_gain);
    if (parameters.dcp_render_plan->uses_look_table) {
      pixel = apply_dcp_rgb(dcp_apply_look_table(
          *parameters.dcp_profile,
          DcpRgb{pixel.red, pixel.green, pixel.blue}));
    }
    if (parameters.dcp_render_plan->uses_tone_curve) {
      pixel = apply_dcp_rgb(dcp_apply_profile_tone_curve(
          *parameters.dcp_profile,
          *parameters.dcp_render_plan,
          DcpRgb{pixel.red, pixel.green, pixel.blue}));
    }
    if (parameters.dcp_render_plan->uses_rgb_tables) {
      pixel = apply_dcp_rgb(dcp_apply_rgb_tables(
          *parameters.dcp_profile,
          *parameters.dcp_render_plan,
          DcpRgb{pixel.red, pixel.green, pixel.blue}));
    }
    pixel = apply_rgb_matrix(pixel, parameters.dcp_profile_to_working);
  }
  if (parameters.white_balance_after_camera_profile) {
    pixel = apply_rgb_matrix(pixel, parameters.white_balance);
  }
  pixel = apply_rgb_matrix(pixel, parameters.camera_matching_look);
  pixel = apply_rgb_matrix(pixel, parameters.calibration);
  if (!parameters.execute_dcp_profile_rendering) {
    if (!parameters.execute_camera_linear_highlight_reconstruction) {
      pixel = apply_highlight_reconstruction(
          pixel,
          parameters.highlight_reconstruction_strength,
          context);
    }
    pixel.red = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                       ? std::max(0.0, static_cast<double>(pixel.red) * parameters.baseline_exposure_gain)
                                       : static_cast<double>(pixel.red) * parameters.baseline_exposure_gain);
    pixel.green = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                         ? std::max(0.0, static_cast<double>(pixel.green) * parameters.baseline_exposure_gain)
                                         : static_cast<double>(pixel.green) * parameters.baseline_exposure_gain);
    pixel.blue = static_cast<float>(parameters.clamp_camera_raw_black_floor
                                        ? std::max(0.0, static_cast<double>(pixel.blue) * parameters.baseline_exposure_gain)
                                        : static_cast<double>(pixel.blue) * parameters.baseline_exposure_gain);
  }
  return pixel;
}

[[nodiscard]] ColorPixel apply_scene_adjustments_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters) {
  return ColorPixel{
      .red = static_cast<float>(
          static_cast<double>(input.red) *
          parameters.scene_exposure_gain),
      .green = static_cast<float>(
          static_cast<double>(input.green) *
          parameters.scene_exposure_gain),
      .blue = static_cast<float>(
          static_cast<double>(input.blue) *
          parameters.scene_exposure_gain),
      .alpha = input.alpha,
  };
}

[[nodiscard]] ColorPixel apply_input_stage(
    const ColorPixel& input,
    const InputStageParameters& parameters,
    const PipelineColorContext& context,
    double normalized_x,
    double normalized_y) {
  return apply_scene_adjustments_stage(
      apply_camera_characterization_stage(
          apply_normalize_raw_stage(
              input,
              parameters,
              context),
          parameters,
          context,
          normalized_x,
          normalized_y),
      parameters);
}

}  // namespace photo_editor::pipeline
