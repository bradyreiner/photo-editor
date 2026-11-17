#include "timeline_multicam_projection_index.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {

using photo_editor::desktop::MulticamClipAddress;
using photo_editor::desktop::MulticamProjectionIndex;

struct Group final {
  QVector<QString> angle_clip_ids;
};

MulticamClipAddress linear_address(
    const QVector<Group>& groups, const QString& clip_id) {
  if (clip_id.isEmpty()) {
    return {};
  }
  for (int group_index = 0; group_index < groups.size(); ++group_index) {
    const auto& angle_clip_ids = groups.at(group_index).angle_clip_ids;
    const auto angle_index = angle_clip_ids.indexOf(clip_id);
    if (angle_index >= 0) {
      return {group_index, static_cast<int>(angle_index)};
    }
  }
  return {};
}

void verify_first_match_and_missing_ids() {
  const QVector<Group> groups{
      Group{{QString{}, QStringLiteral("a"), QStringLiteral("duplicate"), QStringLiteral("duplicate")}},
      Group{{QStringLiteral("duplicate"), QStringLiteral("b")}},
  };
  MulticamProjectionIndex index;
  index.rebuild(groups, 4);
  const auto duplicate = index.addressForClip(QStringLiteral("duplicate"));
  if (duplicate.group_index != 0 || duplicate.angle_index != 2 ||
      index.addressForClip(QStringLiteral("missing")).isValid() ||
      index.addressForClip(QString{}).isValid()) {
    throw std::runtime_error("multicam index changed first-match or absent-ID semantics");
  }
  index.rebuild(QVector<Group>{}, 4);
  if (index.addressForClip(QStringLiteral("duplicate")).isValid() || index.size() != 0) {
    throw std::runtime_error("rebuilding an empty index retained stale multicam IDs");
  }
}

QVector<Group> make_groups(int group_count) {
  QVector<Group> groups;
  groups.reserve(group_count);
  for (int group_index = 0; group_index < group_count; ++group_index) {
    groups.push_back(Group{{
        QStringLiteral("clip-%1-a").arg(group_index),
        QStringLiteral("clip-%1-b").arg(group_index),
    }});
  }
  return groups;
}

QStringList make_queries(const QVector<Group>& groups) {
  QStringList queries;
  queries.reserve(groups.size() * 2);
  for (const auto& group : groups) {
    queries.push_back(group.angle_clip_ids.at(0));
    queries.push_back(group.angle_clip_ids.at(1));
  }
  queries.push_back(QStringLiteral("clip-without-group"));
  return queries;
}

qint64 median_ns(QVector<qint64> samples) {
  std::sort(samples.begin(), samples.end());
  return samples.at(samples.size() / 2);
}

QJsonObject measure_size(int group_count) {
  const auto groups = make_groups(group_count);
  const auto queries = make_queries(groups);
  constexpr int attempts = 7;
  QVector<qint64> linear_samples;
  QVector<qint64> indexed_samples;
  linear_samples.reserve(attempts);
  indexed_samples.reserve(attempts);

  quint64 linear_checksum = 0;
  quint64 indexed_checksum = 0;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    QElapsedTimer timer;
    timer.start();
    quint64 checksum = 0;
    for (const auto& clip_id : queries) {
      const auto address = linear_address(groups, clip_id);
      checksum += static_cast<quint64>(address.group_index + 1) * 65537ULL +
                  static_cast<quint64>(address.angle_index + 1);
    }
    linear_samples.push_back(timer.nsecsElapsed());
    linear_checksum = checksum;

    timer.restart();
    MulticamProjectionIndex index;
    index.rebuild(groups, queries.size());
    checksum = 0;
    for (const auto& clip_id : queries) {
      const auto address = index.addressForClip(clip_id);
      checksum += static_cast<quint64>(address.group_index + 1) * 65537ULL +
                  static_cast<quint64>(address.angle_index + 1);
    }
    indexed_samples.push_back(timer.nsecsElapsed());
    indexed_checksum = checksum;
  }

  MulticamProjectionIndex correctness_index;
  correctness_index.rebuild(groups, queries.size());
  for (const auto& clip_id : queries) {
    const auto expected = linear_address(groups, clip_id);
    const auto actual = correctness_index.addressForClip(clip_id);
    if (expected.group_index != actual.group_index ||
        expected.angle_index != actual.angle_index) {
      throw std::runtime_error("indexed multicam projection differs from the linear reference");
    }
  }
  if (linear_checksum != indexed_checksum) {
    throw std::runtime_error("indexed multicam timing checksum differs from the linear reference");
  }

  const auto linear_ns = median_ns(std::move(linear_samples));
  const auto indexed_ns = median_ns(std::move(indexed_samples));
  return {
      {QStringLiteral("groupCount"), group_count},
      {QStringLiteral("clipQueries"), static_cast<int>(queries.size())},
      {QStringLiteral("linearMedianNs"), linear_ns},
      {QStringLiteral("indexedMedianNs"), indexed_ns},
      {QStringLiteral("speedup"),
       indexed_ns > 0 ? static_cast<double>(linear_ns) / indexed_ns : 0.0},
      {QStringLiteral("checksum"), static_cast<double>(linear_checksum)},
  };
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  verify_first_match_and_missing_ids();

  QJsonArray measurements;
  for (const auto group_count : {64, 256, 1024, 2048}) {
    measurements.push_back(measure_size(group_count));
  }
  std::cout << QJsonDocument(QJsonObject{
                   {QStringLiteral("scenario"),
                    QStringLiteral("multicam_group_lookup_per_projection")},
                   {QStringLiteral("measurements"), measurements},
               })
                   .toJson(QJsonDocument::Compact)
                   .constData()
            << '\n';
  return 0;
}
