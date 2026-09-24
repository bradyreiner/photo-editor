#include "generated_mask_object_remove_assist.h"

#include "edit_recipe_mask_schema.h"

#include <algorithm>
#include <cmath>

#include <QImage>
#include <QString>

namespace photo_editor::desktop {
namespace {

int selected_pixel_count(const QByteArray& data) {
  return static_cast<int>(std::count_if(
      data.cbegin(), data.cend(), [](char value) {
        return static_cast<unsigned char>(value) != 0U;
      }));
}

}  // namespace

ObjectRemoveExpansionResult expand_object_remove_mask_for_shadow_reflection(
    const QByteArray& source_data,
    int width,
    int height) {
  ObjectRemoveExpansionResult result;
  if (!valid_segmentation_payload(source_data, width, height)) {
    return result;
  }

  auto left = width;
  auto right = -1;
  auto top = height;
  auto bottom = -1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto alpha =
          static_cast<unsigned char>(source_data.at(y * width + x));
      if (alpha == 0U) {
        continue;
      }
      ++result.original_pixels;
      left = std::min(left, x);
      right = std::max(right, x);
      top = std::min(top, y);
      bottom = std::max(bottom, y);
    }
  }
  if (result.original_pixels <= 0 || left > right || top > bottom) {
    return result;
  }

  result.data = source_data;
  result.supported = true;
  const auto object_width = right - left + 1;
  const auto object_height = bottom - top + 1;
  result.edge_radius_px = std::clamp(
      static_cast<int>(
          std::lround(std::max(object_width, object_height) * 0.045)),
      1,
      std::max(1, std::min(width, height) / 14));
  result.shadow_reflection_extent_px = std::clamp(
      static_cast<int>(std::lround(object_height * 0.22)),
      result.edge_radius_px,
      std::max(result.edge_radius_px, height / 5));

  const auto write_alpha = [&](int x, int y, int alpha, bool shadow_reflection) {
    if (x < 0 || x >= width || y < 0 || y >= height || alpha <= 0) {
      return;
    }
    const auto index = y * width + x;
    const auto current = static_cast<unsigned char>(result.data.at(index));
    const auto next = static_cast<unsigned char>(std::min(255, alpha));
    if (next <= current) {
      return;
    }
    if (current == 0U) {
      ++result.added_pixels;
      if (shadow_reflection) {
        ++result.shadow_reflection_pixels;
      }
    }
    result.data[index] = static_cast<char>(next);
  };

  const auto radius = result.edge_radius_px;
  const auto radius_sq = radius * radius;
  for (int y = top; y <= bottom; ++y) {
    for (int x = left; x <= right; ++x) {
      const auto alpha =
          static_cast<unsigned char>(source_data.at(y * width + x));
      if (alpha == 0U) {
        continue;
      }
      for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
          const auto distance_sq = dx * dx + dy * dy;
          if (distance_sq > radius_sq) {
            continue;
          }
          const auto distance = std::sqrt(static_cast<double>(distance_sq));
          const auto falloff =
              1.0 - std::clamp(
                        distance / static_cast<double>(radius + 1), 0.0, 1.0);
          write_alpha(
              x + dx,
              y + dy,
              std::max(
                  24,
                  static_cast<int>(
                      std::lround(static_cast<double>(alpha) * falloff))),
              false);
        }
      }
    }
  }

  const auto shadow_pad = std::max(radius, object_width / 5);
  for (int y = bottom + 1;
       y <= bottom + result.shadow_reflection_extent_px;
       ++y) {
    const auto t = result.shadow_reflection_extent_px > 0
                       ? static_cast<double>(y - bottom) /
                             static_cast<double>(
                                 result.shadow_reflection_extent_px + 1)
                       : 1.0;
    const auto alpha = static_cast<int>(std::lround(118.0 * (1.0 - t)));
    for (int x = left - shadow_pad; x <= right + shadow_pad; ++x) {
      write_alpha(x, y, alpha, true);
    }
  }

  result.expanded_pixels = selected_pixel_count(result.data);
  result.added_pixels =
      std::max(0, result.expanded_pixels - result.original_pixels);
  return result;
}

bool apply_object_remove_assisted_expansion(
    QVariantMap* mask,
    const QImage& source_image,
    const QString& runtime_label,
    const QVariantList& prompts) {
  if (mask == nullptr) {
    return false;
  }
  const auto width =
      mask->value(QStringLiteral("segmentationWidth")).toInt();
  const auto height =
      mask->value(QStringLiteral("segmentationHeight")).toInt();
  const auto source_data =
      mask->value(QStringLiteral("segmentationData")).toByteArray();
  const auto expansion = expand_object_remove_mask_for_shadow_reflection(
      source_data, width, height);
  if (!expansion.supported ||
      !valid_segmentation_payload(expansion.data, width, height)) {
    return false;
  }

  const auto runtime = runtime_label.trimmed();
  mask->insert(QStringLiteral("segmentationData"), expansion.data);
  mask->insert(QStringLiteral("objectRemoveAssisted"), true);
  mask->insert(QStringLiteral("objectRemoveAssistSchemaVersion"), 1);
  mask->insert(
      QStringLiteral("objectRemoveAssistMode"),
      QStringLiteral("object-plus-shadow-reflection"));
  mask->insert(QStringLiteral("objectRemoveRuntime"), runtime);
  mask->insert(
      QStringLiteral("objectRemoveOriginalPixelCount"),
      expansion.original_pixels);
  mask->insert(
      QStringLiteral("objectRemoveExpandedPixelCount"),
      expansion.expanded_pixels);
  mask->insert(
      QStringLiteral("objectRemoveExpansionPixels"), expansion.added_pixels);
  mask->insert(
      QStringLiteral("objectRemoveExpansionRadiusPx"),
      expansion.edge_radius_px);
  mask->insert(
      QStringLiteral("objectRemoveShadowReflectionPixels"),
      expansion.shadow_reflection_pixels);
  mask->insert(
      QStringLiteral("objectRemoveShadowReflectionExtentPx"),
      expansion.shadow_reflection_extent_px);

  auto metadata =
      mask->value(QStringLiteral("generatedMaskMetadata")).toMap();
  metadata.insert(QStringLiteral("objectRemoveAssisted"), true);
  metadata.insert(QStringLiteral("objectRemoveAssistSchemaVersion"), 1);
  metadata.insert(
      QStringLiteral("objectRemoveAssistMode"),
      QStringLiteral("object-plus-shadow-reflection"));
  metadata.insert(QStringLiteral("objectRemoveRuntime"), runtime);
  metadata.insert(
      QStringLiteral("objectRemoveSourceResolution"),
      QStringLiteral("%1x%2").arg(width).arg(height));
  metadata.insert(
      QStringLiteral("objectRemoveOriginalPixelCount"),
      expansion.original_pixels);
  metadata.insert(
      QStringLiteral("objectRemoveExpandedPixelCount"),
      expansion.expanded_pixels);
  metadata.insert(
      QStringLiteral("objectRemoveExpansionPixels"), expansion.added_pixels);
  metadata.insert(
      QStringLiteral("objectRemoveExpansionRadiusPx"),
      expansion.edge_radius_px);
  metadata.insert(
      QStringLiteral("objectRemoveShadowReflectionPixels"),
      expansion.shadow_reflection_pixels);
  metadata.insert(
      QStringLiteral("objectRemoveShadowReflectionExtentPx"),
      expansion.shadow_reflection_extent_px);
  metadata.insert(
      QStringLiteral("objectRemoveSourceImageAvailable"),
      !source_image.isNull());
  metadata.insert(QStringLiteral("objectRemovePromptCount"), prompts.size());
  mask->insert(QStringLiteral("generatedMaskMetadata"), metadata);
  return true;
}

}  // namespace photo_editor::desktop
