#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDevice>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QTimer>

#include "timeline_document_controller.h"

namespace photo_editor::desktop {

struct TimelineHistoryTestAccess final {
  static QJsonObject document(const TimelineDocumentController& controller) {
    return controller.documentObject();
  }

  static void clearDirty(TimelineDocumentController& controller) {
    controller.dirty_ = false;
  }

  static int flattenedIndexForClipId(
      const TimelineDocumentController& controller,
      const QString& clip_id) {
    return controller.flattenedIndexForClipId(clip_id);
  }

  static void appendMissingMulticamCut(
      TimelineDocumentController& controller,
      const QString& clip_id) {
    if (!controller.multicam_groups_.isEmpty()) {
      controller.multicam_groups_.first().cuts.push_back({3.0, clip_id});
    }
  }

  static bool seedAnimatedGradeSource(
      TimelineDocumentController& controller,
      const QString& clip_id,
      const QSize& source_size) {
    for (auto& track : controller.tracks_) {
      for (auto& clip : track.clips) {
        if (clip.clip_id != clip_id) continue;
        clip.source_size = source_size;
        clip.keyframes.push_back({
            QStringLiteral("grade:exposure"), 0.0, 0.0, QStringLiteral("linear")});
        return true;
      }
    }
    return false;
  }

  static qsizetype gradeCapabilityCacheSize(
      const TimelineDocumentController& controller) {
    return controller.grade_export_capability_cache_.size();
  }
};

}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::TimelineDocumentController;

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString insert_video(
    TimelineDocumentController& controller,
    const QString& id,
    const QString& path,
    const QString& label,
    int track,
    double start,
    double duration = 1.0) {
  controller.insertClipFromGalleryAtTrack(
      id, path, label, duration, 0.0, duration, QString{}, track, start);
  for (const auto& value : controller.clips()) {
    const auto row = value.toMap();
    if (row.value(QStringLiteral("assetId")).toString() == id) {
      return row.value(QStringLiteral("clipId")).toString();
    }
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("temporary interaction-test workspace should be created");
  }
  qputenv("XDG_DATA_HOME", workspace.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", workspace.filePath(QStringLiteral("cache")).toUtf8());
  qputenv(
      "PHOTO_EDITOR_APP_DATA_ROOT",
      workspace.filePath(QStringLiteral("app-data")).toUtf8());

  QApplication app(argc, argv);
  QStandardPaths::setTestModeEnabled(true);

  const auto media_path = workspace.filePath(QStringLiteral("match.mp4"));
  QFile media(media_path);
  if (!media.open(QIODevice::WriteOnly) || media.write("fixture", 7) != 7) {
    return fail("test media fixture should be writable");
  }
  media.close();

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Interaction Query Contracts")},
      {QStringLiteral("frameRate"), 30.0},
  });
  insert_video(controller, QStringLiteral("clip:first"), media_path,
               QStringLiteral("Search Alpha"), 0, 0.0);
  insert_video(controller, QStringLiteral("clip:second"), media_path,
               QStringLiteral("Search Beta"), 0, 2.0);
  controller.addVideoTrack();
  insert_video(controller, QStringLiteral("clip:overlay"), media_path,
               QStringLiteral("Overlay"), 1, 1.0);
  insert_video(controller, QStringLiteral("clip:linked-overlay"), media_path,
               QStringLiteral("Linked Overlay"), 1, 2.0);
  const auto initial_clip_rows = controller.clips();
  auto second_index = -1;
  auto linked_overlay_index = -1;
  for (int index = 0; index < initial_clip_rows.size(); ++index) {
    const auto id = initial_clip_rows.at(index).toMap()
                        .value(QStringLiteral("assetId")).toString();
    if (id == QStringLiteral("clip:second")) second_index = index;
    if (id == QStringLiteral("clip:linked-overlay")) linked_overlay_index = index;
  }
  if (second_index < 0 || linked_overlay_index < 0) {
    return fail("cross-track linked-group fixture clips should be addressable");
  }
  controller.selectClip(second_index);
  controller.toggleClipSelection(linked_overlay_index);
  if (!controller.groupSelectedClips()) {
    return fail("cross-track clips should form a linked group for gap-safety validation");
  }

  const auto gaps = controller.timelineGaps();
  if (gaps.size() != 1 ||
      gaps.constFirst().toMap().value(QStringLiteral("scope")).toString() !=
          QStringLiteral("track") ||
      gaps.constFirst().toMap().value(QStringLiteral("trackId")).toString().isEmpty() ||
      gaps.constFirst().toMap().value(QStringLiteral("safeToClose")).toBool()) {
    return fail("gap projection should preserve uncovered intervals when another lane overlaps");
  }
  const auto gap = gaps.constFirst().toMap();
  const auto navigation = controller.navigateTimelineGap(0.0, 1, 0.0, 0.0);
  if (navigation.value(QStringLiteral("gapId")).toString() !=
          gap.value(QStringLiteral("gapId")).toString() ||
      qAbs(controller.sequencePlayheadSeconds() - 1.5) > 0.001) {
    return fail("gap navigation should return and seek the selected gap from one query result");
  }

  const auto search = controller.timelineSearchResults(
      QStringLiteral("Search"), QStringLiteral("video"));
  auto expected_alpha_index = -1;
  auto expected_beta_index = -1;
  const auto flattened_clips = controller.clips();
  for (int index = 0; index < flattened_clips.size(); ++index) {
    const auto label = flattened_clips.at(index).toMap()
                           .value(QStringLiteral("displayLabel")).toString();
    if (label == QStringLiteral("Search Alpha")) expected_alpha_index = index;
    if (label == QStringLiteral("Search Beta")) expected_beta_index = index;
  }
  if (search.size() != 2 ||
      search.at(0).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Search Alpha") ||
      search.at(1).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Search Beta") ||
      expected_alpha_index < 0 || expected_beta_index <= expected_alpha_index ||
      search.at(0).toMap().value(QStringLiteral("clipIndex")).toInt() !=
          expected_alpha_index ||
      search.at(1).toMap().value(QStringLiteral("clipIndex")).toInt() !=
          expected_beta_index) {
    std::cerr << "query contract expected alpha/beta indices "
              << expected_alpha_index << '/' << expected_beta_index
              << " but got ";
    for (const auto& value : search) {
      const auto result = value.toMap();
      std::cerr << result.value(QStringLiteral("label")).toString().toStdString()
                << ':' << result.value(QStringLiteral("clipIndex")).toInt() << ' ';
    }
    std::cerr << '\n';
    return fail("search results should retain flattened timeline order and distinct clip indices");
  }

  const auto audio_path = workspace.filePath(QStringLiteral("match.wav"));
  QFile audio(audio_path);
  if (!audio.open(QIODevice::WriteOnly) || audio.write("fixture", 7) != 7) {
    return fail("audio search fixture should be writable");
  }
  audio.close();
  controller.addClipFromGallery(QStringLiteral("asset:audio"), audio_path,
                                QStringLiteral("Audio Query Clip"), 1.0);
  const auto audio_search = controller.timelineSearchResults(
      QStringLiteral("Audio Query"), QStringLiteral("audio"));
  if (audio_search.size() != 1 ||
      audio_search.constFirst().toMap().value(QStringLiteral("mediaType")).toString() !=
          QStringLiteral("audio")) {
    return fail("audio-only search should retain audio-track ordering and type");
  }

  const auto offline_path = workspace.filePath(QStringLiteral("offline.mp4"));
  QFile offline_fixture(offline_path);
  if (!offline_fixture.open(QIODevice::WriteOnly) ||
      offline_fixture.write("offline", 7) != 7) {
    return fail("offline source fixture should exist while timeline clips are inserted");
  }
  offline_fixture.close();
  const auto offline_clip_a = insert_video(
      controller, QStringLiteral("clip:offline-a"), offline_path,
      QStringLiteral("Offline A"), 0, 4.0);
  const auto offline_clip_b = insert_video(
      controller, QStringLiteral("clip:offline-b"), offline_path,
      QStringLiteral("Offline B"), 0, 6.0);
  if (offline_clip_a.isEmpty() || offline_clip_b.isEmpty() ||
      offline_clip_a == offline_clip_b) {
    return fail("offline fixture should insert both distinct timeline clip IDs");
  }
  QFile::remove(offline_path);
  if (!controller.hasOfflineMediaItems()) {
    return fail("offline-media presence should be available without building the full model");
  }
  const auto offline_items = controller.offlineMediaItems();
  const auto offline = std::find_if(
      offline_items.cbegin(), offline_items.cend(), [&offline_path](const QVariant& value) {
        return value.toMap().value(QStringLiteral("originalPath")).toString() == offline_path;
      });
  if (offline == offline_items.cend() ||
      offline->toMap().value(QStringLiteral("useCount")).toInt() != 4) {
    return fail("offline media should preserve the full use count including paired silent audio rows");
  }
  QFile offline_file(offline_path);
  if (!offline_file.open(QIODevice::WriteOnly) || offline_file.write("online", 6) != 6) {
    return fail("offline fixture should become readable");
  }
  offline_file.close();
  if (controller.hasOfflineMediaItems()) {
    return fail("offline-media presence should reflect a source file appearing on disk");
  }
  QFile::remove(offline_path);
  if (!controller.hasOfflineMediaItems()) {
    return fail("offline-media presence should reflect a source file disappearing from disk");
  }

  TimelineDocumentController versioned;
  versioned.createTimeline(QStringLiteral("External Version Index Contracts"));
  insert_video(versioned, QStringLiteral("version:clip"), media_path,
               QStringLiteral("Version Clip"), 0, 0.0);
  if (!versioned.createSequenceVersion(QStringLiteral("Before External Edit"))) {
    return fail("sequence-version fixture should create a snapshot");
  }
  const auto before_external_edit = versioned.sequenceVersions();
  if (before_external_edit.size() != 1) {
    return fail("sequence-version index should contain the created snapshot");
  }
  const auto version_path = before_external_edit.constFirst().toMap()
                                .value(QStringLiteral("documentPath")).toString();
  QFile version_file(version_path);
  if (!version_file.open(QIODevice::ReadOnly)) {
    return fail("sequence-version file should be readable for the external-edit fixture");
  }
  auto version_document = QJsonDocument::fromJson(version_file.readAll()).object();
  version_file.close();
  auto version_metadata = version_document.value(QStringLiteral("_sequenceVersion")).toObject();
  version_metadata.insert(QStringLiteral("label"),
                          QStringLiteral("External file edit changes the indexed metadata"));
  version_document.insert(QStringLiteral("_sequenceVersion"), version_metadata);
  if (!version_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      version_file.write(QJsonDocument(version_document).toJson()) <= 0) {
    return fail("sequence-version file should accept a direct external metadata edit");
  }
  version_file.close();
  const auto after_external_edit = versioned.sequenceVersions();
  if (after_external_edit.size() != 1 ||
      after_external_edit.constFirst().toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("External file edit changes the indexed metadata")) {
    return fail("sequence-version cache should invalidate on external file size or mtime changes");
  }

  const auto real_ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (real_ffmpeg.isEmpty()) {
    return fail("color-scope async contract requires the configured ffmpeg executable");
  }
  const auto wrapper_dir = workspace.filePath(QStringLiteral("delayed-media-tools"));
  if (!QDir().mkpath(wrapper_dir)) {
    return fail("color-scope async fixture should create its tool directory");
  }
  auto shell_quote = real_ffmpeg;
  shell_quote.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  QFile ffmpeg_wrapper(QDir(wrapper_dir).filePath(QStringLiteral("ffmpeg")));
  const auto wrapper_body = QStringLiteral(
      "#!/bin/sh\n"
      "delay=0.05\n"
      "for argument in \"$@\"; do\n"
      "  case \"$argument\" in *scope-slow.png) delay=0.7;; esac\n"
      "done\n"
      "sleep \"$delay\"\n"
      "exec '%1' \"$@\"\n").arg(shell_quote);
  if (!ffmpeg_wrapper.open(QIODevice::WriteOnly) ||
      ffmpeg_wrapper.write(wrapper_body.toUtf8()) != wrapper_body.toUtf8().size()) {
    return fail("color-scope delay wrapper should be writable");
  }
  ffmpeg_wrapper.close();
  if (!ffmpeg_wrapper.setPermissions(
          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
          QFileDevice::ExeOther)) {
    return fail("color-scope delay wrapper should be executable");
  }
  const auto original_path = qgetenv("PATH");
  qputenv("PATH", wrapper_dir.toUtf8() + QByteArrayLiteral(":") + original_path);
  const auto slow_image_path = workspace.filePath(QStringLiteral("scope-slow.png"));
  const auto fast_image_path = workspace.filePath(QStringLiteral("scope-fast.png"));
  QImage slow_image(96, 64, QImage::Format_RGB32);
  slow_image.fill(QColor(QStringLiteral("#cc5544")));
  QImage fast_image(96, 64, QImage::Format_RGB32);
  fast_image.fill(QColor(QStringLiteral("#3366aa")));
  if (!slow_image.save(slow_image_path) || !fast_image.save(fast_image_path)) {
    qputenv("PATH", original_path);
    return fail("color-scope source images should be writable");
  }
  TimelineDocumentController scopes;
  scopes.createTimeline(QStringLiteral("Latest Color Scope Contracts"));
  scopes.addClipFromGallery(QStringLiteral("scope:slow"), slow_image_path,
                            QStringLiteral("Slow Scope"), 1.0);
  scopes.addClipFromGallery(QStringLiteral("scope:fast"), fast_image_path,
                            QStringLiteral("Fast Scope"), 1.0);
  scopes.selectClip(0);
  if (!scopes.requestSelectedClipColorScopes() ||
      !scopes.colorScopeAnalysisBusy()) {
    qputenv("PATH", original_path);
    return fail("source color scopes should start asynchronously and expose busy state");
  }
  scopes.selectClip(1);
  const auto latest_scope_clip_id =
      scopes.currentClip().value(QStringLiteral("clipId")).toString();
  if (!scopes.requestSelectedClipColorScopes()) {
    qputenv("PATH", original_path);
    return fail("a newer source color scope request should be accepted while one is active");
  }
  QEventLoop scope_wait;
  QObject::connect(&scopes, &TimelineDocumentController::colorScopeAnalysisChanged,
                   &scope_wait, [&scopes, &scope_wait]() {
                     if (!scopes.colorScopeAnalysisBusy()) {
                       scope_wait.quit();
                     }
                   });
  QTimer::singleShot(5000, &scope_wait, &QEventLoop::quit);
  if (scopes.colorScopeAnalysisBusy()) {
    scope_wait.exec();
  }
  if (scopes.colorScopeAnalysisBusy() ||
      scopes.colorScopeAnalysisData().value(QStringLiteral("clipId")).toString() !=
          latest_scope_clip_id) {
    qputenv("PATH", original_path);
    return fail("latest source scope result should replace an older in-flight selection result");
  }
  QEventLoop stale_result_wait;
  QTimer::singleShot(1000, &stale_result_wait, &QEventLoop::quit);
  stale_result_wait.exec();
  qputenv("PATH", original_path);
  if (scopes.colorScopeAnalysisData().value(QStringLiteral("clipId")).toString() !=
      latest_scope_clip_id) {
    return fail("a late stale source-scope result should not replace the latest selection");
  }

  const auto grade_video_path = workspace.filePath(QStringLiteral("grade-probe-source.mp4"));
  QProcess video_generator;
  video_generator.start(real_ffmpeg,
                        {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
                         QStringLiteral("error"), QStringLiteral("-f"),
                         QStringLiteral("lavfi"), QStringLiteral("-i"),
                         QStringLiteral("color=c=red:s=320x180:r=30:d=1"),
                         QStringLiteral("-c:v"), QStringLiteral("mpeg4"),
                         QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                         QStringLiteral("-an"), QStringLiteral("-y"), grade_video_path});
  if (!video_generator.waitForStarted(3000) || !video_generator.waitForFinished(15000) ||
      video_generator.exitStatus() != QProcess::NormalExit ||
      video_generator.exitCode() != 0) {
    return fail("a valid short video should be generated for the cold grade-probe contract");
  }
  const auto real_ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (real_ffprobe.isEmpty()) {
    return fail("cold grade-probe contract requires ffprobe");
  }
  const auto probe_wrapper_dir = workspace.filePath(QStringLiteral("delayed-probe-tools"));
  if (!QDir().mkpath(probe_wrapper_dir)) {
    return fail("cold grade-probe fixture should create its tool directory");
  }
  auto probe_shell_quote = real_ffprobe;
  probe_shell_quote.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
  QFile ffprobe_wrapper(QDir(probe_wrapper_dir).filePath(QStringLiteral("ffprobe")));
  const auto probe_wrapper_body = QStringLiteral(
      "#!/bin/sh\n"
      "sleep 0.8\n"
      "exec '%1' \"$@\"\n").arg(probe_shell_quote);
  if (!ffprobe_wrapper.open(QIODevice::WriteOnly) ||
      ffprobe_wrapper.write(probe_wrapper_body.toUtf8()) !=
          probe_wrapper_body.toUtf8().size()) {
    return fail("delayed ffprobe wrapper should be writable");
  }
  ffprobe_wrapper.close();
  if (!ffprobe_wrapper.setPermissions(
          QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
          QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
          QFileDevice::ExeOther)) {
    return fail("delayed ffprobe wrapper should be executable");
  }
  const auto grade_probe_original_path = qgetenv("PATH");
  qputenv("PATH", probe_wrapper_dir.toUtf8() + QByteArrayLiteral(":") +
                       grade_probe_original_path);
  TimelineDocumentController grade_probe;
  grade_probe.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Cold Grade Probe Contract")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 30.0},
  });
  grade_probe.addClipFromGallery(QStringLiteral("grade:probe"), grade_video_path,
                                 QStringLiteral("Grade Probe"), 1.0);
  const auto grade_clip_id = grade_probe.clips().constFirst().toMap()
                                 .value(QStringLiteral("clipId")).toString();
  const auto layer_result =
      grade_probe.addClipColorGradeAdjustmentLayerForAutomation(grade_clip_id);
  if (!layer_result.value(QStringLiteral("ok")).toBool() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::seedAnimatedGradeSource(
          grade_probe, grade_clip_id, QSize(320, 180))) {
    qputenv("PATH", grade_probe_original_path);
    return fail("animated-grade probe fixture should be prepared successfully");
  }
  grade_probe.selectClip(0);
  QEventLoop grade_probe_wait;
  QObject::connect(&grade_probe, &TimelineDocumentController::selectionChanged,
                   &grade_probe_wait, [&grade_probe, &grade_probe_wait]() {
                     if (!grade_probe.currentClip()
                              .value(QStringLiteral("colorGradeSequenceExportPending"))
                              .toBool()) {
                       grade_probe_wait.quit();
                     }
                   });
  QElapsedTimer cold_getter_timer;
  cold_getter_timer.start();
  auto grade_capability = grade_probe.currentClip();
  if (!grade_capability.value(QStringLiteral("colorGradeSequenceExportPending")).toBool() ||
      !grade_capability.value(QStringLiteral("colorGradeSequenceExportSupported")).toBool() ||
      cold_getter_timer.elapsed() > 250) {
    qputenv("PATH", grade_probe_original_path);
    return fail("cold grade inspector should report supported-pending without waiting for ffprobe");
  }
  QTimer::singleShot(5000, &grade_probe_wait, &QEventLoop::quit);
  grade_probe_wait.exec();
  grade_capability = grade_probe.currentClip();
  if (grade_capability.value(QStringLiteral("colorGradeSequenceExportPending")).toBool() ||
      !grade_capability.value(QStringLiteral("colorGradeSequenceExportSupported")).toBool() ||
      !grade_capability.value(QStringLiteral("colorGradeSequenceExportUsesFrameStream")).toBool()) {
    qputenv("PATH", grade_probe_original_path);
    return fail("completed ffprobe should preserve supported frame-stream grade eligibility");
  }
  QFile changed_grade_source(grade_video_path);
  if (!changed_grade_source.open(QIODevice::Append) ||
      changed_grade_source.write("source identity changed", 23) != 23) {
    qputenv("PATH", grade_probe_original_path);
    return fail("grade source should accept a harmless appended identity change");
  }
  changed_grade_source.close();
  cold_getter_timer.restart();
  grade_capability = grade_probe.currentClip();
  const auto changed_source_pending =
      grade_capability.value(QStringLiteral("colorGradeSequenceExportPending")).toBool();
  if (!changed_source_pending || cold_getter_timer.elapsed() > 250) {
    qputenv("PATH", grade_probe_original_path);
    return fail("external source edits should invalidate grade capability and reprobe asynchronously");
  }
  QEventLoop changed_source_probe_wait;
  QObject::connect(&grade_probe, &TimelineDocumentController::selectionChanged,
                   &changed_source_probe_wait, [&grade_probe, &changed_source_probe_wait]() {
                     if (!grade_probe.currentClip()
                              .value(QStringLiteral("colorGradeSequenceExportPending"))
                              .toBool()) {
                       changed_source_probe_wait.quit();
                     }
                   });
  QTimer::singleShot(5000, &changed_source_probe_wait, &QEventLoop::quit);
  changed_source_probe_wait.exec();
  grade_capability = grade_probe.currentClip();
  qputenv("PATH", grade_probe_original_path);
  if (grade_capability.value(QStringLiteral("colorGradeSequenceExportPending")).toBool() ||
      !grade_capability.value(QStringLiteral("colorGradeSequenceExportSupported")).toBool()) {
    return fail("the changed source identity should finish a fresh compatible-source probe");
  }

  TimelineDocumentController adjustment_identity;
  adjustment_identity.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Adjustment Source Identity Contract")},
      {QStringLiteral("width"), 320},
      {QStringLiteral("height"), 180},
      {QStringLiteral("frameRate"), 30.0},
  });
  adjustment_identity.addClipFromGallery(QStringLiteral("ungraded:underlay"),
                                         grade_video_path,
                                         QStringLiteral("Ungraded Underlay"), 1.0);
  const auto adjustment_row =
      adjustment_identity.addAdjustmentClipForAutomation(0.0, 1.0);
  const auto adjustment_clip_id =
      adjustment_row.value(QStringLiteral("clipId")).toString();
  const auto adjustment_layer = adjustment_identity
                                   .addClipColorGradeAdjustmentLayerForAutomation(
                                       adjustment_clip_id);
  if (!adjustment_row.value(QStringLiteral("ok")).toBool() ||
      !adjustment_layer.value(QStringLiteral("ok")).toBool() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::seedAnimatedGradeSource(
          adjustment_identity, adjustment_clip_id, QSize{})) {
    return fail("graded-adjustment cache fixture should retain an ungraded video underlay");
  }
  const auto adjustment_capability_before = adjustment_identity.currentClip();
  const auto adjustment_cache_size_before =
      photo_editor::desktop::TimelineHistoryTestAccess::gradeCapabilityCacheSize(
          adjustment_identity);
  if (!adjustment_capability_before
           .value(QStringLiteral("colorGradeSequenceExportSupported")).toBool() ||
      adjustment_cache_size_before == 0) {
    return fail("graded adjustment capability should be cached over its ungraded video underlay");
  }
  QFile changed_underlay_source(grade_video_path);
  if (!changed_underlay_source.open(QIODevice::Append) ||
      changed_underlay_source.write("ungraded underlay identity", 26) != 26) {
    return fail("ungraded underlying video should accept an external identity change");
  }
  changed_underlay_source.close();
  static_cast<void>(adjustment_identity.currentClip());
  const auto adjustment_cache_size_after =
      photo_editor::desktop::TimelineHistoryTestAccess::gradeCapabilityCacheSize(
          adjustment_identity);
  if (adjustment_cache_size_after <= adjustment_cache_size_before) {
    return fail("external edits to ungraded underlay videos should invalidate adjustment route cache");
  }

  TimelineDocumentController bins;
  bins.createTimeline(QStringLiteral("Bin Query Contracts"));
  const auto parent_id = bins.createMediaBin(QStringLiteral("Parent"));
  const auto child_id = bins.createMediaBin(QStringLiteral("Child"), parent_id);
  if (parent_id.isEmpty() || child_id.isEmpty() ||
      !bins.assignMediaAssetToBin(QStringLiteral("asset:one"), parent_id) ||
      !bins.assignMediaAssetToBin(QStringLiteral("asset:two"), child_id)) {
    return fail("media-bin fixture should create a hierarchy and assignments");
  }
  const auto bin_rows = bins.mediaBins();
  if (bin_rows.size() != 2 ||
      bin_rows.at(0).toMap().value(QStringLiteral("assetCount")).toInt() != 1 ||
      bin_rows.at(1).toMap().value(QStringLiteral("depth")).toInt() != 1 ||
      bin_rows.at(1).toMap().value(QStringLiteral("assetCount")).toInt() != 1) {
    return fail("media-bin projection should preserve hierarchy depth and direct assignment counts");
  }

  TimelineDocumentController bin_notifications;
  bin_notifications.createTimeline(QStringLiteral("Media Bin Notification Contracts"));
  int media_bins_notifications = 0;
  int active_bin_notifications = 0;
  QObject::connect(
      &bin_notifications, &TimelineDocumentController::mediaBinsChanged,
      [&] { ++media_bins_notifications; });
  QObject::connect(
      &bin_notifications, &TimelineDocumentController::activeMediaBinChanged,
      [&] { ++active_bin_notifications; });
  const auto notification_parent =
      bin_notifications.createMediaBin(QStringLiteral("Notification Parent"));
  const auto notification_child = bin_notifications.createMediaBin(
      QStringLiteral("Notification Child"), notification_parent);
  if (notification_parent.isEmpty() || notification_child.isEmpty() ||
      media_bins_notifications != 2 || active_bin_notifications != 2) {
    return fail("bin creation should publish structure and active-id transitions exactly once");
  }
  bin_notifications.setActiveMediaBinId(notification_child);
  if (media_bins_notifications != 2 || active_bin_notifications != 2) {
    return fail("setting the active bin to its current id should publish no change");
  }
  bin_notifications.setActiveMediaBinId(notification_parent);
  const auto selected_bin_rows = bin_notifications.mediaBins();
  const auto selected_parent_row = std::find_if(
      selected_bin_rows.cbegin(), selected_bin_rows.cend(),
      [&notification_parent](const QVariant& value) {
        return value.toMap().value(QStringLiteral("binId")).toString() == notification_parent;
      });
  const auto selected_child_row = std::find_if(
      selected_bin_rows.cbegin(), selected_bin_rows.cend(),
      [&notification_child](const QVariant& value) {
        return value.toMap().value(QStringLiteral("binId")).toString() == notification_child;
      });
  if (media_bins_notifications != 3 || active_bin_notifications != 3 ||
      selected_parent_row == selected_bin_rows.cend() ||
      !selected_parent_row->toMap().value(QStringLiteral("active")).toBool() ||
      selected_child_row == selected_bin_rows.cend() ||
      selected_child_row->toMap().value(QStringLiteral("active")).toBool()) {
    return fail("active-bin selection should refresh the public active flags exactly once");
  }
  bin_notifications.setActiveMediaBinId(notification_parent);
  if (media_bins_notifications != 3 || active_bin_notifications != 3 ||
      !bin_notifications.deleteMediaBin(notification_parent) ||
      media_bins_notifications != 4 || active_bin_notifications != 4) {
    return fail("deleting the active bin should publish its fallback id once");
  }
  bin_notifications.undo();
  if (bin_notifications.activeMediaBinId() != notification_parent ||
      media_bins_notifications != 5 || active_bin_notifications != 5) {
    return fail("restoring an undo snapshot should publish a changed active-bin id");
  }
  QTemporaryDir bin_document_directory;
  if (!bin_document_directory.isValid()) {
    return fail("media-bin load fixture should have a writable temporary directory");
  }
  const auto bin_document_path =
      QDir(bin_document_directory.path()).filePath(QStringLiteral("loaded-bins.json"));
  QFile bin_document_file(bin_document_path);
  const auto bin_document_json = QJsonDocument(
      photo_editor::desktop::TimelineHistoryTestAccess::document(bin_notifications))
                                    .toJson(QJsonDocument::Compact);
  if (!bin_document_file.open(QIODevice::WriteOnly) ||
      bin_document_file.write(bin_document_json) != bin_document_json.size()) {
    return fail("media-bin load fixture should serialize the source document");
  }
  bin_document_file.close();
  TimelineDocumentController loaded_bins;
  loaded_bins.createTimeline(QStringLiteral("Loaded Bin Destination"));
  const auto prior_loaded_bin = loaded_bins.createMediaBin(QStringLiteral("Prior Bin"));
  photo_editor::desktop::TimelineHistoryTestAccess::clearDirty(loaded_bins);
  int loaded_media_bins_notifications = 0;
  int loaded_active_bin_notifications = 0;
  QObject::connect(
      &loaded_bins, &TimelineDocumentController::mediaBinsChanged,
      [&] { ++loaded_media_bins_notifications; });
  QObject::connect(
      &loaded_bins, &TimelineDocumentController::activeMediaBinChanged,
      [&] { ++loaded_active_bin_notifications; });
  if (prior_loaded_bin.isEmpty() ||
      !loaded_bins.loadTimeline(bin_document_path) ||
      loaded_bins.activeMediaBinId() != bin_notifications.activeMediaBinId() ||
      loaded_bins.activeMediaBinId() == prior_loaded_bin ||
      loaded_media_bins_notifications != 1 || loaded_active_bin_notifications != 1) {
    return fail("loading a document should publish its changed active-bin id after structure");
  }

  TimelineDocumentController multicam;
  multicam.createTimeline(QStringLiteral("Multicam Query Contracts"));
  multicam.addVideoTrack();
  multicam.addClipFromGallery(QStringLiteral("camera:a"), media_path,
                              QStringLiteral("Camera A"), 4.0, QString{});
  multicam.addClipFromGallery(QStringLiteral("camera:b"), media_path,
                              QStringLiteral("Camera B"), 4.0, QString{});
  // Row 1 is A1 and maps back to V1 for video moves. Row 2 is V2, so this
  // fixture actually creates two camera angles on separate video lanes.
  multicam.moveSelectedClipToTrackRow(2, 0.0);
  const auto multicam_rows = multicam.clips();
  QString camera_a_clip_id;
  QString camera_b_clip_id;
  for (const auto& row_value : multicam_rows) {
    const auto row = row_value.toMap();
    const auto asset_id = row.value(QStringLiteral("assetId")).toString();
    if (asset_id == QStringLiteral("camera:a")) {
      camera_a_clip_id = row.value(QStringLiteral("clipId")).toString();
    } else if (asset_id == QStringLiteral("camera:b")) {
      camera_b_clip_id = row.value(QStringLiteral("clipId")).toString();
    }
  }
  const auto camera_a_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(multicam, camera_a_clip_id);
  const auto camera_b_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(multicam, camera_b_clip_id);
  if (camera_a_clip_id.isEmpty() || camera_b_clip_id.isEmpty() ||
      camera_a_index < 0 || camera_b_index < 0 || camera_a_index == camera_b_index) {
    return fail("multicam fixture should resolve both actual camera clips independently");
  }
  multicam.selectClip(camera_a_index);
  multicam.seekCurrentClipSeconds(0.20);
  if (!multicam.setSelectedClipMulticamSyncMarkerAtPlayhead()) {
    return fail("multicam fixture should mark the first selected camera clip");
  }
  const auto updated_camera_b_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(multicam, camera_b_clip_id);
  multicam.selectClip(updated_camera_b_index);
  multicam.seekCurrentClipSeconds(0.50);
  if (!multicam.setSelectedClipMulticamSyncMarkerAtPlayhead()) {
    return fail("multicam fixture should mark the second selected camera clip");
  }
  const auto updated_camera_a_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(multicam, camera_a_clip_id);
  const auto final_camera_b_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(multicam, camera_b_clip_id);
  multicam.selectClip(updated_camera_a_index);
  multicam.toggleClipSelection(final_camera_b_index);
  if (multicam.createMulticamGroupFromSelection(
          QStringLiteral("marker"), QStringLiteral("Two Cameras")).isEmpty() ||
      multicam.multicamGroupCount() != 1) {
    return fail("multicam group count should match the constructed groups");
  }
  const auto group_rows = multicam.multicamGroups();
  const auto angles = group_rows.constFirst().toMap()
                          .value(QStringLiteral("angles")).toList();
  const auto cuts = group_rows.constFirst().toMap()
                        .value(QStringLiteral("cuts")).toList();
  if (angles.size() != 2 ||
      angles.at(0).toMap().value(QStringLiteral("clipId")).toString().isEmpty() ||
      angles.at(1).toMap().value(QStringLiteral("clipId")).toString().isEmpty() ||
      angles.at(0).toMap().value(QStringLiteral("sourceUrl")).toString().isEmpty() ||
      angles.at(1).toMap().value(QStringLiteral("sourceUrl")).toString().isEmpty()) {
    return fail("multicam projection should preserve angle identity and source metadata");
  }
  if (cuts.isEmpty()) {
    return fail("multicam projection should retain cuts for exact angle metadata checks");
  }
  for (const auto& cut_value : cuts) {
    const auto cut = cut_value.toMap();
    const auto angle = std::find_if(
        angles.cbegin(), angles.cend(), [&cut](const QVariant& value) {
          return value.toMap().value(QStringLiteral("clipId")).toString() ==
                 cut.value(QStringLiteral("clipId")).toString();
        });
    if (angle == angles.cend() ||
        cut.value(QStringLiteral("angleLabel")).toString() !=
            angle->toMap().value(QStringLiteral("label")).toString()) {
      return fail("multicam cuts should preserve exact clip-to-angle label metadata");
    }
  }
  photo_editor::desktop::TimelineHistoryTestAccess::appendMissingMulticamCut(
      multicam, QStringLiteral("missing-cut-clip-id"));
  const auto missing_cut_groups = multicam.multicamGroups();
  const auto missing_cuts = missing_cut_groups.constFirst().toMap()
                                .value(QStringLiteral("cuts")).toList();
  const auto missing_cut = missing_cuts.constLast().toMap();
  if (!missing_cut.value(QStringLiteral("angleLabel")).toString().isEmpty() ||
      missing_cut.value(QStringLiteral("angleIndex")).toInt() != -1 ||
      missing_cut.value(QStringLiteral("angleNumber")).toInt() != 0) {
    return fail("missing multicam cut IDs should retain empty-label and invalid-angle metadata");
  }

  TimelineDocumentController extract;
  extract.createTimeline(QStringLiteral("Bulk Extract Query Contracts"));
  insert_video(extract, QStringLiteral("extract:first"), media_path,
               QStringLiteral("Extract First"), 0, 0.0, 1.0);
  insert_video(extract, QStringLiteral("extract:second"), media_path,
               QStringLiteral("Extract Second"), 0, 2.0, 1.0);
  insert_video(extract, QStringLiteral("extract:third"), media_path,
               QStringLiteral("Extract Third"), 0, 4.0, 1.0);
  if (!extract.selectClipsInSequenceRange(0.0, 3.0)) {
    return fail("bulk extract fixture should select its first two clips");
  }
  extract.extractSelectedClips();
  if (extract.clipCount() != 1 ||
      extract.clips().constFirst().toMap().value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("extract:third")) {
    return fail("bulk extract should remove the selected clips while preserving the later clip");
  }
  extract.undo();
  if (extract.clipCount() != 3) {
    return fail("bulk extract undo should restore the original clips");
  }

  TimelineDocumentController abutting;
  abutting.createTimeline(QStringLiteral("Abutting Gap Contracts"));
  insert_video(abutting, QStringLiteral("abut:first"), media_path,
               QStringLiteral("Abutting First"), 0, 0.0, 1.0);
  insert_video(abutting, QStringLiteral("abut:second"), media_path,
               QStringLiteral("Abutting Second"), 0, 1.0, 1.0);
  const auto after_gap_clip_id = insert_video(
      abutting, QStringLiteral("abut:third"), media_path,
      QStringLiteral("After Gap"), 0, 3.0, 1.0);
  const auto abutting_gaps = abutting.timelineGaps();
  const auto has_false_abutting_gap = std::any_of(
      abutting_gaps.cbegin(), abutting_gaps.cend(), [](const QVariant& value) {
        return qAbs(value.toMap().value(QStringLiteral("startSeconds")).toDouble() - 1.0) <
               0.001;
      });
  if (has_false_abutting_gap || abutting_gaps.isEmpty()) {
    return fail("gap indexing should treat abutting clips as continuous while retaining real gaps");
  }
  abutting.toggleTrackLocked(0);
  const auto locked_gap_rows = abutting.timelineGaps();
  if (locked_gap_rows.isEmpty() ||
      std::any_of(locked_gap_rows.cbegin(), locked_gap_rows.cend(),
                  [](const QVariant& value) {
                    return value.toMap().value(QStringLiteral("safeToClose")).toBool();
                  })) {
    return fail("gap close-safety should retain locked-track guards");
  }
  abutting.toggleTrackLocked(0);
  const auto selected_gap = abutting.navigateTimelineGap(1.0, 1, 0.0, 0.0);
  const auto selected_gap_id = selected_gap.value(QStringLiteral("gapId")).toString();
  if (selected_gap_id.isEmpty() ||
      std::none_of(abutting_gaps.cbegin(), abutting_gaps.cend(),
                   [&selected_gap_id](const QVariant& value) {
                     return value.toMap().value(QStringLiteral("gapId")).toString() ==
                            selected_gap_id;
                   })) {
    return fail("gap navigation should use the current indexed query result");
  }
  const auto after_gap_index = photo_editor::desktop::TimelineHistoryTestAccess::
      flattenedIndexForClipId(abutting, after_gap_clip_id);
  if (after_gap_clip_id.isEmpty() || after_gap_index < 0) {
    return fail("gap edit fixture should resolve its actual downstream clip ID");
  }
  abutting.selectClip(after_gap_index);
  abutting.moveSelectedClipToTrackRow(0, 2.0);
  if (!abutting.timelineGaps().isEmpty() ||
      !abutting.navigateTimelineGap(1.0, 1, 0.0, 0.0)
           .value(QStringLiteral("gapId")).toString().isEmpty()) {
    return fail("gap navigation should not reuse a stale query result after edits fill the gap");
  }

  TimelineDocumentController paste;
  paste.createTimeline(QStringLiteral("Paste Comparison Contracts"));
  insert_video(paste, QStringLiteral("paste:source"), media_path,
               QStringLiteral("Paste Source"), 0, 0.0);
  insert_video(paste, QStringLiteral("paste:target"), media_path,
               QStringLiteral("Paste Target"), 0, 2.0);
  paste.selectClip(0);
  paste.addSelectedClipEffect(QStringLiteral("Exposure"));
  if (paste.copySelectedClips() != 1) {
    return fail("paste comparison fixture should copy a source with a distinct effect category");
  }
  paste.selectClip(1);
  const auto paste_comparison = paste.pasteAttributesComparison();
  const auto paste_categories = paste_comparison.value(QStringLiteral("categories")).toList();
  if (!paste_comparison.value(QStringLiteral("available")).toBool() ||
      paste_comparison.value(QStringLiteral("targetCount")).toInt() != 1 ||
      std::none_of(paste_categories.cbegin(), paste_categories.cend(),
                   [](const QVariant& value) {
                     const auto category = value.toMap();
                     return category.value(QStringLiteral("key")).toString() ==
                                QStringLiteral("effects") &&
                            category.value(QStringLiteral("available")).toBool() &&
                            category.value(QStringLiteral("changed")).toBool();
                   })) {
    return fail("paste comparison should report an available changed effects category");
  }

  return EXIT_SUCCESS;
}
