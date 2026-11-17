#include "timeline_keyframe_projection_cache.h"

#include <QCoreApplication>
#include <QString>
#include <QVector>

#include <algorithm>
#include <chrono>
#include <iostream>

namespace {

using photo_editor::desktop::timeline_detail::KeyframeProjectionCache;

struct Keyframe final {
  QString parameter;
  double local_seconds = 0.0;
  double value = 0.0;
  bool operator==(const Keyframe&) const = default;
};

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

QVector<Keyframe> retime_projection(const QVector<Keyframe>& source, int* calls) {
  ++*calls;
  QVector<Keyframe> projected;
  for (const auto& keyframe : source) {
    if (keyframe.parameter == QStringLiteral("sourceTime")) {
      projected.push_back(keyframe);
    }
  }
  std::sort(
      projected.begin(), projected.end(),
      [](const Keyframe& left, const Keyframe& right) {
        return left.local_seconds < right.local_seconds;
      });
  return projected;
}

bool test_shared_snapshot_hit_and_projection_key() {
  QVector<Keyframe> source{
      {QStringLiteral("opacity"), 0.25, 0.4},
      {QStringLiteral("sourceTime"), 2.0, 1.75},
      {QStringLiteral("sourceTime"), 0.0, 0.0},
  };
  KeyframeProjectionCache<Keyframe> cache;
  auto calls = 0;
  const auto project = [&calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &calls);
  };

  const auto first = cache.materialize(source, QStringLiteral("retime-v1"), project);
  const auto shallow_copy = source;
  const auto hit = cache.materialize(shallow_copy, QStringLiteral("retime-v1"), project);
  if (!check(first.size() == 2 && first.at(0).local_seconds == 0.0 &&
                 first.at(1).local_seconds == 2.0,
             "retime projection must preserve membership and sort order") ||
      !check(hit == first, "shared source cache hit changed the projection") ||
      !check(calls == 1, "unchanged shared keyframes were scanned more than once")) {
    return false;
  }

  const auto other_projection = cache.materialize(
      shallow_copy, QStringLiteral("retime-v2"), project);
  return check(other_projection == first,
               "a projection-key change should preserve projector semantics") &&
         check(calls == 2, "projection-key change did not rebuild derived data");
}

bool test_copy_on_write_edit_and_undo_backing_restore() {
  QVector<Keyframe> original{
      {QStringLiteral("sourceTime"), 0.0, 0.0},
      {QStringLiteral("sourceTime"), 3.0, 2.0},
      {QStringLiteral("opacity"), 1.0, 0.5},
  };
  KeyframeProjectionCache<Keyframe> cache;
  auto calls = 0;
  const auto project = [&calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &calls);
  };

  const auto original_projection = cache.materialize(
      original, QStringLiteral("retime-v1"), project);
  auto edited = original;
  edited[1].local_seconds = 4.0;  // non-const indexing detaches the shared backing.
  const auto edited_projection = cache.materialize(
      edited, QStringLiteral("retime-v1"), project);
  if (!check(edited_projection.size() == 2 &&
                 edited_projection.constLast().local_seconds == 4.0,
             "detached edit returned stale retime keyframes") ||
      !check(original_projection.constLast().local_seconds == 3.0,
             "cache hit/edit changed the earlier undo snapshot") ||
      !check(calls == 2, "detached keyframe edit did not rebuild the projection")) {
    return false;
  }

  const auto restored = cache.materialize(
      original, QStringLiteral("retime-v1"), project);
  const auto restored_again = cache.materialize(
      original, QStringLiteral("retime-v1"), project);
  return check(restored == original_projection,
               "undo-style restoration did not recover its source projection") &&
         check(restored_again == original_projection,
               "restored shared backing did not become a stable cache hit") &&
         check(calls == 3,
               "restoring a different backing should rebuild once, then hit");
}

bool test_cloned_clip_cache_is_independent() {
  const QVector<Keyframe> shared_keyframes{
      {QStringLiteral("sourceTime"), 0.0, 0.0},
      {QStringLiteral("sourceTime"), 1.0, 0.75},
  };
  KeyframeProjectionCache<Keyframe> original_clip_cache;
  KeyframeProjectionCache<Keyframe> cloned_clip_cache;
  auto original_calls = 0;
  auto clone_calls = 0;
  const auto original_project = [&original_calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &original_calls);
  };
  const auto clone_project = [&clone_calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &clone_calls);
  };

  const auto original = original_clip_cache.materialize(
      shared_keyframes, QStringLiteral("retime-v1"), original_project);
  const auto clone = cloned_clip_cache.materialize(
      shared_keyframes, QStringLiteral("retime-v1"), clone_project);
  return check(original == clone, "clone's derived retime values differ") &&
         check(original_calls == 1 && clone_calls == 1,
               "separate clip identities should own separate derived caches");
}

bool test_clear_discards_retained_source() {
  QVector<Keyframe> source{{QStringLiteral("sourceTime"), 0.0, 0.0}};
  KeyframeProjectionCache<Keyframe> cache;
  auto calls = 0;
  const auto project = [&calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &calls);
  };
  (void)cache.materialize(source, QStringLiteral("retime-v1"), project);
  cache.clear();
  (void)cache.materialize(source, QStringLiteral("retime-v1"), project);
  return check(calls == 2, "cleared cache retained a projection or source snapshot");
}

bool test_large_autosave_keyframe_vector_is_scanned_once() {
  constexpr auto keyframe_count = 805000;
  QVector<Keyframe> source;
  source.reserve(keyframe_count);
  for (auto index = 0; index < keyframe_count; ++index) {
    const auto is_retime = index % 5 == 0;
    source.push_back(Keyframe{
        is_retime ? QStringLiteral("sourceTime") : QStringLiteral("mask:left"),
        static_cast<double>(index) / 30.0,
        static_cast<double>(index % 13) / 12.0});
  }

  KeyframeProjectionCache<Keyframe> cache;
  auto calls = 0;
  const auto project = [&calls](const QVector<Keyframe>& values) {
    return retime_projection(values, &calls);
  };
  const auto begin = std::chrono::steady_clock::now();
  const auto first = cache.materialize(source, QStringLiteral("retime-v1"), project);
  const auto cold_end = std::chrono::steady_clock::now();
  const auto repeated = cache.materialize(source, QStringLiteral("retime-v1"), project);
  const auto hit_end = std::chrono::steady_clock::now();
  const auto cold_ms = std::chrono::duration_cast<std::chrono::milliseconds>(cold_end - begin).count();
  const auto hit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(hit_end - cold_end).count();
  std::cout << "805000-keyframe projection cold_ms=" << cold_ms
            << " cached_lookup_ms=" << hit_ms << '\n';
  return check(first.size() == keyframe_count / 5,
               "large autosave projection returned the wrong retime count") &&
         check(repeated == first,
               "large autosave repeated lookup changed the retime projection") &&
         check(calls == 1,
               "large unchanged autosave keyframes were rescanned on cache hit");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (!test_shared_snapshot_hit_and_projection_key() ||
      !test_copy_on_write_edit_and_undo_backing_restore() ||
      !test_cloned_clip_cache_is_independent() || !test_clear_discards_retained_source() ||
      !test_large_autosave_keyframe_vector_is_scanned_once()) {
    return 1;
  }
  std::cout << "timeline keyframe projection cache checks passed\n";
  return 0;
}
