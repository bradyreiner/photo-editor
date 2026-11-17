#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace photo_editor::desktop::recipe_journal_delta {
inline bool has_large_value(const QJsonValue& value) {
  if (value.isString()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return value.toStringView().size() >= 64 * 1024;
#else
    return value.toString().size() >= 64 * 1024;
#endif
  }
  if (value.isArray()) {
    for (const auto& child : value.toArray()) if (has_large_value(child)) return true;
  } else if (value.isObject()) {
    const auto object = value.toObject();
    for (auto it = object.begin(); it != object.end(); ++it)
      if (has_large_value(it.value())) return true;
  }
  return false;
}
// Typed nodes distinguish an object edit from replacement by a JSON object.
inline QJsonObject between(const QJsonValue& before, const QJsonValue& after, int depth = 0, bool* large_edit = nullptr) {
  if (large_edit && *large_edit) return {};
  if (before == after) return {};
  if (depth >= 128) return {{"replace", after}};
  if (before.isObject() && after.isObject()) {
    const auto old_object = before.toObject();
    const auto new_object = after.toObject();
    QJsonObject edits;
    QJsonArray removed;
    for (auto it = old_object.begin(); it != old_object.end(); ++it)
      if (!new_object.contains(it.key())) removed.append(it.key());
    for (auto it = new_object.begin(); it != new_object.end(); ++it) {
      const auto edit = between(old_object.value(it.key()), it.value(), depth + 1, large_edit);
      if (!edit.isEmpty()) edits.insert(it.key(), edit);
    }
    return {{"object", edits}, {"remove", removed}};
  }
  if (before.isArray() && after.isArray() && before.toArray().size() == after.toArray().size()) {
    const auto old_array = before.toArray();
    const auto new_array = after.toArray();
    QJsonObject edits;
    for (qsizetype i = 0; i < new_array.size(); ++i) {
      const auto edit = between(old_array.at(i), new_array.at(i), depth + 1, large_edit);
      if (!edit.isEmpty()) edits.insert(QString::number(i), edit);
    }
    return {{"array", edits}};
  }
  if (large_edit && has_large_value(after)) { *large_edit = true; return {}; }
  return {{"replace", after}};
}

inline bool apply(QJsonValue* value, const QJsonObject& edit, int depth = 0) {
  if (depth > 128) return false;
  if (edit.size() == 1 && edit.contains("replace")) {
    *value = edit.value("replace");
    return true;
  }
  if (edit.size() == 2 && edit.value("object").isObject() &&
      edit.value("remove").isArray() && value->isObject()) {
    auto object = value->toObject();
    for (const auto& key : edit.value("remove").toArray()) {
      if (!key.isString() || !object.contains(key.toString())) return false;
      object.remove(key.toString());
    }
    const auto edits = edit.value("object").toObject();
    for (auto it = edits.begin(); it != edits.end(); ++it) {
      if (!it.value().isObject()) return false;
      auto child = object.value(it.key());
      if (!apply(&child, it.value().toObject(), depth + 1)) return false;
      object.insert(it.key(), child);
    }
    *value = object;
    return true;
  }
  if (edit.size() == 1 && edit.value("array").isObject() && value->isArray()) {
    auto array = value->toArray();
    const auto edits = edit.value("array").toObject();
    for (auto it = edits.begin(); it != edits.end(); ++it) {
      bool ok = false;
      const auto index = it.key().toLongLong(&ok);
      if (!ok || index < 0 || index >= array.size() || QString::number(index) != it.key() ||
          !it.value().isObject()) return false;
      auto child = array.at(static_cast<qsizetype>(index));
      if (!apply(&child, it.value().toObject(), depth + 1)) return false;
      array[static_cast<qsizetype>(index)] = child;
    }
    *value = array;
    return true;
  }
  return false;
}
}  // namespace photo_editor::desktop::recipe_journal_delta
