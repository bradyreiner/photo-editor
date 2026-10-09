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
  presets.reserve(82);

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
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
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

  // Cinematic and night looks use conventional tonal/color controls only.
  EditRecipeState teal_amber = night_sky;
  teal_amber.exposure = -0.08;
  teal_amber.contrast = 26.0;
  teal_amber.shadows = -8.0;
  teal_amber.blacks = -20.0;
  teal_amber.temperature = 3.0;
  teal_amber.grading_shadows_hue = 0.55;
  teal_amber.grading_shadows_saturation = 22.0;
  teal_amber.grading_highlights_hue = 0.10;
  teal_amber.grading_highlights_saturation = 20.0;
  presets.push_back({
      QStringLiteral("Teal and Amber Cinema"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Cinematic"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      teal_amber,
  });

  EditRecipeState neon_afterglow = teal_amber;
  neon_afterglow.exposure = -0.18;
  neon_afterglow.contrast = 32.0;
  neon_afterglow.vibrance = 30.0;
  neon_afterglow.saturation = 6.0;
  neon_afterglow.grading_shadows_hue = 0.78;
  neon_afterglow.grading_shadows_saturation = 18.0;
  neon_afterglow.grading_highlights_hue = 0.91;
  neon_afterglow.grading_highlights_saturation = 22.0;
  presets.push_back({
      QStringLiteral("Neon Afterglow"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Night"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      neon_afterglow,
  });

  EditRecipeState blue_hour = night_sky;
  blue_hour.exposure = 0.22;
  blue_hour.temperature = -14.0;
  blue_hour.tint = 3.0;
  blue_hour.grading_shadows_hue = 0.62;
  blue_hour.grading_shadows_saturation = 18.0;
  blue_hour.grading_highlights_hue = 0.60;
  blue_hour.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Blue Hour Glow"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Night"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      blue_hour,
  });

  EditRecipeState tungsten_noir = bw_grit;
  tungsten_noir.black_and_white_enabled = false;
  tungsten_noir.temperature = 12.0;
  tungsten_noir.tint = 4.0;
  tungsten_noir.contrast = 24.0;
  tungsten_noir.grading_shadows_hue = 0.62;
  tungsten_noir.grading_shadows_saturation = 12.0;
  tungsten_noir.grading_highlights_hue = 0.10;
  tungsten_noir.grading_highlights_saturation = 12.0;
  presets.push_back({
      QStringLiteral("Tungsten Noir"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Night"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      tungsten_noir,
  });

  EditRecipeState cinema_fade = teal_amber;
  cinema_fade.contrast = -8.0;
  cinema_fade.blacks = 14.0;
  cinema_fade.curve_shadows = 18.0;
  cinema_fade.curve_darks = 8.0;
  cinema_fade.saturation = -10.0;
  cinema_fade.grading_shadows_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Cinema Fade"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Cinematic"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      cinema_fade,
  });

  EditRecipeState copper_shadows = teal_amber;
  copper_shadows.temperature = 8.0;
  copper_shadows.grading_shadows_hue = 0.07;
  copper_shadows.grading_shadows_saturation = 16.0;
  copper_shadows.grading_midtones_hue = 0.08;
  copper_shadows.grading_midtones_saturation = 8.0;
  copper_shadows.grading_highlights_hue = 0.12;
  copper_shadows.grading_highlights_saturation = 14.0;
  presets.push_back({
      QStringLiteral("Copper Shadow Drama"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Cinematic"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      copper_shadows,
  });

  EditRecipeState soft_spotlight = clean_portrait;
  soft_spotlight.exposure = 0.12;
  soft_spotlight.highlights = -32.0;
  soft_spotlight.shadows = -4.0;
  soft_spotlight.curve_highlights = 8.0;
  soft_spotlight.grading_shadows_hue = 0.61;
  soft_spotlight.grading_shadows_saturation = 8.0;
  soft_spotlight.grading_highlights_hue = 0.09;
  soft_spotlight.grading_highlights_saturation = 12.0;
  presets.push_back({
      QStringLiteral("Soft Spotlight"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Cinematic"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      soft_spotlight,
  });

  EditRecipeState night_market = neon_afterglow;
  night_market.exposure = 0.10;
  night_market.highlights = -28.0;
  night_market.shadows = 24.0;
  night_market.temperature = 6.0;
  night_market.vibrance = 24.0;
  night_market.grading_shadows_hue = 0.83;
  night_market.grading_highlights_hue = 0.11;
  presets.push_back({
      QStringLiteral("Night Market Lights"),
      QStringLiteral("Cinematic"),
      QStringLiteral("Night"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      night_market,
  });

  EditRecipeState pastel_base = soft_portrait;
  pastel_base.exposure = 0.24;
  pastel_base.contrast = -12.0;
  pastel_base.highlights = -16.0;
  pastel_base.shadows = 18.0;
  pastel_base.whites = 10.0;
  pastel_base.blacks = 12.0;
  pastel_base.texture = -12.0;
  pastel_base.clarity = -10.0;
  pastel_base.vibrance = 12.0;
  pastel_base.saturation = -4.0;
  pastel_base.curve_shadows = 14.0;
  pastel_base.grading_shadows_hue = 0.70;
  pastel_base.grading_shadows_saturation = 7.0;
  pastel_base.grading_highlights_hue = 0.12;
  pastel_base.grading_highlights_saturation = 8.0;

  EditRecipeState airy_linen = pastel_base;
  airy_linen.exposure = 0.38;
  airy_linen.whites = 18.0;
  airy_linen.blacks = 18.0;
  airy_linen.vibrance = 6.0;
  presets.push_back({
      QStringLiteral("Airy Linen"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      airy_linen,
  });

  EditRecipeState peach_window = pastel_base;
  peach_window.temperature = 7.0;
  peach_window.grading_highlights_hue = 0.08;
  peach_window.grading_highlights_saturation = 12.0;
  peach_window.grading_midtones_hue = 0.07;
  peach_window.grading_midtones_saturation = 6.0;
  presets.push_back({
      QStringLiteral("Peach Window Light"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      peach_window,
  });

  EditRecipeState lavender_haze = pastel_base;
  lavender_haze.tint = 5.0;
  lavender_haze.grading_shadows_hue = 0.76;
  lavender_haze.grading_shadows_saturation = 12.0;
  lavender_haze.grading_highlights_hue = 0.82;
  lavender_haze.grading_highlights_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Lavender Haze"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Pastel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      lavender_haze,
  });

  EditRecipeState coastal_pastel = pastel_base;
  coastal_pastel.temperature = -5.0;
  coastal_pastel.grading_shadows_hue = 0.55;
  coastal_pastel.grading_shadows_saturation = 12.0;
  coastal_pastel.grading_highlights_hue = 0.52;
  coastal_pastel.grading_highlights_saturation = 7.0;
  coastal_pastel.mixer_channels[5].saturation = 8.0;
  presets.push_back({
      QStringLiteral("Coastal Pastel"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Pastel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      coastal_pastel,
  });

  EditRecipeState rose_tint = pastel_base;
  rose_tint.tint = 8.0;
  rose_tint.grading_shadows_hue = 0.94;
  rose_tint.grading_shadows_saturation = 8.0;
  rose_tint.grading_highlights_hue = 0.97;
  rose_tint.grading_highlights_saturation = 12.0;
  presets.push_back({
      QStringLiteral("Rose Tint Editorial"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      rose_tint,
  });

  EditRecipeState porcelain = pastel_base;
  porcelain.exposure = 0.30;
  porcelain.contrast = -16.0;
  porcelain.texture = -24.0;
  porcelain.clarity = -16.0;
  porcelain.saturation = -8.0;
  porcelain.grading_highlights_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Porcelain Bloom"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      porcelain,
  });

  EditRecipeState buttercream = pastel_base;
  buttercream.temperature = 10.0;
  buttercream.tint = 2.0;
  buttercream.grading_highlights_hue = 0.13;
  buttercream.grading_highlights_saturation = 10.0;
  buttercream.grading_shadows_hue = 0.09;
  presets.push_back({
      QStringLiteral("Buttercream Morning"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      buttercream,
  });

  EditRecipeState muted_bloom = pastel_base;
  muted_bloom.exposure = 0.16;
  muted_bloom.contrast = -4.0;
  muted_bloom.vibrance = 2.0;
  muted_bloom.saturation = -12.0;
  muted_bloom.grading_shadows_hue = 0.63;
  muted_bloom.grading_highlights_hue = 0.95;
  presets.push_back({
      QStringLiteral("Muted Flower Market"),
      QStringLiteral("Soft Editorial"),
      QStringLiteral("Pastel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      muted_bloom,
  });

  EditRecipeState analog_base;
  analog_base.exposure = 0.06;
  analog_base.contrast = -6.0;
  analog_base.highlights = -22.0;
  analog_base.shadows = 14.0;
  analog_base.whites = -4.0;
  analog_base.blacks = 14.0;
  analog_base.saturation = -8.0;
  analog_base.grading_shadows_hue = 0.57;
  analog_base.grading_shadows_saturation = 8.0;
  analog_base.grading_highlights_hue = 0.10;
  analog_base.grading_highlights_saturation = 10.0;
  analog_base.lens_vignette = -12.0;

  EditRecipeState warm_negative = analog_base;
  warm_negative.temperature = 8.0;
  warm_negative.grading_shadows_hue = 0.08;
  warm_negative.grading_highlights_hue = 0.12;
  presets.push_back({
      QStringLiteral("Warm Negative"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      warm_negative,
  });

  EditRecipeState faded_seventies = analog_base;
  faded_seventies.temperature = 6.0;
  faded_seventies.blacks = 24.0;
  faded_seventies.vibrance = -8.0;
  faded_seventies.grading_shadows_hue = 0.13;
  faded_seventies.grading_shadows_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Faded Seventies"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      faded_seventies,
  });

  EditRecipeState instant_cream = analog_base;
  instant_cream.exposure = 0.20;
  instant_cream.contrast = -14.0;
  instant_cream.temperature = 11.0;
  instant_cream.blacks = 20.0;
  instant_cream.grading_highlights_saturation = 14.0;
  presets.push_back({
      QStringLiteral("Instant Cream"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      instant_cream,
  });

  EditRecipeState cross_process = analog_base;
  cross_process.contrast = 16.0;
  cross_process.saturation = 10.0;
  cross_process.mixer_channels[0].hue = -8.0;
  cross_process.mixer_channels[4].hue = 12.0;
  cross_process.grading_shadows_hue = 0.52;
  cross_process.grading_shadows_saturation = 14.0;
  cross_process.grading_highlights_hue = 0.11;
  cross_process.grading_highlights_saturation = 16.0;
  presets.push_back({
      QStringLiteral("Cross Process Color"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      cross_process,
  });

  EditRecipeState dusty_rose = analog_base;
  dusty_rose.temperature = 4.0;
  dusty_rose.blacks = 22.0;
  dusty_rose.grading_shadows_hue = 0.91;
  dusty_rose.grading_shadows_saturation = 10.0;
  dusty_rose.grading_highlights_hue = 0.96;
  dusty_rose.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Dusty Rose Matte"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Matte"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      dusty_rose,
  });

  EditRecipeState soft_chrome = analog_base;
  soft_chrome.contrast = 10.0;
  soft_chrome.saturation = -16.0;
  soft_chrome.texture = 12.0;
  soft_chrome.clarity = 8.0;
  soft_chrome.grading_shadows_saturation = 4.0;
  soft_chrome.grading_highlights_saturation = 4.0;
  presets.push_back({
      QStringLiteral("Soft Chrome"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      soft_chrome,
  });

  EditRecipeState expired_film = analog_base;
  expired_film.exposure = 0.12;
  expired_film.temperature = -4.0;
  expired_film.tint = 7.0;
  expired_film.blacks = 26.0;
  expired_film.grading_shadows_hue = 0.78;
  expired_film.grading_shadows_saturation = 10.0;
  expired_film.grading_highlights_hue = 0.10;
  expired_film.grading_highlights_saturation = 6.0;
  presets.push_back({
      QStringLiteral("Expired Film Shift"),
      QStringLiteral("Analog Color"),
      QStringLiteral("Analog"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      expired_film,
  });

  EditRecipeState bw_silver = bw_clean;
  bw_silver.contrast = 22.0;
  bw_silver.highlights = -24.0;
  bw_silver.shadows = 18.0;
  bw_silver.texture = 12.0;
  bw_silver.clarity = 12.0;
  bw_silver.black_and_white_mixer = {12.0, 20.0, 14.0, -6.0, -4.0, -14.0, -8.0, 6.0};
  presets.push_back({
      QStringLiteral("Silver Gelatin"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_silver,
  });

  EditRecipeState bw_fog = bw_matte;
  bw_fog.exposure = 0.18;
  bw_fog.contrast = -10.0;
  bw_fog.blacks = 20.0;
  bw_fog.shadows = 20.0;
  bw_fog.dehaze = -4.0;
  presets.push_back({
      QStringLiteral("Foggy Monochrome"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_fog,
  });

  EditRecipeState bw_noir = bw_grit;
  bw_noir.contrast = 38.0;
  bw_noir.blacks = -26.0;
  bw_noir.shadows = -18.0;
  bw_noir.clarity = 16.0;
  presets.push_back({
      QStringLiteral("Noir Spotlight"),
      QStringLiteral("Monochrome"),
      QStringLiteral("Black and White"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      bw_noir,
  });

  EditRecipeState travel_base = landscape_pop;
  travel_base.exposure = 0.06;
  travel_base.contrast = 12.0;
  travel_base.highlights = -30.0;
  travel_base.shadows = 20.0;
  travel_base.texture = 12.0;
  travel_base.clarity = 10.0;
  travel_base.dehaze = 8.0;
  travel_base.vibrance = 16.0;

  EditRecipeState alpine_clarity = travel_base;
  alpine_clarity.temperature = -6.0;
  alpine_clarity.whites = 18.0;
  alpine_clarity.dehaze = 16.0;
  alpine_clarity.mixer_channels[4].saturation = 8.0;
  presets.push_back({
      QStringLiteral("Alpine Clarity"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      alpine_clarity,
  });

  EditRecipeState tropical_jade = travel_base;
  tropical_jade.temperature = -3.0;
  tropical_jade.vibrance = 24.0;
  tropical_jade.mixer_channels[3].hue = -10.0;
  tropical_jade.mixer_channels[3].saturation = 18.0;
  tropical_jade.mixer_channels[4].saturation = 14.0;
  presets.push_back({
      QStringLiteral("Tropical Jade"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      tropical_jade,
  });

  EditRecipeState desert_amber = travel_base;
  desert_amber.temperature = 10.0;
  desert_amber.tint = 2.0;
  desert_amber.mixer_channels[0].saturation = 10.0;
  desert_amber.mixer_channels[1].saturation = -8.0;
  desert_amber.grading_highlights_hue = 0.11;
  desert_amber.grading_highlights_saturation = 12.0;
  presets.push_back({
      QStringLiteral("Desert Amber"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Travel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      desert_amber,
  });

  EditRecipeState ocean_depth = travel_base;
  ocean_depth.temperature = -10.0;
  ocean_depth.contrast = 20.0;
  ocean_depth.mixer_channels[5].hue = -8.0;
  ocean_depth.mixer_channels[5].saturation = 16.0;
  ocean_depth.mixer_channels[5].luminance = -10.0;
  ocean_depth.grading_shadows_hue = 0.56;
  ocean_depth.grading_shadows_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Ocean Depth"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      ocean_depth,
  });

  EditRecipeState autumn_trail = travel_base;
  autumn_trail.temperature = 7.0;
  autumn_trail.mixer_channels[0].saturation = 14.0;
  autumn_trail.mixer_channels[1].hue = -8.0;
  autumn_trail.mixer_channels[1].saturation = 10.0;
  autumn_trail.mixer_channels[3].saturation = -8.0;
  autumn_trail.grading_highlights_hue = 0.10;
  presets.push_back({
      QStringLiteral("Autumn Trail"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      autumn_trail,
  });

  EditRecipeState wildflower_field = travel_base;
  wildflower_field.exposure = 0.20;
  wildflower_field.contrast = 4.0;
  wildflower_field.texture = -4.0;
  wildflower_field.vibrance = 20.0;
  wildflower_field.mixer_channels[2].saturation = 10.0;
  wildflower_field.grading_highlights_hue = 0.91;
  wildflower_field.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Wildflower Field"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      wildflower_field,
  });

  EditRecipeState nordic_mist = travel_base;
  nordic_mist.exposure = 0.14;
  nordic_mist.contrast = -8.0;
  nordic_mist.temperature = -8.0;
  nordic_mist.saturation = -8.0;
  nordic_mist.dehaze = -4.0;
  nordic_mist.grading_shadows_hue = 0.60;
  nordic_mist.grading_shadows_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Nordic Mist"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      nordic_mist,
  });

  EditRecipeState jungle_rich = travel_base;
  jungle_rich.exposure = -0.04;
  jungle_rich.contrast = 18.0;
  jungle_rich.shadows = 8.0;
  jungle_rich.mixer_channels[3].saturation = 22.0;
  jungle_rich.mixer_channels[3].luminance = -8.0;
  jungle_rich.grading_shadows_hue = 0.43;
  jungle_rich.grading_shadows_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Rainforest Rich"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Nature"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      jungle_rich,
  });

  EditRecipeState canyon_glow = travel_base;
  canyon_glow.temperature = 8.0;
  canyon_glow.highlights = -40.0;
  canyon_glow.shadows = 26.0;
  canyon_glow.grading_highlights_hue = 0.08;
  canyon_glow.grading_highlights_saturation = 15.0;
  presets.push_back({
      QStringLiteral("Canyon Glow"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Travel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      canyon_glow,
  });

  EditRecipeState alpine_sunrise = alpine_clarity;
  alpine_sunrise.exposure = 0.16;
  alpine_sunrise.temperature = 5.0;
  alpine_sunrise.grading_highlights_hue = 0.10;
  alpine_sunrise.grading_highlights_saturation = 14.0;
  presets.push_back({
      QStringLiteral("Alpine Sunrise"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Travel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      alpine_sunrise,
  });

  EditRecipeState city_walk = travel_base;
  city_walk.temperature = -2.0;
  city_walk.contrast = 16.0;
  city_walk.saturation = -4.0;
  city_walk.texture = 8.0;
  city_walk.lens_vignette = -8.0;
  city_walk.grading_shadows_hue = 0.58;
  city_walk.grading_shadows_saturation = 6.0;
  presets.push_back({
      QStringLiteral("City Walk"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Travel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      city_walk,
  });

  EditRecipeState coastal_sun = travel_base;
  coastal_sun.exposure = 0.14;
  coastal_sun.temperature = 4.0;
  coastal_sun.highlights = -38.0;
  coastal_sun.vibrance = 18.0;
  coastal_sun.grading_highlights_hue = 0.12;
  coastal_sun.grading_highlights_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Coastal Sun"),
      QStringLiteral("Travel and Nature"),
      QStringLiteral("Travel"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      coastal_sun,
  });

  EditRecipeState fresh_table = landscape_pop;
  fresh_table.exposure = 0.08;
  fresh_table.contrast = 8.0;
  fresh_table.highlights = -24.0;
  fresh_table.temperature = 3.0;
  fresh_table.texture = 8.0;
  fresh_table.clarity = 5.0;
  fresh_table.vibrance = 12.0;
  fresh_table.mixer_channels[1].saturation = 8.0;
  presets.push_back({
      QStringLiteral("Fresh Table"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Food"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      fresh_table,
  });

  EditRecipeState citrus_menu = fresh_table;
  citrus_menu.temperature = 7.0;
  citrus_menu.vibrance = 22.0;
  citrus_menu.mixer_channels[0].saturation = 12.0;
  citrus_menu.mixer_channels[2].saturation = 10.0;
  citrus_menu.grading_highlights_hue = 0.12;
  citrus_menu.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Citrus Menu"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Food"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      citrus_menu,
  });

  EditRecipeState dark_chocolate = fresh_table;
  dark_chocolate.exposure = -0.16;
  dark_chocolate.contrast = 24.0;
  dark_chocolate.shadows = -8.0;
  dark_chocolate.blacks = -20.0;
  dark_chocolate.temperature = 5.0;
  dark_chocolate.saturation = -6.0;
  dark_chocolate.grading_shadows_hue = 0.08;
  dark_chocolate.grading_shadows_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Dark Chocolate"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Food"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      dark_chocolate,
  });

  EditRecipeState porcelain_product = fresh_table;
  porcelain_product.exposure = 0.15;
  porcelain_product.contrast = 10.0;
  porcelain_product.highlights = -30.0;
  porcelain_product.whites = 12.0;
  porcelain_product.texture = 8.0;
  porcelain_product.clarity = 5.0;
  porcelain_product.lens_vignette = -4.0;
  presets.push_back({
      QStringLiteral("Porcelain Product"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Product"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      porcelain_product,
  });

  EditRecipeState glass_chrome = fresh_table;
  glass_chrome.temperature = -2.0;
  glass_chrome.highlights = -22.0;
  glass_chrome.texture = 16.0;
  glass_chrome.clarity = 14.0;
  glass_chrome.dehaze = 3.0;
  glass_chrome.vibrance = 5.0;
  presets.push_back({
      QStringLiteral("Glass and Chrome"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Product"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      glass_chrome,
  });

  EditRecipeState artisan_bakery = fresh_table;
  artisan_bakery.exposure = 0.10;
  artisan_bakery.temperature = 9.0;
  artisan_bakery.tint = 1.0;
  artisan_bakery.contrast = 8.0;
  artisan_bakery.highlights = -20.0;
  artisan_bakery.grading_highlights_hue = 0.11;
  artisan_bakery.grading_highlights_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Artisan Bakery"),
      QStringLiteral("Food and Product"),
      QStringLiteral("Food"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      artisan_bakery,
  });

  EditRecipeState concrete_geometry = landscape_pop;
  concrete_geometry.exposure = 0.06;
  concrete_geometry.contrast = 16.0;
  concrete_geometry.highlights = -28.0;
  concrete_geometry.shadows = 14.0;
  concrete_geometry.saturation = -12.0;
  concrete_geometry.texture = 12.0;
  concrete_geometry.clarity = 16.0;
  concrete_geometry.dehaze = 4.0;
  presets.push_back({
      QStringLiteral("Concrete Geometry"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Architecture"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      concrete_geometry,
  });

  EditRecipeState warm_facade = concrete_geometry;
  warm_facade.temperature = 5.0;
  warm_facade.contrast = 10.0;
  warm_facade.mixer_channels[0].saturation = 8.0;
  warm_facade.mixer_channels[2].saturation = 7.0;
  warm_facade.grading_highlights_hue = 0.10;
  warm_facade.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Warm Facade"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Architecture"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      warm_facade,
  });

  EditRecipeState blue_hour_architecture = concrete_geometry;
  blue_hour_architecture.exposure = 0.12;
  blue_hour_architecture.temperature = -10.0;
  blue_hour_architecture.shadows = 22.0;
  blue_hour_architecture.grading_shadows_hue = 0.62;
  blue_hour_architecture.grading_shadows_saturation = 12.0;
  blue_hour_architecture.grading_highlights_hue = 0.59;
  blue_hour_architecture.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Blue Hour Architecture"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Architecture"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      blue_hour_architecture,
  });

  EditRecipeState night_grid = concrete_geometry;
  night_grid.exposure = 0.26;
  night_grid.contrast = 18.0;
  night_grid.highlights = -40.0;
  night_grid.shadows = 30.0;
  night_grid.blacks = -12.0;
  night_grid.temperature = -5.0;
  night_grid.vibrance = 12.0;
  night_grid.grading_shadows_hue = 0.64;
  night_grid.grading_shadows_saturation = 10.0;
  presets.push_back({
      QStringLiteral("Night Grid"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Urban"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      night_grid,
  });

  EditRecipeState urban_monochrome = bw_clean;
  urban_monochrome.contrast = 22.0;
  urban_monochrome.highlights = -20.0;
  urban_monochrome.shadows = 8.0;
  urban_monochrome.texture = 16.0;
  urban_monochrome.clarity = 14.0;
  urban_monochrome.black_and_white_mixer = {4.0, 8.0, 12.0, -8.0, 0.0, -16.0, -6.0, 4.0};
  presets.push_back({
      QStringLiteral("Urban Monochrome"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Architecture"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      urban_monochrome,
  });

  EditRecipeState clean_interior = clean_portrait;
  clean_interior.exposure = 0.20;
  clean_interior.contrast = -3.0;
  clean_interior.highlights = -24.0;
  clean_interior.shadows = 24.0;
  clean_interior.whites = 12.0;
  clean_interior.texture = 4.0;
  clean_interior.clarity = 3.0;
  clean_interior.vibrance = 4.0;
  presets.push_back({
      QStringLiteral("Clean Interior"),
      QStringLiteral("Architecture and Urban"),
      QStringLiteral("Architecture"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      clean_interior,
  });

  EditRecipeState rose_teal_duotone = teal_amber;
  rose_teal_duotone.exposure = 0.04;
  rose_teal_duotone.contrast = 12.0;
  rose_teal_duotone.grading_shadows_hue = 0.56;
  rose_teal_duotone.grading_shadows_saturation = 24.0;
  rose_teal_duotone.grading_highlights_hue = 0.94;
  rose_teal_duotone.grading_highlights_saturation = 22.0;
  rose_teal_duotone.grading_amount = 70.0;
  presets.push_back({
      QStringLiteral("Rose and Teal Duotone"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Duotone"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      rose_teal_duotone,
  });

  EditRecipeState indigo_amber = teal_amber;
  indigo_amber.exposure = 0.08;
  indigo_amber.grading_shadows_hue = 0.68;
  indigo_amber.grading_shadows_saturation = 20.0;
  indigo_amber.grading_highlights_hue = 0.11;
  indigo_amber.grading_highlights_saturation = 20.0;
  indigo_amber.grading_amount = 65.0;
  presets.push_back({
      QStringLiteral("Indigo and Amber Duotone"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Duotone"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      indigo_amber,
  });

  EditRecipeState lilac_dream = pastel_base;
  lilac_dream.exposure = 0.18;
  lilac_dream.contrast = -8.0;
  lilac_dream.grading_shadows_hue = 0.77;
  lilac_dream.grading_shadows_saturation = 12.0;
  lilac_dream.grading_midtones_hue = 0.84;
  lilac_dream.grading_midtones_saturation = 7.0;
  lilac_dream.grading_highlights_hue = 0.84;
  lilac_dream.grading_highlights_saturation = 9.0;
  presets.push_back({
      QStringLiteral("Lilac Dream"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Dreamy"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      lilac_dream,
  });

  EditRecipeState mint_reverie = pastel_base;
  mint_reverie.temperature = -4.0;
  mint_reverie.grading_shadows_hue = 0.43;
  mint_reverie.grading_shadows_saturation = 12.0;
  mint_reverie.grading_highlights_hue = 0.40;
  mint_reverie.grading_highlights_saturation = 8.0;
  mint_reverie.mixer_channels[3].hue = -6.0;
  mint_reverie.mixer_channels[3].saturation = 8.0;
  presets.push_back({
      QStringLiteral("Mint Reverie"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Dreamy"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      mint_reverie,
  });

  EditRecipeState peach_halation = soft_spotlight;
  peach_halation.exposure = 0.18;
  peach_halation.temperature = 6.0;
  peach_halation.highlights = -34.0;
  peach_halation.grading_highlights_hue = 0.08;
  peach_halation.grading_highlights_saturation = 15.0;
  peach_halation.grading_midtones_hue = 0.06;
  peach_halation.grading_midtones_saturation = 6.0;
  presets.push_back({
      QStringLiteral("Peach Highlights"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Dreamy"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      peach_halation,
  });

  EditRecipeState candy_neon = neon_afterglow;
  candy_neon.exposure = 0.06;
  candy_neon.contrast = 18.0;
  candy_neon.vibrance = 28.0;
  candy_neon.saturation = 4.0;
  candy_neon.grading_shadows_hue = 0.77;
  candy_neon.grading_shadows_saturation = 20.0;
  candy_neon.grading_highlights_hue = 0.92;
  candy_neon.grading_highlights_saturation = 20.0;
  presets.push_back({
      QStringLiteral("Candy Neon"),
      QStringLiteral("Dreamy Color"),
      QStringLiteral("Dreamy"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      candy_neon,
  });

  EditRecipeState glacier_editorial = clean_portrait;
  glacier_editorial.exposure = 0.12;
  glacier_editorial.contrast = 4.0;
  glacier_editorial.highlights = -26.0;
  glacier_editorial.shadows = 12.0;
  glacier_editorial.temperature = -8.0;
  glacier_editorial.tint = 2.0;
  glacier_editorial.texture = -6.0;
  glacier_editorial.clarity = -4.0;
  glacier_editorial.grading_shadows_hue = 0.61;
  glacier_editorial.grading_shadows_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Glacier Editorial"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      glacier_editorial,
  });

  EditRecipeState slate_portrait = clean_portrait;
  slate_portrait.exposure = -0.04;
  slate_portrait.contrast = 16.0;
  slate_portrait.temperature = -4.0;
  slate_portrait.vibrance = 2.0;
  slate_portrait.grading_shadows_hue = 0.60;
  slate_portrait.grading_shadows_saturation = 12.0;
  slate_portrait.grading_midtones_hue = 0.58;
  slate_portrait.grading_midtones_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Slate Portrait"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Portrait"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      slate_portrait,
  });

  EditRecipeState cyan_window = soft_portrait;
  cyan_window.exposure = 0.14;
  cyan_window.temperature = -7.0;
  cyan_window.tint = -2.0;
  cyan_window.grading_shadows_hue = 0.54;
  cyan_window.grading_shadows_saturation = 10.0;
  cyan_window.grading_highlights_hue = 0.53;
  cyan_window.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Cyan Window"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      cyan_window,
  });

  EditRecipeState clean_steel = concrete_geometry;
  clean_steel.exposure = 0.10;
  clean_steel.contrast = 8.0;
  clean_steel.temperature = -5.0;
  clean_steel.saturation = -8.0;
  clean_steel.texture = 10.0;
  clean_steel.clarity = 8.0;
  clean_steel.grading_shadows_hue = 0.60;
  clean_steel.grading_shadows_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Clean Steel"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Product"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      clean_steel,
  });

  EditRecipeState winter_linen = airy_linen;
  winter_linen.exposure = 0.26;
  winter_linen.temperature = -6.0;
  winter_linen.contrast = -10.0;
  winter_linen.whites = 16.0;
  winter_linen.grading_shadows_hue = 0.60;
  winter_linen.grading_shadows_saturation = 5.0;
  presets.push_back({
      QStringLiteral("Winter Linen"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      winter_linen,
  });

  EditRecipeState moonlit_editorial = night_sky;
  moonlit_editorial.exposure = 0.20;
  moonlit_editorial.contrast = 8.0;
  moonlit_editorial.temperature = -12.0;
  moonlit_editorial.shadows = 30.0;
  moonlit_editorial.blacks = -8.0;
  moonlit_editorial.texture = -4.0;
  moonlit_editorial.clarity = -2.0;
  moonlit_editorial.grading_shadows_hue = 0.64;
  moonlit_editorial.grading_shadows_saturation = 14.0;
  presets.push_back({
      QStringLiteral("Moonlit Editorial"),
      QStringLiteral("Cool Editorial"),
      QStringLiteral("Editorial"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      moonlit_editorial,
  });

  EditRecipeState quiet_canvas = analog_base;
  quiet_canvas.exposure = 0.10;
  quiet_canvas.contrast = -10.0;
  quiet_canvas.temperature = 2.0;
  quiet_canvas.saturation = -16.0;
  quiet_canvas.blacks = 18.0;
  quiet_canvas.texture = -4.0;
  quiet_canvas.grading_shadows_saturation = 4.0;
  quiet_canvas.grading_highlights_saturation = 4.0;
  presets.push_back({
      QStringLiteral("Quiet Canvas"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Minimal"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      quiet_canvas,
  });

  EditRecipeState taupe_still_life = quiet_canvas;
  taupe_still_life.temperature = 6.0;
  taupe_still_life.tint = 1.0;
  taupe_still_life.contrast = -4.0;
  taupe_still_life.grading_shadows_hue = 0.10;
  taupe_still_life.grading_highlights_hue = 0.12;
  taupe_still_life.grading_highlights_saturation = 7.0;
  presets.push_back({
      QStringLiteral("Taupe Still Life"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Minimal"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      taupe_still_life,
  });

  EditRecipeState stone_pastel = pastel_base;
  stone_pastel.exposure = 0.08;
  stone_pastel.contrast = -6.0;
  stone_pastel.temperature = -2.0;
  stone_pastel.saturation = -14.0;
  stone_pastel.grading_shadows_hue = 0.59;
  stone_pastel.grading_shadows_saturation = 4.0;
  stone_pastel.grading_highlights_saturation = 4.0;
  presets.push_back({
      QStringLiteral("Stone Pastel"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Minimal"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      stone_pastel,
  });

  EditRecipeState faded_poster = analog_base;
  faded_poster.exposure = 0.10;
  faded_poster.contrast = 8.0;
  faded_poster.saturation = -10.0;
  faded_poster.blacks = 20.0;
  faded_poster.grading_shadows_hue = 0.58;
  faded_poster.grading_shadows_saturation = 6.0;
  faded_poster.grading_highlights_hue = 0.11;
  faded_poster.grading_highlights_saturation = 8.0;
  presets.push_back({
      QStringLiteral("Faded Poster"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Print"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      faded_poster,
  });

  EditRecipeState graphic_ink = bw_grit;
  graphic_ink.contrast = 34.0;
  graphic_ink.blacks = -22.0;
  graphic_ink.texture = 14.0;
  graphic_ink.clarity = 18.0;
  graphic_ink.black_and_white_mixer = {10.0, 12.0, 8.0, -10.0, -2.0, -18.0, -8.0, 2.0};
  presets.push_back({
      QStringLiteral("Graphic Ink"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Print"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      graphic_ink,
  });

  EditRecipeState soft_letterpress = analog_base;
  soft_letterpress.exposure = 0.04;
  soft_letterpress.contrast = 4.0;
  soft_letterpress.texture = 4.0;
  soft_letterpress.saturation = -20.0;
  soft_letterpress.grading_shadows_hue = 0.08;
  soft_letterpress.grading_shadows_saturation = 5.0;
  soft_letterpress.grading_highlights_hue = 0.12;
  soft_letterpress.grading_highlights_saturation = 6.0;
  presets.push_back({
      QStringLiteral("Soft Letterpress"),
      QStringLiteral("Muted Minimal and Print"),
      QStringLiteral("Print"),
      {QStringLiteral("tone"), QStringLiteral("color"), QStringLiteral("detail")},
      {},
      soft_letterpress,
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
