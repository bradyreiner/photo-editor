#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QColor>
#include <QImage>
#include <QVariantMap>

#include "agent_image_validation.h"
#include "edit_recipe_state.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QImage solid_image(const QColor& color) {
  QImage image(20, 20, QImage::Format_ARGB32);
  image.fill(color);
  return image;
}

QImage neutral_gradient_image() {
  QImage image(20, 20, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto value = 72 + x * 5 + y * 2;
      image.setPixelColor(x, y, QColor(value, value + 2, value + 4));
    }
  }
  return image;
}

QImage varied_scene_image() {
  QImage image(48, 32, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto hue = (x * 7 + y * 3) % 360;
      const auto saturation = 70 + ((x + y) % 5) * 24;
      const auto value = 65 + ((x * 4 + y * 5) % 145);
      image.setPixelColor(x, y, QColor::fromHsv(hue, saturation, value));
    }
  }
  return image;
}

QImage adjusted_image(
    const QImage& source,
    double contrast_scale,
    int brightness_delta,
    int red_delta,
    int blue_delta,
    double saturation_scale) {
  auto adjusted = source;
  for (int y = 0; y < adjusted.height(); ++y) {
    for (int x = 0; x < adjusted.width(); ++x) {
      auto color = source.pixelColor(x, y);
      const auto adjust_channel = [contrast_scale, brightness_delta](int value) {
        return std::clamp(
            static_cast<int>(std::round(
                128.0 + (static_cast<double>(value) - 128.0) * contrast_scale)) +
                brightness_delta,
            0,
            255);
      };
      color.setRed(std::clamp(adjust_channel(color.red()) + red_delta, 0, 255));
      color.setGreen(adjust_channel(color.green()));
      color.setBlue(std::clamp(adjust_channel(color.blue()) + blue_delta, 0, 255));
      color.setHsv(
          color.hsvHue(),
          std::clamp(
              static_cast<int>(std::round(
                  static_cast<double>(color.hsvSaturation()) * saturation_scale)),
              0,
              255),
          color.value());
      adjusted.setPixelColor(x, y, color);
    }
  }
  return adjusted;
}

QVariantMap image_stats(const QImage& image) {
  return photo_editor::desktop::validate_agent_edit_image(image)
      .value(QStringLiteral("imageStats"))
      .toMap();
}

QVariantMap strict_style() {
  return QVariantMap{
      {QStringLiteral("validation"),
       QVariantMap{
           {QStringLiteral("highlight_clip_limit_percent"), 1.0},
           {QStringLiteral("shadow_clip_limit_percent"), 1.0},
       }},
  };
}

QVariantMap skin_check_style() {
  auto style = strict_style();
  auto validation = style.value(QStringLiteral("validation")).toMap();
  validation.insert(QStringLiteral("require_skin_tone_check"), true);
  style.insert(QStringLiteral("validation"), validation);
  return style;
}

QVariantMap point(double x, double y) {
  return QVariantMap{
      {QStringLiteral("x"), x},
      {QStringLiteral("y"), y},
  };
}

QImage retouch_quality_baseline_image() {
  QImage image(200, 100, QImage::Format_ARGB32);
  for (auto y = 0; y < image.height(); ++y) {
    for (auto x = 0; x < image.width(); ++x) {
      image.setPixelColor(
          x,
          y,
          QColor(40 + x, 80 + y, 120 + x / 4));
    }
  }
  return image;
}

QVariantMap materialized_retouch_quality_patch() {
  return QVariantMap{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("appendRetouchStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("quality-proof-retouch")},
                    {QStringLiteral("agentRegion"),
                     QVariantMap{
                         {QStringLiteral("centerX"), 0.5},
                         {QStringLiteral("centerY"), 0.5},
                         {QStringLiteral("radiusX"), 0.1},
                         {QStringLiteral("radiusY"), 0.1},
                     }},
                    {QStringLiteral("segmentationWidth"), 200},
                    {QStringLiteral("segmentationHeight"), 100},
                    {QStringLiteral("inpaintSourceBoundsX"), 70},
                    {QStringLiteral("inpaintSourceBoundsY"), 35},
                    {QStringLiteral("inpaintSourceBoundsWidth"), 60},
                    {QStringLiteral("inpaintSourceBoundsHeight"), 30},
                },
            }},
       }},
  };
}

}  // namespace

int main() {
  const auto retouch_baseline = retouch_quality_baseline_image();
  auto clean_retouch = retouch_baseline;
  for (auto y = 40; y < 60; ++y) {
    for (auto x = 80; x < 120; ++x) {
      const auto normalized_x =
          (static_cast<double>(x) + 0.5 - 100.0) / 20.0;
      const auto normalized_y =
          (static_cast<double>(y) + 0.5 - 50.0) / 10.0;
      if (normalized_x * normalized_x + normalized_y * normalized_y > 1.0) {
        continue;
      }
      const auto before = retouch_baseline.pixelColor(x, y);
      clean_retouch.setPixelColor(
          x,
          y,
          QColor(before.red() + 4, before.green() + 4, before.blue() + 4));
    }
  }
  const auto clean_retouch_quality =
      photo_editor::desktop::evaluate_agent_retouch_proof_quality(
          retouch_baseline,
          clean_retouch,
          materialized_retouch_quality_patch());
  if (!clean_retouch_quality.value(QStringLiteral("required")).toBool() ||
      !clean_retouch_quality.value(QStringLiteral("available")).toBool() ||
      !clean_retouch_quality.value(QStringLiteral("ok")).toBool() ||
      !clean_retouch_quality.value(QStringLiteral("backgroundContinuity"))
           .toBool()) {
    return fail("retouch quality proof should accept a bounded, texture-preserving reconstruction");
  }

  auto haloed_retouch = clean_retouch;
  for (auto y = 35; y < 65; ++y) {
    for (auto x = 70; x < 130; ++x) {
      const auto normalized_x =
          (static_cast<double>(x) + 0.5 - 100.0) / 20.0;
      const auto normalized_y =
          (static_cast<double>(y) + 0.5 - 50.0) / 10.0;
      const auto distance_squared =
          normalized_x * normalized_x + normalized_y * normalized_y;
      if (distance_squared <= 1.0 || distance_squared > 2.25) {
        continue;
      }
      const auto before = retouch_baseline.pixelColor(x, y);
      const auto offset = (x + y) % 2 == 0 ? 10 : -10;
      haloed_retouch.setPixelColor(
          x,
          y,
          QColor(before.red() + offset,
                 before.green() + offset,
                 before.blue() + offset));
    }
  }
  const auto haloed_retouch_quality =
      photo_editor::desktop::evaluate_agent_retouch_proof_quality(
          retouch_baseline,
          haloed_retouch,
          materialized_retouch_quality_patch());
  if (!haloed_retouch_quality.value(QStringLiteral("available")).toBool() ||
      haloed_retouch_quality.value(QStringLiteral("ok")).toBool() ||
      haloed_retouch_quality.value(QStringLiteral("backgroundContinuity"))
          .toBool() ||
      !haloed_retouch_quality.value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("surrounding color or texture"))) {
    return fail("retouch quality proof should reject a visible halo around the target");
  }

  const auto clean = photo_editor::desktop::validate_agent_edit_image(
      neutral_gradient_image(),
      strict_style());
  if (!clean.value(QStringLiteral("ok")).toBool() ||
      !clean.value(QStringLiteral("warnings")).toStringList().isEmpty()) {
    return fail("clean midtone image should validate without warnings");
  }
  if (clean.value(QStringLiteral("qualityScore")).toDouble() < 0.95 ||
      clean.value(QStringLiteral("qualityScoreReport")).toMap().value(QStringLiteral("needsUserReview")).toBool()) {
    return fail("clean midtone image should receive a high quality score without review");
  }
  const auto clean_stats = clean.value(QStringLiteral("imageStats")).toMap();
  const auto clean_histogram = clean_stats.value(QStringLiteral("histogram")).toMap();
  const auto clean_vectorscope = clean_stats.value(QStringLiteral("vectorscope")).toMap();
  if (clean_stats.value(QStringLiteral("version")).toString() != QStringLiteral("agent-image-stats-v1") ||
      clean_histogram.value(QStringLiteral("binsPercent")).toList().size() != 16 ||
      clean_vectorscope.value(QStringLiteral("hueBinsPercent")).toList().size() != 12 ||
      clean_histogram.value(QStringLiteral("midtonePercent")).toDouble() <= 90.0 ||
      clean_vectorscope.value(QStringLiteral("averageSaturation")).toDouble() <= 0.0) {
    return fail("image validation should expose compact histogram and vectorscope stats for agent decisions");
  }

  const auto clipped = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(255, 245, 245)),
      strict_style());
  if (!clipped.value(QStringLiteral("ok")).toBool() ||
      !clipped.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; ")).contains(
          QStringLiteral("highlight clipping"))) {
    return fail("highlight-clipped image should report highlight warning");
  }
  if (clipped.value(QStringLiteral("qualityScore")).toDouble() >=
          clean.value(QStringLiteral("qualityScore")).toDouble() ||
      !clipped.value(QStringLiteral("qualityScoreReport")).toMap().value(QStringLiteral("needsUserReview")).toBool()) {
    return fail("highlight-clipped image should score lower and require review");
  }

  const auto crushed = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(2, 2, 2)),
      strict_style());
  if (!crushed.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; ")).contains(
          QStringLiteral("shadow clipping"))) {
    return fail("shadow-clipped image should report shadow warning");
  }

  const auto saturated = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(255, 0, 0)),
      strict_style());
  const auto saturated_warnings =
      saturated.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (!saturated_warnings.contains(QStringLiteral("highlight clipping")) ||
      !saturated_warnings.contains(QStringLiteral("extreme saturation"))) {
    return fail("highly saturated clipped image should report both risks");
  }

  const auto flat_histogram = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(118, 119, 120)),
      strict_style());
  if (!flat_histogram.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; ")).contains(
          QStringLiteral("compressed histogram shape"))) {
    return fail("flat midtone image should report compressed histogram shape");
  }
  if (!flat_histogram.value(QStringLiteral("imageStats")).toMap()
           .value(QStringLiteral("histogram")).toMap()
           .value(QStringLiteral("compressed")).toBool()) {
    return fail("image stats should mark compressed histogram shape");
  }

  const auto warm_cast = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(190, 122, 72)),
      strict_style());
  if (!warm_cast.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; ")).contains(
          QStringLiteral("white balance plausibility risk"))) {
    return fail("strong color cast should report white balance plausibility risk");
  }

  const auto plausible_skin = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(184, 126, 92)),
      skin_check_style());
  const auto plausible_skin_warnings =
      plausible_skin.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (!plausible_skin.value(QStringLiteral("skinToneCheckRequired")).toBool() ||
      !plausible_skin.value(QStringLiteral("skinTonePlausible")).toBool() ||
      plausible_skin.value(QStringLiteral("skinToneCandidatePercent")).toDouble() < 90.0 ||
      plausible_skin_warnings.contains(QStringLiteral("skin tone plausibility risk"))) {
    return fail("plausible skin-colored pixels should be measured without skin-tone warning");
  }

  const auto implausible_skin = photo_editor::desktop::validate_agent_edit_image(
      solid_image(QColor(230, 100, 25)),
      skin_check_style());
  const auto implausible_skin_warnings =
      implausible_skin.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (implausible_skin.value(QStringLiteral("skinTonePlausible")).toBool() ||
      implausible_skin.value(QStringLiteral("averageSkinSaturation")).toDouble() <= 0.72 ||
      !implausible_skin_warnings.contains(QStringLiteral("skin tone plausibility risk"))) {
    return fail("implausible orange skin-like pixels should report skin-tone plausibility risk");
  }

  const QImage empty;
  const auto empty_report = photo_editor::desktop::validate_agent_edit_image(empty, strict_style());
  if (empty_report.value(QStringLiteral("ok")).toBool() ||
      !empty_report.value(QStringLiteral("errors")).toStringList().contains(QStringLiteral("image is empty"))) {
    return fail("empty image validation should fail with a clear error");
  }

  photo_editor::desktop::EditRecipeState crop_risk_recipe;
  crop_risk_recipe.crop_rect_x = 0.82;
  crop_risk_recipe.crop_rect_y = 0.10;
  crop_risk_recipe.crop_rect_width = 0.30;
  crop_risk_recipe.crop_rect_height = 0.40;
  crop_risk_recipe.transform_rotation = 8.0;
  const auto crop_risk = photo_editor::desktop::validate_agent_edit_image(
      neutral_gradient_image(),
      crop_risk_recipe,
      strict_style());
  const auto crop_warnings = crop_risk.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (crop_risk.value(QStringLiteral("recipeValidation")).toMap().value(QStringLiteral("cropValid")).toBool() ||
      !crop_warnings.contains(QStringLiteral("crop rectangle")) ||
      !crop_warnings.contains(QStringLiteral("crop/rotation risk"))) {
    return fail("recipe-aware validation should report crop bounds and rotation risks");
  }

  photo_editor::desktop::EditRecipeState mask_risk_recipe;
  QVariantMap tiny_brush;
  tiny_brush.insert(QStringLiteral("id"), QStringLiteral("tiny-brush"));
  tiny_brush.insert(QStringLiteral("kind"), QStringLiteral("brush"));
  tiny_brush.insert(QStringLiteral("name"), QStringLiteral("Tiny Brush"));
  tiny_brush.insert(QStringLiteral("brushPoints"), QVariantList{QVariantMap{
      {QStringLiteral("x"), 0.5},
      {QStringLiteral("y"), 0.5},
      {QStringLiteral("size"), 0.005},
  }});
  QVariantMap leaking_lasso;
  leaking_lasso.insert(QStringLiteral("id"), QStringLiteral("leaking-lasso"));
  leaking_lasso.insert(QStringLiteral("kind"), QStringLiteral("lasso"));
  leaking_lasso.insert(QStringLiteral("name"), QStringLiteral("Nearly Full Frame"));
  leaking_lasso.insert(QStringLiteral("polygonPoints"), QVariantList{
      point(0.0, 0.0),
      point(1.0, 0.0),
      point(1.0, 1.0),
      point(0.0, 1.0),
  });
  mask_risk_recipe.mask_stack = {tiny_brush, leaking_lasso};
  const auto mask_risk = photo_editor::desktop::validate_agent_edit_image(
      neutral_gradient_image(),
      mask_risk_recipe,
      strict_style());
  const auto mask_warnings = mask_risk.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (mask_risk.value(QStringLiteral("maskKnownCoverageCount")).toInt() != 2 ||
      !mask_warnings.contains(QStringLiteral("mask coverage risk")) ||
      !mask_warnings.contains(QStringLiteral("mask leakage risk"))) {
    return fail("recipe-aware validation should report mask coverage and leakage risks");
  }

  photo_editor::desktop::EditRecipeState retouch_risk_recipe;
  QVariantMap remove_retouch;
  remove_retouch.insert(QStringLiteral("id"), QStringLiteral("remove-risk"));
  remove_retouch.insert(QStringLiteral("kind"), QStringLiteral("object"));
  remove_retouch.insert(QStringLiteral("name"), QStringLiteral("Remove Risk"));
  remove_retouch.insert(QStringLiteral("retouchMode"), QStringLiteral("remove"));
  remove_retouch.insert(QStringLiteral("removeStrength"), 80.0);
  retouch_risk_recipe.mask_stack = {remove_retouch};
  const auto retouch_risk = photo_editor::desktop::validate_agent_edit_image(
      neutral_gradient_image(),
      retouch_risk_recipe,
      strict_style());
  const auto retouch_warnings =
      retouch_risk.value(QStringLiteral("warnings")).toStringList().join(QStringLiteral("; "));
  if (retouch_risk.value(QStringLiteral("retouchCount")).toInt() != 1 ||
      retouch_risk.value(QStringLiteral("missingRetouchProvenanceCount")).toInt() != 1 ||
      !retouch_warnings.contains(QStringLiteral("retouch provenance risk"))) {
    return fail("recipe-aware validation should report missing retouch provenance");
  }
  if (retouch_risk.value(QStringLiteral("qualityScore")).toDouble() >=
          clean.value(QStringLiteral("qualityScore")).toDouble() ||
      !retouch_risk.value(QStringLiteral("qualityScoreReport")).toMap().value(QStringLiteral("needsUserReview")).toBool()) {
    return fail("retouch provenance risk should reduce the quality score and require review");
  }

  const auto intent_source = varied_scene_image();
  const auto source_stats = image_stats(intent_source);
  const auto brighter_stats = image_stats(adjusted_image(intent_source, 1.0, 20, 0, 0, 1.0));
  const auto darker_stats = image_stats(adjusted_image(intent_source, 1.0, -20, 0, 0, 1.0));
  const auto stronger_stats = image_stats(adjusted_image(intent_source, 1.35, 0, 0, 0, 1.0));
  const auto softer_stats = image_stats(adjusted_image(intent_source, 0.68, 0, 0, 0, 1.0));
  const auto warmer_stats = image_stats(adjusted_image(intent_source, 1.0, 0, 18, -18, 1.0));
  const auto cooler_stats = image_stats(adjusted_image(intent_source, 1.0, 0, -18, 18, 1.0));
  const auto muted_stats = image_stats(adjusted_image(intent_source, 1.0, 0, 0, 0, 0.45));
  const auto monochrome_stats = image_stats(adjusted_image(intent_source, 1.0, 0, 0, 0, 0.0));

  const auto brighter_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      brighter_stats,
      QStringLiteral("Make it brighter."),
      {{QStringLiteral("brightness"), QStringLiteral("brighter")}});
  if (!brighter_alignment.value(QStringLiteral("available")).toBool() ||
      !brighter_alignment.value(QStringLiteral("ok")).toBool() ||
      brighter_alignment.value(QStringLiteral("version")).toString() !=
          QStringLiteral("agent-pixel-intent-alignment-v1") ||
      brighter_alignment.value(QStringLiteral("checks")).toList().size() != 1 ||
      brighter_alignment.value(QStringLiteral("deltas")).toMap()
              .value(QStringLiteral("averageLuma")).toDouble() <= 1.5) {
    return fail("pixel intent alignment should prove a requested brightness increase");
  }

  const auto opposite_brightness = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      darker_stats,
      QStringLiteral("Make it brighter."));
  if (opposite_brightness.value(QStringLiteral("ok")).toBool() ||
      !opposite_brightness.value(QStringLiteral("issues")).toStringList().join(QStringLiteral("; "))
           .contains(QStringLiteral("requested a brighter image"))) {
    return fail("pixel intent alignment should reject an edit that moves opposite brightness intent");
  }

  const auto darker_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      darker_stats,
      QStringLiteral("Darken the image."));
  const auto stronger_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      stronger_stats,
      QStringLiteral("Use stronger contrast."));
  const auto softer_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      softer_stats,
      QStringLiteral("Use softer contrast."));
  if (!darker_alignment.value(QStringLiteral("ok")).toBool() ||
      !stronger_alignment.value(QStringLiteral("ok")).toBool() ||
      !softer_alignment.value(QStringLiteral("ok")).toBool()) {
    return fail("pixel intent alignment should cover darker and both contrast directions");
  }

  const auto warmer_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      warmer_stats,
      QString(),
      {{QStringLiteral("palette"), QStringLiteral("warm")}});
  const auto cooler_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      cooler_stats,
      QStringLiteral("Make this cooler."));
  const auto opposite_warmth = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      cooler_stats,
      QStringLiteral("Make this warmer."));
  if (!warmer_alignment.value(QStringLiteral("ok")).toBool() ||
      !cooler_alignment.value(QStringLiteral("ok")).toBool() ||
      opposite_warmth.value(QStringLiteral("ok")).toBool()) {
    return fail("pixel intent alignment should prove both warmth directions and reject the opposite result");
  }

  const auto muted_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      muted_stats,
      QStringLiteral("Make the color more muted."));
  const auto monochrome_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      monochrome_stats,
      QStringLiteral("Black and white."),
      {{QStringLiteral("colorMode"), QStringLiteral("monochrome")}});
  const auto quieter_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      softer_stats,
      QStringLiteral("Make it quieter."));
  if (!muted_alignment.value(QStringLiteral("ok")).toBool() ||
      !monochrome_alignment.value(QStringLiteral("ok")).toBool() ||
      !quieter_alignment.value(QStringLiteral("ok")).toBool()) {
    return fail("pixel intent alignment should cover muted, monochrome, and quieter directions");
  }

  const auto preserve_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      source_stats,
      QStringLiteral("Keeping the tone unchanged, preserve the color."));
  const auto broken_preservation = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      warmer_stats,
      QStringLiteral("Preserve the color."));
  if (!preserve_alignment.value(QStringLiteral("ok")).toBool() ||
      broken_preservation.value(QStringLiteral("ok")).toBool()) {
    return fail("pixel intent alignment should enforce explicit preservation constraints");
  }

  const auto vague_alignment = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      brighter_stats,
      QStringLiteral("Make it cinematic."));
  const auto localized_warmth = photo_editor::desktop::evaluate_agent_edit_intent_alignment(
      source_stats,
      warmer_stats,
      QStringLiteral("Warm the skin, but leave everything else alone."),
      {
          {QStringLiteral("palette"), QStringLiteral("warm")},
          {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
          {QStringLiteral("localizedActions"),
           QVariantList{QVariantMap{{QStringLiteral("target"), QStringLiteral("skin")}}}},
      });
  if (vague_alignment.value(QStringLiteral("available")).toBool() ||
      localized_warmth.value(QStringLiteral("available")).toBool()) {
    return fail("pixel intent alignment should not invent global requirements for vague or localized requests");
  }

  return EXIT_SUCCESS;
}
