#include "timeline_keyframe_json_array_cache.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

using photo_editor::desktop::timeline_detail::KeyframeJsonArrayCache;

struct Keyframe final {
  QString parameter;
  double seconds = 0.0;
  double value = 0.0;
  QString interpolation = QStringLiteral("linear");
  double bezier_in_time_seconds = 0.0;
  double bezier_in_value = 0.0;
  double bezier_out_time_seconds = 0.0;
  double bezier_out_value = 0.0;
  bool bezier_in_custom = false;
  bool bezier_out_custom = false;
  bool bezier_handles_linked = true;
  bool roving = false;
  bool operator==(const Keyframe&) const = default;
};

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

QJsonArray serialize_reference(const QVector<Keyframe>& keyframes) {
  QJsonArray array;
  for (const auto& keyframe : keyframes) {
    QJsonObject object;
    object.insert(QStringLiteral("parameter"), keyframe.parameter);
    object.insert(QStringLiteral("seconds"), keyframe.seconds);
    object.insert(QStringLiteral("value"), keyframe.value);
    object.insert(QStringLiteral("interpolation"), keyframe.interpolation);
    object.insert(QStringLiteral("bezierInTimeSeconds"), keyframe.bezier_in_time_seconds);
    object.insert(QStringLiteral("bezierInValue"), keyframe.bezier_in_value);
    object.insert(QStringLiteral("bezierOutTimeSeconds"), keyframe.bezier_out_time_seconds);
    object.insert(QStringLiteral("bezierOutValue"), keyframe.bezier_out_value);
    object.insert(QStringLiteral("bezierInCustom"), keyframe.bezier_in_custom);
    object.insert(QStringLiteral("bezierOutCustom"), keyframe.bezier_out_custom);
    object.insert(QStringLiteral("bezierHandlesLinked"), keyframe.bezier_handles_linked);
    object.insert(QStringLiteral("roving"), keyframe.roving);
    array.push_back(object);
  }
  return array;
}

QVector<Keyframe> select_framing_keyframes(const QVector<Keyframe>& source) {
  QVector<Keyframe> projected;
  for (const auto& keyframe : source) {
    if (keyframe.parameter == QStringLiteral("frameScale") ||
        keyframe.parameter == QStringLiteral("frameOffsetX") ||
        keyframe.parameter == QStringLiteral("frameOffsetY") ||
        keyframe.parameter == QStringLiteral("frameRotationDegrees") ||
        keyframe.parameter == QStringLiteral("framePerspectiveX") ||
        keyframe.parameter == QStringLiteral("framePerspectiveY") ||
        keyframe.parameter == QStringLiteral("frameCornerRadius") ||
        keyframe.parameter.startsWith(QStringLiteral("mask:"))) {
      projected.push_back(keyframe);
    }
  }
  return projected;
}

QByteArray compact(const QJsonArray& array) {
  return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

bool test_cache_hits_and_cow_invalidation() {
  QVector<Keyframe> source{
      {QStringLiteral("frameOffsetX"), 0.0, 0.125, QStringLiteral("linear")},
      {QStringLiteral("mask:left"), 1.25, 0.75, QStringLiteral("bezier"),
       0.12, -0.4, 0.31, 0.82, true, true, false, true},
      {QStringLiteral("opacity"), 2.0, 0.5, QStringLiteral("hold")},
  };
  KeyframeJsonArrayCache<Keyframe> cache;
  int projections = 0;
  int serializations = 0;
  const auto projector = [&projections](const QVector<Keyframe>& values) {
    ++projections;
    return values;
  };
  const auto serializer = [&serializations](const QVector<Keyframe>& values) {
    ++serializations;
    return serialize_reference(values);
  };

  const auto expected = serialize_reference(source);
  const auto first = cache.materialize(source, QStringLiteral("all-v1"), projector, serializer);
  if (!check(compact(first.array) == compact(expected), "initial JSON differs from reference") ||
      !check(first.projected == source, "identity projection changed keyframes") ||
      !check(projections == 1 && serializations == 1, "initial materialization did not run once")) {
    return false;
  }

  const auto shallow_copy = source;
  const auto hit = cache.materialize(shallow_copy, QStringLiteral("all-v1"), projector, serializer);
  if (!check(compact(hit.array) == compact(expected), "COW cache hit changed JSON") ||
      !check(projections == 1 && serializations == 1, "shared source did not hit cache")) {
    return false;
  }

  auto edited = source;
  edited[1].value = 0.875;  // non-const indexing detaches the implicitly shared QVector
  const auto changed = cache.materialize(edited, QStringLiteral("all-v1"), projector, serializer);
  const auto changed_expected = serialize_reference(edited);
  if (!check(compact(changed.array) == compact(changed_expected), "detached source JSON is stale") ||
      !check(projections == 2 && serializations == 2, "detached source did not rebuild cache")) {
    return false;
  }

  const auto projected = cache.materialize(edited, QStringLiteral("framing-v1"), projector, serializer);
  if (!check(projections == 3 && serializations == 3, "projection key change did not invalidate cache") ||
      !check(projected.projected == edited, "generic keyed projection unexpectedly changed values")) {
    return false;
  }
  return true;
}

bool test_framing_projection_matches_reference() {
  QVector<Keyframe> source{
      {QStringLiteral("opacity"), 0.0, 0.4, QStringLiteral("linear")},
      {QStringLiteral("frameScale"), 0.5, 1.25, QStringLiteral("bezier"),
       0.08, 0.9, 0.2, 1.4, true, false, true, false},
      {QStringLiteral("mask:right"), 1.0, 1.0, QStringLiteral("hold")},
      {QStringLiteral("titlePositionY"), 1.5, 0.81, QStringLiteral("linear")},
  };
  KeyframeJsonArrayCache<Keyframe> cache;
  const auto expected_values = select_framing_keyframes(source);
  const auto expected_json = serialize_reference(expected_values);
  const auto actual = cache.materialize(
      source,
      QStringLiteral("active-framing-v1"),
      select_framing_keyframes,
      serialize_reference);
  return check(actual.projected == expected_values, "framing projection changed order or membership") &&
      check(compact(actual.array) == compact(expected_json), "framing JSON differs from reference");
}

bool test_concurrent_first_materialization_is_single() {
  QVector<Keyframe> source;
  source.reserve(2048);
  for (int index = 0; index < 2048; ++index) {
    source.push_back(Keyframe{
        QStringLiteral("mask:left"),
        static_cast<double>(index) / 30.0,
        static_cast<double>(index % 7) / 6.0,
        QStringLiteral("bezier"),
        0.01,
        -0.2,
        0.02,
        0.3,
        true,
        false,
        true,
        false});
  }

  KeyframeJsonArrayCache<Keyframe> cache;
  std::atomic<int> projections{0};
  std::atomic<int> serializations{0};
  const auto projector = [&projections](const QVector<Keyframe>& values) {
    projections.fetch_add(1, std::memory_order_relaxed);
    return values;
  };
  const auto serializer = [&serializations](const QVector<Keyframe>& values) {
    serializations.fetch_add(1, std::memory_order_relaxed);
    return serialize_reference(values);
  };

  std::vector<QByteArray> results(8);
  std::vector<std::thread> threads;
  threads.reserve(results.size());
  for (std::size_t index = 0; index < results.size(); ++index) {
    threads.emplace_back([&, index] {
      const auto value = cache.materialize(source, QStringLiteral("all-v1"), projector, serializer);
      results[index] = compact(value.array);
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  const auto expected = compact(serialize_reference(source));
  if (!check(projections.load(std::memory_order_relaxed) == 1,
             "concurrent first read projected more than once") ||
      !check(serializations.load(std::memory_order_relaxed) == 1,
             "concurrent first read serialized more than once")) {
    return false;
  }
  for (const auto& result : results) {
    if (!check(result == expected, "concurrent cache result differs from reference")) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!test_cache_hits_and_cow_invalidation() ||
      !test_framing_projection_matches_reference() ||
      !test_concurrent_first_materialization_is_single()) {
    return 1;
  }
  std::cout << "timeline keyframe JSON cache checks passed\n";
  return 0;
}
