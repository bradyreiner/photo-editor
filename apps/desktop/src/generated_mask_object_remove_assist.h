#pragma once

#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>

class QImage;
class QString;

namespace photo_editor::desktop {

struct ObjectRemoveExpansionResult final {
  QByteArray data;
  int original_pixels = 0;
  int expanded_pixels = 0;
  int added_pixels = 0;
  int edge_radius_px = 0;
  int shadow_reflection_pixels = 0;
  int shadow_reflection_extent_px = 0;
  bool supported = false;
};

[[nodiscard]] ObjectRemoveExpansionResult
expand_object_remove_mask_for_shadow_reflection(
    const QByteArray& source_data,
    int width,
    int height);

[[nodiscard]] bool apply_object_remove_assisted_expansion(
    QVariantMap* mask,
    const QImage& source_image,
    const QString& runtime_label,
    const QVariantList& prompts);

}  // namespace photo_editor::desktop
