#pragma once

#include "photo_extended_effect_bridge.h"
#include "photo_layer_effect_codec.h"

namespace photo_editor::desktop {

// Read and edit the same registered instances used by Edit. The envelope stays
// in the existing photo recipe, so preview, persistence, Undo and export share it.
inline QVariantList timeline_photo_filter_fields(const pipeline::PhotoEffectInstance& effect) {
  if (extended_photo_effect(effect.type_id)) {
    auto fields = extended_photo_effect_fields(QString::fromStdString(effect.type_id));
    const auto values = extended_photo_effect_values(effect, QStringLiteral("acescg"), {});
    for (auto& field : fields) {
      auto row = field.toMap();
      row.insert("value", values.value(row.value("key").toString(), row.value("defaultValue")));
      field = row;
    }
    return fields;
  }
  QVariantList fields;
  const auto add = [&](const char* key, const char* label, double value,
                       double low, double high, double step = 0.01) {
    fields.push_back(QVariantMap{{"key", QString::fromUtf8(key)}, {"label", QString::fromUtf8(label)},
        {"value", value}, {"minimum", low}, {"maximum", high}, {"step", step}});
  };
  const auto& type = effect.type_id;
  if (type == "gaussian_blur") add("sigma", "Radius (px)", effect.gaussian.sigma, 0, 128, 0.1);
  else if (type == "box_blur") add("radius", "Radius (px)", effect.box.radius, 0, 128, 1);
  else if (type == "posterize_linear_rgb") add("levels", "Levels", effect.posterize_levels, 2, 256, 1);
  else if (type == "motion_blur" || type == "dragged_shutter") {
    add("distance", "Distance (px)", type == "motion_blur" ? effect.motion.distance : effect.dragged_shutter.distance, 0, 256, 0.1);
    add("angle", "Angle (degrees)", type == "motion_blur" ? effect.motion.angle : effect.dragged_shutter.angle, -180, 180, 1);
    if (type == "dragged_shutter") {
      add("falloff", "Falloff", effect.dragged_shutter.falloff, 0, 1);
      add("highlightWeighting", "Highlight weighting", effect.dragged_shutter.highlight_weighting, 0, 1);
    }
  } else if (type == "unsharp_mask") {
    add("sharpenRadius", "Radius (px)", effect.unsharp.sigma, 0.1, 64, 0.1);
    add("strength", "Strength", effect.unsharp.strength, 0, 4);
    add("threshold", "Threshold", effect.unsharp.threshold, 0, 1);
  } else if (type == "optical_diffusion") {
    add("diffusionRadius", "Spread (px)", effect.diffusion.radius, 0, 256, 1);
    add("highlightThreshold", "Highlight threshold", effect.diffusion.threshold, 0, 4);
    add("highlightKnee", "Soft knee", effect.diffusion.knee, 0, 1);
    add("diffusionStrength", "Strength", effect.diffusion.strength, 0, 2);
    add("warmth", "Warmth", effect.diffusion.warmth, -1, 1);
    add("highlightProtection", "Highlight protection", effect.diffusion.highlight_protection, 0, 1);
  } else if (type == "film_grain") {
    add("grainSize", "Grain size", effect.grain.size, 0.1, 12, 0.1);
    add("grainStrength", "Strength", effect.grain.strength, 0, 0.4, 0.001);
    add("grainColor", "Color", effect.grain.color, -1, 1);
    add("grainRoughness", "Roughness", effect.grain.roughness, 0, 1);
  } else if (type == "light_leak") {
    add("leakCenterX", "Horizontal position", effect.leak.center_x, 0, 1);
    add("leakCenterY", "Vertical position", effect.leak.center_y, 0, 1);
    add("leakWidth", "Width", effect.leak.width, 0.01, 1);
    add("leakAngle", "Angle (degrees)", effect.leak.angle, -180, 180, 1);
    add("leakStrength", "Strength", effect.leak.strength, 0, 1);
    add("warmth", "Warmth", effect.leak.warmth, -1, 1);
  } else if (type == "split_tone") {
    add("shadowHue", "Shadow hue", effect.split_tone.shadow_hue, 0, 360, 1);
    add("shadowStrength", "Shadow strength", effect.split_tone.shadow_strength, 0, 1);
    add("highlightHue", "Highlight hue", effect.split_tone.highlight_hue, 0, 360, 1);
    add("highlightStrength", "Highlight strength", effect.split_tone.highlight_strength, 0, 1);
    add("balance", "Balance", effect.split_tone.balance, -1, 1);
  } else if (type == "vignette") {
    add("vignetteRadius", "Radius", effect.vignette.radius, 0.05, 2);
    add("roundness", "Roundness", effect.vignette.roundness, 0, 1);
    add("feather", "Feather", effect.vignette.feather, 0, 1);
    add("vignetteStrength", "Strength", effect.vignette.strength, 0, 1);
  } else if (type == "duotone") {
    add("shadowHue", "Shadow hue", effect.duotone.shadow_hue, 0, 360, 1);
    add("highlightHue", "Highlight hue", effect.duotone.highlight_hue, 0, 360, 1);
    add("duotoneStrength", "Strength", effect.duotone.strength, 0, 1);
  } else if (type == "soft_fade") {
    add("fadeLift", "Black lift", effect.soft_fade.lift, 0, 1);
    add("fadeContrast", "Contrast", effect.soft_fade.contrast, 0, 2);
    add("fadeSaturation", "Saturation", effect.soft_fade.saturation, 0, 2);
  } else if (type == "imperfect_print") {
    add("paperScale", "Paper scale", effect.imperfect_print.paper_scale, 1, 256, 1);
    add("paperTexture", "Paper texture", effect.imperfect_print.paper_texture, 0, 1);
    add("inkSpread", "Ink spread", effect.imperfect_print.ink_spread, 0, 1);
    add("inkDensity", "Ink density", effect.imperfect_print.ink_density, 0, 2);
    add("redRegistrationX", "Red horizontal offset", effect.imperfect_print.red_offset_x, -8, 8);
    add("redRegistrationY", "Red vertical offset", effect.imperfect_print.red_offset_y, -8, 8);
    add("blueRegistrationX", "Blue horizontal offset", effect.imperfect_print.blue_offset_x, -8, 8);
    add("blueRegistrationY", "Blue vertical offset", effect.imperfect_print.blue_offset_y, -8, 8);
  }
  return fields;
}

inline bool set_timeline_photo_filter_value(pipeline::PhotoEffectInstance* effect,
                                             const QString& key, double value) {
  if (!effect || !std::isfinite(value)) return false;
  bool admitted = false;
  for (const auto& field : timeline_photo_filter_fields(*effect)) {
    const auto row = field.toMap();
    if (row.value("key").toString() == key) {
      if (value < row.value("minimum").toDouble() || value > row.value("maximum").toDouble()) return false;
      admitted = true;
      break;
    }
  }
  if (!admitted) return false;
  if (extended_photo_effect(effect->type_id))
    return set_extended_photo_effect_values(effect, {{key, value}}, QStringLiteral("acescg"));
  const auto& type = effect->type_id;
  if (key == "sigma") effect->gaussian.sigma = value;
  else if (key == "radius") effect->box.radius = static_cast<int>(std::round(value));
  else if (key == "levels") effect->posterize_levels = static_cast<int>(std::round(value));
  else if (key == "distance") { if (type == "dragged_shutter") effect->dragged_shutter.distance = value; else effect->motion.distance = value; }
  else if (key == "angle") { if (type == "dragged_shutter") effect->dragged_shutter.angle = value; else effect->motion.angle = value; }
  else if (key == "falloff") effect->dragged_shutter.falloff = value;
  else if (key == "highlightWeighting") effect->dragged_shutter.highlight_weighting = value;
  else if (key == "sharpenRadius") effect->unsharp.sigma = value;
  else if (key == "strength") effect->unsharp.strength = value;
  else if (key == "threshold") effect->unsharp.threshold = value;
  else if (key == "diffusionRadius") effect->diffusion.radius = value;
  else if (key == "highlightThreshold") effect->diffusion.threshold = value;
  else if (key == "highlightKnee") effect->diffusion.knee = value;
  else if (key == "diffusionStrength") effect->diffusion.strength = value;
  else if (key == "highlightProtection") effect->diffusion.highlight_protection = value;
  else if (key == "grainSize") effect->grain.size = value;
  else if (key == "grainStrength") effect->grain.strength = value;
  else if (key == "grainColor") effect->grain.color = value;
  else if (key == "grainRoughness") effect->grain.roughness = value;
  else if (key == "leakCenterX") effect->leak.center_x = value;
  else if (key == "leakCenterY") effect->leak.center_y = value;
  else if (key == "leakWidth") effect->leak.width = value;
  else if (key == "leakAngle") effect->leak.angle = value;
  else if (key == "leakStrength") effect->leak.strength = value;
  else if (key == "shadowHue") { if (type == "duotone") effect->duotone.shadow_hue = value; else effect->split_tone.shadow_hue = value; }
  else if (key == "highlightHue") { if (type == "duotone") effect->duotone.highlight_hue = value; else effect->split_tone.highlight_hue = value; }
  else if (key == "shadowStrength") effect->split_tone.shadow_strength = value;
  else if (key == "highlightStrength") effect->split_tone.highlight_strength = value;
  else if (key == "balance") effect->split_tone.balance = value;
  else if (key == "vignetteRadius") effect->vignette.radius = value;
  else if (key == "roundness") effect->vignette.roundness = value;
  else if (key == "feather") effect->vignette.feather = value;
  else if (key == "vignetteStrength") effect->vignette.strength = value;
  else if (key == "duotoneStrength") effect->duotone.strength = value;
  else if (key == "fadeLift") effect->soft_fade.lift = value;
  else if (key == "fadeContrast") effect->soft_fade.contrast = value;
  else if (key == "fadeSaturation") effect->soft_fade.saturation = value;
  else if (key == "warmth") {
    if (type == "light_leak") effect->leak.warmth = value;
    else effect->diffusion.warmth = value;
  } else if (key == "paperScale") effect->imperfect_print.paper_scale = value;
  else if (key == "paperTexture") effect->imperfect_print.paper_texture = value;
  else if (key == "inkSpread") effect->imperfect_print.ink_spread = value;
  else if (key == "inkDensity") effect->imperfect_print.ink_density = value;
  else if (key == "redRegistrationX") effect->imperfect_print.red_offset_x = value;
  else if (key == "redRegistrationY") effect->imperfect_print.red_offset_y = value;
  else if (key == "blueRegistrationX") effect->imperfect_print.blue_offset_x = value;
  else if (key == "blueRegistrationY") effect->imperfect_print.blue_offset_y = value;
  else return false;
  return true;
}
}  // namespace photo_editor::desktop
