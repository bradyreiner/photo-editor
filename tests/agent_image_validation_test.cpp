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

}  // namespace

int main() {
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

  return EXIT_SUCCESS;
}
