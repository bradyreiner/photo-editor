#include "edit_recipe_state.h"

namespace photo_editor::desktop {

namespace {

void insert_serialized_brush_paths(
    const QVariantMap& mask_definition,
    QJsonObject* geometry) {
  QJsonArray brush_points;
  for (const auto& point_value : mask_definition.value(QStringLiteral("brushPoints")).toList()) {
    const auto point = point_value.toMap();
    QJsonObject brush_point;
    brush_point.insert(QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
    brush_point.insert(QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
    brush_point.insert(QStringLiteral("size"), point.value(QStringLiteral("size")).toDouble());
    brush_point.insert(QStringLiteral("flow"), point.value(QStringLiteral("flow")).toDouble());
    brush_point.insert(
        QStringLiteral("opacity"),
        point.contains(QStringLiteral("opacity")) ? point.value(QStringLiteral("opacity")).toDouble() : 1.0);
    brush_point.insert(
        QStringLiteral("autoMaskAnchorX"),
        point.contains(QStringLiteral("autoMaskAnchorX"))
            ? point.value(QStringLiteral("autoMaskAnchorX")).toDouble()
            : point.value(QStringLiteral("x")).toDouble());
    brush_point.insert(
        QStringLiteral("autoMaskAnchorY"),
        point.contains(QStringLiteral("autoMaskAnchorY"))
            ? point.value(QStringLiteral("autoMaskAnchorY")).toDouble()
            : point.value(QStringLiteral("y")).toDouble());
    brush_point.insert(
        QStringLiteral("pressure"),
        point.contains(QStringLiteral("pressure")) ? point.value(QStringLiteral("pressure")).toDouble() : 1.0);
    brush_point.insert(
        QStringLiteral("tiltX"),
        point.contains(QStringLiteral("tiltX")) ? point.value(QStringLiteral("tiltX")).toDouble() : 0.0);
    brush_point.insert(
        QStringLiteral("tiltY"),
        point.contains(QStringLiteral("tiltY")) ? point.value(QStringLiteral("tiltY")).toDouble() : 0.0);
    brush_point.insert(
        QStringLiteral("erase"),
        point.contains(QStringLiteral("erase")) ? point.value(QStringLiteral("erase")).toBool() : false);
    brush_points.append(brush_point);
  }
  geometry->insert(QStringLiteral("brushPoints"), brush_points);

  QJsonArray brush_strokes;
  for (const auto& stroke_value : mask_definition.value(QStringLiteral("brushStrokes")).toList()) {
    const auto stroke_map = stroke_value.toMap();
    QJsonObject stroke;
    stroke.insert(QStringLiteral("id"), stroke_map.value(QStringLiteral("id")).toString());
    stroke.insert(
        QStringLiteral("createdAtOrder"),
        stroke_map.value(QStringLiteral("createdAtOrder")).toInt());
    stroke.insert(QStringLiteral("operation"), stroke_map.value(QStringLiteral("operation")).toString());
    stroke.insert(
        QStringLiteral("sampleCount"),
        stroke_map.contains(QStringLiteral("sampleCount"))
            ? stroke_map.value(QStringLiteral("sampleCount")).toInt()
            : stroke_map.value(QStringLiteral("samples")).toList().size());
    stroke.insert(
        QStringLiteral("settings"),
        QJsonObject::fromVariantMap(stroke_map.value(QStringLiteral("settings")).toMap()));
    QJsonArray samples;
    for (const auto& sample_value : stroke_map.value(QStringLiteral("samples")).toList()) {
      const auto sample_map = sample_value.toMap();
      QJsonObject sample;
      sample.insert(QStringLiteral("x"), sample_map.value(QStringLiteral("x")).toDouble());
      sample.insert(QStringLiteral("y"), sample_map.value(QStringLiteral("y")).toDouble());
      sample.insert(QStringLiteral("size"), sample_map.value(QStringLiteral("size")).toDouble());
      sample.insert(QStringLiteral("flow"), sample_map.value(QStringLiteral("flow")).toDouble());
      sample.insert(
          QStringLiteral("opacity"),
          sample_map.contains(QStringLiteral("opacity")) ? sample_map.value(QStringLiteral("opacity")).toDouble() : 1.0);
      sample.insert(
          QStringLiteral("autoMaskAnchorX"),
          sample_map.contains(QStringLiteral("autoMaskAnchorX"))
              ? sample_map.value(QStringLiteral("autoMaskAnchorX")).toDouble()
              : sample_map.value(QStringLiteral("x")).toDouble());
      sample.insert(
          QStringLiteral("autoMaskAnchorY"),
          sample_map.contains(QStringLiteral("autoMaskAnchorY"))
              ? sample_map.value(QStringLiteral("autoMaskAnchorY")).toDouble()
              : sample_map.value(QStringLiteral("y")).toDouble());
      sample.insert(
          QStringLiteral("pressure"),
          sample_map.contains(QStringLiteral("pressure"))
              ? sample_map.value(QStringLiteral("pressure")).toDouble()
              : 1.0);
      sample.insert(QStringLiteral("tiltX"), sample_map.value(QStringLiteral("tiltX")).toDouble());
      sample.insert(QStringLiteral("tiltY"), sample_map.value(QStringLiteral("tiltY")).toDouble());
      sample.insert(
          QStringLiteral("erase"),
          sample_map.contains(QStringLiteral("erase"))
              ? sample_map.value(QStringLiteral("erase")).toBool()
              : false);
      samples.append(sample);
    }
    stroke.insert(QStringLiteral("samples"), samples);
    brush_strokes.append(stroke);
  }
  geometry->insert(QStringLiteral("brushStrokes"), brush_strokes);
}

void insert_serialized_brush_raster(
    const QVariantMap& mask_definition,
    QJsonObject* geometry) {
  const auto segmentation_data =
      mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
  const auto segmentation_width =
      mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
  const auto segmentation_height =
      mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
  if (!valid_segmentation_payload(
          segmentation_data, segmentation_width, segmentation_height)) {
    return;
  }
  geometry->insert(
      QStringLiteral("segmentationData"),
      QString::fromLatin1(segmentation_data.toBase64()));
  geometry->insert(QStringLiteral("segmentationWidth"), segmentation_width);
  geometry->insert(QStringLiteral("segmentationHeight"), segmentation_height);
  geometry->insert(
      QStringLiteral("rasterSchemaVersion"),
      std::max(
          1,
          map_int_value_or(
              mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
}

bool insert_serialized_shape_or_range_payload(
    const QString& kind,
    const QVariantMap& mask_definition,
    QJsonObject* geometry,
    QJsonObject* constraints) {
  if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
    QJsonArray polygon_points;
    for (const auto& point_value :
         mask_definition.value(QStringLiteral("polygonPoints")).toList()) {
      const auto point = point_value.toMap();
      QJsonObject polygon_point;
      polygon_point.insert(
          QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
      polygon_point.insert(
          QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
      polygon_points.append(polygon_point);
    }
    geometry->insert(QStringLiteral("polygonPoints"), polygon_points);
    if (kind == QStringLiteral("lasso")) {
      geometry->insert(
          QStringLiteral("lassoClosed"),
          mask_definition.value(QStringLiteral("lassoClosed")).toBool());
      geometry->insert(
          QStringLiteral("lassoSimplified"),
          mask_definition.value(QStringLiteral("lassoSimplified")).toBool());
      geometry->insert(
          QStringLiteral("lassoSmoothed"),
          mask_definition.value(QStringLiteral("lassoSmoothed")).toBool());
      geometry->insert(
          QStringLiteral("lassoFreehandPath"),
          mask_definition.contains(QStringLiteral("lassoFreehandPath"))
              ? mask_definition.value(QStringLiteral("lassoFreehandPath")).toBool()
              : true);
      geometry->insert(
          QStringLiteral("lassoRawPointCount"),
          mask_definition.value(QStringLiteral("lassoRawPointCount")).toInt());
      geometry->insert(
          QStringLiteral("lassoSimplifiedPointCount"),
          mask_definition.value(QStringLiteral("lassoSimplifiedPointCount")).toInt());
      QJsonArray lasso_raw_points;
      for (const auto& point_value :
           mask_definition.value(QStringLiteral("lassoRawPoints")).toList()) {
        const auto point = point_value.toMap();
        QJsonObject lasso_point;
        lasso_point.insert(
            QStringLiteral("x"), point.value(QStringLiteral("x")).toDouble());
        lasso_point.insert(
            QStringLiteral("y"), point.value(QStringLiteral("y")).toDouble());
        lasso_raw_points.append(lasso_point);
      }
      geometry->insert(QStringLiteral("lassoRawPoints"), lasso_raw_points);
    }
    return true;
  }

  if (kind == QStringLiteral("depth")) {
    constraints->insert(
        QStringLiteral("depthMin"),
        mask_definition.value(QStringLiteral("depthMin")).toDouble());
    constraints->insert(
        QStringLiteral("depthMax"),
        mask_definition.value(QStringLiteral("depthMax")).toDouble());
    constraints->insert(
        QStringLiteral("rangeFeather"),
        mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    constraints->insert(
        QStringLiteral("depthSamples"),
        QJsonArray::fromVariantList(normalized_depth_range_samples(
            mask_definition.value(QStringLiteral("depthSamples")).toList())));
    const auto depth_data =
        mask_definition.value(QStringLiteral("depthData")).toByteArray();
    const auto depth_width =
        mask_definition.value(QStringLiteral("depthWidth")).toInt();
    const auto depth_height =
        mask_definition.value(QStringLiteral("depthHeight")).toInt();
    if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
      geometry->insert(
          QStringLiteral("depthData"),
          QString::fromLatin1(depth_data.toBase64()));
      geometry->insert(QStringLiteral("depthWidth"), depth_width);
      geometry->insert(QStringLiteral("depthHeight"), depth_height);
    }
    return true;
  }

  if (kind == QStringLiteral("luminance")) {
    constraints->insert(
        QStringLiteral("luminanceMin"),
        mask_definition.value(QStringLiteral("luminanceMin")).toDouble());
    constraints->insert(
        QStringLiteral("luminanceMax"),
        mask_definition.value(QStringLiteral("luminanceMax")).toDouble());
    constraints->insert(
        QStringLiteral("rangeFeather"),
        mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    constraints->insert(
        QStringLiteral("luminanceSamples"),
        QJsonArray::fromVariantList(normalized_luminance_range_samples(
            mask_definition.value(QStringLiteral("luminanceSamples")).toList())));
    return true;
  }

  if (kind == QStringLiteral("color")) {
    constraints->insert(
        QStringLiteral("hueCenter"),
        mask_definition.value(QStringLiteral("hueCenter")).toDouble());
    constraints->insert(
        QStringLiteral("hueRange"),
        mask_definition.value(QStringLiteral("hueRange")).toDouble());
    constraints->insert(
        QStringLiteral("saturationMin"),
        mask_definition.value(QStringLiteral("saturationMin")).toDouble());
    constraints->insert(
        QStringLiteral("saturationMax"),
        mask_definition.value(QStringLiteral("saturationMax")).toDouble());
    constraints->insert(
        QStringLiteral("rangeFeather"),
        mask_definition.value(QStringLiteral("rangeFeather")).toDouble());
    constraints->insert(
        QStringLiteral("colorSamples"),
        QJsonArray::fromVariantList(
            mask_definition.value(QStringLiteral("colorSamples")).toList()));
    return true;
  }

  return false;
}

EditRecipeState parse_edit_adjustment_state_object_versioned(
    const QJsonObject& object,
    int recipe_version) {
  const auto global_adjustments = detail::object_value(object, QStringLiteral("globalAdjustments"));
  const auto curves = detail::object_value(object, QStringLiteral("curves"));
  const auto parametric_curve = detail::object_value(curves, QStringLiteral("parametric"));
  const auto color_mixer = detail::object_value(object, QStringLiteral("colorMixer"));
  const auto crop_transform = detail::object_value(object, QStringLiteral("cropTransform"));
  const auto optics = detail::object_value(object, QStringLiteral("optics"));
  const auto raw_adjustments = detail::object_value(object, QStringLiteral("rawAdjustments"));
  const auto output_transform = detail::object_value(object, QStringLiteral("outputTransform"));
  const auto export_transform = detail::object_value(object, QStringLiteral("exportTransform"));
  const auto color_pipeline = detail::object_value(object, QStringLiteral("colorPipeline"));
  const auto profile_assets = detail::object_value(color_pipeline, QStringLiteral("profileAssets"));
  const auto proofing = detail::object_value(color_pipeline, QStringLiteral("proofing"));

  EditRecipeState state;
  state.input_color_profile = normalized_input_color_profile_name(
      color_pipeline.contains(QStringLiteral("inputProfile"))
          ? color_pipeline.value(QStringLiteral("inputProfile")).toString()
          : raw_adjustments.value(QStringLiteral("inputProfile")).toString());
  state.working_color_profile = normalized_working_color_profile_name(
      color_pipeline.contains(QStringLiteral("workingProfile"))
          ? color_pipeline.value(QStringLiteral("workingProfile")).toString()
          : output_transform.value(QStringLiteral("workingColorSpace")).toString());
  state.camera_dcp_profile_asset = parse_dcp_profile_asset_state(
      color_pipeline.value(QStringLiteral("cameraProfileAsset")));
  state.camera_profile_rendering_model =
      color_pipeline.contains(QStringLiteral("cameraProfileRenderingModel"))
          ? normalized_camera_profile_rendering_model_name(
                color_pipeline.value(QStringLiteral("cameraProfileRenderingModel"))
                    .toString())
          : (recipe_version <= 7
                 ? QStringLiteral("matrix-only-v1")
                 : QStringLiteral("adobe-dng-profile-v2"));
  state.photographic_rendering_model = normalized_photographic_rendering_model_name(
      color_pipeline.contains(QStringLiteral("photographicRenderingModel"))
          ? color_pipeline.value(QStringLiteral("photographicRenderingModel")).toString()
          : QStringLiteral("legacy-oklch-v1"));
  state.renderer_transition_look = normalized_renderer_transition_look_name(
      color_pipeline.contains(QStringLiteral("rendererTransitionLook"))
          ? color_pipeline.value(QStringLiteral("rendererTransitionLook")).toString()
          : QStringLiteral("none"));
  state.input_icc_profile_asset =
      parse_icc_profile_asset_state(profile_assets.value(QStringLiteral("input")));
  state.preview_icc_profile_asset =
      parse_icc_profile_asset_state(profile_assets.value(QStringLiteral("preview")));
  state.proof_icc_profile_asset =
      parse_icc_profile_asset_state(profile_assets.value(QStringLiteral("proof")));
  state.export_icc_profile_asset =
      parse_icc_profile_asset_state(profile_assets.value(QStringLiteral("export")));
  state.render_intent = normalized_render_intent_name(
      color_pipeline.contains(QStringLiteral("renderIntent"))
          ? color_pipeline.value(QStringLiteral("renderIntent")).toString()
          : output_transform.value(QStringLiteral("renderIntent")).toString());
  state.proof_render_intent = normalized_render_intent_name(
      proofing.value(QStringLiteral("renderIntent"))
          .toString(QStringLiteral("Relative Colorimetric")));
  state.proof_simulate_paper_white =
      proofing.value(QStringLiteral("simulatePaperWhite")).toBool(false);
  state.proof_simulate_ink_black =
      proofing.value(QStringLiteral("simulateInkBlack")).toBool(false);
  state.output_gamut_mapping_enabled =
      color_pipeline.contains(QStringLiteral("gamutMappingEnabled"))
          ? color_pipeline.value(QStringLiteral("gamutMappingEnabled")).toBool(true)
          : true;
  state.output_gamut_mapping_policy = normalized_gamut_mapping_policy_name(
      color_pipeline.value(QStringLiteral("gamutMappingPolicy")).toString());
  state.black_point_compensation_enabled =
      color_pipeline.contains(QStringLiteral("blackPointCompensationEnabled"))
          ? color_pipeline.value(QStringLiteral("blackPointCompensationEnabled")).toBool(true)
          : true;
  state.source_black_point = qBound(
      0.0,
      color_pipeline.value(QStringLiteral("sourceBlackPoint")).toDouble(0.0),
      16.0);
  state.output_black_point = qBound(
      0.0,
      color_pipeline.value(QStringLiteral("outputBlackPoint")).toDouble(0.0),
      16.0);
  state.raw_input_transform_enabled =
      raw_adjustments.contains(QStringLiteral("inputTransformEnabled"))
          ? raw_adjustments.value(QStringLiteral("inputTransformEnabled")).toBool()
          : true;
  state.highlight_reconstruction_mode = normalized_highlight_reconstruction_mode_name(
      raw_adjustments.contains(QStringLiteral("highlightReconstructionMode"))
          ? raw_adjustments.value(QStringLiteral("highlightReconstructionMode")).toString()
          : (raw_adjustments.contains(QStringLiteral("highlightRecovery")) &&
                     !raw_adjustments.value(QStringLiteral("highlightRecovery")).toBool()
                 ? QStringLiteral("Disabled")
                 : QStringLiteral("Enabled")));
  state.highlight_reconstruction_model =
      raw_adjustments.contains(QStringLiteral("highlightReconstructionModel"))
          ? normalized_highlight_reconstruction_model_name(
                raw_adjustments.value(QStringLiteral("highlightReconstructionModel"))
                    .toString())
          : (recipe_version <= 8
                 ? QStringLiteral("legacy-working-rgb-v1")
                 : QStringLiteral("camera-linear-opposed-v2"));
  state.chromatic_adaptation_method = normalized_chromatic_adaptation_method_name(
      raw_adjustments.value(QStringLiteral("chromaticAdaptationMethod")).toString());
  state.raw_demosaic_quality = normalized_raw_demosaic_quality_name(
      raw_adjustments.value(QStringLiteral("demosaicQuality")).toString());
  state.raw_exposure = raw_adjustments.value(QStringLiteral("exposure")).toDouble();
  state.raw_boost = raw_adjustments.value(QStringLiteral("boost")).toDouble();
  state.raw_shadow_boost = raw_adjustments.value(QStringLiteral("shadowBoost")).toDouble(1.0);
  state.raw_detail = raw_adjustments.value(QStringLiteral("detail")).toDouble();
  state.raw_chroma_noise_reduction = std::clamp(
      raw_adjustments.value(QStringLiteral("chromaNoiseReduction")).toDouble(),
      0.0,
      1.0);
  state.raw_moire_reduction = std::clamp(
      raw_adjustments.value(QStringLiteral("moireReduction")).toDouble(),
      0.0,
      1.0);
  state.raw_hot_dead_pixel_cleanup = std::clamp(
      raw_adjustments.value(QStringLiteral("hotDeadPixelCleanup")).toDouble(),
      0.0,
      1.0);
  state.raw_false_color_suppression = std::clamp(
      raw_adjustments.value(QStringLiteral("falseColorSuppression")).toDouble(),
      0.0,
      1.0);
  state.raw_highlight_recovery =
      raw_adjustments.contains(QStringLiteral("highlightRecovery"))
          ? raw_adjustments.value(QStringLiteral("highlightRecovery")).toBool()
          : true;
  state.raw_gamut_mapping =
      raw_adjustments.contains(QStringLiteral("gamutMapping"))
          ? raw_adjustments.value(QStringLiteral("gamutMapping")).toBool()
          : true;
  state.raw_lens_correction =
      raw_adjustments.contains(QStringLiteral("lensCorrection"))
          ? raw_adjustments.value(QStringLiteral("lensCorrection")).toBool()
          : true;
  state.raw_white_balance_override =
      raw_adjustments.value(QStringLiteral("whiteBalanceOverride")).toBool();
  state.raw_camera_matching_look = normalized_camera_matching_look_name(
      raw_adjustments.value(QStringLiteral("cameraMatchingLook")).toString());
  state.raw_white_balance_temperature =
      raw_adjustments.value(QStringLiteral("whiteBalanceTemperature")).toDouble(5200.0);
  state.raw_white_balance_tint =
      raw_adjustments.value(QStringLiteral("whiteBalanceTint")).toDouble();
  state.raw_black_level = std::clamp(
      raw_adjustments.value(QStringLiteral("blackLevel")).toDouble(0.0),
      0.0,
      0.95);
  state.raw_white_level = std::clamp(
      raw_adjustments.value(QStringLiteral("whiteLevel")).toDouble(1.0),
      state.raw_black_level + 0.01,
      4.0);
  state.raw_calibration_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("calibrationHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_calibration_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("calibrationSaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_red_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("redPrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_red_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("redPrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_green_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("greenPrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_green_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("greenPrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.raw_blue_primary_hue = std::clamp(
      raw_adjustments.value(QStringLiteral("bluePrimaryHue")).toDouble(),
      -100.0,
      100.0);
  state.raw_blue_primary_saturation = std::clamp(
      raw_adjustments.value(QStringLiteral("bluePrimarySaturation")).toDouble(),
      -100.0,
      100.0);
  state.exposure = detail::read_double(
      global_adjustments, QStringLiteral("exposure"), object, QStringLiteral("exposure"));
  state.contrast = detail::read_double(
      global_adjustments, QStringLiteral("contrast"), object, QStringLiteral("contrast"));
  state.highlights = detail::read_double(
      global_adjustments, QStringLiteral("highlights"), object, QStringLiteral("highlights"));
  state.shadows = detail::read_double(
      global_adjustments, QStringLiteral("shadows"), object, QStringLiteral("shadows"));
  state.whites = detail::read_double(
      global_adjustments, QStringLiteral("whites"), object, QStringLiteral("whites"));
  state.blacks = detail::read_double(
      global_adjustments, QStringLiteral("blacks"), object, QStringLiteral("blacks"));
  state.brightness = detail::read_double(
      global_adjustments, QStringLiteral("brightness"), object, QStringLiteral("brightness"));
  state.hdr_local_tone_mapping = detail::read_double(
      global_adjustments, QStringLiteral("hdrLocalTone"), object, QStringLiteral("hdrLocalTone"));
  state.filmic_shoulder = detail::read_double(
      global_adjustments, QStringLiteral("filmicShoulder"), object, QStringLiteral("filmicShoulder"));
  state.filmic_toe = detail::read_double(
      global_adjustments, QStringLiteral("filmicToe"), object, QStringLiteral("filmicToe"));
  state.adaptive_contrast = detail::read_double(
      global_adjustments, QStringLiteral("adaptiveContrast"), object, QStringLiteral("adaptiveContrast"));
  state.levels_black_point = detail::read_double(
      global_adjustments, QStringLiteral("levelsBlackPoint"), object, QStringLiteral("levelsBlackPoint"));
  state.levels_gamma = detail::read_double(
      global_adjustments, QStringLiteral("levelsGamma"), object, QStringLiteral("levelsGamma"));
  state.levels_white_point = detail::read_double(
      global_adjustments, QStringLiteral("levelsWhitePoint"), object, QStringLiteral("levelsWhitePoint"));
  state.zone_deep_shadows = detail::read_double(
      global_adjustments, QStringLiteral("zoneDeepShadows"), object, QStringLiteral("zoneDeepShadows"));
  state.zone_shadows = detail::read_double(
      global_adjustments, QStringLiteral("zoneShadows"), object, QStringLiteral("zoneShadows"));
  state.zone_midtones = detail::read_double(
      global_adjustments, QStringLiteral("zoneMidtones"), object, QStringLiteral("zoneMidtones"));
  state.zone_lights = detail::read_double(
      global_adjustments, QStringLiteral("zoneLights"), object, QStringLiteral("zoneLights"));
  state.zone_highlights = detail::read_double(
      global_adjustments, QStringLiteral("zoneHighlights"), object, QStringLiteral("zoneHighlights"));
  state.temperature = detail::read_double(
      global_adjustments, QStringLiteral("temperature"), object, QStringLiteral("temperature"));
  state.tint = detail::read_double(
      global_adjustments, QStringLiteral("tint"), object, QStringLiteral("tint"));
  state.texture = detail::read_double(
      global_adjustments, QStringLiteral("texture"), object, QStringLiteral("texture"));
  state.clarity = detail::read_double(
      global_adjustments, QStringLiteral("clarity"), object, QStringLiteral("clarity"));
  state.dehaze = detail::read_double(
      global_adjustments, QStringLiteral("dehaze"), object, QStringLiteral("dehaze"));
  state.sharpening = detail::read_double(
      global_adjustments, QStringLiteral("sharpening"), object, QStringLiteral("sharpening"));
  state.sharpen_radius = detail::read_double(
      global_adjustments, QStringLiteral("sharpenRadius"), object, QStringLiteral("sharpenRadius"), 1.0);
  state.sharpen_detail = detail::read_double(
      global_adjustments, QStringLiteral("sharpenDetail"), object, QStringLiteral("sharpenDetail"));
  state.sharpen_masking = detail::read_double(
      global_adjustments, QStringLiteral("sharpenMasking"), object, QStringLiteral("sharpenMasking"));
  state.luminance_nr = detail::read_double(
      global_adjustments, QStringLiteral("luminanceNr"), object, QStringLiteral("luminanceNr"));
  state.color_nr = detail::read_double(
      global_adjustments, QStringLiteral("colorNr"), object, QStringLiteral("colorNr"));
  state.lens_distortion = detail::read_double(
      optics, QStringLiteral("lensDistortion"), global_adjustments, QStringLiteral("lensDistortion"));
  state.lens_vignette = detail::read_double(
      optics, QStringLiteral("lensVignette"), global_adjustments, QStringLiteral("lensVignette"));
  state.chromatic_aberration = detail::read_double(
      optics, QStringLiteral("chromaticAberration"), global_adjustments, QStringLiteral("chromaticAberration"));
  state.perspective_vertical = detail::read_double(
      crop_transform,
      QStringLiteral("perspectiveVertical"),
      global_adjustments,
      QStringLiteral("perspectiveVertical"));
  state.perspective_horizontal = detail::read_double(
      crop_transform,
      QStringLiteral("perspectiveHorizontal"),
      global_adjustments,
      QStringLiteral("perspectiveHorizontal"));
  state.transform_rotation = detail::read_double(
      crop_transform, QStringLiteral("rotation"), global_adjustments, QStringLiteral("rotation"));
  state.transform_scale = std::clamp(
      detail::read_double(crop_transform, QStringLiteral("scale"), global_adjustments, QStringLiteral("scale"), 1.0),
      0.25,
      4.0);
  state.transform_skew_x = detail::read_double(
      crop_transform, QStringLiteral("skewX"), global_adjustments, QStringLiteral("skewX"));
  state.transform_skew_y = detail::read_double(
      crop_transform, QStringLiteral("skewY"), global_adjustments, QStringLiteral("skewY"));
  state.liquify_center_x = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyCenterX"), global_adjustments, QStringLiteral("liquifyCenterX"), 0.5),
      0.0,
      1.0);
  state.liquify_center_y = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyCenterY"), global_adjustments, QStringLiteral("liquifyCenterY"), 0.5),
      0.0,
      1.0);
  state.liquify_radius = std::clamp(
      detail::read_double(
          crop_transform, QStringLiteral("liquifyRadius"), global_adjustments, QStringLiteral("liquifyRadius"), 0.25),
      0.02,
      1.0);
  state.liquify_strength = detail::read_double(
      crop_transform, QStringLiteral("liquifyStrength"), global_adjustments, QStringLiteral("liquifyStrength"));
  state.content_aware_crop_fill =
      crop_transform.contains(QStringLiteral("contentAwareFill"))
          ? crop_transform.value(QStringLiteral("contentAwareFill")).toBool()
          : object.value(QStringLiteral("contentAwareCropFill")).toBool(false);
  state.vibrance = detail::read_double(
      global_adjustments, QStringLiteral("vibrance"), object, QStringLiteral("vibrance"));
  state.saturation = detail::read_double(
      global_adjustments, QStringLiteral("saturation"), object, QStringLiteral("saturation"));

  state.curve_shadows = detail::read_double(
      parametric_curve, QStringLiteral("shadows"), object, QStringLiteral("curveShadows"));
  state.curve_darks = detail::read_double(
      parametric_curve, QStringLiteral("darks"), object, QStringLiteral("curveDarks"));
  state.curve_lights = detail::read_double(
      parametric_curve, QStringLiteral("lights"), object, QStringLiteral("curveLights"));
  state.curve_highlights = detail::read_double(
      parametric_curve, QStringLiteral("highlights"), object, QStringLiteral("curveHighlights"));

  for (const auto& channel_value : curves.value(QStringLiteral("channels")).toArray()) {
    if (!channel_value.isObject()) {
      continue;
    }

    const auto channel_object = channel_value.toObject();
    const auto channel_name = channel_object.value(QStringLiteral("channel")).toString().toLower();
    const auto channel_points =
        parse_curve_points(channel_object.value(QStringLiteral("points")).toArray());
    if (channel_name == QStringLiteral("master")) {
      state.master_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("luma")) {
      state.luma_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("red")) {
      state.red_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("green")) {
      state.green_curve_points = channel_points;
    } else if (channel_name == QStringLiteral("blue")) {
      state.blue_curve_points = channel_points;
    }
  }

  state.hsl_channel = detail::read_string(
      color_mixer, QStringLiteral("channel"), object, QStringLiteral("hslChannel"), QStringLiteral("Reds"));
  state.color_mixer_model = detail::read_string(
      color_mixer, QStringLiteral("model"), object, QStringLiteral("colorMixerModel"), QStringLiteral("OKLab"));
  state.hue_adjustment = detail::read_double(
      color_mixer, QStringLiteral("hue"), object, QStringLiteral("hueAdjustment"));
  state.saturation_adjustment = detail::read_double(
      color_mixer, QStringLiteral("saturation"), object, QStringLiteral("saturationAdjustment"));
  state.luminance_adjustment = detail::read_double(
      color_mixer, QStringLiteral("luminance"), object, QStringLiteral("luminanceAdjustment"));

  // Deserialize per-channel mixer adjustments.
  static const QStringList kMixerChannelNames = {
      QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
      QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
      QStringLiteral("Purples"), QStringLiteral("Magentas"),
  };
  const auto mixer_channels = color_mixer.value(QStringLiteral("channels")).toObject();
  for (int i = 0; i < 8; ++i) {
    const auto ch = mixer_channels.value(kMixerChannelNames[i]).toObject();
    if (!ch.isEmpty()) {
      state.mixer_channels[static_cast<std::size_t>(i)].hue = ch.value(QStringLiteral("hue")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].saturation = ch.value(QStringLiteral("saturation")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].luminance = ch.value(QStringLiteral("luminance")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_cyan = ch.value(QStringLiteral("selectiveCyan")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_magenta = ch.value(QStringLiteral("selectiveMagenta")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_yellow = ch.value(QStringLiteral("selectiveYellow")).toDouble();
      state.mixer_channels[static_cast<std::size_t>(i)].selective_black = ch.value(QStringLiteral("selectiveBlack")).toDouble();
    }
  }
  // Backward compat: if no per-channel data, populate the selected channel from legacy fields.
  if (mixer_channels.isEmpty()) {
    for (int i = 0; i < 8; ++i) {
      if (kMixerChannelNames[i] == state.hsl_channel) {
        state.mixer_channels[static_cast<std::size_t>(i)].hue = state.hue_adjustment;
        state.mixer_channels[static_cast<std::size_t>(i)].saturation = state.saturation_adjustment;
        state.mixer_channels[static_cast<std::size_t>(i)].luminance = state.luminance_adjustment;
        break;
      }
    }
  }

  const auto rgb_channel_mixer = color_mixer.value(QStringLiteral("rgbChannelMixer")).toObject();
  if (!rgb_channel_mixer.isEmpty()) {
    state.rgb_channel_mixer.red_from_red = rgb_channel_mixer.value(QStringLiteral("redFromRed")).toDouble(100.0);
    state.rgb_channel_mixer.red_from_green = rgb_channel_mixer.value(QStringLiteral("redFromGreen")).toDouble();
    state.rgb_channel_mixer.red_from_blue = rgb_channel_mixer.value(QStringLiteral("redFromBlue")).toDouble();
    state.rgb_channel_mixer.green_from_red = rgb_channel_mixer.value(QStringLiteral("greenFromRed")).toDouble();
    state.rgb_channel_mixer.green_from_green =
        rgb_channel_mixer.value(QStringLiteral("greenFromGreen")).toDouble(100.0);
    state.rgb_channel_mixer.green_from_blue = rgb_channel_mixer.value(QStringLiteral("greenFromBlue")).toDouble();
    state.rgb_channel_mixer.blue_from_red = rgb_channel_mixer.value(QStringLiteral("blueFromRed")).toDouble();
    state.rgb_channel_mixer.blue_from_green = rgb_channel_mixer.value(QStringLiteral("blueFromGreen")).toDouble();
    state.rgb_channel_mixer.blue_from_blue = rgb_channel_mixer.value(QStringLiteral("blueFromBlue")).toDouble(100.0);
  }

  const auto black_and_white = color_mixer.value(QStringLiteral("blackAndWhite")).toObject();
  state.black_and_white_enabled =
      black_and_white.value(QStringLiteral("enabled")).toBool(state.black_and_white_enabled);
  const auto black_and_white_channels = black_and_white.value(QStringLiteral("channels")).toObject();
  for (int i = 0; i < 8; ++i) {
    state.black_and_white_mixer[static_cast<std::size_t>(i)] =
        black_and_white_channels.value(kMixerChannelNames[i]).toDouble();
  }
  const auto color_replacement = color_mixer.value(QStringLiteral("colorReplacement")).toObject();
  if (!color_replacement.isEmpty()) {
    state.color_replacement_enabled = color_replacement.value(QStringLiteral("enabled")).toBool();
    state.color_replacement_source_hue = color_replacement.value(QStringLiteral("sourceHue")).toDouble();
    state.color_replacement_target_hue = color_replacement.value(QStringLiteral("targetHue")).toDouble(0.33);
    state.color_replacement_range = color_replacement.value(QStringLiteral("range")).toDouble(35.0);
    state.color_replacement_softness = color_replacement.value(QStringLiteral("softness")).toDouble(100.0);
    state.color_replacement_strength = color_replacement.value(QStringLiteral("strength")).toDouble();
    state.color_replacement_preserve_luminance =
        color_replacement.value(QStringLiteral("preserveLuminance")).toBool(true);
    state.color_replacement_preserve_saturation =
        color_replacement.value(QStringLiteral("preserveSaturation")).toBool(true);
  }
  const auto skin_tone = color_mixer.value(QStringLiteral("skinToneProtection")).toObject();
  if (!skin_tone.isEmpty()) {
    state.skin_tone_protection_enabled = skin_tone.value(QStringLiteral("enabled")).toBool();
    state.skin_tone_line_hue = skin_tone.value(QStringLiteral("lineHue")).toDouble(0.08);
    state.skin_tone_protection_range = skin_tone.value(QStringLiteral("range")).toDouble(30.0);
    state.skin_tone_protection_strength = skin_tone.value(QStringLiteral("strength")).toDouble();
  }
  const auto color_harmony = color_mixer.value(QStringLiteral("colorHarmony")).toObject();
  if (!color_harmony.isEmpty()) {
    state.color_harmony_enabled = color_harmony.value(QStringLiteral("enabled")).toBool();
    state.color_harmony_mode =
        color_harmony.value(QStringLiteral("mode")).toString(QStringLiteral("Complementary"));
    state.color_harmony_base_hue = color_harmony.value(QStringLiteral("baseHue")).toDouble();
    state.color_harmony_strength = color_harmony.value(QStringLiteral("strength")).toDouble();
  }

  const auto color_grading = detail::object_value(object, QStringLiteral("colorGrading"));
  const auto grading_shadows = detail::object_value(color_grading, QStringLiteral("shadows"));
  const auto grading_midtones = detail::object_value(color_grading, QStringLiteral("midtones"));
  const auto grading_highlights = detail::object_value(color_grading, QStringLiteral("highlights"));
  state.grading_shadows_hue = grading_shadows.value(QStringLiteral("hue")).toDouble();
  state.grading_shadows_saturation = grading_shadows.value(QStringLiteral("saturation")).toDouble();
  state.grading_shadows_luminance = grading_shadows.value(QStringLiteral("luminance")).toDouble();
  state.grading_midtones_hue = grading_midtones.value(QStringLiteral("hue")).toDouble();
  state.grading_midtones_saturation = grading_midtones.value(QStringLiteral("saturation")).toDouble();
  state.grading_midtones_luminance = grading_midtones.value(QStringLiteral("luminance")).toDouble();
  state.grading_highlights_hue = grading_highlights.value(QStringLiteral("hue")).toDouble();
  state.grading_highlights_saturation = grading_highlights.value(QStringLiteral("saturation")).toDouble();
  state.grading_highlights_luminance = grading_highlights.value(QStringLiteral("luminance")).toDouble();
  state.grading_amount = color_grading.value(QStringLiteral("amount")).toDouble(100.0);
  state.grading_balance = color_grading.value(QStringLiteral("balance")).toDouble();

  state.crop_zoom = detail::read_double(
      crop_transform, QStringLiteral("zoom"), object, QStringLiteral("cropZoom"), 1.0);
  state.crop_preset = detail::read_string(
      crop_transform, QStringLiteral("preset"), object, QStringLiteral("cropPreset"), QStringLiteral("Original"));
  state.upright_mode = normalized_upright_mode_name(detail::read_string(
      crop_transform, QStringLiteral("uprightMode"), object, QStringLiteral("uprightMode"), QStringLiteral("Off")));
  state.upright_vertical_guide_angle = detail::read_double(
      crop_transform,
      QStringLiteral("uprightVerticalGuideAngle"),
      object,
      QStringLiteral("uprightVerticalGuideAngle"));
  state.upright_horizontal_guide_angle = detail::read_double(
      crop_transform,
      QStringLiteral("uprightHorizontalGuideAngle"),
      object,
      QStringLiteral("uprightHorizontalGuideAngle"));
  state.crop_rect_x = crop_transform.value(QStringLiteral("x")).toDouble();
  state.crop_rect_y = crop_transform.value(QStringLiteral("y")).toDouble();
  state.crop_rect_width = crop_transform.value(QStringLiteral("width")).toDouble();
  state.crop_rect_height = crop_transform.value(QStringLiteral("height")).toDouble();
  state.output_color_space = normalized_output_color_space_name(
      color_pipeline.contains(QStringLiteral("exportOutputProfile"))
          ? color_pipeline.value(QStringLiteral("exportOutputProfile")).toString()
          : export_transform.contains(QStringLiteral("colorSpace"))
          ? export_transform.value(QStringLiteral("colorSpace")).toString()
          : output_transform.contains(QStringLiteral("colorSpace"))
          ? output_transform.value(QStringLiteral("colorSpace")).toString()
          : object.value(QStringLiteral("outputColorSpace")).toString());
  state.preview_color_space = normalized_output_color_space_name(
      color_pipeline.contains(QStringLiteral("previewOutputProfile"))
          ? color_pipeline.value(QStringLiteral("previewOutputProfile")).toString()
          : output_transform.contains(QStringLiteral("previewColorSpace"))
          ? output_transform.value(QStringLiteral("previewColorSpace")).toString()
          : state.output_color_space);
  state.preview_output_mode =
      normalized_preview_output_mode_name(
          output_transform.value(
              QStringLiteral("previewMode"))
              .toString(QStringLiteral("Monitor")));
  state.monitor_color_profile = normalized_monitor_color_profile_name(
      output_transform.value(QStringLiteral("monitorProfile")).toString());
  state.soft_proof_enabled = output_transform.value(QStringLiteral("softProofEnabled")).toBool(false);
  state.soft_proof_profile = normalized_output_color_space_name(
      output_transform.value(QStringLiteral("softProofProfile")).toString(QStringLiteral("sRGB")));
  state.output_headroom = qBound(
      1.0,
      color_pipeline.contains(QStringLiteral("headroom"))
          ? color_pipeline.value(QStringLiteral("headroom")).toDouble(1.0)
          : output_transform.value(QStringLiteral("headroom")).toDouble(1.0),
      16.0);
  state.preset_applications =
      object.value(QStringLiteral("presetApplications")).toArray().toVariantList();
  state.layers = detail::parse_photo_layers(
      object.contains(QStringLiteral("photoLayers"))
          ? object.value(QStringLiteral("photoLayers")).toArray()
          : object.value(QStringLiteral("layers")).toArray());
  const auto history = detail::object_value(object, QStringLiteral("history"));
  state.active_history_branch = history.value(QStringLiteral("activeBranchId")).toString(QStringLiteral("main"));
  state.recipe_snapshots = history.value(QStringLiteral("snapshots")).toArray().toVariantList();

  QVariantList parsed_mask_stack;
  auto linear_mask_ordinal = 1;
  auto radial_mask_ordinal = 1;
  const auto append_parsed_mask = [&](const QJsonObject& mask_object,
                                      const QJsonObject& adjustments_override,
                                      const QString& default_tool_role,
                                      const QString& mask_set_id,
                                      const QString& mask_set_name) {
    const auto adjustments =
        adjustments_override.isEmpty()
            ? detail::object_value(mask_object, QStringLiteral("adjustments"))
            : adjustments_override;
    const auto geometry = detail::object_value(mask_object, QStringLiteral("geometry"));
    const auto constraints = detail::object_value(mask_object, QStringLiteral("constraints"));
    const auto selection = detail::object_value(mask_object, QStringLiteral("selection"));
    const auto kind = normalized_mask_kind(mask_object.value(QStringLiteral("kind")).toString());
    const auto matte_cleanup_defaults = matte_cleanup_defaults_for_kind(kind);

    QVariantMap parsed_mask;
    parsed_mask.insert(QStringLiteral("id"), mask_object.value(QStringLiteral("id")).toString());
    parsed_mask.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, mask_object.value(QStringLiteral("schemaVersion")).toInt(1)));
    parsed_mask.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, mask_object.value(QStringLiteral("sourceSchemaVersion")).toInt(1)));
    parsed_mask.insert(QStringLiteral("parentId"), mask_object.value(QStringLiteral("parentId")).toString());
    parsed_mask.insert(QStringLiteral("outputId"), mask_object.value(QStringLiteral("outputId")).toString());
    parsed_mask.insert(
        QStringLiteral("reusableOutput"),
        mask_object.value(QStringLiteral("reusableOutput")).toBool(false));
    parsed_mask.insert(QStringLiteral("name"), mask_object.value(QStringLiteral("name")).toString());
    parsed_mask.insert(QStringLiteral("kind"), kind);
    const auto agent_visual_referent_id =
        mask_object.value(QStringLiteral("agentVisualReferentId")).toString().simplified();
    const auto agent_selection_source =
        mask_object.value(QStringLiteral("agentSelectionSource")).toString().simplified();
    if (!agent_visual_referent_id.isEmpty()) {
      parsed_mask.insert(QStringLiteral("agentVisualReferentId"),
                         agent_visual_referent_id.left(80));
    }
    if (!agent_selection_source.isEmpty()) {
      parsed_mask.insert(QStringLiteral("agentSelectionSource"),
                         agent_selection_source.left(80));
    }
    parsed_mask.insert(
        QStringLiteral("toolRole"),
        normalized_mask_tool_role(
            mask_object.contains(QStringLiteral("toolRole"))
                ? mask_object.value(QStringLiteral("toolRole")).toString()
                : default_tool_role));
    parsed_mask.insert(
        QStringLiteral("maskSourceClass"),
        mask_object.value(QStringLiteral("maskSourceClass")).toString());
    parsed_mask.insert(
        QStringLiteral("maskCoordinateSpace"),
        mask_object.value(QStringLiteral("maskCoordinateSpace")).toString());
    parsed_mask.insert(
        QStringLiteral("sourceImageRevision"),
        mask_object.value(QStringLiteral("sourceImageRevision")).toString());
    parsed_mask.insert(
        QStringLiteral("retouchOrder"),
        parsed_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")
            ? std::max(1, mask_object.value(QStringLiteral("retouchOrder")).toInt(
                              static_cast<int>(parsed_mask_stack.size()) + 1))
            : 0);
    parsed_mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_object.value(QStringLiteral("operation")).toString()));
    parsed_mask.insert(
        QStringLiteral("enabled"),
        mask_object.contains(QStringLiteral("enabled"))
            ? mask_object.value(QStringLiteral("enabled")).toBool()
            : true);
    parsed_mask.insert(
        QStringLiteral("solo"),
        mask_object.contains(QStringLiteral("solo"))
            ? mask_object.value(QStringLiteral("solo")).toBool()
            : false);
    parsed_mask.insert(
        QStringLiteral("locked"),
        mask_object.contains(QStringLiteral("locked"))
            ? mask_object.value(QStringLiteral("locked")).toBool()
            : false);
    parsed_mask.insert(
        QStringLiteral("committed"),
        mask_object.contains(QStringLiteral("committed"))
            ? mask_object.value(QStringLiteral("committed")).toBool()
            : true);
    parsed_mask.insert(
        QStringLiteral("opacity"),
        qBound(0.0, mask_object.contains(QStringLiteral("opacity"))
                         ? mask_object.value(QStringLiteral("opacity")).toDouble()
                         : 1.0,
               1.0));
    parsed_mask.insert(
        QStringLiteral("density"),
        qBound(0.0, mask_object.contains(QStringLiteral("density"))
                         ? mask_object.value(QStringLiteral("density")).toDouble()
                         : 1.0,
               1.0));
    parsed_mask.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_object.value(QStringLiteral("blendMode")).toString()));
    parsed_mask.insert(
        QStringLiteral("maskRasterWidth"),
        std::max(0, geometry.value(QStringLiteral("maskRasterWidth")).toInt()));
    parsed_mask.insert(
        QStringLiteral("maskRasterHeight"),
        std::max(0, geometry.value(QStringLiteral("maskRasterHeight")).toInt()));
    parsed_mask.insert(
        QStringLiteral("alphaSamplingMode"),
        geometry.value(QStringLiteral("alphaSamplingMode")).toString());
    parsed_mask.insert(
        QStringLiteral("softnessMode"),
        geometry.value(QStringLiteral("softnessMode")).toString());
    parsed_mask.insert(QStringLiteral("exposure"), adjustments.value(QStringLiteral("exposure")).toDouble());
    parsed_mask.insert(QStringLiteral("contrast"), adjustments.value(QStringLiteral("contrast")).toDouble());
    parsed_mask.insert(QStringLiteral("highlights"), adjustments.value(QStringLiteral("highlights")).toDouble());
    parsed_mask.insert(QStringLiteral("shadows"), adjustments.value(QStringLiteral("shadows")).toDouble());
    parsed_mask.insert(QStringLiteral("whites"), adjustments.value(QStringLiteral("whites")).toDouble());
    parsed_mask.insert(QStringLiteral("blacks"), adjustments.value(QStringLiteral("blacks")).toDouble());
    parsed_mask.insert(
        QStringLiteral("temperature"),
        adjustments.value(QStringLiteral("temperature")).toDouble());
    parsed_mask.insert(QStringLiteral("tint"), adjustments.value(QStringLiteral("tint")).toDouble());
    parsed_mask.insert(QStringLiteral("vibrance"), adjustments.value(QStringLiteral("vibrance")).toDouble());
    parsed_mask.insert(QStringLiteral("saturation"), adjustments.value(QStringLiteral("saturation")).toDouble());
    parsed_mask.insert(QStringLiteral("hueShift"), adjustments.value(QStringLiteral("hueShift")).toDouble());
    parsed_mask.insert(
        QStringLiteral("saturationShift"),
        adjustments.value(QStringLiteral("saturationShift")).toDouble());
    parsed_mask.insert(
        QStringLiteral("colorizeHue"),
        adjustments.value(QStringLiteral("colorizeHue")).toDouble());
    parsed_mask.insert(
        QStringLiteral("colorizeStrength"),
        adjustments.value(QStringLiteral("colorizeStrength")).toDouble());
    parsed_mask.insert(
        QStringLiteral("removeStrength"),
        adjustments.value(QStringLiteral("removeStrength")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionInvert"),
        selection.contains(QStringLiteral("invert"))
            ? selection.value(QStringLiteral("invert")).toBool()
            : (mask_object.contains(QStringLiteral("invert"))
                   ? mask_object.value(QStringLiteral("invert")).toBool()
                   : adjustments.value(QStringLiteral("selectionInvert")).toBool()));
    parsed_mask.insert(
        QStringLiteral("invert"),
        parsed_mask.value(QStringLiteral("selectionInvert")).toBool());
    parsed_mask.insert(
        QStringLiteral("selectionExpand"),
        selection.contains(QStringLiteral("expand"))
            ? selection.value(QStringLiteral("expand")).toDouble()
            : adjustments.value(QStringLiteral("selectionExpand")).toDouble());
    parsed_mask.insert(
        QStringLiteral("selectionSmooth"),
        selection.contains(QStringLiteral("smooth"))
            ? selection.value(QStringLiteral("smooth")).toDouble()
            : adjustments.value(QStringLiteral("selectionSmooth")).toDouble(matte_cleanup_defaults.smooth));
    parsed_mask.insert(
        QStringLiteral("selectionFineEdge"),
        selection.contains(QStringLiteral("fineEdge"))
            ? selection.value(QStringLiteral("fineEdge")).toDouble()
            : adjustments.value(QStringLiteral("selectionFineEdge")).toDouble(
                  matte_cleanup_defaults.fine_edge));
    parsed_mask.insert(
        QStringLiteral("selectionEdgeContrast"),
        adjustments.value(QStringLiteral("selectionEdgeContrast")).toDouble(matte_cleanup_defaults.edge_contrast));
    parsed_mask.insert(
        QStringLiteral("selectionDecontaminate"),
        adjustments.value(QStringLiteral("selectionDecontaminate")).toDouble(matte_cleanup_defaults.decontaminate));
    const auto parse_refine_percent = [&](const QString& adjustment_key, const QString& selection_key) {
      return selection.contains(selection_key) ? selection.value(selection_key).toDouble()
                                               : adjustments.value(adjustment_key).toDouble(
                                                     adjustment_key == QStringLiteral("selectionFeather") ? matte_cleanup_defaults.feather
                                                     : adjustment_key == QStringLiteral("selectionHarden") ? matte_cleanup_defaults.harden
                                                     : adjustment_key == QStringLiteral("selectionDespeckle") ? matte_cleanup_defaults.despeckle
                                                     : adjustment_key == QStringLiteral("selectionFillHoles") ? matte_cleanup_defaults.fill_holes
                                                     : adjustment_key == QStringLiteral("selectionRemoveIslands") ? matte_cleanup_defaults.remove_islands
                                                     : 0.0);
    };
    parsed_mask.insert(
        QStringLiteral("selectionGrow"),
        parse_refine_percent(QStringLiteral("selectionGrow"), QStringLiteral("grow")));
    parsed_mask.insert(
        QStringLiteral("selectionShrink"),
        parse_refine_percent(QStringLiteral("selectionShrink"), QStringLiteral("shrink")));
    parsed_mask.insert(
        QStringLiteral("selectionFeather"),
        parse_refine_percent(QStringLiteral("selectionFeather"), QStringLiteral("feather")));
    parsed_mask.insert(
        QStringLiteral("selectionBlur"),
        parse_refine_percent(QStringLiteral("selectionBlur"), QStringLiteral("blur")));
    parsed_mask.insert(
        QStringLiteral("selectionHarden"),
        parse_refine_percent(QStringLiteral("selectionHarden"), QStringLiteral("harden")));
    parsed_mask.insert(
        QStringLiteral("selectionDespeckle"),
        parse_refine_percent(QStringLiteral("selectionDespeckle"), QStringLiteral("despeckle")));
    parsed_mask.insert(
        QStringLiteral("selectionFillHoles"),
        parse_refine_percent(QStringLiteral("selectionFillHoles"), QStringLiteral("fillHoles")));
    parsed_mask.insert(
        QStringLiteral("selectionRemoveIslands"),
        parse_refine_percent(QStringLiteral("selectionRemoveIslands"), QStringLiteral("removeIslands")));
    parsed_mask.insert(
        QStringLiteral("refineLuminanceMin"),
        constraints.value(QStringLiteral("refineLuminanceMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineLuminanceMax"),
        constraints.value(QStringLiteral("refineLuminanceMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineHueCenter"),
        constraints.value(QStringLiteral("refineHueCenter")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineHueRange"),
        constraints.value(QStringLiteral("refineHueRange")).toDouble(0.5));
    parsed_mask.insert(
        QStringLiteral("refineSaturationMin"),
        constraints.value(QStringLiteral("refineSaturationMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineSaturationMax"),
        constraints.value(QStringLiteral("refineSaturationMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineDepthMin"),
        constraints.value(QStringLiteral("refineDepthMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineDepthMax"),
        constraints.value(QStringLiteral("refineDepthMax")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("refineEdgeConfidenceMin"),
        constraints.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("refineRangeFeather"),
        constraints.value(QStringLiteral("refineRangeFeather")).toDouble(0.05));
    parsed_mask.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        constraints.value(QStringLiteral("generatedMaskHoleFillThreshold"))
            .toDouble(matte_cleanup_defaults.generated_hole_threshold));
    parsed_mask.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        constraints.value(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            .toDouble(matte_cleanup_defaults.generated_island_threshold));
    parsed_mask.insert(
        QStringLiteral("matteCleanupSchemaVersion"),
        std::max(
            1,
            constraints.contains(QStringLiteral("matteCleanupSchemaVersion"))
                ? constraints.value(QStringLiteral("matteCleanupSchemaVersion")).toInt(1)
                : geometry.value(QStringLiteral("matteCleanupSchemaVersion")).toInt(1)));
    parsed_mask.insert(
        QStringLiteral("matteCleanupProfile"),
        (constraints.contains(QStringLiteral("matteCleanupProfile"))
             ? constraints.value(QStringLiteral("matteCleanupProfile")).toString()
             : geometry.value(QStringLiteral("matteCleanupProfile"))
                   .toString(QString::fromLatin1(matte_cleanup_defaults.profile)))
            .simplified());
    parsed_mask.insert(
        QStringLiteral("matteCleanupDefaultsApplied"),
        constraints.contains(QStringLiteral("matteCleanupDefaultsApplied"))
            ? constraints.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool(true)
            : geometry.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool(true));
    if (parsed_mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("selection")) {
      parsed_mask.insert(QStringLiteral("maskSetId"), mask_set_id);
      parsed_mask.insert(QStringLiteral("maskSetName"), mask_set_name);
    }
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionInvert"),
        adjustments.contains(QStringLiteral("maskSetSelectionInvert"))
            ? adjustments.value(QStringLiteral("maskSetSelectionInvert")).toBool()
            : mask_object.value(QStringLiteral("maskSetSelectionInvert")).toBool(false));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionExpand"),
        adjustments.contains(QStringLiteral("maskSetSelectionExpand"))
            ? adjustments.value(QStringLiteral("maskSetSelectionExpand")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionExpand")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionSmooth"),
        adjustments.contains(QStringLiteral("maskSetSelectionSmooth"))
            ? adjustments.value(QStringLiteral("maskSetSelectionSmooth")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionSmooth")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionFineEdge"),
        adjustments.contains(QStringLiteral("maskSetSelectionFineEdge"))
            ? adjustments.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetSelectionEdgeContrast"),
        adjustments.contains(QStringLiteral("maskSetSelectionEdgeContrast"))
            ? adjustments.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble()
            : mask_object.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble(0.0));
    parsed_mask.insert(
        QStringLiteral("maskSetDensity"),
        adjustments.contains(QStringLiteral("maskSetDensity"))
            ? adjustments.value(QStringLiteral("maskSetDensity")).toDouble(1.0)
            : mask_object.value(QStringLiteral("maskSetDensity")).toDouble(1.0));
    parsed_mask.insert(
        QStringLiteral("maskSetOpacity"),
        adjustments.contains(QStringLiteral("maskSetOpacity"))
            ? adjustments.value(QStringLiteral("maskSetOpacity")).toDouble(1.0)
            : mask_object.value(QStringLiteral("maskSetOpacity")).toDouble(1.0));

    if (kind == QStringLiteral("linear")) {
      state.linear_mask_enabled =
          mask_object.contains(QStringLiteral("enabled"))
              ? mask_object.value(QStringLiteral("enabled")).toBool()
              : true;
      state.linear_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
      state.linear_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
      state.linear_mask_feather = geometry.value(QStringLiteral("feather")).toDouble(0.22);
      state.linear_mask_position = geometry.value(QStringLiteral("position")).toDouble(0.5);
      state.linear_mask_rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      parsed_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      parsed_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      parsed_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, linear_mask_ordinal));
      ++linear_mask_ordinal;
      return;
    }

    if (is_center_radius_mask_kind(kind)) {
      const auto default_feather = kind == QStringLiteral("radial") ? 0.25 : 0.08;
      const auto default_radius_x = kind == QStringLiteral("rectangle") ? 0.30 : 0.25;
      const auto default_radius_y = kind == QStringLiteral("rectangle") ? 0.20 : 0.25;
      const auto feather = geometry.value(QStringLiteral("feather")).toDouble(default_feather);
      const auto center_x = geometry.value(QStringLiteral("centerX")).toDouble(0.5);
      const auto center_y = geometry.value(QStringLiteral("centerY")).toDouble(0.5);
      const auto radius_x = geometry.value(QStringLiteral("radiusX")).toDouble(default_radius_x);
      const auto radius_y = geometry.value(QStringLiteral("radiusY")).toDouble(default_radius_y);
      const auto rotation = geometry.value(QStringLiteral("rotation")).toDouble();
      if (kind == QStringLiteral("radial")) {
        state.radial_mask_enabled =
            mask_object.contains(QStringLiteral("enabled"))
                ? mask_object.value(QStringLiteral("enabled")).toBool()
                : true;
        state.radial_mask_exposure = adjustments.value(QStringLiteral("exposure")).toDouble();
        state.radial_mask_temperature = adjustments.value(QStringLiteral("temperature")).toDouble();
        state.radial_mask_feather = feather;
        state.radial_mask_center_x = center_x;
        state.radial_mask_center_y = center_y;
        state.radial_mask_radius_x = radius_x;
        state.radial_mask_radius_y = radius_y;
        state.radial_mask_rotation = rotation;
      }
      parsed_mask.insert(QStringLiteral("feather"), feather);
      parsed_mask.insert(QStringLiteral("centerX"), center_x);
      parsed_mask.insert(QStringLiteral("centerY"), center_y);
      parsed_mask.insert(QStringLiteral("radiusX"), radius_x);
      parsed_mask.insert(QStringLiteral("radiusY"), radius_y);
      parsed_mask.insert(QStringLiteral("rotation"), rotation);
      parsed_mask_stack.append(normalized_mask_definition(parsed_mask, radial_mask_ordinal));
      ++radial_mask_ordinal;
      return;
    }

    if (kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
      const auto brush_preset = geometry.value(QStringLiteral("brushPreset")).toString().simplified();
      parsed_mask.insert(
          QStringLiteral("brushPreset"),
          brush_preset.isEmpty() ? QStringLiteral("Soft Round") : brush_preset);
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.75));
      parsed_mask.insert(
          QStringLiteral("brushSize"),
          geometry.value(QStringLiteral("brushSize")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("brushFlow"),
          geometry.value(QStringLiteral("brushFlow")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("brushHardness"),
          geometry.value(QStringLiteral("brushHardness")).toDouble(0.25));
      parsed_mask.insert(
          QStringLiteral("brushSpacing"),
          geometry.value(QStringLiteral("brushSpacing")).toDouble(0.75));
      parsed_mask.insert(
          QStringLiteral("brushSmoothing"),
          geometry.value(QStringLiteral("brushSmoothing")).toDouble(0.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureSize"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureSize")).toDouble(0.65), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureFlow"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureFlow")).toDouble(1.0), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushPressureOpacity"),
          qBound(0.0, geometry.value(QStringLiteral("brushPressureOpacity")).toDouble(0.0), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushBuildup"),
          geometry.contains(QStringLiteral("brushBuildup"))
              ? geometry.value(QStringLiteral("brushBuildup")).toBool()
              : true);
      parsed_mask.insert(
          QStringLiteral("brushEraseMode"),
          geometry.contains(QStringLiteral("brushEraseMode"))
              ? geometry.value(QStringLiteral("brushEraseMode")).toBool()
              : false);
      parsed_mask.insert(
          QStringLiteral("brushAutoMask"),
          geometry.contains(QStringLiteral("brushAutoMask"))
              ? geometry.value(QStringLiteral("brushAutoMask")).toBool()
              : false);
      parsed_mask.insert(
          QStringLiteral("brushAutoMaskTolerance"),
          geometry.value(QStringLiteral("brushAutoMaskTolerance")).toDouble(0.18));
      parsed_mask.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          geometry.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("brushProtectEdges"),
          geometry.value(QStringLiteral("brushProtectEdges")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("healTextureBlend"),
          qBound(0.0, geometry.value(QStringLiteral("healTextureBlend")).toDouble(0.68), 1.0));
      parsed_mask.insert(
          QStringLiteral("healColorBlend"),
          qBound(0.0, geometry.value(QStringLiteral("healColorBlend")).toDouble(0.46), 1.0));
      parsed_mask.insert(
          QStringLiteral("brushRefineMode"),
          geometry.value(QStringLiteral("brushRefineMode")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("refineBrushRecalculate"),
          geometry.value(QStringLiteral("refineBrushRecalculate")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetId"),
          geometry.value(QStringLiteral("brushRefineTargetId")).toString());
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetName"),
          geometry.value(QStringLiteral("brushRefineTargetName")).toString());
      parsed_mask.insert(
          QStringLiteral("brushRefineTargetKind"),
          geometry.value(QStringLiteral("brushRefineTargetKind")).toString());
      parsed_mask.insert(
          QStringLiteral("cloneSourceSet"),
          geometry.value(QStringLiteral("cloneSourceSet")).toBool());
      parsed_mask.insert(
          QStringLiteral("cloneSourceX"),
          geometry.value(QStringLiteral("cloneSourceX")).toDouble(0.5));
      parsed_mask.insert(
          QStringLiteral("cloneSourceY"),
          geometry.value(QStringLiteral("cloneSourceY")).toDouble(0.5));
      parsed_mask.insert(
          QStringLiteral("cloneOffsetX"),
          geometry.value(QStringLiteral("cloneOffsetX")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneOffsetY"),
          geometry.value(QStringLiteral("cloneOffsetY")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneScale"),
          geometry.value(QStringLiteral("cloneScale")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("cloneRotation"),
          geometry.value(QStringLiteral("cloneRotation")).toDouble());
      parsed_mask.insert(
          QStringLiteral("cloneAligned"),
          geometry.contains(QStringLiteral("cloneAligned"))
              ? geometry.value(QStringLiteral("cloneAligned")).toBool(true)
              : true);
      QVariantList clone_sources;
      const auto clone_source_array = geometry.value(QStringLiteral("cloneSources")).toArray();
      for (const auto& source_value : clone_source_array) {
        if (!source_value.isObject()) {
          continue;
        }
        const auto source_object = source_value.toObject();
        QVariantMap clone_source;
        clone_source.insert(QStringLiteral("name"), source_object.value(QStringLiteral("name")).toString());
        clone_source.insert(QStringLiteral("x"), source_object.value(QStringLiteral("x")).toDouble(0.5));
        clone_source.insert(QStringLiteral("y"), source_object.value(QStringLiteral("y")).toDouble(0.5));
        clone_source.insert(QStringLiteral("scale"), source_object.value(QStringLiteral("scale")).toDouble(1.0));
        clone_source.insert(QStringLiteral("rotation"), source_object.value(QStringLiteral("rotation")).toDouble());
        clone_source.insert(
            QStringLiteral("aligned"),
            source_object.contains(QStringLiteral("aligned"))
                ? source_object.value(QStringLiteral("aligned")).toBool(true)
                : true);
        clone_sources.append(clone_source);
      }
      parsed_mask.insert(QStringLiteral("cloneSources"), clone_sources);
      const auto clone_source_index =
          geometry.value(QStringLiteral("cloneSourceIndex")).toInt(-1);
      parsed_mask.insert(
          QStringLiteral("cloneSourceIndex"),
          clone_source_index >= 0 && clone_source_index < clone_sources.size()
              ? clone_source_index
              : -1);
      const auto patch_source_width =
          std::clamp(geometry.value(QStringLiteral("patchSourceWidth")).toDouble(0.18), 0.02, 1.0);
      const auto patch_source_height =
          std::clamp(geometry.value(QStringLiteral("patchSourceHeight")).toDouble(0.18), 0.02, 1.0);
      parsed_mask.insert(
          QStringLiteral("patchSourceSet"),
          geometry.value(QStringLiteral("patchSourceSet")).toBool(false));
      parsed_mask.insert(
          QStringLiteral("patchSourceX"),
          std::clamp(geometry.value(QStringLiteral("patchSourceX")).toDouble(0.5), 0.0, 1.0 - patch_source_width));
      parsed_mask.insert(
          QStringLiteral("patchSourceY"),
          std::clamp(geometry.value(QStringLiteral("patchSourceY")).toDouble(0.5), 0.0, 1.0 - patch_source_height));
      parsed_mask.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
      parsed_mask.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
      QVariantList brush_points;
      const auto strokes = geometry.value(QStringLiteral("brushPoints")).toArray();
      for (const auto& stroke_value : strokes) {
        if (!stroke_value.isObject()) {
          continue;
        }
        const auto stroke = stroke_value.toObject();
        QVariantMap point;
        point.insert(QStringLiteral("x"), stroke.value(QStringLiteral("x")).toDouble());
        point.insert(QStringLiteral("y"), stroke.value(QStringLiteral("y")).toDouble());
        point.insert(QStringLiteral("size"), stroke.value(QStringLiteral("size")).toDouble(0.08));
        point.insert(QStringLiteral("flow"), stroke.value(QStringLiteral("flow")).toDouble(1.0));
        point.insert(QStringLiteral("opacity"), stroke.value(QStringLiteral("opacity")).toDouble(1.0));
        point.insert(
            QStringLiteral("autoMaskAnchorX"),
            stroke.value(QStringLiteral("autoMaskAnchorX")).toDouble(
                stroke.value(QStringLiteral("x")).toDouble()));
        point.insert(
            QStringLiteral("autoMaskAnchorY"),
            stroke.value(QStringLiteral("autoMaskAnchorY")).toDouble(
                stroke.value(QStringLiteral("y")).toDouble()));
        point.insert(QStringLiteral("pressure"), stroke.value(QStringLiteral("pressure")).toDouble(1.0));
        point.insert(QStringLiteral("tiltX"), stroke.value(QStringLiteral("tiltX")).toDouble());
        point.insert(QStringLiteral("tiltY"), stroke.value(QStringLiteral("tiltY")).toDouble());
        point.insert(QStringLiteral("erase"), stroke.value(QStringLiteral("erase")).toBool(false));
        brush_points.append(point);
      }
      parsed_mask.insert(QStringLiteral("brushPoints"), brush_points);
      QVariantList brush_strokes;
      const auto stroke_array = geometry.value(QStringLiteral("brushStrokes")).toArray();
      for (const auto& stroke_value : stroke_array) {
        if (!stroke_value.isObject()) {
          continue;
        }
        const auto stroke_object = stroke_value.toObject();
        QVariantList samples;
        for (const auto& sample_value : stroke_object.value(QStringLiteral("samples")).toArray()) {
          if (!sample_value.isObject()) {
            continue;
          }
          const auto sample = sample_value.toObject();
          QVariantMap point;
          point.insert(QStringLiteral("x"), sample.value(QStringLiteral("x")).toDouble());
          point.insert(QStringLiteral("y"), sample.value(QStringLiteral("y")).toDouble());
          point.insert(QStringLiteral("size"), sample.value(QStringLiteral("size")).toDouble(0.08));
          point.insert(QStringLiteral("flow"), sample.value(QStringLiteral("flow")).toDouble(1.0));
          point.insert(QStringLiteral("opacity"), sample.value(QStringLiteral("opacity")).toDouble(1.0));
          point.insert(
              QStringLiteral("autoMaskAnchorX"),
              sample.value(QStringLiteral("autoMaskAnchorX")).toDouble(
                  sample.value(QStringLiteral("x")).toDouble()));
          point.insert(
              QStringLiteral("autoMaskAnchorY"),
              sample.value(QStringLiteral("autoMaskAnchorY")).toDouble(
                  sample.value(QStringLiteral("y")).toDouble()));
          point.insert(QStringLiteral("pressure"), sample.value(QStringLiteral("pressure")).toDouble(1.0));
          point.insert(QStringLiteral("tiltX"), sample.value(QStringLiteral("tiltX")).toDouble());
          point.insert(QStringLiteral("tiltY"), sample.value(QStringLiteral("tiltY")).toDouble());
          point.insert(QStringLiteral("erase"), sample.value(QStringLiteral("erase")).toBool(false));
          samples.append(point);
        }
        QVariantMap stroke;
        stroke.insert(QStringLiteral("id"), stroke_object.value(QStringLiteral("id")).toString());
        stroke.insert(
            QStringLiteral("createdAtOrder"),
            stroke_object.value(QStringLiteral("createdAtOrder"))
                .toInt(static_cast<int>(brush_strokes.size()) + 1));
        stroke.insert(QStringLiteral("operation"), stroke_object.value(QStringLiteral("operation")).toString());
        stroke.insert(QStringLiteral("settings"), stroke_object.value(QStringLiteral("settings")).toObject().toVariantMap());
        stroke.insert(QStringLiteral("samples"), samples);
        brush_strokes.append(stroke);
      }
      parsed_mask.insert(QStringLiteral("brushStrokes"), brush_strokes);
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        parsed_mask.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, geometry.value(QStringLiteral("rasterSchemaVersion")).toInt(1)));
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
      read_generated_mask_metadata(&parsed_mask, geometry);
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("polygon") || kind == QStringLiteral("lasso")) {
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.06));
      QVariantList polygon_points;
      const auto points = geometry.value(QStringLiteral("polygonPoints")).toArray();
      for (const auto& point_value : points) {
        if (!point_value.isObject()) {
          continue;
        }
        const auto point_object = point_value.toObject();
        QVariantMap point;
        point.insert(QStringLiteral("x"), point_object.value(QStringLiteral("x")).toDouble());
        point.insert(QStringLiteral("y"), point_object.value(QStringLiteral("y")).toDouble());
        polygon_points.append(point);
      }
      parsed_mask.insert(QStringLiteral("polygonPoints"), polygon_points);
      if (kind == QStringLiteral("lasso")) {
        parsed_mask.insert(QStringLiteral("lassoClosed"), geometry.value(QStringLiteral("lassoClosed")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("lassoSimplified"),
            geometry.value(QStringLiteral("lassoSimplified")).toBool(false));
        parsed_mask.insert(QStringLiteral("lassoSmoothed"), geometry.value(QStringLiteral("lassoSmoothed")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("lassoFreehandPath"),
            geometry.value(QStringLiteral("lassoFreehandPath")).toBool(true));
        parsed_mask.insert(
            QStringLiteral("lassoRawPointCount"),
            std::max(0, geometry.value(QStringLiteral("lassoRawPointCount")).toInt(0)));
        parsed_mask.insert(
            QStringLiteral("lassoSimplifiedPointCount"),
            std::max(
                0,
                geometry.contains(QStringLiteral("lassoSimplifiedPointCount"))
                    ? geometry.value(QStringLiteral("lassoSimplifiedPointCount")).toInt()
                    : static_cast<int>(polygon_points.size())));
        QVariantList lasso_raw_points;
        const auto raw_points = geometry.value(QStringLiteral("lassoRawPoints")).toArray();
        for (const auto& point_value : raw_points) {
          if (!point_value.isObject()) {
            continue;
          }
          const auto point_object = point_value.toObject();
          QVariantMap point;
          point.insert(QStringLiteral("x"), point_object.value(QStringLiteral("x")).toDouble());
          point.insert(QStringLiteral("y"), point_object.value(QStringLiteral("y")).toDouble());
          lasso_raw_points.append(point);
        }
        parsed_mask.insert(QStringLiteral("lassoRawPoints"), lasso_raw_points);
      }
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("depth")) {
      parsed_mask.insert(
          QStringLiteral("depthMin"),
          constraints.value(QStringLiteral("depthMin")).toDouble(0.2));
      parsed_mask.insert(
          QStringLiteral("depthMax"),
          constraints.value(QStringLiteral("depthMax")).toDouble(0.8));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("depthSamples"),
          constraints.value(QStringLiteral("depthSamples")).toArray().toVariantList());
      const auto depth_data = detail::read_byte_array(geometry, QStringLiteral("depthData"));
      const auto depth_width = geometry.value(QStringLiteral("depthWidth")).toInt();
      const auto depth_height = geometry.value(QStringLiteral("depthHeight")).toInt();
      if (valid_segmentation_payload(depth_data, depth_width, depth_height)) {
        parsed_mask.insert(QStringLiteral("depthData"), depth_data);
        parsed_mask.insert(QStringLiteral("depthWidth"), depth_width);
        parsed_mask.insert(QStringLiteral("depthHeight"), depth_height);
      }
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("luminance")) {
      parsed_mask.insert(
          QStringLiteral("luminanceMin"),
          constraints.value(QStringLiteral("luminanceMin")).toDouble(0.2));
      parsed_mask.insert(
          QStringLiteral("luminanceMax"),
          constraints.value(QStringLiteral("luminanceMax")).toDouble(0.8));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("luminanceSamples"),
          constraints.value(QStringLiteral("luminanceSamples")).toArray().toVariantList());
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (kind == QStringLiteral("color")) {
      parsed_mask.insert(
          QStringLiteral("hueCenter"),
          constraints.value(QStringLiteral("hueCenter")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("hueRange"),
          constraints.value(QStringLiteral("hueRange")).toDouble(0.12));
      parsed_mask.insert(
          QStringLiteral("saturationMin"),
          constraints.value(QStringLiteral("saturationMin")).toDouble(0.1));
      parsed_mask.insert(
          QStringLiteral("saturationMax"),
          constraints.value(QStringLiteral("saturationMax")).toDouble(1.0));
      parsed_mask.insert(
          QStringLiteral("rangeFeather"),
          constraints.value(QStringLiteral("rangeFeather")).toDouble(0.08));
      parsed_mask.insert(
          QStringLiteral("colorSamples"),
          constraints.value(QStringLiteral("colorSamples")).toArray().toVariantList());
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
      return;
    }

    if (is_raster_mask_kind(kind) || kind == QStringLiteral("brush")) {
      parsed_mask.insert(
          QStringLiteral("retouchMode"),
          geometry.value(QStringLiteral("retouchMode")).toString());
      parsed_mask.insert(
          QStringLiteral("feather"),
          geometry.value(QStringLiteral("feather")).toDouble(0.5));
      if (kind == QStringLiteral("alpha")) {
        parsed_mask.insert(
            QStringLiteral("alphaSourcePath"),
            geometry.value(QStringLiteral("alphaSourcePath")).toString());
        parsed_mask.insert(
            QStringLiteral("alphaImportInvert"),
            geometry.value(QStringLiteral("alphaImportInvert")).toBool(false));
        parsed_mask.insert(
            QStringLiteral("alphaImportThreshold"),
            geometry.value(QStringLiteral("alphaImportThreshold")).toDouble(0.0));
        parsed_mask.insert(
            QStringLiteral("alphaImportResampleMode"),
            geometry.value(QStringLiteral("alphaImportResampleMode")).toString(QStringLiteral("Original")));
      }
      const auto segmentation_data =
          detail::read_byte_array(geometry, QStringLiteral("segmentationData"));
      const auto segmentation_width = geometry.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = geometry.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        parsed_mask.insert(QStringLiteral("segmentationData"), segmentation_data);
        parsed_mask.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        parsed_mask.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        parsed_mask.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, geometry.value(QStringLiteral("rasterSchemaVersion")).toInt(1)));
      }
      read_inpaint_patch_payload(&parsed_mask, geometry);
      read_generated_mask_metadata(&parsed_mask, geometry);
      parsed_mask_stack.append(
          normalized_mask_definition(parsed_mask, static_cast<int>(parsed_mask_stack.size()) + 1));
    }
  };

  const auto mask_sets = object.value(QStringLiteral("maskSets")).toArray();
  for (const auto& set_value : mask_sets) {
    if (!set_value.isObject()) {
      continue;
    }

    const auto mask_set = set_value.toObject();
    const auto set_adjustments = detail::object_value(mask_set, QStringLiteral("adjustments"));
    const auto set_id = mask_set.value(QStringLiteral("id")).toString();
    const auto set_name =
        mask_set.value(QStringLiteral("name")).toString(
            default_mask_set_name(QStringLiteral("linear"), static_cast<int>(parsed_mask_stack.size()) + 1));
    const auto sources = mask_set.value(QStringLiteral("sources")).toArray();
    for (const auto& source_value : sources) {
      if (!source_value.isObject()) {
        continue;
      }
      const auto source_object = source_value.toObject();
      auto source_adjustments = set_adjustments;
      const auto adjustment_overrides =
          detail::object_value(source_object, QStringLiteral("adjustmentOverrides"));
      for (auto iterator = adjustment_overrides.constBegin();
           iterator != adjustment_overrides.constEnd();
           ++iterator) {
        source_adjustments.insert(iterator.key(), iterator.value());
      }
      append_parsed_mask(
          source_object,
          source_adjustments,
          QStringLiteral("selection"),
          set_id,
          set_name);
    }
  }

  const auto retouch_stack = object.value(QStringLiteral("retouchStack")).toArray();
  for (const auto& value : retouch_stack) {
    if (!value.isObject()) {
      continue;
    }
    append_parsed_mask(
        value.toObject(),
        QJsonObject{},
        QStringLiteral("retouch"),
        QString(),
        QString());
  }

  const auto legacy_mask_stack = object.value(QStringLiteral("maskStack")).toArray();
  if (parsed_mask_stack.isEmpty()) {
    for (const auto& value : legacy_mask_stack) {
      if (!value.isObject()) {
        continue;
      }
      append_parsed_mask(
          value.toObject(),
          QJsonObject{},
          QStringLiteral("selection"),
          QString(),
          QString());
    }
  }

  if (parsed_mask_stack.isEmpty()) {
    state.linear_mask_enabled =
        object.value(QStringLiteral("linearMaskEnabled")).toBool();
    state.linear_mask_exposure =
        object.value(QStringLiteral("linearMaskExposure")).toDouble();
    state.linear_mask_temperature =
        object.value(QStringLiteral("linearMaskTemperature")).toDouble();
    state.linear_mask_feather =
        object.value(QStringLiteral("linearMaskFeather")).toDouble(0.22);
    state.linear_mask_position =
        object.value(QStringLiteral("linearMaskPosition")).toDouble(0.5);
    state.linear_mask_rotation =
        object.value(QStringLiteral("linearMaskRotation")).toDouble();

    if (has_structured_linear_mask(state)) {
      QVariantMap linear_mask = default_mask_definition(QStringLiteral("linear"), 1);
      linear_mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
      linear_mask.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
      linear_mask.insert(QStringLiteral("temperature"), state.linear_mask_temperature);
      linear_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      linear_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      linear_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      parsed_mask_stack.append(linear_mask);
    }

    state.radial_mask_enabled = object.value(QStringLiteral("radialMaskEnabled")).toBool();
    state.radial_mask_exposure = object.value(QStringLiteral("radialMaskExposure")).toDouble();
    state.radial_mask_temperature = object.value(QStringLiteral("radialMaskTemperature")).toDouble();
    state.radial_mask_feather = object.value(QStringLiteral("radialMaskFeather")).toDouble(0.25);
    state.radial_mask_center_x = object.value(QStringLiteral("radialMaskCenterX")).toDouble(0.5);
    state.radial_mask_center_y = object.value(QStringLiteral("radialMaskCenterY")).toDouble(0.5);
    state.radial_mask_radius_x = object.value(QStringLiteral("radialMaskRadiusX")).toDouble(0.25);
    state.radial_mask_radius_y = object.value(QStringLiteral("radialMaskRadiusY")).toDouble(0.25);
    state.radial_mask_rotation = object.value(QStringLiteral("radialMaskRotation")).toDouble();

    if (has_structured_radial_mask(state)) {
      QVariantMap radial_mask = default_mask_definition(QStringLiteral("radial"), 1);
      radial_mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
      radial_mask.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
      radial_mask.insert(QStringLiteral("temperature"), state.radial_mask_temperature);
      radial_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      radial_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      radial_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      radial_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      radial_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      radial_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      parsed_mask_stack.append(radial_mask);
    }
  }

  state.mask_stack = normalize_mask_stack(parsed_mask_stack);

  return state;
}

}  // namespace

EditRecipeState parse_edit_adjustment_state_object(
    const QJsonObject& adjustment_state) {
  if (adjustment_state.contains(QStringLiteral("recipeVersion"))) {
    return {};
  }
  return parse_edit_adjustment_state_object_versioned(
      adjustment_state, kCurrentEditRecipeVersion);
}

EditRecipeState parse_edit_recipe_json(const QString& recipe_json) {
  if (recipe_json.trimmed().isEmpty()) {
    return {};
  }

  const auto document = QJsonDocument::fromJson(recipe_json.toUtf8());
  if (!document.isObject()) {
    return {};
  }

  const auto object = document.object();
  return parse_edit_adjustment_state_object_versioned(
      object,
      object.value(QStringLiteral("recipeVersion")).toInt(1));
}

// This serializer is intentionally exhaustive and contains hundreds of short-lived Qt JSON
// temporaries. Keep its largest collection builder in a distinct frame: Clang's unoptimized
// layout can otherwise exceed the 64 KiB event-loop stack segment on macOS debug builds.
#if defined(__clang__) || defined(__GNUC__)
#define PHOTO_EDITOR_RECIPE_NOINLINE __attribute__((noinline))
#else
#define PHOTO_EDITOR_RECIPE_NOINLINE
#endif
QJsonObject build_edit_adjustment_state_object(
    const EditRecipeState& state) {
  const auto primary_sections = [&state]() PHOTO_EDITOR_RECIPE_NOINLINE {
  QJsonObject raw_adjustments;
  raw_adjustments.insert(QStringLiteral("inputTransformEnabled"), state.raw_input_transform_enabled);
  raw_adjustments.insert(
      QStringLiteral("highlightReconstructionMode"),
      normalized_highlight_reconstruction_mode_name(state.highlight_reconstruction_mode));
  raw_adjustments.insert(
      QStringLiteral("highlightReconstructionModel"),
      normalized_highlight_reconstruction_model_name(
          state.highlight_reconstruction_model));
  raw_adjustments.insert(
      QStringLiteral("chromaticAdaptationMethod"),
      normalized_chromatic_adaptation_method_name(state.chromatic_adaptation_method));
  raw_adjustments.insert(
      QStringLiteral("demosaicQuality"),
      normalized_raw_demosaic_quality_name(state.raw_demosaic_quality));
  raw_adjustments.insert(
      QStringLiteral("inputProfile"),
      normalized_input_color_profile_name(state.input_color_profile));
  raw_adjustments.insert(QStringLiteral("exposure"), state.raw_exposure);
  raw_adjustments.insert(QStringLiteral("boost"), state.raw_boost);
  raw_adjustments.insert(QStringLiteral("shadowBoost"), state.raw_shadow_boost);
  raw_adjustments.insert(QStringLiteral("detail"), state.raw_detail);
  raw_adjustments.insert(QStringLiteral("chromaNoiseReduction"), state.raw_chroma_noise_reduction);
  raw_adjustments.insert(QStringLiteral("moireReduction"), state.raw_moire_reduction);
  raw_adjustments.insert(QStringLiteral("hotDeadPixelCleanup"), state.raw_hot_dead_pixel_cleanup);
  raw_adjustments.insert(QStringLiteral("falseColorSuppression"), state.raw_false_color_suppression);
  raw_adjustments.insert(QStringLiteral("highlightRecovery"), state.raw_highlight_recovery);
  raw_adjustments.insert(QStringLiteral("gamutMapping"), state.raw_gamut_mapping);
  raw_adjustments.insert(QStringLiteral("lensCorrection"), state.raw_lens_correction);
  raw_adjustments.insert(QStringLiteral("whiteBalanceOverride"), state.raw_white_balance_override);
  raw_adjustments.insert(
      QStringLiteral("cameraMatchingLook"),
      normalized_camera_matching_look_name(state.raw_camera_matching_look));
  raw_adjustments.insert(
      QStringLiteral("whiteBalanceTemperature"), state.raw_white_balance_temperature);
  raw_adjustments.insert(QStringLiteral("whiteBalanceTint"), state.raw_white_balance_tint);
  raw_adjustments.insert(QStringLiteral("blackLevel"), state.raw_black_level);
  raw_adjustments.insert(QStringLiteral("whiteLevel"), state.raw_white_level);
  raw_adjustments.insert(QStringLiteral("calibrationHue"), state.raw_calibration_hue);
  raw_adjustments.insert(QStringLiteral("calibrationSaturation"), state.raw_calibration_saturation);
  raw_adjustments.insert(QStringLiteral("redPrimaryHue"), state.raw_red_primary_hue);
  raw_adjustments.insert(QStringLiteral("redPrimarySaturation"), state.raw_red_primary_saturation);
  raw_adjustments.insert(QStringLiteral("greenPrimaryHue"), state.raw_green_primary_hue);
  raw_adjustments.insert(QStringLiteral("greenPrimarySaturation"), state.raw_green_primary_saturation);
  raw_adjustments.insert(QStringLiteral("bluePrimaryHue"), state.raw_blue_primary_hue);
  raw_adjustments.insert(QStringLiteral("bluePrimarySaturation"), state.raw_blue_primary_saturation);

  QJsonObject global_adjustments;
  global_adjustments.insert(QStringLiteral("exposure"), state.exposure);
  global_adjustments.insert(QStringLiteral("contrast"), state.contrast);
  global_adjustments.insert(QStringLiteral("highlights"), state.highlights);
  global_adjustments.insert(QStringLiteral("shadows"), state.shadows);
  global_adjustments.insert(QStringLiteral("whites"), state.whites);
  global_adjustments.insert(QStringLiteral("blacks"), state.blacks);
  global_adjustments.insert(QStringLiteral("brightness"), state.brightness);
  global_adjustments.insert(QStringLiteral("hdrLocalTone"), state.hdr_local_tone_mapping);
  global_adjustments.insert(QStringLiteral("filmicShoulder"), state.filmic_shoulder);
  global_adjustments.insert(QStringLiteral("filmicToe"), state.filmic_toe);
  global_adjustments.insert(QStringLiteral("adaptiveContrast"), state.adaptive_contrast);
  global_adjustments.insert(QStringLiteral("levelsBlackPoint"), state.levels_black_point);
  global_adjustments.insert(QStringLiteral("levelsGamma"), state.levels_gamma);
  global_adjustments.insert(QStringLiteral("levelsWhitePoint"), state.levels_white_point);
  global_adjustments.insert(QStringLiteral("zoneDeepShadows"), state.zone_deep_shadows);
  global_adjustments.insert(QStringLiteral("zoneShadows"), state.zone_shadows);
  global_adjustments.insert(QStringLiteral("zoneMidtones"), state.zone_midtones);
  global_adjustments.insert(QStringLiteral("zoneLights"), state.zone_lights);
  global_adjustments.insert(QStringLiteral("zoneHighlights"), state.zone_highlights);
  global_adjustments.insert(QStringLiteral("temperature"), state.temperature);
  global_adjustments.insert(QStringLiteral("tint"), state.tint);
  global_adjustments.insert(QStringLiteral("texture"), state.texture);
  global_adjustments.insert(QStringLiteral("clarity"), state.clarity);
  global_adjustments.insert(QStringLiteral("dehaze"), state.dehaze);
  global_adjustments.insert(QStringLiteral("sharpening"), state.sharpening);
  global_adjustments.insert(QStringLiteral("sharpenRadius"), state.sharpen_radius);
  global_adjustments.insert(QStringLiteral("sharpenDetail"), state.sharpen_detail);
  global_adjustments.insert(QStringLiteral("sharpenMasking"), state.sharpen_masking);
  global_adjustments.insert(QStringLiteral("luminanceNr"), state.luminance_nr);
  global_adjustments.insert(QStringLiteral("colorNr"), state.color_nr);
  global_adjustments.insert(QStringLiteral("vibrance"), state.vibrance);
  global_adjustments.insert(QStringLiteral("saturation"), state.saturation);

  QJsonObject optics;
  optics.insert(QStringLiteral("lensDistortion"), state.lens_distortion);
  optics.insert(QStringLiteral("lensVignette"), state.lens_vignette);
  optics.insert(QStringLiteral("chromaticAberration"), state.chromatic_aberration);

  return std::array<QJsonObject, 3>{
      raw_adjustments,
      global_adjustments,
      optics,
  };
  }();
  const auto& raw_adjustments = primary_sections[0];
  const auto& global_adjustments = primary_sections[1];
  const auto& optics = primary_sections[2];

  const auto creative_sections = [&state]() PHOTO_EDITOR_RECIPE_NOINLINE {
  QJsonObject parametric_curve;
  parametric_curve.insert(QStringLiteral("shadows"), state.curve_shadows);
  parametric_curve.insert(QStringLiteral("darks"), state.curve_darks);
  parametric_curve.insert(QStringLiteral("lights"), state.curve_lights);
  parametric_curve.insert(QStringLiteral("highlights"), state.curve_highlights);

  QJsonObject curves;
  curves.insert(QStringLiteral("mode"), QStringLiteral("parametric"));
  curves.insert(QStringLiteral("parametric"), parametric_curve);
  QJsonArray channels;
  const auto append_channel = [&channels](const QString& channel_name, const QVariantList& points) {
    if (points.isEmpty()) {
      return;
    }

    QJsonObject channel;
    channel.insert(QStringLiteral("channel"), channel_name.toLower());
    channel.insert(QStringLiteral("points"), serialize_curve_points(points));
    channels.append(channel);
  };
  append_channel(QStringLiteral("Master"), state.master_curve_points);
  append_channel(QStringLiteral("Luma"), state.luma_curve_points);
  append_channel(QStringLiteral("Red"), state.red_curve_points);
  append_channel(QStringLiteral("Green"), state.green_curve_points);
  append_channel(QStringLiteral("Blue"), state.blue_curve_points);
  curves.insert(QStringLiteral("channels"), channels);

  QJsonObject color_mixer;
  color_mixer.insert(QStringLiteral("model"), state.color_mixer_model);
  color_mixer.insert(QStringLiteral("channel"), state.hsl_channel);
  color_mixer.insert(QStringLiteral("hue"), state.hue_adjustment);
  color_mixer.insert(QStringLiteral("saturation"), state.saturation_adjustment);
  color_mixer.insert(QStringLiteral("luminance"), state.luminance_adjustment);
  {
    static const QStringList kMixerChannelNames = {
        QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
        QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
        QStringLiteral("Purples"), QStringLiteral("Magentas"),
    };
    QJsonObject mixer_channels_obj;
    for (int i = 0; i < 8; ++i) {
      const auto& ch = state.mixer_channels[static_cast<std::size_t>(i)];
      if (std::abs(ch.hue) > 1e-6 || std::abs(ch.saturation) > 1e-6 || std::abs(ch.luminance) > 1e-6 ||
          std::abs(ch.selective_cyan) > 1e-6 || std::abs(ch.selective_magenta) > 1e-6 ||
          std::abs(ch.selective_yellow) > 1e-6 || std::abs(ch.selective_black) > 1e-6) {
        QJsonObject ch_obj;
        ch_obj.insert(QStringLiteral("hue"), ch.hue);
        ch_obj.insert(QStringLiteral("saturation"), ch.saturation);
        ch_obj.insert(QStringLiteral("luminance"), ch.luminance);
        ch_obj.insert(QStringLiteral("selectiveCyan"), ch.selective_cyan);
        ch_obj.insert(QStringLiteral("selectiveMagenta"), ch.selective_magenta);
        ch_obj.insert(QStringLiteral("selectiveYellow"), ch.selective_yellow);
        ch_obj.insert(QStringLiteral("selectiveBlack"), ch.selective_black);
        mixer_channels_obj.insert(kMixerChannelNames[i], ch_obj);
      }
    }
    if (!mixer_channels_obj.isEmpty()) {
      color_mixer.insert(QStringLiteral("channels"), mixer_channels_obj);
    }
  }
  if (std::abs(state.rgb_channel_mixer.red_from_red - 100.0) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.red_from_green) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.red_from_blue) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_red) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_green - 100.0) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.green_from_blue) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_red) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_green) > 1e-6 ||
      std::abs(state.rgb_channel_mixer.blue_from_blue - 100.0) > 1e-6) {
    QJsonObject rgb_channel_mixer;
    rgb_channel_mixer.insert(QStringLiteral("redFromRed"), state.rgb_channel_mixer.red_from_red);
    rgb_channel_mixer.insert(QStringLiteral("redFromGreen"), state.rgb_channel_mixer.red_from_green);
    rgb_channel_mixer.insert(QStringLiteral("redFromBlue"), state.rgb_channel_mixer.red_from_blue);
    rgb_channel_mixer.insert(QStringLiteral("greenFromRed"), state.rgb_channel_mixer.green_from_red);
    rgb_channel_mixer.insert(QStringLiteral("greenFromGreen"), state.rgb_channel_mixer.green_from_green);
    rgb_channel_mixer.insert(QStringLiteral("greenFromBlue"), state.rgb_channel_mixer.green_from_blue);
    rgb_channel_mixer.insert(QStringLiteral("blueFromRed"), state.rgb_channel_mixer.blue_from_red);
    rgb_channel_mixer.insert(QStringLiteral("blueFromGreen"), state.rgb_channel_mixer.blue_from_green);
    rgb_channel_mixer.insert(QStringLiteral("blueFromBlue"), state.rgb_channel_mixer.blue_from_blue);
    color_mixer.insert(QStringLiteral("rgbChannelMixer"), rgb_channel_mixer);
  }
  if (state.black_and_white_enabled ||
      std::any_of(state.black_and_white_mixer.begin(), state.black_and_white_mixer.end(), [](double value) {
        return std::abs(value) > 1e-6;
      })) {
    static const QStringList kMixerChannelNames = {
        QStringLiteral("Reds"), QStringLiteral("Oranges"), QStringLiteral("Yellows"),
        QStringLiteral("Greens"), QStringLiteral("Aquas"), QStringLiteral("Blues"),
        QStringLiteral("Purples"), QStringLiteral("Magentas"),
    };
    QJsonObject channels;
    for (int i = 0; i < 8; ++i) {
      channels.insert(kMixerChannelNames[i], state.black_and_white_mixer[static_cast<std::size_t>(i)]);
    }
    QJsonObject black_and_white;
    black_and_white.insert(QStringLiteral("enabled"), state.black_and_white_enabled);
    black_and_white.insert(QStringLiteral("channels"), channels);
    color_mixer.insert(QStringLiteral("blackAndWhite"), black_and_white);
  }
  if (state.color_replacement_enabled ||
      std::abs(state.color_replacement_strength) > 1e-6 ||
      std::abs(state.color_replacement_source_hue) > 1e-6 ||
      std::abs(state.color_replacement_target_hue - 0.33) > 1e-6 ||
      std::abs(state.color_replacement_range - 35.0) > 1e-6 ||
      std::abs(state.color_replacement_softness - 100.0) > 1e-6 ||
      !state.color_replacement_preserve_luminance ||
      !state.color_replacement_preserve_saturation) {
    QJsonObject color_replacement;
    color_replacement.insert(QStringLiteral("enabled"), state.color_replacement_enabled);
    color_replacement.insert(QStringLiteral("sourceHue"), state.color_replacement_source_hue);
    color_replacement.insert(QStringLiteral("targetHue"), state.color_replacement_target_hue);
    color_replacement.insert(QStringLiteral("range"), state.color_replacement_range);
    color_replacement.insert(QStringLiteral("softness"), state.color_replacement_softness);
    color_replacement.insert(QStringLiteral("strength"), state.color_replacement_strength);
    color_replacement.insert(QStringLiteral("preserveLuminance"), state.color_replacement_preserve_luminance);
    color_replacement.insert(QStringLiteral("preserveSaturation"), state.color_replacement_preserve_saturation);
    color_mixer.insert(QStringLiteral("colorReplacement"), color_replacement);
  }
  if (state.skin_tone_protection_enabled ||
      std::abs(state.skin_tone_line_hue - 0.08) > 1e-6 ||
      std::abs(state.skin_tone_protection_range - 30.0) > 1e-6 ||
      std::abs(state.skin_tone_protection_strength) > 1e-6) {
    QJsonObject skin_tone;
    skin_tone.insert(QStringLiteral("enabled"), state.skin_tone_protection_enabled);
    skin_tone.insert(QStringLiteral("lineHue"), state.skin_tone_line_hue);
    skin_tone.insert(QStringLiteral("range"), state.skin_tone_protection_range);
    skin_tone.insert(QStringLiteral("strength"), state.skin_tone_protection_strength);
    color_mixer.insert(QStringLiteral("skinToneProtection"), skin_tone);
  }
  if (state.color_harmony_enabled ||
      state.color_harmony_mode != QStringLiteral("Complementary") ||
      std::abs(state.color_harmony_base_hue) > 1e-6 ||
      std::abs(state.color_harmony_strength) > 1e-6) {
    QJsonObject color_harmony;
    color_harmony.insert(QStringLiteral("enabled"), state.color_harmony_enabled);
    color_harmony.insert(QStringLiteral("mode"), state.color_harmony_mode);
    color_harmony.insert(QStringLiteral("baseHue"), state.color_harmony_base_hue);
    color_harmony.insert(QStringLiteral("strength"), state.color_harmony_strength);
    color_mixer.insert(QStringLiteral("colorHarmony"), color_harmony);
  }

  return std::array<QJsonObject, 2>{
      curves,
      color_mixer,
  };
  }();
  const auto& curves = creative_sections[0];
  const auto& color_mixer = creative_sections[1];

  const auto output_sections = [&state]() PHOTO_EDITOR_RECIPE_NOINLINE {
  QJsonObject grading_shadows;
  grading_shadows.insert(QStringLiteral("hue"), state.grading_shadows_hue);
  grading_shadows.insert(QStringLiteral("saturation"), state.grading_shadows_saturation);
  grading_shadows.insert(QStringLiteral("luminance"), state.grading_shadows_luminance);
  QJsonObject grading_midtones;
  grading_midtones.insert(QStringLiteral("hue"), state.grading_midtones_hue);
  grading_midtones.insert(QStringLiteral("saturation"), state.grading_midtones_saturation);
  grading_midtones.insert(QStringLiteral("luminance"), state.grading_midtones_luminance);
  QJsonObject grading_highlights;
  grading_highlights.insert(QStringLiteral("hue"), state.grading_highlights_hue);
  grading_highlights.insert(QStringLiteral("saturation"), state.grading_highlights_saturation);
  grading_highlights.insert(QStringLiteral("luminance"), state.grading_highlights_luminance);
  QJsonObject color_grading;
  color_grading.insert(QStringLiteral("shadows"), grading_shadows);
  color_grading.insert(QStringLiteral("midtones"), grading_midtones);
  color_grading.insert(QStringLiteral("highlights"), grading_highlights);
  color_grading.insert(QStringLiteral("amount"), state.grading_amount);
  color_grading.insert(QStringLiteral("balance"), state.grading_balance);

  QJsonObject crop_transform;
  crop_transform.insert(QStringLiteral("preset"), state.crop_preset);
  crop_transform.insert(QStringLiteral("zoom"), state.crop_zoom);
  crop_transform.insert(QStringLiteral("uprightMode"), normalized_upright_mode_name(state.upright_mode));
  crop_transform.insert(QStringLiteral("uprightVerticalGuideAngle"), state.upright_vertical_guide_angle);
  crop_transform.insert(QStringLiteral("uprightHorizontalGuideAngle"), state.upright_horizontal_guide_angle);
  crop_transform.insert(QStringLiteral("perspectiveVertical"), state.perspective_vertical);
  crop_transform.insert(QStringLiteral("perspectiveHorizontal"), state.perspective_horizontal);
  crop_transform.insert(QStringLiteral("rotation"), state.transform_rotation);
  crop_transform.insert(QStringLiteral("scale"), state.transform_scale);
  crop_transform.insert(QStringLiteral("skewX"), state.transform_skew_x);
  crop_transform.insert(QStringLiteral("skewY"), state.transform_skew_y);
  crop_transform.insert(QStringLiteral("liquifyCenterX"), state.liquify_center_x);
  crop_transform.insert(QStringLiteral("liquifyCenterY"), state.liquify_center_y);
  crop_transform.insert(QStringLiteral("liquifyRadius"), state.liquify_radius);
  crop_transform.insert(QStringLiteral("liquifyStrength"), state.liquify_strength);
  crop_transform.insert(QStringLiteral("contentAwareFill"), state.content_aware_crop_fill);
  if (state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0) {
    crop_transform.insert(QStringLiteral("x"), state.crop_rect_x);
    crop_transform.insert(QStringLiteral("y"), state.crop_rect_y);
    crop_transform.insert(QStringLiteral("width"), state.crop_rect_width);
    crop_transform.insert(QStringLiteral("height"), state.crop_rect_height);
  }

  QJsonObject output_transform;
  output_transform.insert(
      QStringLiteral("previewColorSpace"),
      normalized_output_color_space_name(state.preview_color_space));
  output_transform.insert(
      QStringLiteral("previewMode"),
      normalized_preview_output_mode_name(
          state.preview_output_mode));
  const auto monitor_profile = normalized_monitor_color_profile_name(state.monitor_color_profile);
  if (!monitor_profile.isEmpty()) {
    output_transform.insert(QStringLiteral("monitorProfile"), monitor_profile);
  }
  output_transform.insert(QStringLiteral("softProofEnabled"), state.soft_proof_enabled);
  output_transform.insert(
      QStringLiteral("softProofProfile"),
      normalized_output_color_space_name(state.soft_proof_profile));

  QJsonObject export_transform;
  export_transform.insert(
      QStringLiteral("colorSpace"),
      normalized_output_color_space_name(state.output_color_space));

  QJsonObject color_pipeline;
  color_pipeline.insert(
      QStringLiteral("inputProfile"),
      normalized_input_color_profile_name(state.input_color_profile));
  color_pipeline.insert(
      QStringLiteral("workingProfile"),
      normalized_working_color_profile_name(state.working_color_profile));
  color_pipeline.insert(
      QStringLiteral("previewOutputProfile"),
      normalized_output_color_space_name(state.preview_color_space));
  color_pipeline.insert(
      QStringLiteral("exportOutputProfile"),
      normalized_output_color_space_name(state.output_color_space));
  color_pipeline.insert(
      QStringLiteral("renderIntent"),
      normalized_render_intent_name(state.render_intent));
  color_pipeline.insert(
      QStringLiteral("proofing"),
      QJsonObject{
          {QStringLiteral("renderIntent"),
           normalized_render_intent_name(state.proof_render_intent)},
          {QStringLiteral("simulatePaperWhite"),
           state.proof_simulate_paper_white},
          {QStringLiteral("simulateInkBlack"),
           state.proof_simulate_ink_black},
      });
  color_pipeline.insert(QStringLiteral("headroom"), qBound(1.0, state.output_headroom, 16.0));
  color_pipeline.insert(QStringLiteral("gamutMappingEnabled"), state.output_gamut_mapping_enabled);
  color_pipeline.insert(
      QStringLiteral("gamutMappingPolicy"),
      normalized_gamut_mapping_policy_name(state.output_gamut_mapping_policy));
  color_pipeline.insert(
      QStringLiteral("blackPointCompensationEnabled"),
      state.black_point_compensation_enabled);
  color_pipeline.insert(QStringLiteral("sourceBlackPoint"), qBound(0.0, state.source_black_point, 16.0));
  color_pipeline.insert(QStringLiteral("outputBlackPoint"), qBound(0.0, state.output_black_point, 16.0));
  color_pipeline.insert(
      QStringLiteral("cameraProfileAsset"),
      serialize_dcp_profile_asset_state(state.camera_dcp_profile_asset));
  color_pipeline.insert(
      QStringLiteral("cameraProfileRenderingModel"),
      normalized_camera_profile_rendering_model_name(
          state.camera_profile_rendering_model));
  color_pipeline.insert(
      QStringLiteral("photographicRenderingModel"),
      normalized_photographic_rendering_model_name(
          state.photographic_rendering_model));
  color_pipeline.insert(
      QStringLiteral("rendererTransitionLook"),
      normalized_renderer_transition_look_name(
          state.renderer_transition_look));
  QJsonObject profile_assets;
  profile_assets.insert(
      QStringLiteral("input"),
      serialize_icc_profile_asset_state(state.input_icc_profile_asset));
  profile_assets.insert(
      QStringLiteral("preview"),
      serialize_icc_profile_asset_state(state.preview_icc_profile_asset));
  profile_assets.insert(
      QStringLiteral("proof"),
      serialize_icc_profile_asset_state(state.proof_icc_profile_asset));
  profile_assets.insert(
      QStringLiteral("export"),
      serialize_icc_profile_asset_state(state.export_icc_profile_asset));
  color_pipeline.insert(QStringLiteral("profileAssets"), profile_assets);

  return std::array<QJsonObject, 5>{
      color_grading,
      crop_transform,
      output_transform,
      export_transform,
      color_pipeline,
  };
  }();
  const auto& color_grading = output_sections[0];
  const auto& crop_transform = output_sections[1];
  const auto& output_transform = output_sections[2];
  const auto& export_transform = output_sections[3];
  const auto& color_pipeline = output_sections[4];

  const auto serialized_mask_collections = [&state]() PHOTO_EDITOR_RECIPE_NOINLINE {
  const auto serialize_mask_object = [](
      const QVariantMap& candidate,
      int ordinal) PHOTO_EDITOR_RECIPE_NOINLINE {
    const auto mask_definition = normalized_mask_definition(candidate, ordinal);
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());

    QJsonObject adjustments;
    adjustments.insert(QStringLiteral("exposure"), mask_definition.value(QStringLiteral("exposure")).toDouble());
    adjustments.insert(QStringLiteral("contrast"), mask_definition.value(QStringLiteral("contrast")).toDouble());
    adjustments.insert(QStringLiteral("highlights"), mask_definition.value(QStringLiteral("highlights")).toDouble());
    adjustments.insert(QStringLiteral("shadows"), mask_definition.value(QStringLiteral("shadows")).toDouble());
    adjustments.insert(QStringLiteral("whites"), mask_definition.value(QStringLiteral("whites")).toDouble());
    adjustments.insert(QStringLiteral("blacks"), mask_definition.value(QStringLiteral("blacks")).toDouble());
    adjustments.insert(
        QStringLiteral("temperature"),
        mask_definition.value(QStringLiteral("temperature")).toDouble());
    adjustments.insert(QStringLiteral("tint"), mask_definition.value(QStringLiteral("tint")).toDouble());
    adjustments.insert(QStringLiteral("vibrance"), mask_definition.value(QStringLiteral("vibrance")).toDouble());
    adjustments.insert(QStringLiteral("saturation"), mask_definition.value(QStringLiteral("saturation")).toDouble());
    adjustments.insert(QStringLiteral("hueShift"), mask_definition.value(QStringLiteral("hueShift")).toDouble());
    adjustments.insert(
        QStringLiteral("saturationShift"),
        mask_definition.value(QStringLiteral("saturationShift")).toDouble());
    adjustments.insert(QStringLiteral("colorizeHue"), mask_definition.value(QStringLiteral("colorizeHue")).toDouble());
    adjustments.insert(
        QStringLiteral("colorizeStrength"),
        mask_definition.value(QStringLiteral("colorizeStrength")).toDouble());
    adjustments.insert(
        QStringLiteral("removeStrength"),
        mask_definition.value(QStringLiteral("removeStrength")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionInvert"),
        mask_definition.value(QStringLiteral("selectionInvert")).toBool());
    adjustments.insert(
        QStringLiteral("selectionExpand"),
        mask_definition.value(QStringLiteral("selectionExpand")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionSmooth"),
        mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionFineEdge"),
        mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionEdgeContrast"),
        mask_definition.value(QStringLiteral("selectionEdgeContrast")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionDecontaminate"),
        mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());
    adjustments.insert(QStringLiteral("selectionGrow"), mask_definition.value(QStringLiteral("selectionGrow")).toDouble());
    adjustments.insert(QStringLiteral("selectionShrink"), mask_definition.value(QStringLiteral("selectionShrink")).toDouble());
    adjustments.insert(QStringLiteral("selectionFeather"), mask_definition.value(QStringLiteral("selectionFeather")).toDouble());
    adjustments.insert(QStringLiteral("selectionBlur"), mask_definition.value(QStringLiteral("selectionBlur")).toDouble());
    adjustments.insert(QStringLiteral("selectionHarden"), mask_definition.value(QStringLiteral("selectionHarden")).toDouble());
    adjustments.insert(QStringLiteral("selectionDespeckle"), mask_definition.value(QStringLiteral("selectionDespeckle")).toDouble());
    adjustments.insert(QStringLiteral("selectionFillHoles"), mask_definition.value(QStringLiteral("selectionFillHoles")).toDouble());
    adjustments.insert(
        QStringLiteral("selectionRemoveIslands"),
        mask_definition.value(QStringLiteral("selectionRemoveIslands")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionInvert"),
        mask_definition.value(QStringLiteral("maskSetSelectionInvert")).toBool());
    adjustments.insert(
        QStringLiteral("maskSetSelectionExpand"),
        mask_definition.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionSmooth"),
        mask_definition.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionFineEdge"),
        mask_definition.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
    adjustments.insert(
        QStringLiteral("maskSetSelectionEdgeContrast"),
        mask_definition.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
    adjustments.insert(QStringLiteral("maskSetDensity"), mask_definition.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
    adjustments.insert(QStringLiteral("maskSetOpacity"), mask_definition.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());

    QJsonObject geometry;
    geometry.insert(QStringLiteral("feather"), mask_definition.value(QStringLiteral("feather")).toDouble());
    geometry.insert(QStringLiteral("rotation"), mask_definition.value(QStringLiteral("rotation")).toDouble());
    geometry.insert(
        QStringLiteral("maskRasterWidth"),
        std::max(0, mask_definition.value(QStringLiteral("maskRasterWidth")).toInt()));
    geometry.insert(
        QStringLiteral("maskRasterHeight"),
        std::max(0, mask_definition.value(QStringLiteral("maskRasterHeight")).toInt()));
    geometry.insert(
        QStringLiteral("alphaSamplingMode"),
        mask_definition.value(QStringLiteral("alphaSamplingMode")).toString());
    geometry.insert(
        QStringLiteral("softnessMode"),
        mask_definition.value(QStringLiteral("softnessMode")).toString());
    QJsonObject constraints;
    constraints.insert(QStringLiteral("refineLuminanceMin"), mask_definition.value(QStringLiteral("refineLuminanceMin")).toDouble());
    constraints.insert(QStringLiteral("refineLuminanceMax"), mask_definition.value(QStringLiteral("refineLuminanceMax")).toDouble());
    constraints.insert(QStringLiteral("refineHueCenter"), mask_definition.value(QStringLiteral("refineHueCenter")).toDouble());
    constraints.insert(QStringLiteral("refineHueRange"), mask_definition.value(QStringLiteral("refineHueRange")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMin"), mask_definition.value(QStringLiteral("refineSaturationMin")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMax"), mask_definition.value(QStringLiteral("refineSaturationMax")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMin"), mask_definition.value(QStringLiteral("refineDepthMin")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMax"), mask_definition.value(QStringLiteral("refineDepthMax")).toDouble());
    constraints.insert(QStringLiteral("refineEdgeConfidenceMin"), mask_definition.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble());
    constraints.insert(QStringLiteral("refineRangeFeather"), mask_definition.value(QStringLiteral("refineRangeFeather")).toDouble());
    constraints.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskHoleFillThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble()
            : 0.004);
    constraints.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble()
            : 0.0015);
    constraints.insert(
        QStringLiteral("matteCleanupSchemaVersion"),
        std::max(1, mask_definition.value(QStringLiteral("matteCleanupSchemaVersion"), 1).toInt()));
    constraints.insert(
        QStringLiteral("matteCleanupProfile"),
        mask_definition.value(QStringLiteral("matteCleanupProfile")).toString());
    constraints.insert(
        QStringLiteral("matteCleanupDefaultsApplied"),
        mask_definition.value(QStringLiteral("matteCleanupDefaultsApplied")).toBool());
    if (is_center_radius_mask_kind(kind)) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      geometry.insert(QStringLiteral("brushPreset"), mask_definition.value(QStringLiteral("brushPreset")).toString());
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      geometry.insert(QStringLiteral("brushHardness"), mask_definition.value(QStringLiteral("brushHardness")).toDouble());
      geometry.insert(QStringLiteral("brushSpacing"), mask_definition.value(QStringLiteral("brushSpacing")).toDouble());
      geometry.insert(QStringLiteral("brushSmoothing"), mask_definition.value(QStringLiteral("brushSmoothing")).toDouble());
      geometry.insert(QStringLiteral("brushPressureSize"), mask_definition.value(QStringLiteral("brushPressureSize")).toDouble());
      geometry.insert(QStringLiteral("brushPressureFlow"), mask_definition.value(QStringLiteral("brushPressureFlow")).toDouble());
      geometry.insert(
          QStringLiteral("brushPressureOpacity"),
          mask_definition.value(QStringLiteral("brushPressureOpacity")).toDouble());
      geometry.insert(QStringLiteral("brushBuildup"), mask_definition.value(QStringLiteral("brushBuildup")).toBool());
      geometry.insert(QStringLiteral("brushEraseMode"), mask_definition.value(QStringLiteral("brushEraseMode")).toBool());
      geometry.insert(QStringLiteral("brushAutoMask"), mask_definition.value(QStringLiteral("brushAutoMask")).toBool());
      geometry.insert(QStringLiteral("brushAutoMaskTolerance"), mask_definition.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
      geometry.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          mask_definition.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      geometry.insert(
          QStringLiteral("brushProtectEdges"),
          mask_definition.value(QStringLiteral("brushProtectEdges")).toBool());
      geometry.insert(
          QStringLiteral("healTextureBlend"),
          qBound(0.0, mask_definition.value(QStringLiteral("healTextureBlend"), 0.68).toDouble(), 1.0));
      geometry.insert(
          QStringLiteral("healColorBlend"),
          qBound(0.0, mask_definition.value(QStringLiteral("healColorBlend"), 0.46).toDouble(), 1.0));
      geometry.insert(QStringLiteral("brushRefineMode"), mask_definition.value(QStringLiteral("brushRefineMode")).toBool());
      geometry.insert(QStringLiteral("refineBrushRecalculate"), mask_definition.value(QStringLiteral("refineBrushRecalculate")).toBool());
      geometry.insert(QStringLiteral("brushRefineTargetId"), mask_definition.value(QStringLiteral("brushRefineTargetId")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetName"), mask_definition.value(QStringLiteral("brushRefineTargetName")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetKind"), mask_definition.value(QStringLiteral("brushRefineTargetKind")).toString());
      geometry.insert(QStringLiteral("cloneSourceSet"), mask_definition.value(QStringLiteral("cloneSourceSet")).toBool());
      geometry.insert(QStringLiteral("cloneSourceX"), mask_definition.value(QStringLiteral("cloneSourceX")).toDouble());
      geometry.insert(QStringLiteral("cloneSourceY"), mask_definition.value(QStringLiteral("cloneSourceY")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetX"), mask_definition.value(QStringLiteral("cloneOffsetX")).toDouble());
      geometry.insert(QStringLiteral("cloneOffsetY"), mask_definition.value(QStringLiteral("cloneOffsetY")).toDouble());
      geometry.insert(QStringLiteral("cloneScale"), mask_definition.value(QStringLiteral("cloneScale")).toDouble());
      geometry.insert(QStringLiteral("cloneRotation"), mask_definition.value(QStringLiteral("cloneRotation")).toDouble());
      geometry.insert(QStringLiteral("cloneAligned"), mask_definition.value(QStringLiteral("cloneAligned"), true).toBool());
      QJsonArray clone_sources;
      for (const auto& source_value : mask_definition.value(QStringLiteral("cloneSources")).toList()) {
        const auto source_map = source_value.toMap();
        QJsonObject clone_source;
        clone_source.insert(QStringLiteral("name"), source_map.value(QStringLiteral("name")).toString());
        clone_source.insert(QStringLiteral("x"), source_map.value(QStringLiteral("x"), 0.5).toDouble());
        clone_source.insert(QStringLiteral("y"), source_map.value(QStringLiteral("y"), 0.5).toDouble());
        clone_source.insert(QStringLiteral("scale"), source_map.value(QStringLiteral("scale"), 1.0).toDouble());
        clone_source.insert(QStringLiteral("rotation"), source_map.value(QStringLiteral("rotation")).toDouble());
        clone_source.insert(QStringLiteral("aligned"), source_map.value(QStringLiteral("aligned"), true).toBool());
        clone_sources.append(clone_source);
      }
      geometry.insert(QStringLiteral("cloneSources"), clone_sources);
      geometry.insert(
          QStringLiteral("cloneSourceIndex"),
          mask_definition.value(QStringLiteral("cloneSourceIndex"), -1).toInt());
      const auto patch_source_width =
          qBound(0.02, mask_definition.value(QStringLiteral("patchSourceWidth"), 0.18).toDouble(), 1.0);
      const auto patch_source_height =
          qBound(0.02, mask_definition.value(QStringLiteral("patchSourceHeight"), 0.18).toDouble(), 1.0);
      geometry.insert(
          QStringLiteral("patchSourceSet"),
          mask_definition.value(QStringLiteral("patchSourceSet")).toBool());
      geometry.insert(
          QStringLiteral("patchSourceX"),
          qBound(0.0, mask_definition.value(QStringLiteral("patchSourceX"), 0.5).toDouble(), 1.0 - patch_source_width));
      geometry.insert(
          QStringLiteral("patchSourceY"),
          qBound(0.0, mask_definition.value(QStringLiteral("patchSourceY"), 0.5).toDouble(), 1.0 - patch_source_height));
      geometry.insert(QStringLiteral("patchSourceWidth"), patch_source_width);
      geometry.insert(QStringLiteral("patchSourceHeight"), patch_source_height);
      insert_serialized_brush_paths(mask_definition, &geometry);
      insert_serialized_brush_raster(mask_definition, &geometry);
    } else if (insert_serialized_shape_or_range_payload(
                   kind, mask_definition, &geometry, &constraints)) {
    } else if (is_raster_mask_kind(kind)) {
      geometry.insert(QStringLiteral("retouchMode"), mask_definition.value(QStringLiteral("retouchMode")).toString());
      if (kind == QStringLiteral("alpha")) {
        geometry.insert(
            QStringLiteral("alphaSourcePath"),
            mask_definition.value(QStringLiteral("alphaSourcePath")).toString());
        geometry.insert(
            QStringLiteral("alphaImportInvert"),
            mask_definition.contains(QStringLiteral("alphaImportInvert"))
                ? mask_definition.value(QStringLiteral("alphaImportInvert")).toBool()
                : false);
        geometry.insert(
            QStringLiteral("alphaImportThreshold"),
            mask_definition.value(QStringLiteral("alphaImportThreshold")).toDouble());
        geometry.insert(
            QStringLiteral("alphaImportResampleMode"),
            mask_definition.value(QStringLiteral("alphaImportResampleMode")).toString());
      }
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (!segmentation_data.isEmpty() && segmentation_width > 0 && segmentation_height > 0) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    }
    if (is_ai_segmentation_mask_kind(kind)) {
      write_generated_mask_metadata(&geometry, mask_definition);
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject mask;
    mask.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
    mask.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("schemaVersion"), 1)));
    mask.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("sourceSchemaVersion"), 1)));
    mask.insert(QStringLiteral("parentId"), mask_definition.value(QStringLiteral("parentId")).toString());
    mask.insert(QStringLiteral("outputId"), mask_definition.value(QStringLiteral("outputId")).toString());
    mask.insert(
        QStringLiteral("reusableOutput"),
        mask_definition.value(QStringLiteral("reusableOutput")).toBool());
    mask.insert(QStringLiteral("name"), mask_definition.value(QStringLiteral("name")).toString());
    mask.insert(QStringLiteral("kind"), kind);
    if (mask_definition.contains(QStringLiteral("agentVisualReferentId"))) {
      mask.insert(
          QStringLiteral("agentVisualReferentId"),
          mask_definition.value(QStringLiteral("agentVisualReferentId")).toString());
    }
    if (mask_definition.contains(QStringLiteral("agentSelectionSource"))) {
      mask.insert(
          QStringLiteral("agentSelectionSource"),
          mask_definition.value(QStringLiteral("agentSelectionSource")).toString());
    }
    mask.insert(
        QStringLiteral("toolRole"),
        normalized_mask_tool_role(mask_definition.value(QStringLiteral("toolRole")).toString()));
    mask.insert(
        QStringLiteral("maskSourceClass"),
        mask_definition.value(QStringLiteral("maskSourceClass")).toString());
    mask.insert(
        QStringLiteral("maskCoordinateSpace"),
        mask_definition.value(QStringLiteral("maskCoordinateSpace")).toString());
    mask.insert(
        QStringLiteral("sourceImageRevision"),
        mask_definition.value(QStringLiteral("sourceImageRevision")).toString());
    mask.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_definition.value(QStringLiteral("operation")).toString()));
    mask.insert(
        QStringLiteral("enabled"),
        mask_definition.contains(QStringLiteral("enabled"))
            ? mask_definition.value(QStringLiteral("enabled")).toBool()
            : true);
    mask.insert(
        QStringLiteral("solo"),
        mask_definition.contains(QStringLiteral("solo"))
            ? mask_definition.value(QStringLiteral("solo")).toBool()
            : false);
    mask.insert(
        QStringLiteral("locked"),
        mask_definition.contains(QStringLiteral("locked"))
            ? mask_definition.value(QStringLiteral("locked")).toBool()
            : false);
    mask.insert(
        QStringLiteral("committed"),
        mask_definition.contains(QStringLiteral("committed"))
            ? mask_definition.value(QStringLiteral("committed")).toBool()
            : true);
    mask.insert(
        QStringLiteral("retouchOrder"),
        mask_definition.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")
            ? std::max(1, map_int_value_or(mask_definition, QStringLiteral("retouchOrder"), ordinal))
            : 0);
    mask.insert(
        QStringLiteral("opacity"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("opacity"))
                   ? mask_definition.value(QStringLiteral("opacity")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("density"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("density"))
                   ? mask_definition.value(QStringLiteral("density")).toDouble()
                   : 1.0,
               1.0));
    mask.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    mask.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_definition.value(QStringLiteral("blendMode")).toString()));
    mask.insert(QStringLiteral("adjustments"), adjustments);
    mask.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      mask.insert(QStringLiteral("constraints"), constraints);
    }
    return mask;
  };

  QVariantList serialized_masks = normalize_mask_stack(state.mask_stack);
  if (!state.mask_stack.isEmpty()) {
  } else {
    QVariantList synthesized_masks;
    if (has_structured_linear_mask(state)) {
      QVariantMap linear_mask = default_mask_definition(QStringLiteral("linear"), 1);
      linear_mask.insert(QStringLiteral("enabled"), state.linear_mask_enabled);
      linear_mask.insert(QStringLiteral("exposure"), state.linear_mask_exposure);
      linear_mask.insert(QStringLiteral("temperature"), state.linear_mask_temperature);
      linear_mask.insert(QStringLiteral("feather"), state.linear_mask_feather);
      linear_mask.insert(QStringLiteral("position"), state.linear_mask_position);
      linear_mask.insert(QStringLiteral("rotation"), state.linear_mask_rotation);
      synthesized_masks.append(linear_mask);
    }

    if (has_structured_radial_mask(state)) {
      QVariantMap radial_mask = default_mask_definition(
          QStringLiteral("radial"), static_cast<int>(synthesized_masks.size()) + 1);
      radial_mask.insert(QStringLiteral("enabled"), state.radial_mask_enabled);
      radial_mask.insert(QStringLiteral("exposure"), state.radial_mask_exposure);
      radial_mask.insert(QStringLiteral("temperature"), state.radial_mask_temperature);
      radial_mask.insert(QStringLiteral("feather"), state.radial_mask_feather);
      radial_mask.insert(QStringLiteral("centerX"), state.radial_mask_center_x);
      radial_mask.insert(QStringLiteral("centerY"), state.radial_mask_center_y);
      radial_mask.insert(QStringLiteral("radiusX"), state.radial_mask_radius_x);
      radial_mask.insert(QStringLiteral("radiusY"), state.radial_mask_radius_y);
      radial_mask.insert(QStringLiteral("rotation"), state.radial_mask_rotation);
      synthesized_masks.append(radial_mask);
    }
    serialized_masks = synthesized_masks;
  }

  return [&serialized_masks,
          &serialize_mask_object]() PHOTO_EDITOR_RECIPE_NOINLINE {
  QJsonArray mask_stack;
  QJsonArray retouch_stack;
  QJsonArray mask_sets;
  QHash<QString, int> mask_set_indices;
  auto ordinal = 1;
  for (const auto& mask_value : serialized_masks) {
    const auto mask_definition = normalized_mask_definition(mask_value.toMap(), ordinal);
    mask_stack.append(serialize_mask_object(mask_definition, ordinal));
    if (mask_definition.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")) {
      retouch_stack.append(serialize_mask_object(mask_definition, ordinal));
      ++ordinal;
      continue;
    }

    const auto set_id = mask_definition.value(QStringLiteral("maskSetId")).toString();
    const auto set_name = mask_definition.value(QStringLiteral("maskSetName")).toString();
    auto set_index = mask_set_indices.value(set_id, -1);
    if (set_index < 0) {
      QJsonObject set_adjustments;
      set_adjustments.insert(
          QStringLiteral("exposure"),
          mask_definition.value(QStringLiteral("exposure")).toDouble());
      set_adjustments.insert(
          QStringLiteral("contrast"),
          mask_definition.value(QStringLiteral("contrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("highlights"),
          mask_definition.value(QStringLiteral("highlights")).toDouble());
      set_adjustments.insert(
          QStringLiteral("shadows"),
          mask_definition.value(QStringLiteral("shadows")).toDouble());
      set_adjustments.insert(
          QStringLiteral("whites"),
          mask_definition.value(QStringLiteral("whites")).toDouble());
      set_adjustments.insert(
          QStringLiteral("blacks"),
          mask_definition.value(QStringLiteral("blacks")).toDouble());
      set_adjustments.insert(
          QStringLiteral("temperature"),
          mask_definition.value(QStringLiteral("temperature")).toDouble());
      set_adjustments.insert(
          QStringLiteral("tint"),
          mask_definition.value(QStringLiteral("tint")).toDouble());
      set_adjustments.insert(
          QStringLiteral("vibrance"),
          mask_definition.value(QStringLiteral("vibrance")).toDouble());
      set_adjustments.insert(
          QStringLiteral("saturation"),
          mask_definition.value(QStringLiteral("saturation")).toDouble());
      set_adjustments.insert(
          QStringLiteral("hueShift"),
          mask_definition.value(QStringLiteral("hueShift")).toDouble());
      set_adjustments.insert(
          QStringLiteral("saturationShift"),
          mask_definition.value(QStringLiteral("saturationShift")).toDouble());
      set_adjustments.insert(
          QStringLiteral("colorizeHue"),
          mask_definition.value(QStringLiteral("colorizeHue")).toDouble());
      set_adjustments.insert(
          QStringLiteral("colorizeStrength"),
          mask_definition.value(QStringLiteral("colorizeStrength")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionEdgeContrast"),
          mask_definition.value(QStringLiteral("selectionEdgeContrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionSmooth"),
          mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionFineEdge"),
          mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionDecontaminate"),
          mask_definition.value(QStringLiteral("selectionDecontaminate")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionGrow"), mask_definition.value(QStringLiteral("selectionGrow")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionShrink"), mask_definition.value(QStringLiteral("selectionShrink")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionFeather"), mask_definition.value(QStringLiteral("selectionFeather")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionBlur"), mask_definition.value(QStringLiteral("selectionBlur")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionHarden"), mask_definition.value(QStringLiteral("selectionHarden")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionDespeckle"), mask_definition.value(QStringLiteral("selectionDespeckle")).toDouble());
      set_adjustments.insert(QStringLiteral("selectionFillHoles"), mask_definition.value(QStringLiteral("selectionFillHoles")).toDouble());
      set_adjustments.insert(
          QStringLiteral("selectionRemoveIslands"),
          mask_definition.value(QStringLiteral("selectionRemoveIslands")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionInvert"),
          mask_definition.value(QStringLiteral("maskSetSelectionInvert")).toBool());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionExpand"),
          mask_definition.value(QStringLiteral("maskSetSelectionExpand")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionSmooth"),
          mask_definition.value(QStringLiteral("maskSetSelectionSmooth")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionFineEdge"),
          mask_definition.value(QStringLiteral("maskSetSelectionFineEdge")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetSelectionEdgeContrast"),
          mask_definition.value(QStringLiteral("maskSetSelectionEdgeContrast")).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetDensity"),
          mask_definition.value(QStringLiteral("maskSetDensity"), 1.0).toDouble());
      set_adjustments.insert(
          QStringLiteral("maskSetOpacity"),
          mask_definition.value(QStringLiteral("maskSetOpacity"), 1.0).toDouble());

      QJsonObject set_object;
      set_object.insert(QStringLiteral("schemaVersion"), 2);
      set_object.insert(QStringLiteral("adjustmentsSchemaVersion"), 1);
      set_object.insert(QStringLiteral("componentSchemaVersion"), 1);
      set_object.insert(QStringLiteral("id"), set_id);
      set_object.insert(QStringLiteral("name"), set_name);
      set_object.insert(QStringLiteral("adjustments"), set_adjustments);
      set_object.insert(QStringLiteral("sources"), QJsonArray{});
      set_index = static_cast<int>(mask_sets.size());
      mask_sets.append(set_object);
      mask_set_indices.insert(set_id, set_index);
    }

    auto set_object = mask_sets.at(set_index).toObject();
    set_object = [](
        QJsonObject set_object,
        const QVariantMap& mask_definition) PHOTO_EDITOR_RECIPE_NOINLINE {
    auto sources = set_object.value(QStringLiteral("sources")).toArray();

    QJsonObject geometry;
    geometry.insert(
        QStringLiteral("feather"),
        mask_definition.value(QStringLiteral("feather")).toDouble());
    geometry.insert(
        QStringLiteral("rotation"),
        mask_definition.value(QStringLiteral("rotation")).toDouble());
    geometry.insert(
        QStringLiteral("maskRasterWidth"),
        std::max(0, mask_definition.value(QStringLiteral("maskRasterWidth")).toInt()));
    geometry.insert(
        QStringLiteral("maskRasterHeight"),
        std::max(0, mask_definition.value(QStringLiteral("maskRasterHeight")).toInt()));
    geometry.insert(
        QStringLiteral("alphaSamplingMode"),
        mask_definition.value(QStringLiteral("alphaSamplingMode")).toString());
    geometry.insert(
        QStringLiteral("softnessMode"),
        mask_definition.value(QStringLiteral("softnessMode")).toString());
    QJsonObject constraints;
    constraints.insert(QStringLiteral("refineLuminanceMin"), mask_definition.value(QStringLiteral("refineLuminanceMin")).toDouble());
    constraints.insert(QStringLiteral("refineLuminanceMax"), mask_definition.value(QStringLiteral("refineLuminanceMax")).toDouble());
    constraints.insert(QStringLiteral("refineHueCenter"), mask_definition.value(QStringLiteral("refineHueCenter")).toDouble());
    constraints.insert(QStringLiteral("refineHueRange"), mask_definition.value(QStringLiteral("refineHueRange")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMin"), mask_definition.value(QStringLiteral("refineSaturationMin")).toDouble());
    constraints.insert(QStringLiteral("refineSaturationMax"), mask_definition.value(QStringLiteral("refineSaturationMax")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMin"), mask_definition.value(QStringLiteral("refineDepthMin")).toDouble());
    constraints.insert(QStringLiteral("refineDepthMax"), mask_definition.value(QStringLiteral("refineDepthMax")).toDouble());
    constraints.insert(QStringLiteral("refineEdgeConfidenceMin"), mask_definition.value(QStringLiteral("refineEdgeConfidenceMin")).toDouble());
    constraints.insert(QStringLiteral("refineRangeFeather"), mask_definition.value(QStringLiteral("refineRangeFeather")).toDouble());
    constraints.insert(
        QStringLiteral("generatedMaskHoleFillThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskHoleFillThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskHoleFillThreshold")).toDouble()
            : 0.004);
    constraints.insert(
        QStringLiteral("generatedMaskIslandRemovalThreshold"),
        mask_definition.contains(QStringLiteral("generatedMaskIslandRemovalThreshold"))
            ? mask_definition.value(QStringLiteral("generatedMaskIslandRemovalThreshold")).toDouble()
            : 0.0015);
    const auto kind = normalized_mask_kind(mask_definition.value(QStringLiteral("kind")).toString());
    if (is_center_radius_mask_kind(kind)) {
      geometry.insert(QStringLiteral("centerX"), mask_definition.value(QStringLiteral("centerX")).toDouble());
      geometry.insert(QStringLiteral("centerY"), mask_definition.value(QStringLiteral("centerY")).toDouble());
      geometry.insert(QStringLiteral("radiusX"), mask_definition.value(QStringLiteral("radiusX")).toDouble());
      geometry.insert(QStringLiteral("radiusY"), mask_definition.value(QStringLiteral("radiusY")).toDouble());
    } else if (kind == QStringLiteral("linear")) {
      geometry.insert(QStringLiteral("position"), mask_definition.value(QStringLiteral("position")).toDouble());
    } else if (kind == QStringLiteral("brush")) {
      geometry.insert(QStringLiteral("brushPreset"), mask_definition.value(QStringLiteral("brushPreset")).toString());
      geometry.insert(QStringLiteral("brushSize"), mask_definition.value(QStringLiteral("brushSize")).toDouble());
      geometry.insert(QStringLiteral("brushFlow"), mask_definition.value(QStringLiteral("brushFlow")).toDouble());
      geometry.insert(QStringLiteral("brushHardness"), mask_definition.value(QStringLiteral("brushHardness")).toDouble());
      geometry.insert(QStringLiteral("brushSpacing"), mask_definition.value(QStringLiteral("brushSpacing")).toDouble());
      geometry.insert(QStringLiteral("brushSmoothing"), mask_definition.value(QStringLiteral("brushSmoothing")).toDouble());
      geometry.insert(QStringLiteral("brushPressureSize"), mask_definition.value(QStringLiteral("brushPressureSize")).toDouble());
      geometry.insert(QStringLiteral("brushPressureFlow"), mask_definition.value(QStringLiteral("brushPressureFlow")).toDouble());
      geometry.insert(
          QStringLiteral("brushPressureOpacity"),
          mask_definition.value(QStringLiteral("brushPressureOpacity")).toDouble());
      geometry.insert(QStringLiteral("brushBuildup"), mask_definition.value(QStringLiteral("brushBuildup")).toBool());
      geometry.insert(QStringLiteral("brushEraseMode"), mask_definition.value(QStringLiteral("brushEraseMode")).toBool());
      geometry.insert(QStringLiteral("brushAutoMask"), mask_definition.value(QStringLiteral("brushAutoMask")).toBool());
      geometry.insert(QStringLiteral("brushAutoMaskTolerance"), mask_definition.value(QStringLiteral("brushAutoMaskTolerance")).toDouble());
      geometry.insert(
          QStringLiteral("brushAutoMaskContinuousSampling"),
          mask_definition.value(QStringLiteral("brushAutoMaskContinuousSampling")).toBool());
      geometry.insert(
          QStringLiteral("brushProtectEdges"),
          mask_definition.value(QStringLiteral("brushProtectEdges")).toBool());
      geometry.insert(QStringLiteral("brushRefineMode"), mask_definition.value(QStringLiteral("brushRefineMode")).toBool());
      geometry.insert(QStringLiteral("refineBrushRecalculate"), mask_definition.value(QStringLiteral("refineBrushRecalculate")).toBool());
      geometry.insert(QStringLiteral("brushRefineTargetId"), mask_definition.value(QStringLiteral("brushRefineTargetId")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetName"), mask_definition.value(QStringLiteral("brushRefineTargetName")).toString());
      geometry.insert(QStringLiteral("brushRefineTargetKind"), mask_definition.value(QStringLiteral("brushRefineTargetKind")).toString());
      insert_serialized_brush_paths(mask_definition, &geometry);
      insert_serialized_brush_raster(mask_definition, &geometry);
    } else if (insert_serialized_shape_or_range_payload(
                   kind, mask_definition, &geometry, &constraints)) {
    } else if (is_raster_mask_kind(kind)) {
      if (kind == QStringLiteral("alpha")) {
        geometry.insert(
            QStringLiteral("alphaSourcePath"),
            mask_definition.value(QStringLiteral("alphaSourcePath")).toString());
        geometry.insert(
            QStringLiteral("alphaImportInvert"),
            mask_definition.contains(QStringLiteral("alphaImportInvert"))
                ? mask_definition.value(QStringLiteral("alphaImportInvert")).toBool()
                : false);
        geometry.insert(
            QStringLiteral("alphaImportThreshold"),
            mask_definition.value(QStringLiteral("alphaImportThreshold")).toDouble());
        geometry.insert(
            QStringLiteral("alphaImportResampleMode"),
            mask_definition.value(QStringLiteral("alphaImportResampleMode")).toString());
      }
      const auto segmentation_data = mask_definition.value(QStringLiteral("segmentationData")).toByteArray();
      const auto segmentation_width = mask_definition.value(QStringLiteral("segmentationWidth")).toInt();
      const auto segmentation_height = mask_definition.value(QStringLiteral("segmentationHeight")).toInt();
      if (valid_segmentation_payload(segmentation_data, segmentation_width, segmentation_height)) {
        geometry.insert(
            QStringLiteral("segmentationData"),
            QString::fromLatin1(segmentation_data.toBase64()));
        geometry.insert(QStringLiteral("segmentationWidth"), segmentation_width);
        geometry.insert(QStringLiteral("segmentationHeight"), segmentation_height);
        geometry.insert(
            QStringLiteral("rasterSchemaVersion"),
            std::max(1, map_int_value_or(mask_definition, QStringLiteral("rasterSchemaVersion"), 1)));
      }
    }
    if (is_ai_segmentation_mask_kind(kind)) {
      write_generated_mask_metadata(&geometry, mask_definition);
    }
    write_inpaint_patch_payload(&geometry, mask_definition);

    QJsonObject selection;
    selection.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    selection.insert(
        QStringLiteral("expand"),
        mask_definition.value(QStringLiteral("selectionExpand")).toDouble());
    selection.insert(
        QStringLiteral("smooth"),
        mask_definition.value(QStringLiteral("selectionSmooth")).toDouble());
    selection.insert(
        QStringLiteral("fineEdge"),
        mask_definition.value(QStringLiteral("selectionFineEdge")).toDouble());

    QJsonObject source_object;
    source_object.insert(QStringLiteral("componentSchemaVersion"), 1);
    source_object.insert(QStringLiteral("id"), mask_definition.value(QStringLiteral("id")).toString());
    source_object.insert(
        QStringLiteral("schemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("schemaVersion"), 1)));
    source_object.insert(
        QStringLiteral("sourceSchemaVersion"),
        std::max(1, map_int_value_or(mask_definition, QStringLiteral("sourceSchemaVersion"), 1)));
    source_object.insert(QStringLiteral("parentId"), mask_definition.value(QStringLiteral("parentId")).toString());
    source_object.insert(QStringLiteral("outputId"), mask_definition.value(QStringLiteral("outputId")).toString());
    source_object.insert(
        QStringLiteral("reusableOutput"),
        mask_definition.value(QStringLiteral("reusableOutput")).toBool());
    source_object.insert(QStringLiteral("name"), mask_definition.value(QStringLiteral("name")).toString());
    source_object.insert(QStringLiteral("kind"), kind);
    if (mask_definition.contains(QStringLiteral("agentVisualReferentId"))) {
      source_object.insert(
          QStringLiteral("agentVisualReferentId"),
          mask_definition.value(QStringLiteral("agentVisualReferentId")).toString());
    }
    if (mask_definition.contains(QStringLiteral("agentSelectionSource"))) {
      source_object.insert(
          QStringLiteral("agentSelectionSource"),
          mask_definition.value(QStringLiteral("agentSelectionSource")).toString());
    }
    source_object.insert(
        QStringLiteral("maskSourceClass"),
        mask_definition.value(QStringLiteral("maskSourceClass")).toString());
    source_object.insert(
        QStringLiteral("maskCoordinateSpace"),
        mask_definition.value(QStringLiteral("maskCoordinateSpace")).toString());
    source_object.insert(
        QStringLiteral("sourceImageRevision"),
        mask_definition.value(QStringLiteral("sourceImageRevision")).toString());
    source_object.insert(
        QStringLiteral("operation"),
        normalized_mask_operation(mask_definition.value(QStringLiteral("operation")).toString()));
    source_object.insert(
        QStringLiteral("enabled"),
        mask_definition.contains(QStringLiteral("enabled"))
            ? mask_definition.value(QStringLiteral("enabled")).toBool()
            : true);
    source_object.insert(
        QStringLiteral("solo"),
        mask_definition.contains(QStringLiteral("solo"))
            ? mask_definition.value(QStringLiteral("solo")).toBool()
            : false);
    source_object.insert(
        QStringLiteral("locked"),
        mask_definition.contains(QStringLiteral("locked"))
            ? mask_definition.value(QStringLiteral("locked")).toBool()
            : false);
    source_object.insert(
        QStringLiteral("opacity"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("opacity"))
                   ? mask_definition.value(QStringLiteral("opacity")).toDouble()
                   : 1.0,
               1.0));
    source_object.insert(
        QStringLiteral("density"),
        qBound(0.0,
               mask_definition.contains(QStringLiteral("density"))
                   ? mask_definition.value(QStringLiteral("density")).toDouble()
                   : 1.0,
               1.0));
    source_object.insert(
        QStringLiteral("invert"),
        mask_definition.value(QStringLiteral("invert")).toBool());
    source_object.insert(
        QStringLiteral("blendMode"),
        normalized_mask_blend_mode(mask_definition.value(QStringLiteral("blendMode")).toString()));
    const auto adjustment_overrides = mask_source_adjustment_overrides(
        mask_definition,
        set_object.value(QStringLiteral("adjustments")).toObject().toVariantMap());
    if (!adjustment_overrides.isEmpty()) {
      source_object.insert(
          QStringLiteral("adjustmentOverrides"),
          QJsonObject::fromVariantMap(adjustment_overrides));
    }
    source_object.insert(QStringLiteral("selection"), selection);
    source_object.insert(QStringLiteral("geometry"), geometry);
    if (!constraints.isEmpty()) {
      source_object.insert(QStringLiteral("constraints"), constraints);
    }
    sources.append(source_object);
    set_object.insert(QStringLiteral("sources"), sources);
    return set_object;
    }(std::move(set_object), mask_definition);
    mask_sets.replace(set_index, set_object);
    ++ordinal;
  }

  return std::array<QJsonArray, 3>{mask_stack, retouch_stack, mask_sets};
  }();
  }();
  const auto& mask_stack = serialized_mask_collections[0];
  const auto& retouch_stack = serialized_mask_collections[1];
  const auto& mask_sets = serialized_mask_collections[2];

  QJsonObject guide_settings;
  guide_settings.insert(QStringLiteral("customGuides"), QJsonArray{});

  QJsonObject history;
  history.insert(
      QStringLiteral("activeBranchId"),
      state.active_history_branch.trimmed().isEmpty()
          ? QStringLiteral("main")
          : state.active_history_branch.trimmed());
  history.insert(QStringLiteral("snapshots"), QJsonArray::fromVariantList(state.recipe_snapshots));

  QJsonObject object;
  object.insert(QStringLiteral("rawAdjustments"), raw_adjustments);
  object.insert(QStringLiteral("globalAdjustments"), global_adjustments);
  object.insert(QStringLiteral("colorPipeline"), color_pipeline);
  object.insert(QStringLiteral("curves"), curves);
  object.insert(QStringLiteral("colorMixer"), color_mixer);
  object.insert(QStringLiteral("colorGrading"), color_grading);
  object.insert(QStringLiteral("cropTransform"), crop_transform);
  object.insert(QStringLiteral("outputTransform"), output_transform);
  object.insert(QStringLiteral("exportTransform"), export_transform);
  object.insert(QStringLiteral("optics"), optics);
  object.insert(QStringLiteral("maskStack"), mask_stack);
  object.insert(QStringLiteral("maskSetSchemaVersion"), 2);
  object.insert(QStringLiteral("maskSets"), mask_sets);
  object.insert(QStringLiteral("retouchStack"), retouch_stack);
  object.insert(
      QStringLiteral("presetApplications"),
      QJsonArray::fromVariantList(state.preset_applications));
  const auto serialized_layers = detail::serialize_photo_layers(state.layers);
  object.insert(QStringLiteral("photoLayers"), QJsonArray::fromVariantList(serialized_layers));
  object.insert(QStringLiteral("layers"), QJsonArray::fromVariantList(serialized_layers));
  object.insert(QStringLiteral("history"), history);
  object.insert(QStringLiteral("overlayItems"), QJsonArray{});
  object.insert(QStringLiteral("guideSettings"), guide_settings);
  return object;
}

QJsonObject build_edit_recipe_object(
    const EditRecipeState& state) {
  auto object = build_edit_adjustment_state_object(state);
  object.insert(QStringLiteral("recipeVersion"), detail::photo_layers_have_paint_tiles(state.layers)
      ? kPhotoPaintEditRecipeVersion : detail::photo_layers_have_typography(state.layers)
      ? kPhotoTypographyEditRecipeVersion : detail::photo_layers_have_effect_stacks(state.layers)
      ? kPhotoEffectsEditRecipeVersion : kCurrentEditRecipeVersion);
  return object;
}

QString serialize_edit_recipe_json(const EditRecipeState& state) {
  return QString::fromUtf8(QJsonDocument(build_edit_recipe_object(state)).toJson(QJsonDocument::Compact));
}
#undef PHOTO_EDITOR_RECIPE_NOINLINE

}  // namespace photo_editor::desktop
