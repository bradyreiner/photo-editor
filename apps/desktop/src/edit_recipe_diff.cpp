#include "edit_recipe_state.h"

#include <algorithm>

namespace photo_editor::desktop {

namespace {

QString recipe_diff_value_summary(const QJsonValue& value) {
  if (value.isUndefined()) {
    return QStringLiteral("<missing>");
  }
  if (value.isNull()) {
    return QStringLiteral("null");
  }
  if (value.isString()) {
    return QStringLiteral("\"%1\"").arg(value.toString());
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 12);
  }
  if (value.isArray()) {
    return QStringLiteral("[array:%1]").arg(value.toArray().size());
  }
  if (value.isObject()) {
    return QStringLiteral("{object:%1}").arg(value.toObject().size());
  }
  return QStringLiteral("<unknown>");
}

void append_recipe_json_diffs(
    QStringList* differences,
    const QString& path,
    const QJsonValue& before,
    const QJsonValue& after) {
  if (before.isObject() && after.isObject()) {
    const auto before_object = before.toObject();
    const auto after_object = after.toObject();
    QStringList keys = before_object.keys();
    for (const auto& key : after_object.keys()) {
      if (!keys.contains(key)) {
        keys.append(key);
      }
    }
    keys.sort();

    for (const auto& key : keys) {
      append_recipe_json_diffs(
          differences,
          path.isEmpty() ? key : QStringLiteral("%1.%2").arg(path, key),
          before_object.value(key),
          after_object.value(key));
    }
    return;
  }

  if (before.isArray() && after.isArray()) {
    const auto before_array = before.toArray();
    const auto after_array = after.toArray();
    const auto maximum_size =
        std::max(before_array.size(), after_array.size());
    for (int index = 0; index < maximum_size; ++index) {
      append_recipe_json_diffs(
          differences,
          QStringLiteral("%1[%2]").arg(path).arg(index),
          index < before_array.size()
              ? before_array.at(index)
              : QJsonValue(QJsonValue::Undefined),
          index < after_array.size()
              ? after_array.at(index)
              : QJsonValue(QJsonValue::Undefined));
    }
    return;
  }

  if (before == after ||
      path == QStringLiteral("id") ||
      path.endsWith(QStringLiteral(".id"))) {
    return;
  }
  differences->append(
      QStringLiteral("%1: %2 -> %3")
          .arg(path.isEmpty() ? QStringLiteral("$") : path)
          .arg(
              recipe_diff_value_summary(before),
              recipe_diff_value_summary(after)));
}

}  // namespace

EditRecipeDiffResult diff_json_documents(
    const QString& before_json,
    const QString& after_json) {
  EditRecipeDiffResult result;
  QJsonParseError before_error;
  QJsonParseError after_error;
  const auto before_document =
      QJsonDocument::fromJson(
          before_json.toUtf8(),
          &before_error);
  const auto after_document =
      QJsonDocument::fromJson(
          after_json.toUtf8(),
          &after_error);
  if (before_error.error != QJsonParseError::NoError ||
      !before_document.isObject()) {
    result.errors.append(
        QStringLiteral(
            "before: valid JSON object required"));
  }
  if (after_error.error != QJsonParseError::NoError ||
      !after_document.isObject()) {
    result.errors.append(
        QStringLiteral(
            "after: valid JSON object required"));
  }
  if (!result.errors.isEmpty()) {
    return result;
  }
  append_recipe_json_diffs(
      &result.differences,
      QString(),
      before_document.object(),
      after_document.object());
  result.valid = true;
  return result;
}

EditRecipeDiffResult diff_edit_recipe_json(
    const QString& before_recipe_json,
    const QString& after_recipe_json) {
  EditRecipeDiffResult result;
  const auto before_migration = migrate_edit_recipe_json(before_recipe_json);
  if (!before_migration.errors.isEmpty()) {
    for (const auto& error : before_migration.errors) {
      result.errors.append(QStringLiteral("before: %1").arg(error));
    }
  }
  const auto after_migration = migrate_edit_recipe_json(after_recipe_json);
  if (!after_migration.errors.isEmpty()) {
    for (const auto& error : after_migration.errors) {
      result.errors.append(QStringLiteral("after: %1").arg(error));
    }
  }
  if (!result.errors.isEmpty()) {
    return result;
  }

  return diff_json_documents(
      before_migration.recipe_json,
      after_migration.recipe_json);
}

}  // namespace photo_editor::desktop
