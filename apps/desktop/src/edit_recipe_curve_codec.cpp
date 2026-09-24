#include "edit_recipe_state.h"

namespace photo_editor::desktop {

QVariantList parse_curve_points(const QJsonArray& points) {
  QVariantList parsed_points;
  parsed_points.reserve(points.size());
  for (const auto& value : points) {
    if (!value.isObject()) {
      continue;
    }

    const auto point = value.toObject();
    QVariantMap map;
    map.insert(
        QStringLiteral("x"),
        point.value(QStringLiteral("x")).toDouble());
    map.insert(
        QStringLiteral("y"),
        point.value(QStringLiteral("y")).toDouble());
    parsed_points.append(map);
  }
  return parsed_points;
}

QJsonArray serialize_curve_points(const QVariantList& points) {
  QJsonArray serialized_points;
  for (const auto& value : points) {
    const auto point = value.toMap();
    if (point.isEmpty()) {
      continue;
    }

    QJsonObject object;
    object.insert(
        QStringLiteral("x"),
        point.value(QStringLiteral("x")).toDouble());
    object.insert(
        QStringLiteral("y"),
        point.value(QStringLiteral("y")).toDouble());
    serialized_points.append(object);
  }
  return serialized_points;
}

}  // namespace photo_editor::desktop
