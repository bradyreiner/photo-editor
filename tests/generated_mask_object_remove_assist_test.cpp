#include "generated_mask_object_remove_assist.h"

#include <cstdlib>
#include <iostream>

#include <QImage>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main() {
  using namespace photo_editor::desktop;

  if (expand_object_remove_mask_for_shadow_reflection({}, 0, 0).supported) {
    return fail("invalid object-removal alpha must fail closed");
  }
  const QByteArray empty(49, char(0));
  if (expand_object_remove_mask_for_shadow_reflection(empty, 7, 7).supported) {
    return fail("an empty object-removal alpha must not manufacture a target");
  }

  QByteArray source(49, char(0));
  source[3 * 7 + 3] = char(0xff);
  const auto expansion =
      expand_object_remove_mask_for_shadow_reflection(source, 7, 7);
  if (!expansion.supported || expansion.original_pixels != 1 ||
      expansion.edge_radius_px != 1 ||
      expansion.shadow_reflection_extent_px != 1 ||
      expansion.expanded_pixels != 7 || expansion.added_pixels != 6 ||
      expansion.shadow_reflection_pixels != 2 ||
      expansion.data.at(3 * 7 + 3) != char(0xff)) {
    return fail("object-removal expansion must preserve the source and add exact bounded support");
  }
  const auto repeated =
      expand_object_remove_mask_for_shadow_reflection(source, 7, 7);
  if (repeated.data != expansion.data ||
      repeated.expanded_pixels != expansion.expanded_pixels ||
      repeated.shadow_reflection_pixels !=
          expansion.shadow_reflection_pixels) {
    return fail("object-removal expansion must be deterministic");
  }

  QVariantMap mask{
      {QStringLiteral("segmentationData"), source},
      {QStringLiteral("segmentationWidth"), 7},
      {QStringLiteral("segmentationHeight"), 7},
      {QStringLiteral("generatedMaskMetadata"),
       QVariantMap{{QStringLiteral("preserved"), true}}},
  };
  QImage source_image(2, 2, QImage::Format_ARGB32);
  source_image.fill(Qt::black);
  if (!apply_object_remove_assisted_expansion(
          &mask,
          source_image,
          QStringLiteral("  Vision Runtime  "),
          QVariantList{QStringLiteral("shoe"), QStringLiteral("shadow")})) {
    return fail("valid object-removal alpha must apply its assisted expansion");
  }
  const auto metadata =
      mask.value(QStringLiteral("generatedMaskMetadata")).toMap();
  if (!mask.value(QStringLiteral("objectRemoveAssisted")).toBool() ||
      mask.value(QStringLiteral("objectRemoveRuntime")).toString() !=
          QStringLiteral("Vision Runtime") ||
      mask.value(QStringLiteral("objectRemoveExpansionPixels")).toInt() != 6 ||
      metadata.value(QStringLiteral("preserved")).toBool() != true ||
      metadata.value(QStringLiteral("objectRemovePromptCount")).toInt() != 2 ||
      metadata.value(QStringLiteral("objectRemoveSourceImageAvailable")).toBool() !=
          true ||
      metadata.value(QStringLiteral("objectRemoveSourceResolution")).toString() !=
          QStringLiteral("7x7")) {
    return fail("assisted expansion must publish complete, additive provenance");
  }

  QVariantMap invalid_mask;
  if (apply_object_remove_assisted_expansion(
          &invalid_mask, QImage{}, QString{}, QVariantList{})) {
    return fail("invalid assisted-expansion mutation must fail closed");
  }
  return EXIT_SUCCESS;
}
