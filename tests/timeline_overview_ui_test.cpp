#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPoint>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQmlContext>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <QStringList>
#include <QtMath>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return EXIT_FAILURE;
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name)) return found;
  }
  return nullptr;
}

bool near(qreal actual, qreal expected, qreal tolerance = 0.01) {
  return std::abs(actual - expected) <= tolerance;
}

void settle_qml() {
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

void send_mouse_click(QQuickWindow& window, const QPointF& position) {
  QMouseEvent press(QEvent::MouseButtonPress, position, position, position,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &press);
  QMouseEvent release(QEvent::MouseButtonRelease, position, position, position,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &release);
}

bool is_near_white(const QColor& color) {
  return color.red() >= 240 && color.green() >= 240 && color.blue() >= 240;
}

}  // namespace

class CountingTimelineMapController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(QVariantMap programClip READ programClip NOTIFY playbackStateChanged)
  Q_PROPERTY(QVariantMap sequenceSettings READ sequenceSettings NOTIFY sequenceChanged)

 public:
  static constexpr int kKeyframeCount = 1204;

  CountingTimelineMapController()
      : current_clip_(make_clip(QStringLiteral("selected-0"), 0)),
        program_clip_(make_clip(QStringLiteral("program-0"), 10)),
        sequence_settings_(make_settings(0)) {}

  QVariantMap currentClip() const {
    ++current_clip_property_reads_;
    return current_clip_;
  }
  QVariantMap programClip() const {
    ++program_clip_property_reads_;
    return program_clip_;
  }
  QVariantMap sequenceSettings() const {
    ++sequence_settings_property_reads_;
    return sequence_settings_;
  }
  Q_INVOKABLE QVariantMap snapshotCurrentClip() const {
    ++current_clip_snapshot_reads_;
    return current_clip_;
  }
  Q_INVOKABLE QVariantMap snapshotProgramClip() const {
    ++program_clip_snapshot_reads_;
    return program_clip_;
  }
  Q_INVOKABLE QVariantMap snapshotSequenceSettings() const {
    ++sequence_settings_snapshot_reads_;
    return sequence_settings_;
  }

  int currentClipPropertyReads() const { return current_clip_property_reads_; }
  int programClipPropertyReads() const { return program_clip_property_reads_; }
  int sequenceSettingsPropertyReads() const { return sequence_settings_property_reads_; }
  int currentClipSnapshotReads() const { return current_clip_snapshot_reads_; }
  int programClipSnapshotReads() const { return program_clip_snapshot_reads_; }
  int sequenceSettingsSnapshotReads() const { return sequence_settings_snapshot_reads_; }

  void publishSelection(int revision) {
    current_clip_ = make_clip(QStringLiteral("selected-%1").arg(revision), revision);
    program_clip_ = make_clip(QStringLiteral("program-selection-%1").arg(revision), revision + 10);
    emit selectionChanged();
  }
  void publishPlayback(int revision) {
    current_clip_ = make_clip(QStringLiteral("selected-playback-%1").arg(revision), revision);
    program_clip_ = make_clip(QStringLiteral("program-%1").arg(revision), revision + 10);
    emit playbackStateChanged();
  }
  void publishSettings(int revision) {
    sequence_settings_ = make_settings(revision);
    emit sequenceChanged();
  }
  void publishTimelineState(int revision) {
    current_clip_ = make_clip(QStringLiteral("selected-timeline-%1").arg(revision), revision);
    program_clip_ = make_clip(QStringLiteral("program-timeline-%1").arg(revision), revision + 10);
    sequence_settings_ = make_settings(revision);
    emit timelineStateChanged();
  }
  void publishSelectedClipState(int revision) {
    current_clip_ = make_clip(QStringLiteral("selected-state-%1").arg(revision), revision);
    program_clip_ = make_clip(QStringLiteral("program-selected-state-%1").arg(revision), revision + 10);
    // The native selected-parameter edit emits this same notification sequence.
    emit selectionChanged();
    emit playbackStateChanged();
    emit selectedClipStateChanged();
  }

 signals:
  void selectionChanged();
  void playbackStateChanged();
  void sequenceChanged();
  void timelineStateChanged();
  void selectedClipStateChanged();

 private:
  static QVariantMap make_clip(const QString& clip_id, int revision) {
    QVariantList keyframes;
    keyframes.reserve(kKeyframeCount);
    for (int index = 0; index < kKeyframeCount; ++index) {
      const double value = static_cast<double>(revision) +
                           static_cast<double>(index) / 10000.0;
      keyframes.push_back(QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("localSeconds"), static_cast<double>(index) / 30.0},
          {QStringLiteral("value"), value},
          {QStringLiteral("interpolation"), QStringLiteral("bezier")},
          {QStringLiteral("bezierInTimeSeconds"), 0.01},
          {QStringLiteral("bezierInValue"), value - 0.05},
          {QStringLiteral("bezierOutTimeSeconds"), 0.02},
          {QStringLiteral("bezierOutValue"), value + 0.05},
          {QStringLiteral("bezierInCustom"), true},
          {QStringLiteral("bezierOutCustom"), true},
          {QStringLiteral("bezierHandlesLinked"), false},
      });
    }
    return {
        {QStringLiteral("clipId"), clip_id},
        {QStringLiteral("trackId"), QStringLiteral("video-track-17")},
        {QStringLiteral("mediaType"), QStringLiteral("video")},
        {QStringLiteral("trackKind"), QStringLiteral("video")},
        {QStringLiteral("displayLabel"), QStringLiteral("Full map %1").arg(revision)},
        {QStringLiteral("startSeconds"), static_cast<double>(revision)},
        {QStringLiteral("trimmedDurationSeconds"), 40.0},
        {QStringLiteral("opacity"), 0.75},
        {QStringLiteral("titleText"), QStringLiteral("Nested controller payload")},
        {QStringLiteral("availableKeyframeParameters"), QStringList{QStringLiteral("opacity")}},
        {QStringLiteral("keyframes"), keyframes},
    };
  }

  static QVariantMap make_settings(int revision) {
    return {
        {QStringLiteral("width"), 1920 + revision * 2},
        {QStringLiteral("height"), 1080 + revision * 2},
        {QStringLiteral("frameRate"), 30.0},
        {QStringLiteral("audioSampleRate"), 48000},
        {QStringLiteral("colorSpace"), QStringLiteral("rec709")},
        {QStringLiteral("exportPreset"), QStringLiteral("youtube")},
        {QStringLiteral("activeOutputVariant"), QStringLiteral("landscape")},
        {QStringLiteral("revisionTag"), revision},
    };
  }

  QVariantMap current_clip_;
  QVariantMap program_clip_;
  QVariantMap sequence_settings_;
  mutable int current_clip_property_reads_ = 0;
  mutable int program_clip_property_reads_ = 0;
  mutable int sequence_settings_property_reads_ = 0;
  mutable int current_clip_snapshot_reads_ = 0;
  mutable int program_clip_snapshot_reads_ = 0;
  mutable int sequence_settings_snapshot_reads_ = 0;
};

namespace {

bool verify_owned_timeline_map_snapshots() {
  CountingTimelineMapController controller;
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("timelineMaps"), &controller);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import QtQml
Item {
    id: fixture
    property var selectedClipSnapshot: ({})
    property var programClipSnapshot: ({})
    property var settingsSnapshot: ({})

    function refreshSelectedClip() { selectedClipSnapshot = timelineMaps.snapshotCurrentClip() }
    function refreshProgramClip() { programClipSnapshot = timelineMaps.snapshotProgramClip() }
    function refreshSettings() { settingsSnapshot = timelineMaps.snapshotSequenceSettings() }
    function refreshAll() {
        refreshSelectedClip()
        refreshProgramClip()
        refreshSettings()
    }
    function refreshCurrentAndProgram() {
        refreshSelectedClip()
        refreshProgramClip()
    }

    function legacySelectedClipNestedRead() {
        var sum = 0
        for (var i = 0; i < timelineMaps.currentClip.keyframes.length; ++i)
            sum += Number(timelineMaps.currentClip.keyframes[i].value)
        return sum
    }
    function legacyProgramClipNestedRead() {
        var sum = 0
        for (var i = 0; i < timelineMaps.programClip.keyframes.length; ++i)
            sum += Number(timelineMaps.programClip.keyframes[i].value)
        return sum
    }
    function legacySettingsNestedRead() {
        var sum = 0
        for (var i = 0; i < 128; ++i)
            sum += Number(timelineMaps.sequenceSettings.width) + Number(timelineMaps.sequenceSettings.height)
        return sum
    }
    function ownedSelectedClipNestedRead() {
        var sum = 0
        for (var i = 0; i < selectedClipSnapshot.keyframes.length; ++i)
            sum += Number(selectedClipSnapshot.keyframes[i].value)
        return sum
    }
    function ownedProgramClipNestedRead() {
        var sum = 0
        for (var i = 0; i < programClipSnapshot.keyframes.length; ++i)
            sum += Number(programClipSnapshot.keyframes[i].value)
        return sum
    }
    function ownedSettingsNestedRead() {
        var sum = 0
        for (var i = 0; i < 128; ++i)
            sum += Number(settingsSnapshot.width) + Number(settingsSnapshot.height)
        return sum
    }

    Connections {
        target: timelineMaps
        function onSelectionChanged() { fixture.refreshCurrentAndProgram() }
        function onPlaybackStateChanged() { fixture.refreshCurrentAndProgram() }
        function onSequenceChanged() { fixture.refreshSettings() }
        function onTimelineStateChanged() { fixture.refreshAll() }
        function onSelectedClipStateChanged() { fixture.refreshCurrentAndProgram() }
    }
    Component.onCompleted: refreshAll()
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/TimelineMapSnapshotProbe.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return false;
  }

  if (controller.currentClipSnapshotReads() != 1 ||
      controller.programClipSnapshotReads() != 1 ||
      controller.sequenceSettingsSnapshotReads() != 1) {
    std::cerr << "The QML fixture should take one owned initial snapshot for each controller map.\n";
    return false;
  }
  const auto selected = fixture->property("selectedClipSnapshot").toMap();
  const auto initial_program = fixture->property("programClipSnapshot").toMap();
  const auto selected_keyframes = selected.value(QStringLiteral("keyframes")).toList();
  const auto selected_last_keyframe = selected_keyframes.last().toMap();
  const auto settings = fixture->property("settingsSnapshot").toMap();
  if (selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-0") ||
      initial_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-0") ||
      selected_keyframes.size() != CountingTimelineMapController::kKeyframeCount ||
      selected_last_keyframe.value(QStringLiteral("bezierOutCustom")).toBool() != true ||
      selected_last_keyframe.value(QStringLiteral("bezierOutValue")).toDouble() <= 0.0 ||
      settings.value(QStringLiteral("width")).toInt() != 1920 ||
      settings.value(QStringLiteral("exportPreset")).toString() != QStringLiteral("youtube")) {
    std::cerr << "Owned snapshots must preserve the full 1,204-keyframe maps and sequence settings fields.\n";
    return false;
  }

  QVariant legacy_result;
  QVariant owned_result;
  const auto current_reads_before_legacy = controller.currentClipPropertyReads();
  if (!QMetaObject::invokeMethod(fixture.get(), "legacySelectedClipNestedRead",
                                 Q_RETURN_ARG(QVariant, legacy_result)) ||
      controller.currentClipPropertyReads() - current_reads_before_legacy <
          CountingTimelineMapController::kKeyframeCount) {
    std::cerr << "The Q_PROPERTY baseline should count repeated nested references across the 1,204 keyframes.\n";
    return false;
  }
  const auto program_reads_before_legacy = controller.programClipPropertyReads();
  if (!QMetaObject::invokeMethod(fixture.get(), "legacyProgramClipNestedRead",
                                 Q_RETURN_ARG(QVariant, legacy_result)) ||
      controller.programClipPropertyReads() - program_reads_before_legacy <
          CountingTimelineMapController::kKeyframeCount) {
    std::cerr << "The programClip Q_PROPERTY baseline should count repeated nested references across keyframes.\n";
    return false;
  }
  const auto settings_reads_before_legacy = controller.sequenceSettingsPropertyReads();
  if (!QMetaObject::invokeMethod(fixture.get(), "legacySettingsNestedRead",
                                 Q_RETURN_ARG(QVariant, legacy_result)) ||
      controller.sequenceSettingsPropertyReads() - settings_reads_before_legacy < 128) {
    std::cerr << "The sequenceSettings Q_PROPERTY baseline should expose repeated nested map reads.\n";
    return false;
  }

  const auto current_reads_before_owned = controller.currentClipPropertyReads();
  const auto program_reads_before_owned = controller.programClipPropertyReads();
  const auto settings_reads_before_owned = controller.sequenceSettingsPropertyReads();
  for (const auto* method : {"ownedSelectedClipNestedRead", "ownedProgramClipNestedRead",
                             "ownedSettingsNestedRead"}) {
    if (!QMetaObject::invokeMethod(fixture.get(), method, Q_RETURN_ARG(QVariant, owned_result)) ||
        !std::isfinite(owned_result.toDouble())) {
      std::cerr << "Owned-map nested-read probes should return finite results.\n";
      return false;
    }
  }
  if (controller.currentClipPropertyReads() != current_reads_before_owned ||
      controller.programClipPropertyReads() != program_reads_before_owned ||
      controller.sequenceSettingsPropertyReads() != settings_reads_before_owned ||
      controller.currentClipSnapshotReads() != 1 ||
      controller.programClipSnapshotReads() != 1 ||
      controller.sequenceSettingsSnapshotReads() != 1) {
    std::cerr << "Repeated nested reads from owned snapshots must not re-enter the QObject property getters.\n";
    return false;
  }

  controller.publishSelection(2);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  auto refreshed_selected = fixture->property("selectedClipSnapshot").toMap();
  auto refreshed_program = fixture->property("programClipSnapshot").toMap();
  auto refreshed_settings = fixture->property("settingsSnapshot").toMap();
  if (controller.currentClipSnapshotReads() != 2 ||
      controller.programClipSnapshotReads() != 2 ||
      controller.sequenceSettingsSnapshotReads() != 1 ||
      refreshed_selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-2") ||
      refreshed_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-selection-2") ||
      refreshed_settings.value(QStringLiteral("revisionTag")).toInt() != 0) {
    std::cerr << "Selection notification should refresh current and Program snapshots, but leave settings untouched.\n";
    return false;
  }
  const auto program_after_selection = refreshed_program;

  controller.publishPlayback(3);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  refreshed_selected = fixture->property("selectedClipSnapshot").toMap();
  refreshed_program = fixture->property("programClipSnapshot").toMap();
  refreshed_settings = fixture->property("settingsSnapshot").toMap();
  if (controller.currentClipSnapshotReads() != 3 ||
      controller.programClipSnapshotReads() != 3 ||
      controller.sequenceSettingsSnapshotReads() != 1 ||
      refreshed_selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-playback-3") ||
      refreshed_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-3") ||
      refreshed_settings.value(QStringLiteral("revisionTag")).toInt() != 0) {
    std::cerr << "Playback notification should refresh current and Program snapshots, but leave settings untouched.\n";
    return false;
  }
  const auto selected_after_playback = refreshed_selected;
  const auto program_after_playback = refreshed_program;

  controller.publishSettings(4);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  refreshed_selected = fixture->property("selectedClipSnapshot").toMap();
  refreshed_program = fixture->property("programClipSnapshot").toMap();
  refreshed_settings = fixture->property("settingsSnapshot").toMap();
  if (controller.currentClipSnapshotReads() != 3 ||
      controller.programClipSnapshotReads() != 3 ||
      controller.sequenceSettingsSnapshotReads() != 2 ||
      refreshed_selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-playback-3") ||
      refreshed_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-3") ||
      refreshed_settings.value(QStringLiteral("width")).toInt() != 1928 ||
      refreshed_settings.value(QStringLiteral("revisionTag")).toInt() != 4) {
    std::cerr << "Sequence notification should refresh settings only and preserve current/Program snapshots.\n";
    return false;
  }
  const auto settings_after_sequence = refreshed_settings;

  controller.publishTimelineState(5);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  refreshed_selected = fixture->property("selectedClipSnapshot").toMap();
  refreshed_program = fixture->property("programClipSnapshot").toMap();
  refreshed_settings = fixture->property("settingsSnapshot").toMap();
  if (controller.currentClipSnapshotReads() != 4 ||
      controller.programClipSnapshotReads() != 4 ||
      controller.sequenceSettingsSnapshotReads() != 3 ||
      refreshed_selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-timeline-5") ||
      refreshed_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-timeline-5") ||
      refreshed_settings.value(QStringLiteral("revisionTag")).toInt() != 5) {
    std::cerr << "Timeline-state notification should refresh all three owned snapshots.\n";
    return false;
  }
  const auto timeline_state_snapshot = refreshed_selected;

  controller.publishSelectedClipState(6);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  refreshed_selected = fixture->property("selectedClipSnapshot").toMap();
  refreshed_program = fixture->property("programClipSnapshot").toMap();
  refreshed_settings = fixture->property("settingsSnapshot").toMap();
  if (controller.currentClipSnapshotReads() != 7 ||
      controller.programClipSnapshotReads() != 7 ||
      controller.sequenceSettingsSnapshotReads() != 3 ||
      refreshed_selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-state-6") ||
      refreshed_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-selected-state-6") ||
      refreshed_settings.value(QStringLiteral("revisionTag")).toInt() != 5) {
    std::cerr << "Selected-clip-state notification should refresh only the current clip snapshot.\n";
    return false;
  }

  if (selected.value(QStringLiteral("clipId")).toString() != QStringLiteral("selected-0") ||
      initial_program.value(QStringLiteral("clipId")).toString() != QStringLiteral("program-0") ||
      program_after_selection.value(QStringLiteral("clipId")).toString() !=
          QStringLiteral("program-selection-2") ||
      selected_after_playback.value(QStringLiteral("clipId")).toString() !=
          QStringLiteral("selected-playback-3") ||
      program_after_playback.value(QStringLiteral("clipId")).toString() !=
          QStringLiteral("program-3") ||
      settings_after_sequence.value(QStringLiteral("revisionTag")).toInt() != 4 ||
      timeline_state_snapshot.value(QStringLiteral("clipId")).toString() !=
          QStringLiteral("selected-timeline-5") ||
      settings.value(QStringLiteral("revisionTag")).toInt() != 0) {
    std::cerr << "Previously returned owned snapshots should retain their captured values after later notifications.\n";
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication application(argc, argv);
  if (!verify_owned_timeline_map_snapshots())
    return fail(QStringLiteral("Timeline map snapshots must eliminate repeated Qt property-reference getters and refresh correctly."));
  QQmlEngine engine;
  const QUrl harness_url = QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/TimelineOverviewTestHarness.qml"));
  QQmlComponent component(&engine, harness_url);
  if (component.isError()) return fail(component.errorString());
  std::unique_ptr<QObject> object(component.create());
  if (!object) return fail(component.errorString());

  auto* root = qobject_cast<QQuickItem*>(object.get());
  if (root == nullptr) return fail(QStringLiteral("Timeline overview harness must be a QQuickItem."));
  QQuickWindow window;
  window.resize(420, 260);
  root->setParentItem(window.contentItem());
  window.show();
  settle_qml();

  const QString expected_active_playback_ids = QStringLiteral(
      "playback-clip-2,playback-clip-3,playback-clip-6,playback-clip-205,"
      "playback-clip-406,playback-clip-576,playback-clip-677,playback-clip-778,"
      "playback-clip-879,playback-clip-980,playback-clip-1081,playback-clip-1182");
  if (root->property("playbackSnapshotTracksVisited").toInt() != 940 ||
      root->property("playbackSnapshotInputClipCount").toInt() != 1222 ||
      root->property("playbackSnapshotRangeSearches").toInt() != 940 ||
      root->property("playbackSnapshotActiveTrackCount").toInt() != 11 ||
      root->property("playbackSnapshotActiveClipCount").toInt() != 12 ||
      !root->property("playbackSnapshotDetectedAudioSolo").toBool() ||
      root->property("playbackSnapshotActiveIds").toString() != expected_active_playback_ids)
    return fail(QStringLiteral(
        "The production playback snapshot must query the 940-track, 1,222-clip fixture once and return exact half-open active IDs plus solo state."));
  if (root->property("playbackSyncInitialInsertCount").toInt() != 2 ||
      root->property("playbackSyncStableInsertCount").toInt() != 0 ||
      root->property("playbackSyncStableMoveCount").toInt() != 0 ||
      root->property("playbackSyncStableRemoveCount").toInt() != 0 ||
      root->property("playbackSyncReorderMoveCount").toInt() != 1 ||
      root->property("playbackSyncFinalInsertCount").toInt() != 1 ||
      root->property("playbackSyncFinalRemoveCount").toInt() != 1 ||
      root->property("playbackSyncDelegateCreated").toInt() != 3 ||
      root->property("playbackSyncDelegateDestroyed").toInt() != 1 ||
      !root->property("playbackSyncOmitsClipMapRole").toBool())
    return fail(QStringLiteral(
        "Active playback delegates must reconcile by stable ID, retain unchanged delegate instances, and keep full clip maps outside ListModel roles."));
  const int playback_payload_clip_count = root->property("playbackPayloadClipCount").toInt();
  if (playback_payload_clip_count != 583 ||
      !root->property("playbackPayloadHeavyRolePresent").toBool() ||
      !root->property("playbackPayloadIdsOnlyRoleOmitted").toBool())
    return fail(QStringLiteral(
        "The playback payload probe must compare 583 detailed clip maps with ID-only delegate roles."));
  qInfo().noquote() << QStringLiteral(
      "Timeline playback ListModel payload: heavyMaps=%1ms, stableIds=%2ms, clips=%3, keyframesPerClip=%4")
      .arg(root->property("playbackPayloadHeavyMapMs").toInt())
      .arg(root->property("playbackPayloadIdsOnlyMs").toInt())
      .arg(playback_payload_clip_count)
      .arg(root->property("playbackPayloadKeyframeCount").toInt());

  auto* ruler = find_item(root, QStringLiteral("timelineRuler"));
  auto* viewport = find_item(root, QStringLiteral("trackViewport"));
  auto* minimap = find_item(root, QStringLiteral("timelineMinimapBar"));
  auto* canvas = find_item(root, QStringLiteral("timelineMinimapCanvas"));
  auto* first_subpixel_body = find_item(root, QStringLiteral("firstSubpixelClipBody"));
  auto* second_subpixel_body = find_item(root, QStringLiteral("secondSubpixelClipBody"));
  if (ruler == nullptr || viewport == nullptr || minimap == nullptr || canvas == nullptr)
    return fail(QStringLiteral("The production ruler and minimap components must load in the runtime fixture."));
  if (first_subpixel_body == nullptr || second_subpixel_body == nullptr)
    return fail(QStringLiteral("The production short-clip pointer geometry must load in the runtime fixture."));

  const qreal fit_298_zoom = root->property("fitZoom").toDouble();
  const qreal two_hour_fit_zoom = root->property("twoHourFitZoom").toDouble();
  const qreal year_fit_zoom = root->property("yearFitZoom").toDouble();
  const qreal long_fit_zoom = root->property("longFitZoom").toDouble();
  const qreal available_width = root->property("viewportWidth").toDouble() - 40.0;
  const qreal fit_duration = root->property("fitDurationSeconds").toDouble();
  const qreal two_hour_duration = root->property("twoHourDurationSeconds").toDouble();
  const qreal year_duration = root->property("yearDurationSeconds").toDouble();
  const qreal long_duration = root->property("longDurationSeconds").toDouble();
  if (!(fit_298_zoom > 0.01 && fit_298_zoom < 12.0) ||
      fit_298_zoom * fit_duration > available_width + 0.01 ||
      !(two_hour_fit_zoom > 0.01 && two_hour_fit_zoom < 1.0) ||
      two_hour_fit_zoom * two_hour_duration > available_width + 0.01 ||
      !root->property("lowZoomStepIsApplied").toBool() ||
      root->property("lowZoomNoiseIsIgnored").toBool() ||
      !(year_fit_zoom > 0 && year_fit_zoom < 0.01) ||
      !near(year_fit_zoom * year_duration, available_width, 0.01) ||
      !(long_fit_zoom > 0.000000000001 && long_fit_zoom < 0.000001) ||
      !near(long_fit_zoom * long_duration, available_width, 0.01))
    return fail(QStringLiteral("Production fit math must fit 298 seconds through very long sequences below the old 12 px/s floor."));

  const qreal one_frame_width = root->property("firstShortClipWidth").toDouble();
  const qreal first_clip_start_x = root->property("firstShortClipStartX").toDouble();
  const qreal second_clip_start_x = root->property("secondShortClipStartX").toDouble();
  const qreal second_clip_width = root->property("secondShortClipWidth").toDouble();
  const qreal separate_hit_width = root->property("firstShortClipHitWidth").toDouble();
  if (!(one_frame_width > 0 && one_frame_width < 1) ||
      !near(first_clip_start_x + one_frame_width, second_clip_start_x, 0.0001) ||
      !near(second_clip_start_x + second_clip_width,
            first_clip_start_x + 2 * one_frame_width, 0.0001) ||
      separate_hit_width < 1.0 || separate_hit_width <= one_frame_width ||
      first_subpixel_body->width() >= 1.0 || second_subpixel_body->width() >= 1.0 ||
      first_subpixel_body->x() + first_subpixel_body->width() > second_subpixel_body->x() + 0.0001 ||
      !root->property("shortClipOwnsItsStart").toBool() ||
      root->property("shortClipOwnsItsEnd").toBool())
    return fail(QStringLiteral("Clip bodies must keep exact subpixel timing geometry while hit targets and half-open time ownership stay separate."));

  root->setProperty("clipHitAreaSelection", QString());
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(50, 238));
  settle_qml();
  if (root->property("clipHitAreaSelection").toString() != QStringLiteral("first"))
    return fail(QStringLiteral("The first expanded pointer target must select only the short clip that owns the clicked time."));

  root->setProperty("clipHitAreaSelection", QString());
  send_mouse_click(window, QPointF(50.8, 238));
  settle_qml();
  if (root->property("clipHitAreaSelection").toString() != QStringLiteral("second") ||
      root->property("clipHitAreaRejections").toInt() < 1)
    return fail(QStringLiteral("Overlapping hit-slop must propagate to the adjacent short clip that owns the clicked time."));

  root->setProperty("clipHitAreaSelection", QString());
  send_mouse_click(window, QPointF(51.6, 238));
  settle_qml();
  if (!root->property("clipHitAreaSelection").toString().isEmpty() ||
      root->property("clipHitAreaRejections").toInt() < 2)
    return fail(QStringLiteral("An expanded short-clip hit target must reject clicks outside its exact time interval."));

  const QPointF ruler_origin_before = ruler->mapToItem(root, QPointF(0, 0));
  const QPointF viewport_origin_before = viewport->mapToItem(root, QPointF(0, 0));
  if (!near(ruler_origin_before.y(), 0) || !near(ruler_origin_before.x(), 96) ||
      !near(viewport_origin_before.y(), 24) || !near(viewport_origin_before.x(), 96))
    return fail(QStringLiteral("The pinned ruler and track body must share the lane-header edge."));

  viewport->setProperty("contentY", 1170.0);
  settle_qml();
  const QPointF ruler_origin_after = ruler->mapToItem(root, QPointF(0, 0));
  if (!near(ruler_origin_after.y(), ruler_origin_before.y()) ||
      !near(viewport->property("contentY").toDouble(), 1170.0))
    return fail(QStringLiteral("Vertical track scrolling must leave the ruler pinned in the viewport."));

  QVariant ruler_tick_x;
  if (!QMetaObject::invokeMethod(root, "rulerViewportXForSeconds",
                                 Q_RETURN_ARG(QVariant, ruler_tick_x),
                                 Q_ARG(QVariant, 12.5)) ||
      !near(ruler_tick_x.toDouble(), 200.0))
    return fail(QStringLiteral("Ruler tick x must account for the same horizontal scroll offset as the track body."));

  const QPoint click_position = ruler->mapToScene(QPointF(200, 12)).toPoint();
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, click_position);
  settle_qml();
  if (!near(root->property("requestedSeekSeconds").toDouble(), 12.5) ||
      !near(root->property("rulerPointerContentX").toDouble(), 250.0))
    return fail(QStringLiteral("Ruler seek hit mapping must add the existing horizontal content offset."));

  QElapsedTimer paint_wait;
  paint_wait.start();
  while (minimap->property("densityPaintedPixelCount").toInt() <= 0 &&
         paint_wait.elapsed() < 3000) {
    settle_qml();
    QTest::qWait(10);
  }

  const int bin_count = minimap->property("densityBinCount").toInt();
  const int mapped_tracks = minimap->property("densityMappedTrackCount").toInt();
  const int maximum_bin_tracks = minimap->property("densityMaximumTracksPerBin").toInt();
  const int painted_pixels = minimap->property("densityPaintedPixelCount").toInt();
  const int pixel_width = qMax(1, qFloor(minimap->width()) - 4);
  if (mapped_tracks != 936 || bin_count <= 0 ||
      bin_count > qFloor(minimap->height()) - 4 || maximum_bin_tracks <= 1 ||
      painted_pixels <= 0 || painted_pixels > bin_count * pixel_width)
    return fail(QStringLiteral("The 936-track overview must retain full extent while painting at most one pixel per density bin."));

  const QImage frame = window.grabWindow();
  if (frame.isNull()) return fail(QStringLiteral("The overview test window should produce a rendered frame."));
  const qreal image_scale_x = static_cast<qreal>(frame.width()) / window.width();
  const qreal image_scale_y = static_cast<qreal>(frame.height()) / window.height();
  const QPointF top_pixel = minimap->mapToScene(QPointF(320, 2.5));
  const QPointF bottom_pixel = minimap->mapToScene(QPointF(320, minimap->height() - 2.5));
  const QColor top_density = frame.pixelColor(qRound(top_pixel.x() * image_scale_x),
                                               qRound(top_pixel.y() * image_scale_y));
  const QColor bottom_density = frame.pixelColor(qRound(bottom_pixel.x() * image_scale_x),
                                                  qRound(bottom_pixel.y() * image_scale_y));
  if (is_near_white(top_density) || is_near_white(bottom_density) ||
      (top_density.red() < 10 && top_density.green() < 10 && top_density.blue() < 10) ||
      (bottom_density.red() < 10 && bottom_density.green() < 10 && bottom_density.blue() < 10))
    return fail(QStringLiteral("Top and bottom density bins must remain colored without white row overpaint."));

  root->setProperty("horizontalOffset", 0.0);
  root->setProperty("horizontalZoom", long_fit_zoom);
  root->setProperty("sequenceDuration", long_duration);
  QVariant long_sequence_seconds;
  if (!QMetaObject::invokeMethod(root, "rulerSequenceSecondsAt",
                                 Q_RETURN_ARG(QVariant, long_sequence_seconds),
                                 Q_ARG(QVariant, 1.0)) ||
      !near(long_sequence_seconds.toDouble(), 1.0 / long_fit_zoom, 0.01))
    return fail(QStringLiteral("The production ruler must preserve pointer-to-time mapping below one pixel per second."));

  return EXIT_SUCCESS;
}

#include "timeline_overview_ui_test.moc"
