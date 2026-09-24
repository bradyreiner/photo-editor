#include "preset_registry.h"

#include <QJsonObject>
#include <QVector>

namespace photo_editor::desktop {
namespace {

struct BuiltInPresetDefinition final {
  QString name;
  QString pack;
  QString category;
  QStringList adjustment_groups;
  QJsonArray adaptive_rules;
  EditRecipeState state;
};

QJsonObject adaptive_rule(
    const QString& source,
    const QString& contains,
    const QString& field,
    double delta) {
  QJsonObject rule;
  rule.insert(QStringLiteral("source"), source);
  rule.insert(QStringLiteral("contains"), contains);
  rule.insert(QStringLiteral("field"), field);
  rule.insert(QStringLiteral("delta"), delta);
  return rule;
}

QJsonObject adaptive_metric_rule(
    const QString& metric,
    int above,
    const QString& field,
    double delta) {
  QJsonObject rule;
  rule.insert(QStringLiteral("source"), QStringLiteral("image"));
  rule.insert(QStringLiteral("metric"), metric);
  rule.insert(QStringLiteral("above"), above);
  rule.insert(QStringLiteral("field"), field);
  rule.insert(QStringLiteral("delta"), delta);
  return rule;
}

// PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_BEGIN
QVector<BuiltInPresetDefinition> built_in_preset_definitions() {
  QVector<BuiltInPresetDefinition> presets;
  presets.reserve(14);

  EditRecipeState clean_portrait;
  clean_portrait.exposure = 0.18;
  clean_portrait.contrast = 10.0;
  clean_portrait.highlights = -18.0;
  clean_portrait.shadows = 16.0;
  clean_portrait.whites = 8.0;
  clean_portrait.blacks = -8.0;
  clean_portrait.texture = -8.0;
  clean_portrait.clarity = -6.0;
  clean_portrait.sharpening = 22.0;
  clean_portrait.sharpen_masking = 58.0;
  clean_portrait.vibrance = 8.0;
  clean_portrait.mixer_channels[0].saturation = -6.0;
  clean_portrait.mixer_channels[1].saturation = 5.0;
  clean_portrait.mixer_channels[1].luminance = 8.0;
  presets.push_back({
      QStringLiteral("Clean Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      clean_portrait,
  });

  EditRecipeState camera_aware_portrait = clean_portrait;
  camera_aware_portrait.texture = -10.0;
  camera_aware_portrait.clarity = -8.0;
  QJsonArray camera_aware_portrait_rules;
  camera_aware_portrait_rules.push_back(adaptive_rule(
      QStringLiteral("camera"),
      QStringLiteral("Canon EOS R5"),
      QStringLiteral("temperature"),
      2.0));
  camera_aware_portrait_rules.push_back(adaptive_rule(
      QStringLiteral("lens"),
      QStringLiteral("RF 28-70"),
      QStringLiteral("clarity"),
      4.0));
  camera_aware_portrait_rules.push_back(adaptive_metric_rule(
      QStringLiteral("highlightClips"),
      0,
      QStringLiteral("highlights"),
      -8.0));
  presets.push_back({
      QStringLiteral("Camera Aware Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      camera_aware_portrait_rules,
      camera_aware_portrait,
  });

  EditRecipeState soft_portrait = clean_portrait;
  soft_portrait.exposure = 0.10;
  soft_portrait.contrast = -4.0;
  soft_portrait.highlights = -24.0;
  soft_portrait.texture = -18.0;
  soft_portrait.clarity = -14.0;
  soft_portrait.curve_shadows = 10.0;
  soft_portrait.curve_highlights = -6.0;
  soft_portrait.grading_shadows_hue = 0.60;
  soft_portrait.grading_shadows_saturation = 8.0;
  soft_portrait.grading_highlights_hue = 0.09;
  soft_portrait.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Soft Skin Editorial"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      soft_portrait,
  });

  EditRecipeState dramatic_portrait = clean_portrait;
  dramatic_portrait.contrast = 24.0;
  dramatic_portrait.highlights = -12.0;
  dramatic_portrait.shadows = -12.0;
  dramatic_portrait.blacks = -18.0;
  dramatic_portrait.clarity = 12.0;
  dramatic_portrait.dehaze = 5.0;
  dramatic_portrait.grading_shadows_hue = 0.62;
  dramatic_portrait.grading_shadows_saturation = 10.0;
  dramatic_portrait.grading_midtones_hue = 0.08;
  dramatic_portrait.grading_midtones_saturation = 4.0;
  presets.push_back({
      QStringLiteral("Dramatic Portrait"),
      QStringLiteral("Portrait Essentials"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      dramatic_portrait,
  });

  EditRecipeState landscape_pop;
  landscape_pop.exposure = 0.08;
  landscape_pop.contrast = 18.0;
  landscape_pop.highlights = -28.0;
  landscape_pop.shadows = 18.0;
  landscape_pop.whites = 14.0;
  landscape_pop.blacks = -14.0;
  landscape_pop.texture = 18.0;
  landscape_pop.clarity = 20.0;
  landscape_pop.dehaze = 14.0;
  landscape_pop.vibrance = 22.0;
  landscape_pop.mixer_channels[3].saturation = 12.0;
  landscape_pop.mixer_channels[5].saturation = 16.0;
  landscape_pop.mixer_channels[5].luminance = -8.0;
  presets.push_back({
      QStringLiteral("Landscape Pop"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      landscape_pop,
  });

  EditRecipeState golden_hour = landscape_pop;
  golden_hour.temperature = 8.0;
  golden_hour.tint = 2.0;
  golden_hour.contrast = 12.0;
  golden_hour.highlights = -22.0;
  golden_hour.grading_highlights_hue = 0.10;
  golden_hour.grading_highlights_saturation = 18.0;
  golden_hour.grading_midtones_hue = 0.08;
  golden_hour.grading_midtones_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Golden Hour Landscape"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      golden_hour,
  });

  EditRecipeState moody_forest = landscape_pop;
  moody_forest.exposure = -0.10;
  moody_forest.contrast = 20.0;
  moody_forest.shadows = -8.0;
  moody_forest.blacks = -22.0;
  moody_forest.vibrance = 8.0;
  moody_forest.mixer_channels[3].hue = -8.0;
  moody_forest.mixer_channels[3].saturation = -8.0;
  moody_forest.mixer_channels[3].luminance = -12.0;
  presets.push_back({
      QStringLiteral("Moody Forest"),
      QStringLiteral("Landscape Core"),
      QStringLiteral("Landscape"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      moody_forest,
  });

  EditRecipeState bw_clean;
  bw_clean.black_and_white_enabled = true;
  bw_clean.contrast = 16.0;
  bw_clean.highlights = -12.0;
  bw_clean.shadows = 10.0;
  bw_clean.blacks = -16.0;
  bw_clean.texture = 8.0;
  bw_clean.clarity = 10.0;
  bw_clean.black_and_white_mixer = {8.0, 16.0, 12.0, -4.0, 0.0, -12.0, -4.0, 4.0};
  presets.push_back({
      QStringLiteral("Clean Black and White"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_clean,
  });

  EditRecipeState bw_matte = bw_clean;
  bw_matte.contrast = 8.0;
  bw_matte.blacks = -4.0;
  bw_matte.curve_shadows = 18.0;
  bw_matte.curve_darks = 8.0;
  bw_matte.grading_shadows_hue = 0.11;
  bw_matte.grading_shadows_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Matte Black and White"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_matte,
  });

  EditRecipeState bw_grit = bw_clean;
  bw_grit.contrast = 28.0;
  bw_grit.texture = 24.0;
  bw_grit.clarity = 24.0;
  bw_grit.dehaze = 8.0;
  bw_grit.sharpening = 34.0;
  bw_grit.black_and_white_mixer = {-8.0, 8.0, 10.0, -12.0, -6.0, -20.0, -10.0, -4.0};
  presets.push_back({
      QStringLiteral("High Contrast Grit"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_grit,
  });

  EditRecipeState night_sky;
  night_sky.exposure = 0.35;
  night_sky.contrast = 18.0;
  night_sky.highlights = -38.0;
  night_sky.shadows = 42.0;
  night_sky.whites = 12.0;
  night_sky.blacks = -18.0;
  night_sky.temperature = -8.0;
  night_sky.tint = 5.0;
  night_sky.texture = 18.0;
  night_sky.clarity = 14.0;
  night_sky.dehaze = 18.0;
  night_sky.vibrance = 10.0;
  night_sky.color_nr = 28.0;
  night_sky.luminance_nr = 16.0;
  night_sky.grading_shadows_hue = 0.64;
  night_sky.grading_shadows_saturation = 12.0;
  night_sky.grading_highlights_hue = 0.10;
  night_sky.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Night Sky Recovery"),
      QStringLiteral("Low Light"),
      QStringLiteral("Night"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      night_sky,
  });

  EditRecipeState drone_clean;
  drone_clean.exposure = 0.06;
  drone_clean.contrast = 14.0;
  drone_clean.highlights = -32.0;
  drone_clean.shadows = 20.0;
  drone_clean.whites = 10.0;
  drone_clean.blacks = -12.0;
  drone_clean.temperature = -3.0;
  drone_clean.texture = 12.0;
  drone_clean.clarity = 14.0;
  drone_clean.dehaze = 10.0;
  drone_clean.vibrance = 18.0;
  drone_clean.chromatic_aberration = 55.0;
  drone_clean.lens_vignette = -10.0;
  drone_clean.mixer_channels[3].saturation = 8.0;
  drone_clean.mixer_channels[5].saturation = 12.0;
  presets.push_back({
      QStringLiteral("Aerial Clean Color"),
      QStringLiteral("Drone Core"),
      QStringLiteral("Drone"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail"), QStringLiteral("lens")},
      {},
      drone_clean,
  });

  EditRecipeState raw_recovery;
  raw_recovery.raw_input_transform_enabled = true;
  raw_recovery.raw_highlight_recovery = true;
  raw_recovery.highlight_reconstruction_mode = QStringLiteral("Enabled");
  raw_recovery.raw_exposure = -0.35;
  raw_recovery.raw_false_color_suppression = 12.0;
  raw_recovery.texture = 16.0;
  raw_recovery.color_nr = 18.0;
  raw_recovery.highlights = -48.0;
  raw_recovery.shadows = 34.0;
  raw_recovery.whites = -6.0;
  raw_recovery.blacks = 8.0;
  raw_recovery.hdr_local_tone_mapping = 22.0;
  raw_recovery.filmic_shoulder = 20.0;
  raw_recovery.filmic_toe = 8.0;
  presets.push_back({
      QStringLiteral("RAW Highlight Recovery"),
      QStringLiteral("RAW Recovery"),
      QStringLiteral("RAW Recovery"),
      {QStringLiteral("raw"), QStringLiteral("tone"), QStringLiteral("detail")},
      {},
      raw_recovery,
  });

  EditRecipeState hdr_natural;
  hdr_natural.exposure = -0.04;
  hdr_natural.contrast = 8.0;
  hdr_natural.highlights = -52.0;
  hdr_natural.shadows = 36.0;
  hdr_natural.whites = -8.0;
  hdr_natural.blacks = 6.0;
  hdr_natural.hdr_local_tone_mapping = 38.0;
  hdr_natural.filmic_shoulder = 28.0;
  hdr_natural.filmic_toe = 12.0;
  hdr_natural.adaptive_contrast = 12.0;
  hdr_natural.texture = 8.0;
  hdr_natural.clarity = 6.0;
  hdr_natural.vibrance = 10.0;
  presets.push_back({
      QStringLiteral("Natural HDR Balance"),
      QStringLiteral("Dynamic Range"),
      QStringLiteral("HDR"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      hdr_natural,
  });

  return presets;
}
// PHOTO_EDITOR_BUILT_IN_PRESET_REGISTRY_END

}  // namespace


QMap<QString, EditRecipeState> built_in_preset_map() {
  QMap<QString, EditRecipeState> presets;
  for (const auto& preset : built_in_preset_definitions()) {
    presets.insert(preset.name, preset.state);
  }
  return presets;
}

QMap<QString, QString> built_in_preset_pack_map() {
  QMap<QString, QString> packs;
  for (const auto& preset : built_in_preset_definitions()) {
    packs.insert(preset.name, preset.pack);
  }
  return packs;
}

QMap<QString, QString> built_in_preset_category_map() {
  QMap<QString, QString> categories;
  for (const auto& preset : built_in_preset_definitions()) {
    categories.insert(preset.name, preset.category);
  }
  return categories;
}

QMap<QString, QStringList> built_in_preset_group_map() {
  QMap<QString, QStringList> groups;
  for (const auto& preset : built_in_preset_definitions()) {
    groups.insert(preset.name, preset.adjustment_groups);
  }
  return groups;
}

QMap<QString, QJsonArray> built_in_preset_adaptive_rule_map() {
  QMap<QString, QJsonArray> rules;
  for (const auto& preset : built_in_preset_definitions()) {
    if (!preset.adaptive_rules.isEmpty()) {
      rules.insert(preset.name, preset.adaptive_rules);
    }
  }
  return rules;
}

}  // namespace photo_editor::desktop

