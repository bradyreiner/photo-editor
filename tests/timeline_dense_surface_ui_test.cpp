#include <QApplication>
#include <QColor>
#include <QDir>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QVariantMap collect(QObject* helper, const QVariantList& tracks,
                    double start, double end, const QVariantList& selected,
                    const QVariantMap& groups = {}) {
  QVariant result;
  if (!QMetaObject::invokeMethod(helper, "collect", Q_RETURN_ARG(QVariant, result),
      Q_ARG(QVariant, tracks), Q_ARG(QVariant, start), Q_ARG(QVariant, end),
      Q_ARG(QVariant, selected), Q_ARG(QVariant, groups))) return {};
  return result.value<QJSValue>().toVariant().toMap();
}
}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent keyframe_fixture_component(&engine);
  keyframe_fixture_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    C.TimelineKeyframeIndex { id: index }
    function indexNative(value) { return index.clipParameter(value, "frameScale").length }
    function cacheSize() { return index.snapshotChannels.length }
    function verifyIndex() {
        const points = []
        for (let i = 180; i >= 0; --i) {
            points.push({parameter: "frameScale", seconds: i / 30, value: 1 + i / 180,
                         interpolation: i % 3 ? "linear" : "hold",
                         bezierOutCustom: true, bezierOutValue: 0.15})
            points.push({parameter: "opacity", seconds: i / 30, value: i / 180})
        }
        const clip = {keyframes: points}
        const channel = index.clipParameter(clip, "frameScale")
        if (channel.length !== 181 || channel[0].seconds !== 0 || points[0].seconds !== 6)
            return "Indexing must sort separate channels without modifying source order"
        for (let i = 0; i < 400; ++i) {
            const time = 0.001 + i * 5.998 / 400
            let expected = 1
            while (expected < channel.length - 1 && channel[expected].seconds < time) ++expected
            if (index.rightEndpoint(channel, time) !== expected)
                return "Indexed evaluation must preserve every segment and exact boundary"
            if (index.clipParameter(clip, "frameScale") !== channel)
                return "Scalar evaluations must reuse the snapshot channel"
        }
        if (!channel[0].bezierOutCustom || channel[0].bezierOutValue !== 0.15)
            return "Indexing must preserve custom tangent data"
        const edited = {keyframes: [{parameter: "frameScale", seconds: 0, value: 3}]}
        if (index.clipParameter(edited, "frameScale")[0].value !== 3 || channel[0].value !== 1)
            return "Replacing an authored snapshot must refresh the index"
        for (let i = 0; i < 196; ++i) {
            const empty = {clipId: "static-" + i, keyframes: [], automationKeyframes: []}
            if (index.clipParameter(empty, "frameScale").length !== 0
                    || index.trackParameter(empty, null, "opacity").length !== 0)
                return "Static snapshots must have no animated channels"
        }
        if (index.clipParameter(clip, "frameScale") !== channel)
            return "Static lanes must not evict the active animated channel cache"
        const firstView = {clipId: "shared-id", keyframes: [{parameter: "opacity", seconds: 0, value: 0.2}]}
        const editedView = {clipId: "shared-id", keyframes: [{parameter: "opacity", seconds: 0, value: 0.9}]}
        for (let i = 0; i < 100; ++i) {
            if (index.clipParameter(firstView, "opacity")[0].value !== 0.2
                    || index.clipParameter(editedView, "opacity")[0].value !== 0.9)
                return "Same-ID immutable views must retain their independent authored values"
        }
        const track = {automationKeyframes: points}
        const override = [{parameter: "opacity", seconds: 0, value: 0.25}]
        if (index.trackParameter(track, null, "opacity").length !== 181
                || index.trackParameter(track, override, "opacity")[0].value !== 0.25
                || index.trackParameter(track, null, "opacity")[0].value !== 0)
            return "Track overrides must have independent indexes and restore the base"
        return ""
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/keyframe-index-inline.qml")));
  std::unique_ptr<QObject> keyframe_fixture(keyframe_fixture_component.create());
  if (!keyframe_fixture) {
    std::cerr << keyframe_fixture_component.errorString().toStdString();
    return fail("The production keyframe index must load");
  }
  QVariant keyframe_index_error;
  if (!QMetaObject::invokeMethod(keyframe_fixture.get(), "verifyIndex",
      Q_RETURN_ARG(QVariant, keyframe_index_error)))
    return fail("The indexed evaluation proof must execute");
  if (!keyframe_index_error.toString().isEmpty()) {
    std::cerr << keyframe_index_error.toString().toStdString() << '\n';
    return EXIT_FAILURE;
  }
  // Exercise C++-backed QVariant snapshots across collections, as the real
  // Program model does. The cache must retain safe references and stay bounded.
  for (int index = 0; index < 180; ++index) {
    const QVariant snapshot = QVariantMap{
        {"clipId", QStringLiteral("native-%1").arg(index)},
        {"keyframes", QVariantList{
            QVariantMap{{"parameter", "frameScale"}, {"seconds", 0.0}, {"value", 1.0}},
            QVariantMap{{"parameter", "frameScale"}, {"seconds", 1.0}, {"value", 2.0}}}}};
    QVariant result;
    if (!QMetaObject::invokeMethod(keyframe_fixture.get(), "indexNative",
        Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, snapshot)) || result.toInt() != 2)
      return fail("C++-backed snapshot channels must remain available across garbage collection");
    if (index % 8 == 0) engine.collectGarbage();
  }
  QVariant cache_size;
  if (!QMetaObject::invokeMethod(keyframe_fixture.get(), "cacheSize",
      Q_RETURN_ARG(QVariant, cache_size)) || cache_size.toInt() != 128)
    return fail("Keyframe snapshot retention must remain bounded during a long edit");
  QQmlComponent helper_component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineTrackWindow.qml")));
  std::unique_ptr<QObject> helper(helper_component.create());
  if (!helper) return fail("The production track-window helper must load");

  QVariantList tracks;
  for (int row = 0; row < 192; ++row) {
    QVariantList clips;
    for (int column = 0; column < 7; ++column) {
      const auto start = double(row + column * 300);
      clips.push_back(QVariantMap{
          {"clipId", QStringLiteral("clip-%1-%2").arg(row).arg(column)},
          {"startSeconds", start}, {"endSeconds", start + 4.0}});
    }
    tracks.push_back(QVariantMap{
        {"trackId", QStringLiteral("track-%1").arg(row)}, {"clips", clips}});
  }
  const auto before = tracks;
  const auto range = collect(helper.get(), tracks, 50, 60, {});
  const auto visible = range.value("trackIds").toMap();
  if (visible.size() != 13 || visible.contains("track-46") ||
      !visible.contains("track-47") || !visible.contains("track-59") ||
      visible.contains("track-60"))
    return fail("Track filtering must honor half-open range boundaries");
  const auto selected = collect(helper.get(), tracks, 50, 60,
      {QStringLiteral("clip-180-0")}).value("trackIds").toMap();
  if (selected.size() != 14 || !selected.contains("track-180"))
    return fail("A selected clip outside the viewport must remain available");
  auto targeted = tracks.at(181).toMap();
  targeted.insert("targeted", true);
  targeted.insert("clips", QVariantList{});
  tracks[181] = targeted;
  if (!collect(helper.get(), tracks, 50, 60, {}).value("trackIds").toMap().contains("track-181"))
    return fail("An empty targeted insertion lane must remain available");
  targeted.insert("targeted", false);
  tracks[181] = targeted;
  if (!collect(helper.get(), tracks, 50, 60, {}).value("trackIds").toMap().contains("track-181"))
    return fail("A newly added empty lane must not disappear from the filtered view");
  for (int row = 182; row < 192; ++row) {
    auto empty = tracks.at(row).toMap();
    empty.insert("clips", QVariantList{});
    empty.insert("trackKind", row == 190 ? "audio" : "video");
    tracks[row] = empty;
  }
  auto insertion = collect(helper.get(), tracks, 50, 60, {}).value("trackIds").toMap();
  if (insertion.contains("track-181") || insertion.contains("track-189") ||
      !insertion.contains("track-190") || !insertion.contains("track-191") ||
      insertion.size() != 15)
    return fail("Focused tracks must retain one newest empty lane per kind, not every abandoned lane");
  targeted.insert("targeted", true);
  tracks[181] = targeted;
  insertion = collect(helper.get(), tracks, 50, 60, {}).value("trackIds").toMap();
  if (!insertion.contains("track-181") || insertion.size() != 16)
    return fail("Explicit targeting must keep an older empty lane available");
  // Keep nested group controls without including every unrelated group member.
  tracks = before;
  for (const int row : {0, 1, 55}) {
    auto track = tracks.at(row).toMap();
    track.insert("groupId", row == 0 ? "parent" : "child");
    tracks[row] = track;
  }
  const QVariantMap groups{{"parent", QVariantMap{}},
      {"child", QVariantMap{{"parentGroupId", "parent"}}}};
  const auto grouped = collect(helper.get(), tracks, 50, 60, {}, groups)
      .value("trackIds").toMap();
  if (!grouped.contains("track-0") || !grouped.contains("track-1") ||
      !grouped.contains("track-55"))
    return fail("Nested group heads must stay available in a filtered view");
  if (before.at(55).toMap().value("clips") != tracks.at(55).toMap().value("clips"))
    return fail("Filtering must not rewrite or reorder authored clips");

  QQmlComponent selection_component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineSelectionIntent.qml")));
  std::unique_ptr<QObject> selection(selection_component.create());
  if (!selection) return fail("The production selection intent helper must load");
  const auto choose = [&](double time, const QString& selected_id) {
    QVariant result;
    const QVariantList lane_clips{
        QVariantMap{{"clipId", "earlier"}, {"startSeconds", 0.0}, {"endSeconds", 3.0}},
        QVariantMap{{"clipId", "under-playhead"}, {"startSeconds", 5.0}, {"endSeconds", 8.0}},
        QVariantMap{{"clipId", "top-overlay"}, {"startSeconds", 5.0}, {"endSeconds", 7.0}},
        QVariantMap{{"clipId", "later"}, {"startSeconds", 10.0}, {"endSeconds", 13.0}}};
    const QVariantList lanes{QVariantMap{{"trackId", "layer"}, {"clips", lane_clips}}};
    if (!QMetaObject::invokeMethod(selection.get(), "clipForTrack", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, lanes), Q_ARG(QVariant, QStringLiteral("layer")),
        Q_ARG(QVariant, time), Q_ARG(QVariant, selected_id))) return QString{};
    const auto value = result.metaType() == QMetaType::fromType<QJSValue>()
        ? result.value<QJSValue>().toVariant() : result;
    return value.toMap().value("clipId").toString();
  };
  if (choose(6.0, {}) != "top-overlay" || choose(6.0, "earlier") != "earlier" ||
      choose(8.0, {}) != "under-playhead" || choose(9.5, {}) != "later")
    return fail("Track inspection must preserve selection, choose the top active layer, or the nearest clip");
  QVariant chosen_empty;
  const QVariant empty_lanes = QVariantList{
      QVariantMap{{"trackId", "empty"}, {"clips", QVariantList{}}}};
  if (!QMetaObject::invokeMethod(selection.get(), "clipForTrack", Q_RETURN_ARG(QVariant, chosen_empty),
      Q_ARG(QVariant, empty_lanes),
      Q_ARG(QVariant, QStringLiteral("empty")), Q_ARG(QVariant, 2.0), Q_ARG(QVariant, QString{})) ||
      (!chosen_empty.isNull() && chosen_empty.value<QJSValue>().toVariant().isValid()))
    return fail("Empty tracks must not inspect an unrelated clip");

  QQmlComponent surface_component(&engine);
  surface_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    width: 320; height: 26
    C.Theme { id: testTheme }
    C.TimelineClipSurface {
        id: surface
        objectName: "surface"
        anchors.fill: parent
        theme: testTheme
        mediaType: "title"
        C.TimelineClipContent {
            objectName: "content"
            anchors.fill: parent
            title: "One photograph. Across every canvas."
            clipData: ({mediaType: "title", timelineLabel: "One photograph. Across every canvas.",
                        keyframeCount: 2, timelineMaskSummary: "Clip mask · ellipse"})
            labelOnly: true
            editSummary: "Masked · Animated"
            horizontalInset: 4
        }
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/dense-surface-inline.qml")));
  std::unique_ptr<QObject> surface(surface_component.create());
  if (!surface) {
    std::cerr << surface_component.errorString().toStdString();
    return fail("The compact production clip surface must instantiate");
  }
  QQuickWindow window;
  window.resize(320, 26);
  qobject_cast<QQuickItem*>(surface.get())->setParentItem(window.contentItem());
  window.show();
  QTest::qWait(80);
  if (surface->findChild<QObject*>(QStringLiteral("timelineClipMaterialRail")))
    return fail("Clips must not have a top material border");
  const auto labels = surface->findChildren<QQuickItem*>();
  QQuickItem* title = nullptr;
  QQuickItem* operations = nullptr;
  for (auto* item : labels) {
    if (item->objectName() == "timelineClipTitleLabel") title = item;
    if (item->objectName() == "timelineClipOperationSummary") operations = item;
  }
  if (!title || !title->isVisible() || (operations && operations->isVisible()))
    return fail("Compact clips must expose their actual text without visible operation summaries");
  const auto title_bottom = title->mapToScene(QPointF(0, title->height())).y();
  if (title_bottom > 26)
    return fail("The clip identity must fit within the compact row");
  window.hide();

  QQmlComponent effects_component(&engine);
  effects_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    id: effectFixture
    width: 320; height: 100
    property int effectRequests: 0
    C.Theme { id: effectTheme; objectName: "effectTestTheme" }
    C.TimelineClipSurface {
        anchors.fill: parent
        theme: effectTheme
        mediaType: "video"
        C.TimelineClipContent {
            objectName: "effectsContent"
            anchors.fill: parent
            title: "River boat"
            mediaType: "video"
            clipData: ({mediaType: "video", timelineLabel: "River boat",
                        effectCount: 2, effectNames: ["Glow", "Grain"], effectsEnabled: true})
            editSummary: "Glow · Grain · FX 2"
            statusBadges: [{key: "effects", label: "FX 2", iconName: "effects",
                            tooltip: "2 applied effects"}]
            onEffectControlsRequested: effectFixture.effectRequests += 1
        }
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/clip-effects-inline.qml")));
  std::unique_ptr<QObject> effects_fixture(effects_component.create());
  if (!effects_fixture) {
    std::cerr << effects_component.errorString().toStdString();
    return fail("The compact clip effects-control fixture must instantiate");
  }
  QQuickWindow effects_window;
  effects_window.resize(320, 100);
  qobject_cast<QQuickItem*>(effects_fixture.get())->setParentItem(effects_window.contentItem());
  effects_window.show();
  QTest::qWait(80);

  auto* effects_content = effects_fixture->findChild<QObject*>("effectsContent");
  auto* effects_button = effects_fixture->findChild<QQuickItem*>("timelineClipEffectsButton");
  auto* effects_status = effects_fixture->findChild<QQuickItem*>("timelineClipStatusStrip");
  auto* effect_theme = effects_fixture->findChild<QObject*>("effectTestTheme");
  if (!effects_content || !effects_button || !effects_status || !effect_theme ||
      !effects_button->isVisible() || effects_status->isVisible())
    return fail("Effect status text should collapse into one visible FX control");
  const auto effect_summary = effects_content->property("displaySummary").toString();
  if (effect_summary.contains(QStringLiteral("Glow"), Qt::CaseInsensitive) ||
      effect_summary.contains(QStringLiteral("Grain"), Qt::CaseInsensitive) ||
      effect_summary.contains(QStringLiteral("FX 2"), Qt::CaseInsensitive))
    return fail("Effect names and FX counts should not be listed in clip-row summaries");
  if (effects_content->property("effectIndicatorColor").value<QColor>() !=
      effect_theme->property("success").value<QColor>())
    return fail("Enabled clip effects should use the success indicator color");

  QPointF button_center = effects_button->mapToScene(
      QPointF(effects_button->width() / 2.0, effects_button->height() / 2.0));
  QTest::mouseClick(&effects_window, Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(button_center.x()), qRound(button_center.y())));
  if (effects_fixture->property("effectRequests").toInt() != 1)
    return fail("The FX icon should emit effectControlsRequested on click");

  QVariantMap bypassed_data{{QStringLiteral("mediaType"), QStringLiteral("video")},
                            {QStringLiteral("timelineLabel"), QStringLiteral("River boat")},
                            {QStringLiteral("effectCount"), 2},
                            {QStringLiteral("effectsEnabled"), false}};
  effects_content->setProperty("clipData", bypassed_data);
  QTest::qWait(10);
  if (effects_content->property("effectIndicatorColor").value<QColor>() !=
      effect_theme->property("warning").value<QColor>())
    return fail("Bypassed clip effects should use the warning indicator color");

  QVariantMap no_effects_data{{QStringLiteral("mediaType"), QStringLiteral("video")},
                              {QStringLiteral("timelineLabel"), QStringLiteral("River boat")},
                              {QStringLiteral("effectCount"), 0}};
  effects_content->setProperty("clipData", no_effects_data);
  QTest::qWait(10);
  if (effects_content->property("effectIndicatorColor").value<QColor>() !=
      effect_theme->property("textFaint").value<QColor>())
    return fail("Clips without effects should use the subdued FX indicator color");
  no_effects_data.insert(QStringLiteral("colorGradeRecipeEnabled"), true);
  effects_content->setProperty("clipData", no_effects_data);
  QTest::qWait(10);
  if (!effects_content->property("hasAppliedEffects").toBool() ||
      effects_content->property("effectIndicatorColor").value<QColor>() !=
          effect_theme->property("success").value<QColor>())
    return fail("Shared photo-engine effects and color layers must also light the clip FX indicator");
  effects_window.hide();

  QQmlComponent hover_component(&engine);
  hover_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    id: fixture
    width: 400; height: 240
    property int clicks: 0
    MouseArea { anchors.fill: parent; onClicked: fixture.clicks += 1 }
    Item { id: group; x: 30; y: 15; rotation: 7; scale: 1.1
        Item { id: target; x: 80; y: 40; width: 100; height: 60; rotation: 20; scale: 1.5 }
    }
    C.TimelineHoverOutline {
        id: outline
        anchors.fill: parent
        targetItem: target
        active: true
    }
    function verifyGeometry() {
        const expected = target.mapToItem(outline, 0, 0)
        if (!outline.hasOutline || outline.corners.length !== 4
                || Math.abs(outline.corners[0].x - expected.x) > .001
                || Math.abs(outline.corners[0].y - expected.y) > .001)
            return "Hover bounds must follow actual nested translation, rotation and scale"
        group.x += 40
        const moved = target.mapToItem(outline, 0, 0)
        if (Math.abs(outline.corners[0].x - moved.x) > .001)
            return "Hover bounds must refresh after an ancestor moves"
        target.opacity = 0
        if (outline.hasOutline) return "Invisible elements must not produce misleading hover bounds"
        target.opacity = 1
        outline.active = false
        if (outline.hasOutline) return "Hover exit must clear the outline"
        outline.active = true
        outline.projectionItem = target
        outline.sceneQuad = [{x:.1,y:.2},{x:.9,y:.1},{x:0,y:1},{x:1,y:1}]
        const projectedLocal = [Qt.point(10,12),Qt.point(90,6),Qt.point(100,60),Qt.point(0,60)]
        for (let index = 0; index < 4; ++index) {
            const projected = target.mapToItem(outline, projectedLocal[index].x, projectedLocal[index].y)
            if (Math.abs(outline.corners[index].x - projected.x) > .001
                    || Math.abs(outline.corners[index].y - projected.y) > .001)
                return "All four hover corners must match Program's TL-TR-BL-BR order without crossed edges"
        }
        return ""
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/clip-hover-inline.qml")));
  std::unique_ptr<QObject> hover_fixture(hover_component.create());
  if (!hover_fixture) {
    std::cerr << hover_component.errorString().toStdString();
    return fail("The production clip hover outline must load");
  }
  QQuickWindow hover_window;
  hover_window.resize(400, 240);
  qobject_cast<QQuickItem*>(hover_fixture.get())->setParentItem(hover_window.contentItem());
  hover_window.show();
  QTest::qWait(40);
  QVariant hover_error;
  if (!QMetaObject::invokeMethod(hover_fixture.get(), "verifyGeometry", Q_RETURN_ARG(QVariant, hover_error)) ||
      !hover_error.toString().isEmpty()) {
    std::cerr << hover_error.toString().toStdString();
    return fail("The clip hover outline must match the rendered element geometry");
  }
  QTest::mouseClick(&hover_window, Qt::LeftButton, Qt::NoModifier, QPoint(100, 100));
  if (hover_fixture->property("clicks").toInt() != 1)
    return fail("Viewer hover outlines must not intercept editing input");
  hover_window.hide();

  QQmlComponent marker_component(&engine);
  marker_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    id: fixture
    width: 320; height: 220
    property int clipClicks: 0
    property int markerJumps: 0
    MouseArea { anchors.fill: parent; onClicked: fixture.clipClicks += 1 }
    C.TimelineMarkerPin {
        x: 50; y: 0; height: 220
        anchorPositionPixels: 120
        compactPresentation: true
        label: "A chapter with a useful descriptive name"
        onJumpRequested: fixture.markerJumps += 1
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/marker-hit-inline.qml")));
  std::unique_ptr<QObject> marker_fixture(marker_component.create());
  if (!marker_fixture) return fail("The production marker hit-area fixture must load");
  QQuickWindow marker_window;
  marker_window.resize(320, 220);
  qobject_cast<QQuickItem*>(marker_fixture.get())->setParentItem(marker_window.contentItem());
  marker_window.show();
  QTest::qWait(80);
  QTest::mouseClick(&marker_window, Qt::LeftButton, Qt::NoModifier, QPoint(120, 110));
  if (marker_fixture->property("clipClicks").toInt() != 1 ||
      marker_fixture->property("markerJumps").toInt() != 0)
    return fail("Marker guides must let clip-body pointer input through below their heads");
  QTest::mouseClick(&marker_window, Qt::LeftButton, Qt::NoModifier, QPoint(120, 10));
  if (marker_fixture->property("markerJumps").toInt() != 1)
    return fail("The compact marker head must retain its direct jump action");
  marker_window.hide();
  QQmlComponent notes_scale_component(&engine);
  notes_scale_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Item {
    id: fixture
    width: 900; height: 500
    property var saved: ({})
    property int horizontalChanges: 0
    property int verticalChanges: 0
    property int noteEdits: 0
    property bool followEnabled: true
    property real clock: 37.5
    C.Theme { id: t }
    QtObject {
        id: controller
        property bool reject: false
        property real totalDurationSeconds: 10
        property string status: "Save failed"
        function addAnchoredReviewNote(label, note, start, end, ids, assignee, approval) {
            if (reject) return ""
            fixture.saved = {note: note, start: start, end: end, ids: ids.slice()}
            return "new-note"
        }
    }
    C.TimelineScaleStrip {
        id: scales
        objectName: "scales"
        theme: t
        width: parent.width
        onHorizontalScaleRequested: fixture.horizontalChanges += 1
        onVerticalScaleRequested: fixture.verticalChanges += 1
        followPlayhead: fixture.followEnabled
        onFollowPlayheadRequested: function(enabled) { fixture.followEnabled = enabled }
    }
    C.TimelineNoteComposer {
        id: composer
        theme: t
        documentController: controller
    }
    C.TimelineMarkerPin {
        id: pin
        x: 100; y: 60; height: 200
        anchorPositionPixels: 122
        rangeStartPixels: 122
        rangeEndPixels: 244
        reviewNote: true
        compactPresentation: true
        accent: "#47b5ff"
        onEditRequested: fixture.noteEdits += 1
        onJumpRequested: fixture.clock = 0
    }
    function verifyDraft() {
        const ids = ["clip-a"]
        composer.beginDraft(123, 123, ids, "Color adjustment")
        ids[0] = "changed-selection"
        clock = 48
        composer.noteText = "  Lift the shadows  "
        if (!composer.saveNote() || saved.start !== 123 || saved.end !== 123
                || saved.ids[0] !== "clip-a" || saved.note !== "Lift the shadows" || clock !== 48)
            return "Notes must retain their frame and clip context without seeking"
        composer.beginDraft(90, 180, ["clip-b"], "Whole clip")
        composer.noteText = "Keep this range"
        controller.reject = true
        if (composer.saveNote() || !composer.visible || composer.noteText !== "Keep this range" || !composer.errorText.length)
            return "A failed note save must keep the draft and show the error"
        controller.reject = false
        if (!composer.saveNote() || saved.start !== 90 || saved.end !== 180 || saved.ids[0] !== "clip-b")
            return "Whole-clip notes must retain their frame range"
        composer.beginDraft(60, 60, ["clip-c"], "Range draft")
        composer.noteText = "Check the transition"
        if (!composer.changeAnchorKind(true) || !composer.commitAnchorText("2.25", false)
                || !composer.commitAnchorText("4.5", true) || composer.startFrame !== 68 || composer.endFrame !== 135)
            return "Note time editing must snap to exact native frame boundaries"
        if (composer.commitAnchorText("1", true) || composer.saveNote() || !composer.visible)
            return "An invalid range must preserve the draft and prevent saving"
        if (!composer.commitAnchorText("4.5", true) || !composer.saveNote()
                || saved.start !== 68 || saved.end !== 135 || saved.ids[0] !== "clip-c")
            return "Range note editing must retain the frozen clip attachment"
        return ""
    }
    function compactLayout() { width = 480 }
    function embeddedLayout() { width = 340; scales.embedded = true; scales.noteCount = 3 }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/notes-scale-inline.qml")));
  std::unique_ptr<QObject> notes_scale_fixture(notes_scale_component.create());
  if (!notes_scale_fixture) {
    std::cerr << notes_scale_component.errorString().toStdString();
    return fail("Direct note composer and timeline scales must instantiate");
  }
  QQuickWindow scale_window;
  scale_window.resize(900, 500);
  qobject_cast<QQuickItem*>(notes_scale_fixture.get())->setParentItem(scale_window.contentItem());
  scale_window.show();
  QTest::qWait(80);
  QVariant draft_error;
  if (!QMetaObject::invokeMethod(notes_scale_fixture.get(), "verifyDraft", Q_RETURN_ARG(QVariant, draft_error)) ||
      !draft_error.toString().isEmpty()) {
    std::cerr << draft_error.toString().toStdString();
    return fail("Direct note creation must freeze anchors and retain failed drafts");
  }
  QTest::qWait(200);
  auto* range_rail = notes_scale_fixture->findChild<QQuickItem*>(QStringLiteral("timelineElectricNoteRange"));
  if (!range_rail || !range_rail->isVisible() || qAbs(range_rail->width() - 122) > 0.5)
    return fail("A note range must draw its actual duration in the notes lane");
  auto* follow_button = notes_scale_fixture->findChild<QQuickItem*>(QStringLiteral("timelineFollowPlayheadButton"));
  if (!follow_button) return fail("Playback follow must be directly discoverable");
  QTest::mouseClick(&scale_window, Qt::LeftButton, Qt::NoModifier,
      follow_button->mapToScene(follow_button->boundingRect().center()).toPoint());
  if (notes_scale_fixture->property("followEnabled").toBool())
    return fail("The follow control must update the real view state");
  QTest::mouseClick(&scale_window, Qt::LeftButton, Qt::NoModifier, QPoint(122, 70));
  if (notes_scale_fixture->property("noteEdits").toInt() != 1 || notes_scale_fixture->property("clock").toDouble() != 48)
    return fail("Blue note pins must open editing without seeking playback");
  QMetaObject::invokeMethod(notes_scale_fixture.get(), "compactLayout");
  QTest::qWait(80);
  for (const auto* name : {"timelineHorizontalScaleSlider", "timelineVerticalScaleSlider"}) {
    auto* slider = notes_scale_fixture->findChild<QQuickItem*>(QString::fromUtf8(name));
    if (!slider || !slider->isVisible() || slider->width() < 64 || slider->mapToScene(QPointF(slider->width(), 0)).x() > 480)
      return fail("Both scaling controls must remain reachable in compact timeline layouts");
    const auto point = slider->mapToScene(QPointF(slider->width() * 0.8, slider->height() / 2)).toPoint();
    QTest::mouseClick(&scale_window, Qt::LeftButton, Qt::NoModifier, point);
  }
  if (notes_scale_fixture->property("horizontalChanges").toInt() < 1 ||
      notes_scale_fixture->property("verticalChanges").toInt() < 1)
    return fail("Both timeline scale sliders must respond directly to pointer input");
  const auto changes_before = notes_scale_fixture->property("horizontalChanges").toInt();
  auto* accessible_zoom = notes_scale_fixture->findChild<QObject*>(QStringLiteral("timelineHorizontalScaleSlider"));
  accessible_zoom->setProperty("value", 5.0);
  if (notes_scale_fixture->property("horizontalChanges").toInt() != changes_before + 1)
    return fail("An accessibility scale adjustment must update the timeline, not just its slider handle");
  QMetaObject::invokeMethod(notes_scale_fixture.get(), "embeddedLayout");
  QTest::qWait(50);
  auto* embedded_scales = notes_scale_fixture->findChild<QQuickItem*>(QStringLiteral("scales"));
  if (!embedded_scales || embedded_scales->height() > 36)
    return fail("Embedded timeline scales must fit in one compact command row");
  for (const auto* name : {"timelineHorizontalScaleSlider", "timelineVerticalScaleSlider", "timelineDirectAddNoteButton"}) {
    auto* control = notes_scale_fixture->findChild<QQuickItem*>(QString::fromUtf8(name));
    if (!control || !control->isVisible() || control->mapToScene(QPointF(0, 0)).x() < 0 ||
        control->mapToScene(QPointF(control->width(), 0)).x() > 340.5)
      return fail("Embedded time, lane scale and Notes controls must fit without overlap or clipping");
  }
  const auto evidence_dir = qEnvironmentVariable("LOOKSY_TIMELINE_SURFACE_EVIDENCE_DIR");
  if (!evidence_dir.isEmpty()) {
    QDir().mkpath(evidence_dir);
    if (!scale_window.grabWindow().save(QDir(evidence_dir).filePath(QStringLiteral("compact-scales.png"))))
      return fail("Timeline surface proof must capture the rendered compact scales");
  }
  std::cout << "Dense surface, note anchors, note pointer actions and timeline scaling passed\n";
  return EXIT_SUCCESS;
}
