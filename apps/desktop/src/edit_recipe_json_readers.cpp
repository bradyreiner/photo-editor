#include "edit_recipe_state.h"

namespace photo_editor::desktop::detail {

QJsonObject object_value(
    const QJsonObject& object,
    const QString& key) {
  const auto value = object.value(key);
  return value.isObject() ? value.toObject() : QJsonObject{};
}

double read_double(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    double default_value) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toDouble(default_value);
  }
  return legacy.value(legacy_key).toDouble(default_value);
}

QString read_string(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    const QString& default_value) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toString(default_value);
  }
  return legacy.value(legacy_key).toString(default_value);
}

bool read_bool(
    const QJsonObject& primary,
    const QString& primary_key,
    const QJsonObject& legacy,
    const QString& legacy_key,
    bool default_value) {
  if (primary.contains(primary_key)) {
    return primary.value(primary_key).toBool(default_value);
  }
  return legacy.value(legacy_key).toBool(default_value);
}

QByteArray read_byte_array(
    const QJsonObject& object,
    const QString& key) {
  return QByteArray::fromBase64(
      object.value(key).toString().toLatin1());
}

}  // namespace photo_editor::desktop::detail
