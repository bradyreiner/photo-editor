#include "timeline_document_controller.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVariantList>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace photo_editor::desktop {

struct TimelineMulticamProjectionTestAccess final {
  struct ExpectedClip final {
    QString clip_id;
    QString group_id;
    int angle_index = -1;
    int angle_count = 0;
    bool active = true;
    bool audio_active = true;
  };

  static void seed(TimelineDocumentController& controller, int clip_count) {
    clip_count = qMax(2, clip_count - (clip_count % 2));
    controller.tracks_.clear();
    controller.multicam_groups_.clear();
    controller.has_active_timeline_ = true;
    controller.timeline_id_.clear();
    controller.selected_clip_id_.clear();
    controller.selected_clip_ids_.clear();
    controller.sequence_playhead_seconds_ = 0.75;
    controller.timeline_tracks_projection_cache_.clear();
    controller.timeline_tracks_projection_cache_valid_ = false;

    TimelineDocumentController::Track track;
    track.track_id = QStringLiteral("timeline-luna-multicam-track");
    track.display_label = QStringLiteral("Multicam projection benchmark");
    track.kind = TimelineDocumentController::TrackKind::Video;
    track.role = QStringLiteral("media");
    track.independent = true;
    track.clips.reserve(clip_count);

    const auto group_count = clip_count / 2;
    controller.multicam_groups_.reserve(group_count + 2);
    for (int group_index = 0; group_index < group_count; ++group_index) {
      TimelineDocumentController::MulticamGroup group;
      group.group_id = QStringLiteral("multicam-%1").arg(group_index);
      group.label = group.group_id;
      group.audio_mode = group_index % 3 == 0
                             ? QStringLiteral("follow-video")
                             : group_index % 3 == 1
                                   ? QStringLiteral("fixed-angle")
                                   : QStringLiteral("mute");
      group.audio_angle_index = group_index % 2;
      const auto first_clip_id = QStringLiteral("clip-%1").arg(group_index * 2);
      const auto second_clip_id = QStringLiteral("clip-%1").arg(group_index * 2 + 1);
      group.angle_clip_ids = {first_clip_id, second_clip_id};
      group.cuts.push_back({0.5, second_clip_id});
      controller.multicam_groups_.push_back(std::move(group));
    }

    // Empty IDs are present in the model but, as in the original lookup, they
    // never associate with a projected clip.
    TimelineDocumentController::MulticamGroup empty_id_group;
    empty_id_group.group_id = QStringLiteral("multicam-empty-id");
    empty_id_group.angle_clip_ids = {QString{}};
    controller.multicam_groups_.push_back(std::move(empty_id_group));

    // Duplicate membership across later groups must not shadow the first
    // group found by the original linear query.
    TimelineDocumentController::MulticamGroup duplicate_membership_group;
    duplicate_membership_group.group_id = QStringLiteral("multicam-late-duplicate");
    duplicate_membership_group.angle_clip_ids = {QStringLiteral("clip-0")};
    controller.multicam_groups_.push_back(std::move(duplicate_membership_group));

    for (int clip_index = 0; clip_index < clip_count; ++clip_index) {
      TimelineDocumentController::Clip clip;
      clip.clip_id = QStringLiteral("clip-%1").arg(clip_index);
      clip.display_label = clip.clip_id;
      clip.media_type = QStringLiteral("motion-generator");
      clip.duration_seconds = 1.0;
      clip.trim_end_seconds = 1.0;
      clip.start_seconds = static_cast<double>(clip_index) * 1.25;
      clip.has_audio = true;
      track.clips.push_back(std::move(clip));
    }
    controller.tracks_.push_back(std::move(track));
  }

  static QVector<ExpectedClip> expected(const TimelineDocumentController& controller) {
    QVector<ExpectedClip> results;
    for (const auto& clip : controller.tracks_.constFirst().clips) {
      ExpectedClip result;
      result.clip_id = clip.clip_id;
      const TimelineDocumentController::MulticamGroup* found_group = nullptr;
      for (const auto& group : controller.multicam_groups_) {
        if (group.angle_clip_ids.contains(clip.clip_id)) {
          found_group = &group;
          break;
        }
      }
      if (found_group == nullptr) {
        results.push_back(std::move(result));
        continue;
      }
      result.group_id = found_group->group_id;
      result.angle_index = static_cast<int>(
          found_group->angle_clip_ids.indexOf(clip.clip_id));
      result.angle_count = static_cast<int>(found_group->angle_clip_ids.size());
      auto active_clip_id = found_group->angle_clip_ids.isEmpty()
                                ? QString{}
                                : found_group->angle_clip_ids.constFirst();
      for (const auto& cut : found_group->cuts) {
        if (cut.sequence_seconds > controller.sequence_playhead_seconds_ + 0.001) {
          break;
        }
        active_clip_id = cut.clip_id;
      }
      result.active = active_clip_id == clip.clip_id;
      if (found_group->audio_mode == QStringLiteral("mute")) {
        result.audio_active = false;
      } else if (found_group->audio_mode == QStringLiteral("fixed-angle")) {
        const auto bounded_index = qBound(
            0, found_group->audio_angle_index,
            qMax(0, static_cast<int>(found_group->angle_clip_ids.size()) - 1));
        result.audio_active = !found_group->angle_clip_ids.isEmpty() &&
                              found_group->angle_clip_ids.at(bounded_index) == clip.clip_id;
      } else {
        result.audio_active = active_clip_id == clip.clip_id;
      }
      results.push_back(std::move(result));
    }
    return results;
  }

  static bool matches(
      const QVariantList& tracks, const QVector<ExpectedClip>& expected_clips,
      QString* error) {
    if (tracks.size() != 1) {
      if (error != nullptr) *error = QStringLiteral("projection did not contain one track");
      return false;
    }
    const auto clip_values = tracks.constFirst().toMap()
                                 .value(QStringLiteral("clips")).toList();
    if (clip_values.size() != expected_clips.size()) {
      if (error != nullptr) *error = QStringLiteral("projection clip count changed");
      return false;
    }
    for (qsizetype index = 0; index < expected_clips.size(); ++index) {
      const auto actual = clip_values.at(index).toMap();
      const auto& expected = expected_clips.at(index);
      if (actual.value(QStringLiteral("clipId")).toString() != expected.clip_id ||
          actual.value(QStringLiteral("multicamGroupId")).toString() != expected.group_id ||
          actual.value(QStringLiteral("multicamAngleIndex")).toInt() != expected.angle_index ||
          actual.value(QStringLiteral("multicamAngleCount")).toInt() != expected.angle_count ||
          actual.value(QStringLiteral("multicamActive")).toBool() != expected.active ||
          actual.value(QStringLiteral("multicamAudioActive")).toBool() != expected.audio_active) {
        if (error != nullptr) {
          *error = QStringLiteral("multicam projection mismatch at clip index %1").arg(index);
        }
        return false;
      }
    }
    return true;
  }

  static QVariantMap measure(
      TimelineDocumentController& controller,
      const QVector<ExpectedClip>& expected_clips,
      bool compact,
      int repetitions) {
    QVector<qint64> samples;
    samples.reserve(repetitions);
    for (int repetition = 0; repetition < repetitions; ++repetition) {
      controller.timeline_tracks_projection_cache_.clear();
      controller.timeline_tracks_projection_cache_valid_ = false;
      QElapsedTimer timer;
      timer.start();
      const auto projection = compact ? controller.snapshotTimelineTracks()
                                     : controller.snapshotSequenceTracks();
      samples.push_back(timer.nsecsElapsed());
      QString error;
      if (!matches(projection, expected_clips, &error)) {
        throw std::runtime_error(error.toStdString());
      }
    }
    std::sort(samples.begin(), samples.end());
    return {
        {QStringLiteral("projection"), compact ? QStringLiteral("compact")
                                                : QStringLiteral("full")},
        {QStringLiteral("repetitions"), repetitions},
        {QStringLiteral("medianNanoseconds"), samples.at(samples.size() / 2)},
        {QStringLiteral("minNanoseconds"), samples.constFirst()},
        {QStringLiteral("maxNanoseconds"), samples.constLast()},
        {QStringLiteral("parity"), true},
    };
  }

  static void clearMulticamGroups(TimelineDocumentController& controller) {
    controller.multicam_groups_.clear();
    controller.timeline_tracks_projection_cache_.clear();
    controller.timeline_tracks_projection_cache_valid_ = false;
  }

  static void setPlayhead(TimelineDocumentController& controller, double seconds) {
    controller.sequence_playhead_seconds_ = seconds;
    controller.timeline_tracks_projection_cache_.clear();
    controller.timeline_tracks_projection_cache_valid_ = false;
  }
};

}  // namespace photo_editor::desktop

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    std::cerr << "could not create isolated Timeline test workspace\n";
    return EXIT_FAILURE;
  }
  const auto app_data_root = workspace.filePath(QStringLiteral("app-data"));
  const auto cache_root = workspace.filePath(QStringLiteral("cache"));
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", app_data_root.toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8());
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("xdg-data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("xdg-cache")).toUtf8());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(
      QSettings::IniFormat, QSettings::UserScope,
      workspace.filePath(QStringLiteral("settings/user")));
  QSettings::setPath(
      QSettings::IniFormat, QSettings::SystemScope,
      workspace.filePath(QStringLiteral("settings/system")));
  QApplication application(argc, argv);
  QStandardPaths::setTestModeEnabled(true);
  int clip_count = 4096;
  int repetitions = 5;
  for (int index = 1; index + 1 < argc; ++index) {
    const auto argument = QString::fromLocal8Bit(argv[index]);
    bool valid = false;
    const auto value = QString::fromLocal8Bit(argv[index + 1]).toInt(&valid);
    if (!valid) continue;
    if (argument == QStringLiteral("--clips")) clip_count = value;
    if (argument == QStringLiteral("--repetitions")) repetitions = value;
  }

  try {
    photo_editor::desktop::TimelineDocumentController controller;
    photo_editor::desktop::TimelineMulticamProjectionTestAccess::seed(
        controller, clip_count);
    const auto expected =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::expected(controller);
    const auto full = photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
        controller, expected, false, repetitions);
    const auto compact = photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
        controller, expected, true, repetitions);
    photo_editor::desktop::TimelineMulticamProjectionTestAccess::setPlayhead(
        controller, 0.4995);
    const auto near_cut_expected =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::expected(controller);
    const auto near_cut_full =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, near_cut_expected, false, 1);
    const auto near_cut_compact =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, near_cut_expected, true, 1);
    photo_editor::desktop::TimelineMulticamProjectionTestAccess::setPlayhead(
        controller, 0.25);
    const auto alternate_playhead_expected =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::expected(controller);
    const auto alternate_playhead_full =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, alternate_playhead_expected, false, 1);
    const auto alternate_playhead_compact =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, alternate_playhead_expected, true, 1);
    photo_editor::desktop::TimelineMulticamProjectionTestAccess::clearMulticamGroups(
        controller);
    const auto no_multicam_expected =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::expected(controller);
    const auto no_multicam_full =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, no_multicam_expected, false, repetitions);
    const auto no_multicam_compact =
        photo_editor::desktop::TimelineMulticamProjectionTestAccess::measure(
            controller, no_multicam_expected, true, repetitions);
    std::cout << QJsonDocument(QJsonObject{
                     {QStringLiteral("scenario"),
                      QStringLiteral("timeline_multicam_projection")},
                     {QStringLiteral("clipCount"), static_cast<int>(expected.size())},
                     {QStringLiteral("groupCount"), static_cast<int>(expected.size() / 2)},
                     {QStringLiteral("results"), QJsonArray{
                          QJsonObject::fromVariantMap(full),
                          QJsonObject::fromVariantMap(compact),
                          QJsonObject::fromVariantMap(near_cut_full),
                          QJsonObject::fromVariantMap(near_cut_compact),
                          QJsonObject::fromVariantMap(alternate_playhead_full),
                          QJsonObject::fromVariantMap(alternate_playhead_compact),
                          QJsonObject::fromVariantMap(no_multicam_full),
                          QJsonObject::fromVariantMap(no_multicam_compact)}},
                 })
                     .toJson(QJsonDocument::Compact)
                     .constData()
              << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
