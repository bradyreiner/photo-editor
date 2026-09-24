#pragma once

#include "edit_adjustment_patch.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

namespace photo_editor::desktop {

inline constexpr qsizetype maximum_photo_recipe_clipboard_bytes = 64 * 1024 * 1024;
inline constexpr auto photo_recipe_mime_type = "application/x-framehouse-photo-edits-v1";

inline QString photo_recipe_clipboard_patch() {
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) return {};
  const auto* mime = QGuiApplication::clipboard()->mimeData();
  if (!mime || !mime->hasFormat(photo_recipe_mime_type)) return {};
  const auto bytes = mime->data(photo_recipe_mime_type);
  if (bytes.isEmpty() || bytes.size() > maximum_photo_recipe_clipboard_bytes) return {};
  const auto json = QString::fromUtf8(bytes);
  const auto patch = parse_adjustment_patch_json(json);
  if (!patch.ok() || patch.scope != AdjustmentPatchScope::SourceAgnostic) return {};
  return json;
}

inline bool copy_photo_recipe(const QString& payload, const QStringList& requested_groups) {
  if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance()) || requested_groups.isEmpty())
    return false;
  const QStringList allowed{"tone", "color", "detail", "crop", "raw", "masks", "layers"};
  QStringList groups;
  for (const auto& group : requested_groups) {
    if (!allowed.contains(group)) return false;
    if (!groups.contains(group)) groups.append(group);
  }
  // Masks and their adjustment layers are one transferable unit.
  if (groups.contains("masks") || groups.contains("layers")) {
    if (!groups.contains("masks")) groups.append("masks");
    if (!groups.contains("layers")) groups.append("layers");
  }
  EditRecipeState state;
  if (!payload.trimmed().isEmpty()) {
    const auto parsed = parse_adjustment_payload_or_legacy_recipe(payload);
    if (!parsed.ok()) return false;
    state = *parsed.state;
  }
  const auto json = serialize_adjustment_patch_json(state, groups, AdjustmentPatchScope::SourceAgnostic);
  const auto bytes = json.toUtf8();
  if (bytes.size() > maximum_photo_recipe_clipboard_bytes) return false;
  auto* mime = new QMimeData;
  mime->setData(photo_recipe_mime_type, bytes);
  mime->setText(json);
  QGuiApplication::clipboard()->setMimeData(mime);
  return true;
}

}  // namespace photo_editor::desktop
