#include "timeline_document_controller.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif

namespace photo_editor::desktop {
struct TimelineHistoryTestAccess {
  static void seed(TimelineDocumentController& controller, int count, int lanes, bool last = false) {
    controller.tracks_.clear();
    controller.undo_stack_.clear();
    controller.redo_stack_.clear();
    controller.has_active_timeline_ = true;
    controller.timeline_id_.clear(); // No autosave destination in this isolated fixture.
    for (int lane = 0; lane < lanes; ++lane) {
      TimelineDocumentController::Track track;
      track.track_id = QStringLiteral("track-%1").arg(lane);
      track.lane_index = lane;
      track.independent = true;
      for (int i = lane; i < count; i += lanes) {
        TimelineDocumentController::Clip clip;
        clip.clip_id = QStringLiteral("clip-%1").arg(i);
        clip.display_label = clip.clip_id;
        clip.duration_seconds = 4.0;
        clip.trim_end_seconds = 4.0;
        clip.start_seconds = static_cast<double>(i / lanes) * 4.0;
        clip.effect_stack = {QStringLiteral("Blur"), QStringLiteral("Vignette"),
                            QStringLiteral("Sharpen"), QStringLiteral("Grain")};
        clip.keyframes = {{QStringLiteral("opacity"), 0.0, 0.8, QStringLiteral("linear")}};
        track.clips.push_back(std::move(clip));
      }
      controller.tracks_.push_back(std::move(track));
    }
    const auto& selected = last ? controller.tracks_.at(0).clips.constLast() : controller.tracks_.at(0).clips.constFirst();
    controller.selected_clip_id_ = selected.clip_id;
    controller.sequence_playhead_seconds_ = selected.start_seconds;
    controller.selected_clip_ids_ = {controller.selected_clip_id_};
    controller.updateCurrentIndexFromSelection();
  }
  static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
  }
  static QJsonObject document(const TimelineDocumentController& controller) {
    auto value = controller.documentObject();
    value.remove(QStringLiteral("updatedAt"));
    return value;
  }
  static QString referenceRenderCacheSignature(
      const QJsonObject& document,
      double start_seconds,
      double end_seconds) {
    auto root = document;
    root.remove(QStringLiteral("name"));
    root.remove(QStringLiteral("createdAt"));
    root.remove(QStringLiteral("updatedAt"));
    root.remove(QStringLiteral("renderPath"));
    root.remove(QStringLiteral("posterPath"));
    root.remove(QStringLiteral("activeTool"));
    root.remove(QStringLiteral("editMode"));
    root.remove(QStringLiteral("linkedSelectionEnabled"));
    root.remove(QStringLiteral("projectBinding"));
    root.remove(QStringLiteral("markers"));
    root.remove(QStringLiteral("clips"));
    root.insert(QStringLiteral("cacheVersion"), 4);
    root.insert(QStringLiteral("rangeStart"), start_seconds);
    root.insert(QStringLiteral("rangeEnd"), end_seconds);

    QJsonArray filtered_tracks;
    for (const auto& track_value : root.value(QStringLiteral("tracks")).toArray()) {
      auto track = track_value.toObject();
      track.remove(QStringLiteral("trackId"));
      track.remove(QStringLiteral("displayLabel"));
      track.remove(QStringLiteral("locked"));
      track.remove(QStringLiteral("targeted"));
      QJsonArray filtered_clips;
      for (const auto& clip_value : track.value(QStringLiteral("clips")).toArray()) {
        auto clip = clip_value.toObject();
        const auto clip_start = clip.value(QStringLiteral("startSeconds")).toDouble();
        const auto clip_duration = qMax(
            0.0,
            clip.value(QStringLiteral("trimEndSeconds")).toDouble() -
                clip.value(QStringLiteral("trimStartSeconds")).toDouble());
        const auto clip_end = clip_start + clip_duration;
        const auto transition_preroll = qMax(
            0.0, clip.value(QStringLiteral("transitionInSeconds")).toDouble());
        if (clip_end <= start_seconds + 0.001 ||
            clip_start - transition_preroll >= end_seconds - 0.001) {
          continue;
        }
        const QFileInfo source_info(clip.value(QStringLiteral("filePath")).toString());
        clip.insert(QStringLiteral("sourceSize"),
                    source_info.exists() ? source_info.size() : -1);
        clip.insert(
            QStringLiteral("sourceModifiedMs"),
            source_info.exists() ? source_info.lastModified().toMSecsSinceEpoch() : -1);
        const QFileInfo color_lut_info(clip.value(QStringLiteral("colorLutPath")).toString());
        clip.insert(QStringLiteral("colorLutFileSize"),
                    color_lut_info.exists() ? color_lut_info.size() : -1);
        clip.insert(
            QStringLiteral("colorLutFileModifiedMs"),
            color_lut_info.exists()
                ? color_lut_info.lastModified().toMSecsSinceEpoch()
                : -1);
        for (const auto* key : {"assetId", "displayLabel", "posterSource", "linkedGroupId",
                                "waveformSource", "sourceContentSha256", "proxyPath",
                                "proxyEnabled"}) {
          clip.remove(QString::fromLatin1(key));
        }
        filtered_clips.push_back(clip);
      }
      track.insert(QStringLiteral("clips"), filtered_clips);
      filtered_tracks.push_back(track);
    }
    root.insert(QStringLiteral("tracks"), filtered_tracks);
    return QString::fromLatin1(
        QCryptographicHash::hash(
            QJsonDocument(root).toJson(QJsonDocument::Compact),
            QCryptographicHash::Sha256)
            .toHex());
  }
  static void verifyRenderCacheSignatureParity(TimelineDocumentController& controller) {
    seed(controller, 18, 3);
    controller.tracks_[0].clips[0].file_path = QStringLiteral("/tmp/signature-source.mp4");
    controller.tracks_[0].clips[0].color_lut_path = QStringLiteral("/tmp/signature-look.cube");
    const auto tasks = controller.expectedRenderCacheTasks();
    const auto document_snapshot = controller.documentObject();
    require(!tasks.isEmpty(), "Render-cache signature fixture should create ranges");
    for (const auto& task : tasks) {
      const auto reference = referenceRenderCacheSignature(
          document_snapshot, task.start_seconds, task.end_seconds);
      require(task.signature == reference,
          "Cached render range signature differs from the task-start reference bytes");
    }
  }
  static void verify(TimelineDocumentController& controller, const QString& directory) {
    seed(controller, 60, 3);
    const auto before = document(controller);
    auto* track = &controller.tracks_[0];
    auto* clip = &track->clips[0];
    const auto old_clip = *clip;
    controller.pushUndoSnapshot();
    clip->trim_start_seconds = 0.5;
    clip->effect_stack.move(0, 3);
    clip->effect_states[QStringLiteral("Blur")].mask.center_x = 0.25;
    clip->keyframes[0].value = 0.4;
    clip->output_variant_overrides[QStringLiteral("portrait")].frame_scale = 1.5;
    clip->caption_words.push_back({QStringLiteral("word"), 0.1, 0.2, QStringLiteral("voice"), 0.9});
    track->custom_name = QStringLiteral("Edited track");
    track->keyframes.push_back({QStringLiteral("opacity"), 0.0, 0.5, QStringLiteral("linear")});
    require(controller.undo_stack_.constLast().tracks.at(0).clips.at(0) == old_clip,
            "Mutation through an existing clip pointer changed history");
    const auto changed = document(controller);
    controller.undo();
    require(document(controller) == before, "Undo lost nested clip or track values");
    controller.redo();
    require(document(controller) == changed, "Redo lost nested clip or track values");

    seed(controller, 60, 3);
    controller.pushUndoSnapshot();
    controller.tracks_[0].clips[0].trim_start_seconds = 0.1;
    controller.pushUndoSnapshot();
    require(controller.undo_stack_.at(0).tracks.at(1).clips.constData() ==
                controller.undo_stack_.at(1).tracks.at(1).clips.constData(),
            "Unchanged snapshot tracks did not share immutable clip storage");
    require(controller.undo_stack_.at(0).tracks.at(0).clips.constData() !=
                controller.undo_stack_.at(1).tracks.at(0).clips.constData(),
            "Changed snapshot clips shared mutable storage");
    controller.tracks_[2].clips.last().frame_scale = 2.0;
    controller.pushUndoSnapshot();
    require(controller.undo_stack_.constLast().tracks.at(2).clips.constLast().frame_scale == 2.0 &&
                controller.undo_stack_.at(1).tracks.at(2).clips.constLast().frame_scale == 1.0,
            "An edit outside the selection reused stale clip values");
    require(controller.undo_stack_.at(1).tracks.at(1).clips.constData() ==
                controller.undo_stack_.constLast().tracks.at(1).clips.constData(),
            "An unrelated track lost immutable sharing");
    controller.undo_stack_.clear();

    const auto round_trip = [&](auto edit, const char* label) {
      const auto original = document(controller);
      const auto depth = controller.undo_stack_.size();
      edit();
      require(controller.undo_stack_.size() == depth + 1, label);
      const auto edited = document(controller);
      require(edited != original, "Fixture edit did not change the document");
      controller.undo();
      require(document(controller) == original, label);
      controller.redo();
      require(document(controller) == edited, label);
    };
    round_trip([&] {
      controller.beginContinuousEdit();
      for (int i = 1; i <= 20; ++i) controller.setSelectedTrimStartSeconds(0.02 * i);
      controller.endContinuousEdit();
    }, "Coalesced trim undo/redo changed the document");
    round_trip([&] {
      require(controller.setSelectedClipKeyframeValue(QStringLiteral("opacity"), 0.0, 0.3),
              "Keyframe edit was rejected");
    }, "Keyframe undo/redo changed the document");
    round_trip([&] {
      require(controller.moveSelectedClipEffect(QStringLiteral("Blur"), 1), "Effect move rejected");
    }, "Effect-order undo/redo changed the document");
    round_trip([&] { controller.moveSelectedClipToTrackRow(1, 100.0); },
               "Clip move undo/redo changed the document");
    round_trip([&] { require(controller.moveTrackRow(1, 1), "Track move rejected"); },
               "Track reorder undo/redo changed the document");

    controller.undo();
    require(controller.canRedo(), "Undo did not create redo history");
    controller.beginContinuousEdit();
    controller.setSelectedTrimStartSeconds(0.25);
    controller.endContinuousEdit();
    require(!controller.canRedo(), "New edit retained an obsolete redo branch");

    const auto external_path = directory + QStringLiteral("/external-timeline.json");
    const QByteArray old_bytes = R"({"timelineId":"external-fixture","name":"Before","tracks":[]})";
    const QByteArray new_bytes = R"({"timelineId":"external-fixture","name":"After","tracks":[]})";
    const auto write = [&](const QByteArray& bytes) {
      QFile file(external_path);
      require(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "External fixture write failed");
    };
    const auto read = [&] {
      QFile file(external_path);
      require(file.open(QIODevice::ReadOnly), "External fixture read failed");
      return file.readAll();
    };
    write(old_bytes);
    controller.pushUndoSnapshot();
    controller.undo_stack_.last().external_document_bytes.insert(external_path, old_bytes);
    write(new_bytes);
    controller.undo();
    require(read() == old_bytes, "Undo did not restore external sequence bytes");
    controller.redo();
    require(read() == new_bytes, "Redo did not restore external sequence bytes");
  }
  static void verifyProjection(TimelineDocumentController& controller) {
  static const QStringList reference_keys{
      QStringLiteral("audioDetached"),
      QStringLiteral("audioGainDb"),
      QStringLiteral("audioLinked"),
      QStringLiteral("clipId"),
      QStringLiteral("compoundError"),
      QStringLiteral("compoundStatus"),
      QStringLiteral("displayLabel"),
      QStringLiteral("durationSeconds"),
      QStringLiteral("durationText"),
      QStringLiteral("effectivePlaybackRate"),
      QStringLiteral("endSeconds"),
      QStringLiteral("effectCount"),
      QStringLiteral("effectsEnabled"),
      QStringLiteral("fadeInSeconds"),
      QStringLiteral("fadeOutSeconds"),
      QStringLiteral("filePath"),
      QStringLiteral("frameMode"),
      QStringLiteral("frameScale"),
      QStringLiteral("groupTransformOffsetX"),
      QStringLiteral("groupTransformOffsetY"),
      QStringLiteral("groupTransformRotationDegrees"),
      QStringLiteral("groupTransformScale"),
      QStringLiteral("groupedClipCount"),
      QStringLiteral("hasAudio"),
      QStringLiteral("index"),
      QStringLiteral("isCompound"),
      QStringLiteral("laneIndex"),
      QStringLiteral("mediaAnalysisStatus"),
      QStringLiteral("mediaOnline"),
      QStringLiteral("mediaType"),
      QStringLiteral("multicamActive"),
      QStringLiteral("multicamAngleCount"),
      QStringLiteral("multicamAngleIndex"),
      QStringLiteral("multicamAudioActive"),
      QStringLiteral("multicamGroupId"),
      QStringLiteral("opacity"),
      QStringLiteral("playbackRate"),
      QStringLiteral("posterSource"),
      QStringLiteral("proxyReady"),
      QStringLiteral("proxyStatus"),
      QStringLiteral("retimeActive"),
      QStringLiteral("retimeDirection"),
      QStringLiteral("retimePoints"),
      QStringLiteral("seconds"),
      QStringLiteral("selected"),
      QStringLiteral("silentPlaceholder"),
      QStringLiteral("sourceUrl"),
      QStringLiteral("startSeconds"),
      QStringLiteral("trackId"),
      QStringLiteral("trackKind"),
      QStringLiteral("trackLabel"),
      QStringLiteral("trackRowIndex"),
      QStringLiteral("transitionInName"),
      QStringLiteral("transitionOutName"),
      QStringLiteral("trimEndSeconds"),
      QStringLiteral("trimStartSeconds"),
      QStringLiteral("trimmedDurationSeconds"),
      QStringLiteral("trimmedDurationText"),
      QStringLiteral("usingProxy"),
      QStringLiteral("waveformSource"),
  };

    seed(controller, 60, 3);
    TimelineDocumentController::MulticamGroup multicam;
    multicam.group_id = QStringLiteral("projection-cameras");
    multicam.angle_clip_ids = {controller.tracks_.at(1).clips.at(1).clip_id,
                              controller.tracks_.at(2).clips.at(1).clip_id};
    controller.multicam_groups_ = {multicam};
    controller.tracks_[1].group_id = QStringLiteral("projection-group");
    controller.tracks_[1].group_transform_scale = 1.4;
    controller.tracks_[1].group_transform_offset_x = 0.2;
    controller.tracks_[1].clips[1].linked_group_id = QStringLiteral("linked-projection");
    controller.tracks_[2].clips[1].linked_group_id = QStringLiteral("linked-projection");
    for (int variant = 0; variant < 6; ++variant) {
      auto& track = controller.tracks_[1];
      auto& clip = track.clips[1];
      track.kind = variant == 1 ? TimelineDocumentController::TrackKind::Audio
                               : TimelineDocumentController::TrackKind::Video;
      clip.media_type = variant == 2 ? QStringLiteral("caption")
          : variant == 3 ? QStringLiteral("adjustment") : QStringLiteral("video");
      clip.frame_scale = 1.7;
      clip.opacity = 0.6;
      clip.audio_gain_db = -3.0;
      clip.has_audio = variant % 2 == 0;
      clip.audio_detached = variant == 4;
      clip.proxy_path = QStringLiteral("/absent/proxy.mov");
      clip.proxy_status = QStringLiteral("ready");
      clip.proxy_enabled = true;
      clip.trim_start_seconds = 0.25;
      clip.transition_in_name = variant == 0 ? QString{} : QStringLiteral("Dissolve");
      clip.transition_in_seconds = variant == 0 ? 0.0 : 0.5;
      clip.keyframes = {{QStringLiteral("opacity"), 0.0, 0.2, QStringLiteral("linear")}};
      if (variant >= 4) {
        clip.keyframes.push_back({QStringLiteral("sourceTime"), 0.0, 0.25, QStringLiteral("linear")});
        clip.keyframes.push_back({QStringLiteral("sourceTime"), 1.0, 1.25, QStringLiteral("linear")});
      }
      const auto context = controller.clipMapContext(controller.orderedSelectableClipRefs());
      const auto index = context.flattened_index_by_clip_id.value(clip.clip_id);
      const auto full = controller.clipMap(clip, index, track, &context, false, false, false);
      QVariantMap expected;
      for (const auto& key : reference_keys) {
        const auto found = full.constFind(key);
        if (found != full.cend()) expected.insert(key, found.value());
      }
      const auto compact = controller.compactTimelineClipMap(clip, index, track, context);
      require(compact == expected, "Compact timeline record changed field values or presence");
      const auto selected = controller.tracks_.at(0).clips.constFirst();
      const auto active = controller.timelineTracks().at(0).toMap()
          .value(QStringLiteral("clips")).toList().constFirst().toMap();
      const auto active_expected = controller.clipMap(selected, 0, controller.tracks_.at(0),
          &context, true, false, true);
      require(active == active_expected, "Active timeline clip lost inspector fields");
    }
  }
  static void verifyMultiSelectionAndCheckpoint(TimelineDocumentController& controller) {
    seed(controller, 60, 3);
    const auto before = document(controller);
    const auto& secondary = controller.tracks_.at(1).clips.at(1);
    controller.selected_clip_ids_.append(secondary.clip_id);
    controller.timeline_tracks_projection_cache_valid_ = false;
    const auto projection = controller.timelineTracks();
    const auto projected = projection.at(1).toMap().value("clips").toList().at(1).toMap();
    const auto full = controller.clipMap(secondary, 4, controller.tracks_.at(1));
    require(projected.value("selected").toBool(), "Secondary selection lost its selected flag");
    require(projected.value("keyframes") == full.value("keyframes") &&
        projected.value("keyframedParameters") == full.value("keyframedParameters"),
        "Secondary selection lost editable inline keyframes");
    for (const auto* field : {"opacity", "frameScale", "audioGainDb"})
      require(projected.value(field) == full.value(field), "Secondary selection lost animated strip values");
    require(!projected.contains("effectDetails"), "Secondary selection allocated inspector effect records");
    require(controller.currentClip().contains("effectDetails"), "Primary inspector lost effect records");
    auto* clip = &controller.tracks_[0].clips[0];
    controller.markers_.append(TimelineDocumentController::Marker{});
    auto* marker = &controller.markers_[0];
    const auto expected = document(controller);
    const auto snapshot = controller.captureDocumentSnapshot();
    clip->display_label = "Changed through an existing pointer";
    clip->effect_states[QStringLiteral("Blur")].mask.center_x = 0.25;
    marker->label = "Changed marker";
    controller.timeline_name_ = "Changed document";
    auto captured = controller.serializeDocumentSnapshot(snapshot);
    captured.remove("updatedAt");
    require(captured == expected, "Worker snapshot changed with live edits");
    controller.selected_clip_ids_ = {controller.selected_clip_id_};
    require(document(controller) != before, "Snapshot mutation fixture did not change live values");
  }
  static void verifyTrackPublication(TimelineDocumentController& controller) {
    seed(controller, 60, 3);
    int sequence_events = 0;
    int editor_events = 0;
    int unrelated_events = 0;
    const auto sequence = QObject::connect(&controller, &TimelineDocumentController::sequenceChanged,
        [&] { ++sequence_events; });
    const auto editor = QObject::connect(&controller, &TimelineDocumentController::editorStateChanged,
        [&] { ++editor_events; });
    const auto media = QObject::connect(&controller, &TimelineDocumentController::mediaBinsChanged,
        [&] { ++unrelated_events; });
    const auto identity = QObject::connect(&controller, &TimelineDocumentController::timelineStateChanged,
        [&] { ++unrelated_events; });
    for (int type = 0; type < 4; ++type) {
      const auto before = document(controller);
      controller.timeline_tracks_projection_cache_valid_ = false;
      const auto warm_projection = controller.timelineTracks();
      require(!warm_projection.isEmpty(), "Track-add projection fixture should be warm");
      sequence_events = editor_events = unrelated_events = 0;
      if (type == 0) controller.addVideoTrack();
      else if (type == 1) require(controller.addIndependentVideoTrack(), "Video track add rejected");
      else if (type == 2) require(controller.addGraphicsTrack(), "Graphics track add rejected");
      else require(controller.addIndependentAudioTrack("music"), "Audio track add rejected");
      require(sequence_events == 1 && editor_events == 1 && unrelated_events == 0,
          "Track add lost editorial state or republished unchanged identity/media organization");
      const auto added = document(controller);
      require(added != before, "Track add did not change the document");
      const auto warm_incremental_projection = controller.timelineTracks();
      controller.timeline_tracks_projection_cache_valid_ = false;
      const auto fresh_projection = controller.timelineTracks();
      require(warm_incremental_projection == fresh_projection,
          "Warm empty-track insertion projection differs from a fresh full projection");
      controller.undo();
      require(document(controller) == before, "Track add undo changed the original document");
      controller.redo();
      require(document(controller) == added, "Track add redo lost the new track");
    }
    QObject::disconnect(sequence);
    QObject::disconnect(editor);
    QObject::disconnect(media);
    QObject::disconnect(identity);
  }
  static double p95(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(samples.size()))) - 1U];
  }
  static QJsonObject measure(TimelineDocumentController& controller) {
    const auto selected_index = controller.selectedClipRef().clip_index;
    std::vector<double> captures;
    for (int iteration = 0; iteration < 36; ++iteration) {
      // Existing edit methods obtain mutable pointers before capturing history.
      auto* clip = &controller.tracks_[0].clips[selected_index];
      const auto previous = clip->trim_start_seconds;
      QElapsedTimer timer; timer.start();
      controller.pushUndoSnapshot();
      captures.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      clip->trim_start_seconds = static_cast<double>(iteration + 1) / 60.0;
      if (controller.undo_stack_.constLast().tracks.at(0).clips.at(selected_index).trim_start_seconds != previous)
        throw std::runtime_error("Mutable clip pointer altered a captured snapshot");
    }
    QSet<const void*> clip_blocks;
    qint64 retained_clip_capacity_bytes = 0;
    for (const auto& snapshot : std::as_const(controller.undo_stack_)) {
      for (const auto& track : snapshot.tracks) {
        if (clip_blocks.contains(track.clips.constData())) continue;
        clip_blocks.insert(track.clips.constData());
        retained_clip_capacity_bytes += static_cast<qint64>(track.clips.capacity()) *
            static_cast<qint64>(sizeof(TimelineDocumentController::Clip));
      }
    }
    const auto depth = controller.undo_stack_.size();
    controller.undo_stack_.clear();
    std::vector<double> trims;
    for (int iteration = 0; iteration < 36; ++iteration) {
      QElapsedTimer timer; timer.start();
      controller.beginContinuousEdit();
      controller.setSelectedTrimStartSeconds(static_cast<double>(iteration % 20 + 1) / 30.0);
      controller.endContinuousEdit();
      trims.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
    }
    if (controller.undo_stack_.isEmpty()) throw std::runtime_error("Public trim did not record history");
    const auto final_trim = controller.tracks_.at(0).clips.at(selected_index).trim_start_seconds;
    controller.undo();
    controller.redo();
    if (controller.tracks_.at(0).clips.at(selected_index).trim_start_seconds != final_trim)
      throw std::runtime_error("Public undo/redo did not restore the trim");
    std::vector<double> projection_times;
    std::vector<double> selection_times;
    for (int iteration = 0; iteration < 12; ++iteration) {
      QElapsedTimer timer; timer.start();
      const auto projection = controller.timelineTracks();
      projection_times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (projection.size() != controller.tracks_.size())
        throw std::runtime_error("Timeline projection lost tracks");
      timer.restart();
      const auto selection = controller.currentClip();
      selection_times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      if (selection.value(QStringLiteral("clipId")).toString() != controller.selected_clip_id_)
        throw std::runtime_error("Selection projection lost the selected clip");
    }
    controller.timeline_tracks_projection_cache_valid_ = false;
    static_cast<void>(controller.timelineTracks());
    QVariantList inserted_projection;
    const auto projection_subscription = QObject::connect(
        &controller, &TimelineDocumentController::sequenceChanged,
        &controller, [&] { inserted_projection = controller.timelineTracks(); });
    QElapsedTimer track_add_timer;
    track_add_timer.start();
    controller.addVideoTrack();
    const auto track_add_projection_ms =
        static_cast<double>(track_add_timer.nsecsElapsed()) / 1e6;
    QObject::disconnect(projection_subscription);
    if (inserted_projection.size() != controller.tracks_.size())
      throw std::runtime_error("Warm track insertion did not publish the complete projection");
    return {{"capture_p95_ms", p95(captures)}, {"trim_p95_ms", p95(trims)},
        {"timeline_projection_p95_ms", p95(projection_times)},
        {"selection_projection_p95_ms", p95(selection_times)},
        {"warm_track_add_projection_ms", track_add_projection_ms},
        {"retained_clip_capacity_bytes", static_cast<double>(retained_clip_capacity_bytes)},
        {"unique_clip_blocks", clip_blocks.size()}, {"history_depth", depth}, {"exact_restore", true}};
  }
};
}

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QTemporaryDir workspace;
  if (!workspace.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", workspace.path().toUtf8());
  QApplication app(argc, argv);
  if (argc == 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--verify")) {
    try {
      photo_editor::desktop::TimelineDocumentController controller;
      photo_editor::desktop::TimelineHistoryTestAccess::verify(controller, workspace.path());
      photo_editor::desktop::TimelineHistoryTestAccess::verifyProjection(controller);
      photo_editor::desktop::TimelineHistoryTestAccess::verifyMultiSelectionAndCheckpoint(controller);
      photo_editor::desktop::TimelineHistoryTestAccess::verifyTrackPublication(controller);
      photo_editor::desktop::TimelineHistoryTestAccess::verifyRenderCacheSignatureParity(controller);
      std::cout << "timeline_history_contract_passed\n";
      return 0;
    } catch (const std::exception& error) {
      std::cerr << error.what() << '\n'; return 1;
    }
  }
  if (argc != 4 && argc != 5) return 2;
  const bool last = argc == 5 && QString::fromLocal8Bit(argv[4]) == QStringLiteral("--last");
  if (argc == 5 && !last) return 2;
  bool count_ok = false, lanes_ok = false;
  const int count = QString::fromLocal8Bit(argv[1]).toInt(&count_ok);
  const int lanes = QString::fromLocal8Bit(argv[2]).toInt(&lanes_ok);
  if (!count_ok || !lanes_ok || count < 1 || count > 10000 || lanes < 1 || lanes > count) return 2;
  try {
    photo_editor::desktop::TimelineDocumentController controller;
    photo_editor::desktop::TimelineHistoryTestAccess::seed(controller, count, lanes, last);
    auto report = photo_editor::desktop::TimelineHistoryTestAccess::measure(controller);
#if !defined(_WIN32)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 3;
    auto peak_rss_bytes = static_cast<double>(usage.ru_maxrss);
#if !defined(__APPLE__)
    peak_rss_bytes *= 1024.0;
#endif
    report.insert("peak_rss_bytes", peak_rss_bytes);
#else
    report.insert("peak_rss_bytes", QJsonValue(QJsonValue::Null));
#endif
    report.insert("position", last ? "last" : "first");
    report.insert("clips", count);
    report.insert("tracks", lanes);
    QFile file(QString::fromLocal8Bit(argv[3]));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) return 3;
    std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << '\n';
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
  }
  return 0;
}
