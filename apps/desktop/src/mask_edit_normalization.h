#pragma once

#include "edit_recipe_mask_schema.h"

#include <QHash>

namespace photo_editor::desktop {

// Previous definitions come from the normalized live stack. Reuse its canonical
// samples only when both representations are unchanged; changed/imported geometry
// still goes through the ordinary normalizer.
inline QVariantMap normalized_mask_edit_definition(
    const QVariantMap& candidate, const QVariantMap& previous, int ordinal) {
  if (candidate == previous) return previous;
  const auto reuse_brush_geometry =
      candidate.value(QStringLiteral("kind")).toString() == QStringLiteral("brush") &&
      previous.value(QStringLiteral("kind")).toString() == QStringLiteral("brush") &&
      candidate.value(QStringLiteral("brushPoints")) == previous.value(QStringLiteral("brushPoints")) &&
      candidate.value(QStringLiteral("brushStrokes")) == previous.value(QStringLiteral("brushStrokes"));
  if (!reuse_brush_geometry) return normalized_mask_definition(candidate, ordinal);

  auto scalar_candidate = candidate;
  scalar_candidate.remove(QStringLiteral("brushPoints"));
  scalar_candidate.remove(QStringLiteral("brushStrokes"));
  auto normalized = normalized_mask_definition(scalar_candidate, ordinal);
  normalized.insert(QStringLiteral("brushPoints"), previous.value(QStringLiteral("brushPoints")));
  normalized.insert(QStringLiteral("brushStrokes"), previous.value(QStringLiteral("brushStrokes")));
  return normalized;
}

inline QVariantList normalized_mask_edit_stack(
    const QVariantList& stack, const QVariantList& previous_stack) {
  QHash<QString, QVariantMap> previous_by_id;
  for (const auto& value : previous_stack) {
    const auto mask = value.toMap();
    previous_by_id.insert(mask.value(QStringLiteral("id")).toString(), mask);
  }
  QVariantList normalized;
  normalized.reserve(stack.size());
  auto ordinal = 1;
  auto retouch_order = 1;
  for (const auto& value : stack) {
    const auto candidate = value.toMap();
    const auto previous = previous_by_id.constFind(candidate.value(QStringLiteral("id")).toString());
    auto mask = previous == previous_by_id.cend()
        ? normalized_mask_definition(candidate, ordinal)
        : normalized_mask_edit_definition(candidate, *previous, ordinal);
    mask.insert(QStringLiteral("retouchOrder"),
        mask.value(QStringLiteral("toolRole")).toString() == QStringLiteral("retouch")
            ? retouch_order++ : 0);
    normalized.append(mask);
    ++ordinal;
  }
  return normalized;
}

}  // namespace photo_editor::desktop
