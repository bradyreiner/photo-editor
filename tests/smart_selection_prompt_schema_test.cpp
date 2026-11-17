#include "smart_selection_prompt_schema.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap point(double x, double y, const QString& type = {}) {
  QVariantMap value{
      {QStringLiteral("x"), x},
      {QStringLiteral("y"), y},
  };
  if (!type.isEmpty()) {
    value.insert(QStringLiteral("type"), type);
  }
  return value;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (normalized_smart_select_prompt_type(
          QVariantMap{{QStringLiteral("kind"), QStringLiteral("exclude")}}) !=
          QStringLiteral("negative-point") ||
      normalized_smart_select_prompt_type(
          QVariantMap{{QStringLiteral("type"), QStringLiteral("bbox")}}) !=
          QStringLiteral("box") ||
      normalized_smart_select_prompt_type(
          QVariantMap{{QStringLiteral("type"), QStringLiteral("stroke")}}) !=
          QStringLiteral("brush") ||
      normalized_smart_select_prompt_type({}) !=
          QStringLiteral("positive-point")) {
    return fail("smart-selection prompt aliases must normalize to canonical types");
  }

  const auto clamped = normalized_smart_select_prompt_map(point(-0.2, 1.2));
  if (clamped.value(QStringLiteral("x")).toDouble() != 0.0 ||
      clamped.value(QStringLiteral("y")).toDouble() != 1.0 ||
      clamped.value(QStringLiteral("negative")).toBool()) {
    return fail("point prompts must clamp coordinates and publish polarity");
  }
  const auto box = normalized_smart_select_prompt_map(QVariantMap{
      {QStringLiteral("type"), QStringLiteral("rectangle")},
      {QStringLiteral("left"), 0.8},
      {QStringLiteral("top"), 0.9},
      {QStringLiteral("right"), 0.2},
      {QStringLiteral("bottom"), 0.1},
  });
  if (box.value(QStringLiteral("left")).toDouble() != 0.2 ||
      box.value(QStringLiteral("top")).toDouble() != 0.1 ||
      box.value(QStringLiteral("right")).toDouble() != 0.8 ||
      box.value(QStringLiteral("bottom")).toDouble() != 0.9 ||
      box.value(QStringLiteral("x")).toDouble() != 0.5 ||
      box.value(QStringLiteral("y")).toDouble() != 0.5) {
    return fail("reversed box prompts must normalize to exact bounded geometry");
  }
  if (!normalized_smart_select_prompt_map(
           QVariantMap{{QStringLiteral("type"), QStringLiteral("box")},
                       {QStringLiteral("x"), 0.5},
                       {QStringLiteral("y"), 0.5}})
           .isEmpty() ||
      !normalized_smart_select_prompt_map(
           QVariantMap{{QStringLiteral("type"), QStringLiteral("positive")}})
           .isEmpty()) {
    return fail("degenerate boxes and coordinate-free points must fail closed");
  }

  const QVariantList source_prompts{
      point(0.0, 0.0, QStringLiteral("brush")),
      point(0.1, 0.0, QStringLiteral("brush")),
      point(0.7, 0.7, QStringLiteral("negative")),
      box,
  };
  const auto normalized = normalized_smart_select_prompt_maps(source_prompts);
  const auto path = normalized_smart_select_path(source_prompts);
  if (normalized.size() != 4 || path.size() != 7 ||
      path.constFirst() != QPointF(0.0, 0.0) ||
      path.at(2) != QPointF(0.5, 0.5)) {
    return fail("normalized paths must omit negative prompts and expand box geometry");
  }

  const auto quantized = quantized_smart_select_preview_path(
      QVariantList{point(0.1, 0.1)});
  if (quantized.size() != 1 ||
      quantized.constFirst().toMap().value(QStringLiteral("x")).toDouble() !=
          51.0 / 512.0) {
    return fail("preview prompt paths must use exact 512-step quantization");
  }
  const auto sampled = sampled_smart_select_path_points(
      QVector<QPointF>{QPointF(0.0, 0.0), QPointF(0.1, 0.0)});
  if (sampled.size() != 4 || sampled.constFirst() != QPointF(0.0, 0.0) ||
      std::abs(sampled.constLast().x() - 0.1) > 1e-12) {
    return fail("brush paths must be deterministically sampled below the spacing limit");
  }

  const auto prompts = smart_select_prompts_for_path(source_prompts);
  if (prompts.size() != 6 || prompts.at(4).negative != true ||
      prompts.at(4).x != 0.7 || prompts.at(5).x != 0.5 ||
      prompts.at(5).y != 0.5 ||
      prompts.at(5).box != QRectF(QPointF(0.2, 0.1), QPointF(0.8, 0.9))) {
    return fail("canonical prompts must preserve sampled brush, negative, and box semantics");
  }

  const auto clicks = smart_select_prompts_for_path(
      {point(0.1, 0.5), point(0.9, 0.5), point(0.5, 0.5, QStringLiteral("negative"))});
  if (clicks.size() != 3 || clicks[0].x != 0.1 || clicks[1].x != 0.9 || !clicks[2].negative) {
    return fail("separate foreground clicks must not select the background between them");
  }
  const auto brush = smart_select_prompts_for_path(
      {point(0.1, 0.5, QStringLiteral("brush")), point(0.9, 0.5, QStringLiteral("brush"))});
  if (brush.size() <= 2 || brush.first().x != 0.1 || std::abs(brush.last().x - 0.9) > 1e-12) {
    return fail("explicit brush paths still sample the complete stroke");
  }
  QVariantList long_stroke{box};
  for (int i = 0; i < 12; ++i)
    long_stroke.append(point((i % 2) == 0 ? 0.1 : 0.9, 0.1 + i * 0.06, QStringLiteral("brush")));
  long_stroke.append(point(0.95, 0.95, QStringLiteral("negative")));
  const auto bounded_stroke = smart_select_prompts_for_path(long_stroke);
  if (bounded_stroke.size() != 64 || !bounded_stroke.first().box.isValid() ||
      !bounded_stroke.last().negative || bounded_stroke[1].x != 0.1 ||
      std::abs(bounded_stroke[62].x - 0.9) > 1e-12) {
    return fail("long strokes fit the model budget while preserving box, endpoints, and negative constraints");
  }

  const auto metadata = smart_select_prompt_metadata_for_path(source_prompts);
  if (metadata.value(QStringLiteral("positivePromptCount")).toInt() != 0 ||
      metadata.value(QStringLiteral("negativePromptCount")).toInt() != 1 ||
      metadata.value(QStringLiteral("boxPromptCount")).toInt() != 1 ||
      metadata.value(QStringLiteral("brushPromptCount")).toInt() != 2 ||
      metadata.value(QStringLiteral("smartSelectionPromptSummary")).toString() !=
          QStringLiteral("negative-point,box,brush")) {
    return fail("prompt metadata must publish exact canonical counts and type order");
  }
  QVariantMap mask{
      {QStringLiteral("smartSelectionPrompts"),
       QVariantList{point(0.25, 0.25)}},
      {QStringLiteral("generatedMaskMetadata"),
       QVariantMap{{QStringLiteral("preserved"), true}}},
  };
  if (!merge_smart_select_prompt_metadata(&mask, source_prompts) ||
      mask.value(QStringLiteral("positivePromptCount")).toInt() != 1 ||
      mask.value(QStringLiteral("negativePromptCount")).toInt() != 1 ||
      mask.value(QStringLiteral("boxPromptCount")).toInt() != 1 ||
      mask.value(QStringLiteral("brushPromptCount")).toInt() != 2 ||
      mask.value(QStringLiteral("smartSelectionPrompts")).toList().size() != 5 ||
      !mask.value(QStringLiteral("generatedMaskMetadata"))
           .toMap()
           .value(QStringLiteral("preserved"))
           .toBool() ||
      merge_smart_select_prompt_metadata(nullptr, source_prompts) ||
      merge_smart_select_prompt_metadata(&mask, {})) {
    return fail("prompt metadata merging must be additive, mirrored, and fail closed");
  }
  const auto bounds = bounds_for_normalized_variant_points(QVariantList{
      point(-0.2, 0.25), point(0.8, 1.4), QVariantMap{{QStringLiteral("x"), 0.5}}});
  if (bounds != QRectF(0.0, 0.25, 0.8, 0.75) ||
      bounds_for_normalized_variant_points(QVariantList{point(0.5, 0.5)})
          .isValid() ||
      bounds_for_normalized_variant_points({}).isValid()) {
    return fail("normalized point bounds must clamp, ignore invalid rows, and reject zero area");
  }
  return EXIT_SUCCESS;
}
