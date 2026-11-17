#include "timeline_document_controller.h"
#include "timeline_video_grade_runtime.h"
#include "edit_recipe_state.h"
#include "photo_editor/pipeline/photo_effect_registry.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QProcess>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <cstdlib>
#include <iostream>
#include <memory>

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return EXIT_FAILURE; }
QObject* find(QQuickWindow* window, const QString& name);
}

class CountingFilterTrackDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(bool hasActiveTimeline READ hasActiveTimeline CONSTANT)

 public:
  CountingFilterTrackDocument() {
    tracks_.reserve(192);
    for (int index = 0; index < 192; ++index) {
      const auto track_id = QStringLiteral("filter-track-%1").arg(index, 3, 10, QLatin1Char('0'));
      const auto clip_id = QStringLiteral("filter-clip-%1").arg(index, 3, 10, QLatin1Char('0'));
      tracks_.push_back(QVariantMap{
          {QStringLiteral("trackId"), track_id},
          {QStringLiteral("trackName"), QStringLiteral("Video %1").arg(index + 1)},
          {QStringLiteral("trackKind"), QStringLiteral("video")},
          {QStringLiteral("trackRole"), QStringLiteral("media")},
          {QStringLiteral("locked"), index == 0},
          {QStringLiteral("clips"), QVariantList{QVariantMap{
               {QStringLiteral("clipId"), clip_id},
               {QStringLiteral("mediaType"), QStringLiteral("video")},
               {QStringLiteral("startSeconds"), index * 0.5},
               {QStringLiteral("trimmedDurationSeconds"), 0.5},
               {QStringLiteral("keyframes"), QVariantList{}},
           }}},
      });
    }
    current_clip_ = {
        {QStringLiteral("clipId"), QStringLiteral("filter-clip-191")},
        {QStringLiteral("mediaType"), QStringLiteral("video")},
        {QStringLiteral("trackId"), QStringLiteral("filter-track-191")},
    };
  }

  QVariantList sequenceTracks() const {
    ++sequence_track_property_reads_;
    return tracks_;
  }
  Q_INVOKABLE QVariantList snapshotSequenceTracks() const {
    ++snapshot_calls_;
    return tracks_;
  }
  QVariantMap currentClip() const { return current_clip_; }
  bool hasActiveTimeline() const { return true; }
  int sequenceTrackPropertyReads() const { return sequence_track_property_reads_; }
  int snapshotCalls() const { return snapshot_calls_; }

  void selectTrack(int index) {
    if (index < 0 || index >= tracks_.size())
      return;
    current_clip_.insert(QStringLiteral("clipId"),
                         QStringLiteral("filter-clip-%1").arg(index, 3, 10, QLatin1Char('0')));
    current_clip_.insert(QStringLiteral("trackId"),
                         QStringLiteral("filter-track-%1").arg(index, 3, 10, QLatin1Char('0')));
    emit selectionChanged();
  }
  void setTrackLocked(int index, bool locked) {
    if (index < 0 || index >= tracks_.size())
      return;
    auto track = tracks_.at(index).toMap();
    track.insert(QStringLiteral("locked"), locked);
    tracks_[index] = track;
    emit sequenceChanged();
  }

 signals:
  void sequenceChanged();
  void selectionChanged();

 private:
  QVariantList tracks_;
  QVariantMap current_clip_;
  mutable int sequence_track_property_reads_ = 0;
  mutable int snapshot_calls_ = 0;
};

namespace {
bool verify_track_snapshot_binding() {
  CountingFilterTrackDocument document;
  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("filterTrackDocument", &document);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components" as Components
Window {
    id: fixture
    width: 500
    height: 700
    visible: true
    property bool panelVisible: false
    Components.Theme { id: theme }
    Components.TimelinePhotoFilters {
        id: filterPanel
        objectName: "snapshotPhotoFilterPanel"
        anchors.fill: parent
        visible: fixture.panelVisible
        theme: theme
        documentController: filterTrackDocument
        currentClip: filterTrackDocument.currentClip
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/filter_host.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return false;
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!window)
    return false;
  window->show();
  QTest::qWait(100);
  auto* panel = find(window, "snapshotPhotoFilterPanel");
  if (!panel || document.snapshotCalls() != 0 || document.sequenceTrackPropertyReads() != 0) {
    std::cerr << "Hidden Photo Filters should not project timeline tracks.\n";
    return false;
  }

  fixture->setProperty("panelVisible", true);
  QTest::qWait(100);
  if (document.snapshotCalls() != 1 || document.sequenceTrackPropertyReads() != 0 ||
      !panel->property("canEdit").toBool()) {
    std::cerr << "Opening visible Photo Filters should take one owned track snapshot and find the unlocked 192nd track.\n";
    return false;
  }

  for (int index = 0; index < 48; ++index) {
    const bool select_locked_track = index % 2 == 0;
    document.selectTrack(select_locked_track ? 0 : 191);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (panel->property("canEdit").toBool() == select_locked_track) {
      std::cerr << "Changing the selected clip should refresh lock state from the owned track snapshot.\n";
      return false;
    }
    for (int read = 0; read < 8; ++read)
      (void)panel->property("canEdit");
  }
  if (document.snapshotCalls() != 1 || document.sequenceTrackPropertyReads() != 0) {
    std::cerr << "Repeated clip selection and binding reads must not reproject the 192-track property.\n";
    return false;
  }

  fixture->setProperty("panelVisible", false);
  QTest::qWait(50);
  document.setTrackLocked(191, true);
  QTest::qWait(50);
  if (document.snapshotCalls() != 1 || document.sequenceTrackPropertyReads() != 0) {
    std::cerr << "A hidden Photo Filters panel should skip sequence-change snapshots.\n";
    return false;
  }
  fixture->setProperty("panelVisible", true);
  QTest::qWait(50);
  document.selectTrack(191);
  QTest::qWait(25);
  if (document.snapshotCalls() != 2 || panel->property("canEdit").toBool() ||
      document.sequenceTrackPropertyReads() != 0) {
    std::cerr << "Reopening Photo Filters should refresh one snapshot and reflect the current locked-track state.\n";
    return false;
  }
  document.setTrackLocked(191, false);
  QTest::qWait(50);
  if (document.snapshotCalls() != 3 || !panel->property("canEdit").toBool() ||
      document.sequenceTrackPropertyReads() != 0) {
    std::cerr << "A visible sequence change should refresh one owned snapshot and update canEdit.\n";
    return false;
  }
  window->close();
  return true;
}

QObject* find(QObject* object, const QString& name, QSet<QObject*>& visited) {
  if (!object || visited.contains(object)) return nullptr;
  visited.insert(object);
  if (object->objectName() == name) return object;
  if (auto* item = qobject_cast<QQuickItem*>(object)) {
    for (auto* child : item->childItems()) if (auto* found = find(child, name, visited)) return found;
  }
  for (auto* child : object->children()) if (auto* found = find(child, name, visited)) return found;
  return nullptr;
}
QObject* find(QQuickWindow* window, const QString& name) {
  QSet<QObject*> visited;
  return find(window->contentItem(), name, visited);
}
QImage decoded_frame(const QString& path) {
  QProcess decoder;
  decoder.start(QStandardPaths::findExecutable("ffmpeg"),
      {"-v", "error", "-ss", "0.10", "-i", path, "-frames:v", "1",
       "-f", "image2pipe", "-c:v", "png", "pipe:1"});
  if (!decoder.waitForFinished(30000) || decoder.exitCode() != 0) return {};
  auto image = QImage::fromData(decoder.readAllStandardOutput(), "PNG");
  image.setColorSpace(timeline_video_rec709_color_space());
  return image;
}
double image_difference(const QImage& first, const QImage& second) {
  if (first.isNull() || second.isNull() || first.size() != second.size()) return 1000;
  double difference = 0;
  for (int y = 0; y < first.height(); ++y) for (int x = 0; x < first.width(); ++x) {
    const auto a = first.pixelColor(x, y), b = second.pixelColor(x, y);
    difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
  }
  return difference / (first.width() * first.height() * 3.0);
}
bool verify_export(TimelineDocumentController& document, const QString& clip_id,
                   const QString& folder, const QString& label, QJsonArray* results) {
  const auto recipe = document.colorGradeRecipeJsonForClip(clip_id);
  const auto restored_recipe = QJsonDocument::fromJson(recipe.toUtf8()).object();
  if (!document.clearClipColorGradeRecipeForAutomation(clip_id).value("ok").toBool()) return false;
  const auto baseline_path = QDir(folder).filePath(label + "-unfiltered.mp4");
  if (!document.renderCurrentTimeline(baseline_path)) {
    std::cerr << "Baseline export: " << document.status().toStdString() << '\n'; return false;
  }
  if (!document.setClipColorGradeRecipeForAutomation(clip_id, restored_recipe).value("ok").toBool()) return false;
  const auto exported_path = QDir(folder).filePath(label + "-filters.mp4");
  if (!document.renderCurrentTimeline(exported_path)) {
    std::cerr << "Filter export: " << document.status().toStdString() << '\n'; return false;
  }
  QProcess probe;
  probe.start(QStandardPaths::findExecutable("ffprobe"),
      {"-v", "error", "-select_streams", "v:0", "-show_entries", "stream=nb_frames,r_frame_rate",
       "-of", "json", exported_path});
  if (!probe.waitForFinished(30000) || probe.exitCode() != 0) return false;
  const auto streams = QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("streams").toArray();
  if (streams.size() != 1) return false;
  const auto stream = streams.first().toObject();
  const int expected_frames = qRound(document.totalDurationSecondsValue() * document.framesPerSecond());
  if (stream.value("nb_frames").toString().toInt() != expected_frames) {
    std::cerr << "Shared filter export must preserve every timeline frame at the authored rate\n"; return false;
  }
  const auto baseline = decoded_frame(baseline_path), output = decoded_frame(exported_path);
  const auto preview = render_timeline_video_grade_frame(baseline, recipe, clip_id,
      timeline_video_rec709_color_space());
  if (!preview.ok() || baseline.isNull() || output.isNull()) return false;
  const double error = image_difference(preview.image, output);
  const double impact = image_difference(baseline, output);
  baseline.save(QDir(folder).filePath(label + "-baseline.png"));
  preview.image.save(QDir(folder).filePath(label + "-preview-reference.png"));
  output.save(QDir(folder).filePath(label + "-decoded-export.png"));
  results->append(QJsonObject{{"placement", label}, {"frames", expected_frames}, {"frameRate", stream.value("r_frame_rate")}, {"previewExportMeanAbsoluteRgbError", error},
      {"filterMeanAbsoluteRgbChange", impact}, {"errorLimit", 8}, {"minimumImpact", 12}});
  std::cout << label.toStdString() << " preview/export RGB error=" << error << " filter impact=" << impact << '\n';
  return error <= 8 && impact >= 12;
}
bool click(QQuickWindow* window, const QString& name) {
  window->requestActivate();
  QTest::qWait(25);
  auto* item = qobject_cast<QQuickItem*>(find(window, name));
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      item->mapToScene(QPointF(item->width()/2, item->height()/2)).toPoint());
  QTest::qWait(180);
  return true;
}
}
int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QTemporaryDir temp;
  if (!temp.isValid()) return fail("Filter proof needs isolated storage");
  qputenv("PHOTO_EDITOR_APP_DATA_ROOT", temp.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", temp.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/filter_fixture");
  const auto proof_folder = qEnvironmentVariable("PHOTO_EDITOR_FILTER_PROOF_DIR", temp.filePath("export-proof"));
  if (!QDir().mkpath(proof_folder)) return fail("Filter proof needs a writable artifact directory");
  if (!verify_track_snapshot_binding())
    return fail("Photo Filters must use one owned snapshot while visible and avoid track projections while hidden");
  QImage source(128, 96, QImage::Format_RGBA8888);
  source.fill(QColor(200, 40, 20));
  for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x)
    if ((x / 16 + y / 16) % 2) source.setPixelColor(x, y, QColor(20, 70, 210));
  source.setColorSpace(QColorSpace::SRgb);
  const auto source_path = QDir(proof_folder).filePath("source.png");
  source.save(source_path);
  TimelineDocumentController document;
  document.createTimelineWithSettings({{"timelineName", "Shared filters"}, {"width", 128}, {"height", 96}, {"frameRate", 12.0}});
  document.addClipFromGallery("photo:filter", source_path, "Picture", 0.5, {});
  if (!document.addAdjustmentClip(0.5)) return fail("An adjustment clip should host shared Edit filters");
  const auto clip_id = document.currentClip().value("clipId").toString();
  if (document.availableClipPhotoFilters().size() !=
      static_cast<qsizetype>(photo_editor::pipeline::photo_effect_registry.size()))
    return fail("Timeline must expose the same registered filter catalog as Edit");

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty("document", &document);
  engine.loadData(R"QML(
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components" as Components
Window {
    width: 460; height: 640; visible: true
    Components.Theme { id: theme }
    Components.TimelinePhotoFilters {
        objectName: "filterPanel"
        anchors.left: parent.left; anchors.right: parent.right
        anchors.top: parent.top; anchors.margins: 12
        theme: theme
        documentController: document
        currentClip: document ? document.currentClip : ({})
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/filter_host.qml")));
  if (engine.rootObjects().isEmpty()) return fail("The production shared filter UI must load");
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  QTest::qWait(100);
  if (!click(window, "timelinePhotoFilterSearch")) return fail("Filter search must receive pointer focus");
  for (const char key : QByteArray("invert")) QTest::keyClick(window, key);
  if (!click(window, "timelineAddPhotoFilterButtonPrimaryAction") ||
      !click(window, "timelineAddPhotoFilterButtonPopupItem0") || document.clipPhotoFilters(clip_id).size() != 1)
  {
    auto* panel = find(window, "filterPanel");
    std::cerr << "Filter UI: clip=" << document.currentClip().value("mediaType").toString().toStdString()
              << " supported=" << (panel && panel->property("supported").toBool())
              << " canEdit=" << (panel && panel->property("canEdit").toBool())
              << " search=" << (panel ? panel->property("searchText").toString().toStdString() : "missing")
              << " message=" << (panel ? panel->property("message").toString().toStdString() : "missing") << '\n';
    return fail("Searching and choosing an Edit filter must add a real editable recipe stage");
  }
  const auto id = document.clipPhotoFilters(clip_id).front().toMap().value("id").toString();
  const auto recipe = document.colorGradeRecipeJsonForClip(clip_id);
  const auto rendered = render_timeline_video_grade_frame(source, recipe, clip_id, QColorSpace::SRgb);
  const auto identity = render_timeline_video_grade_frame(source, serialize_edit_recipe_json(EditRecipeState{}), clip_id, QColorSpace::SRgb);
  if (!rendered.ok() || !identity.ok() || rendered.image == identity.image)
    return fail("The shared filter must change actual engine-rendered pixels");
  if (!click(window, "timelinePhotoFilterEnabled_0")) return fail("Filter bypass must receive a pointer click");
  const auto bypassed = render_timeline_video_grade_frame(source, document.colorGradeRecipeJsonForClip(clip_id), clip_id, QColorSpace::SRgb);
  if (!bypassed.ok() || bypassed.image != identity.image)
    return fail("Bypassing the shared filter must restore the unfiltered pixels");
  document.undo();
  if (!document.clipPhotoFilters(clip_id).front().toMap().value("enabled").toBool())
    return fail("Bypass must undo independently");
  if (!click(window, "timelinePhotoFilterAmount_0ValueField")) return fail("A filter must expose its readable amount field");
  auto* amount_input = find(window, "timelinePhotoFilterAmount_0ValueField");
  if (!amount_input || !amount_input->property("activeFocus").toBool())
    return fail("Pointer input must focus the real filter amount editor before typing");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(window, '5'); QTest::keyClick(window, '0'); QTest::keyClick(window, Qt::Key_Return);
  QTest::qWait(100);
  if (std::abs(document.clipPhotoFilters(clip_id).front().toMap().value("amount").toDouble() - 0.5) > 0.001) {
    auto* field = find(window, "timelinePhotoFilterAmount_0ValueField");
    auto* panel = find(window, "filterPanel");
    std::cerr << "Amount edit: stored=" << document.clipPhotoFilters(clip_id).front().toMap().value("amount").toDouble()
              << " text=" << (field ? field->property("text").toString().toStdString() : "missing")
              << " focus=" << (field && field->property("activeFocus").toBool())
              << " message=" << (panel ? panel->property("message").toString().toStdString() : "missing") << '\n';
    return fail("Typing filter amount must update its stored and rendered strength");
  }
  const auto half = render_timeline_video_grade_frame(source, document.colorGradeRecipeJsonForClip(clip_id), clip_id, QColorSpace::SRgb);
  if (!half.ok() || half.image == rendered.image || half.image == identity.image)
    return fail("Filter amount must produce an intermediate rendered result");
  if (!document.editClipPhotoFilter(clip_id, "amount", id, {}, {{"amount", 1.0}}).value("ok").toBool())
    return fail("The spatial fixture must restore contrast before adding blur");
  if (!document.editClipPhotoFilter(clip_id, "add", {}, "posterize_linear_rgb", {}).value("ok").toBool())
    return fail("A second shared filter should stack without replacing the first");
  const auto second_id = document.clipPhotoFilters(clip_id).last().toMap().value("id").toString();
  if (!document.editClipPhotoFilter(clip_id, "parameter", second_id, {}, {{"parameter", "levels"}, {"value", 8}}).value("ok").toBool())
    return fail("A registered filter parameter should update its stored envelope");
  const auto before_spatial = render_timeline_video_grade_frame(source, document.colorGradeRecipeJsonForClip(clip_id), clip_id, QColorSpace::SRgb);
  if (!document.editClipPhotoFilter(clip_id, "add", {}, "gaussian_blur", {}).value("ok").toBool())
    return fail("A spatial Edit filter must be admitted into the same timeline recipe");
  const auto spatial = render_timeline_video_grade_frame(source, document.colorGradeRecipeJsonForClip(clip_id), clip_id, QColorSpace::SRgb);
  if (!before_spatial.ok() || !spatial.ok() || image_difference(before_spatial.image, spatial.image) < 3)
    return fail("A spatial Edit filter must render its full frame graph rather than a pointwise approximation");
  const auto before_invalid = document.deterministicDocumentHash();
  if (document.editClipPhotoFilter(clip_id, "parameter", second_id, {}, {{"parameter", "levels"}, {"value", -3}}).value("ok").toBool() ||
      document.deterministicDocumentHash() != before_invalid)
    return fail("Invalid parameters must not mutate the filter or history");
  const auto track_id = document.currentClip().value("trackId").toString();
  int track_index = -1;
  const auto tracks = document.sequenceTracks();
  for (int index = 0; index < tracks.size(); ++index)
    if (tracks[index].toMap().value("trackId").toString() == track_id) track_index = index;
  if (track_index < 0) return fail("The filter fixture must expose its track");
  document.toggleTrackLocked(track_index);
  const auto locked_hash = document.deterministicDocumentHash();
  if (document.editClipPhotoFilter(clip_id, "remove", id, {}, {}).value("ok").toBool() ||
      document.deterministicDocumentHash() != locked_hash)
    return fail("Locked tracks must reject filter edits without changing the document");
  document.toggleTrackLocked(track_index);
  const auto expected = document.clipPhotoFilters(clip_id);
  if (!document.saveTimeline(false)) return fail("Shared filters must save with their timeline");
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(document.savedDocumentPath()) || reopened.clipPhotoFilters(clip_id) != expected)
    return fail("Shared filter order, parameters, bypass and amount must survive reopening");
  if (!QTest::qWaitFor([&] { return !reopened.mediaAnalysisBusy(); }, 10000))
    return fail("Filter export proof requires analyzed media and an output directory");
  QJsonArray export_results;
  if (!verify_export(reopened, clip_id, proof_folder, "adjustment-clip", &export_results))
    return fail("Reopened adjustment filters must match the shared preview in decoded export pixels");

  const auto video_path = QDir(proof_folder).filePath("opaque-source.mov");
  QProcess encoder;
  encoder.start(QStandardPaths::findExecutable("ffmpeg"),
      {"-v", "error", "-loop", "1", "-i", source_path, "-t", "0.5", "-r", "12",
       "-c:v", "png", "-pix_fmt", "rgb24", "-y", video_path});
  if (!encoder.waitForFinished(30000) || encoder.exitCode() != 0)
    return fail("Video filter export proof needs a real opaque source");
  TimelineDocumentController video;
  video.createTimelineWithSettings({{"timelineName", "Video filter export"}, {"width", 128}, {"height", 96}, {"frameRate", 12.0}});
  video.addClipFromGallery("video:filters", video_path, "Filter camera", 0.5, {});
  if (!QTest::qWaitFor([&] { return !video.mediaAnalysisBusy(); }, 10000)) return fail("Video dimensions must be analyzed before export");
  const auto video_id = video.currentClip().value("clipId").toString();
  for (const auto& type : {"invert_linear_rgb", "gaussian_blur"})
    if (!video.editClipPhotoFilter(video_id, "add", {}, type, {}).value("ok").toBool()) return fail("Video filters must author real shared stages");
  if (!video.saveTimeline(false)) return fail("Video filters must save");
  TimelineDocumentController video_reopened;
  if (!video_reopened.loadTimeline(video.savedDocumentPath()) ||
      !QTest::qWaitFor([&] { return !video_reopened.mediaAnalysisBusy(); }, 10000) ||
      !verify_export(video_reopened, video_id, proof_folder, "video-clip", &export_results))
    return fail("Reopened video filters must match the shared preview in decoded export pixels");
  const auto retain_project = [&](TimelineDocumentController& controller, const QString& name) {
    if (!controller.saveTimeline(false)) return false;
    QFile original(controller.savedDocumentPath());
    QFile retained(QDir(proof_folder).filePath(name + "-project.json"));
    return original.open(QIODevice::ReadOnly) && retained.open(QIODevice::WriteOnly)
        && retained.write(original.readAll()) > 0;
  };
  if (!retain_project(reopened, "adjustment-filter") || !retain_project(video_reopened, "video-filter"))
    return fail("Export proof must retain reopenable projects alongside their media");
  video_reopened.selectClip(0);
  video_reopened.setSelectedFrameScale(1.2);
  const auto spatial_capability = video_reopened.inspectClipColorGradeForAutomation(video_id);
  if (!spatial_capability.value("ok").toBool() ||
      !spatial_capability.value("sequenceExportSupportsGrade").toBool() ||
      !spatial_capability.value("sequenceExportUsesSourceBake").toBool())
    return fail("A supported static spatial grade must report the source-bake export route");
  engine.rootContext()->setContextProperty("document", &video_reopened);
  if (!QTest::qWaitFor([&] {
        auto* status = qobject_cast<QQuickItem*>(find(window, "timelinePhotoFilterExportStatus"));
        return status && status->isVisible() &&
               status->property("text").toString().contains("source-space bake");
      }, 3000)) return fail("The filter panel must explain the source-bake delivery route and its limits");
  engine.rootContext()->setContextProperty("document", &document);
  QCoreApplication::processEvents();
  QFile export_receipt(QDir(proof_folder).filePath("filter-export-proof.json"));
  if (!export_receipt.open(QIODevice::WriteOnly) ||
      export_receipt.write(QJsonDocument(QJsonObject{{"passed", true}, {"cases", export_results}}).toJson()) <= 0)
    return fail("Filter export checks must retain their measured pixel errors");
  std::cout << "Shared Edit filter catalog, UI search/add/bypass, rendered pixels, amount, parameters, lock, undo and save/reopen passed.\n";
  return EXIT_SUCCESS;
}

#include "timeline_photo_filters_test.moc"
