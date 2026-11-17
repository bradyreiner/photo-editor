#include "timeline_document_controller.h"

#include <QAccessible>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

using photo_editor::desktop::TimelineDocumentController;

class CountingEditorialDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList clips READ clips NOTIFY clipsChanged)
  Q_PROPERTY(bool hasActiveTimeline READ hasActiveTimeline CONSTANT)
  Q_PROPERTY(QVariantMap currentClip READ currentClip CONSTANT)
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(bool canUndo READ canUndo CONSTANT)
  Q_PROPERTY(int captionCount READ captionCount CONSTANT)
  Q_PROPERTY(QString status READ status CONSTANT)

 public:
  CountingEditorialDocument() {
    for (int index = 0; index < 518; ++index) {
      clips_.push_back(QVariantMap{
          {QStringLiteral("clipId"), QStringLiteral("clip-%1").arg(index)},
          {QStringLiteral("mediaType"), QStringLiteral("video")},
          {QStringLiteral("keyframes"), QVariantList{QVariantMap{
               {QStringLiteral("parameter"), QStringLiteral("opacity")},
               {QStringLiteral("localSeconds"), 0.0},
               {QStringLiteral("value"), 1.0}}}},
      });
    }
    sequence_tracks_.push_back(QVariantMap{
        {QStringLiteral("trackId"), QStringLiteral("caption-track")},
        {QStringLiteral("locked"), false},
    });
    caption_cues_.push_back(QVariantMap{
        {QStringLiteral("clipId"), QStringLiteral("caption-1")},
        {QStringLiteral("index"), 518},
        {QStringLiteral("mediaType"), QStringLiteral("caption")},
        {QStringLiteral("trackId"), QStringLiteral("caption-track")},
        {QStringLiteral("startSeconds"), 0.0},
        {QStringLiteral("endSeconds"), 2.0},
        {QStringLiteral("titleText"), QStringLiteral("A bounded caption cue")},
    });
  }

  [[nodiscard]] QVariantList clips() const {
    ++clip_property_reads_;
    return clips_;
  }
  [[nodiscard]] bool hasActiveTimeline() const { return true; }
  [[nodiscard]] QVariantMap currentClip() const { return {}; }
  [[nodiscard]] QVariantList sequenceTracks() const {
    ++sequence_track_property_reads_;
    return sequence_tracks_;
  }
  [[nodiscard]] bool canUndo() const { return false; }
  [[nodiscard]] int captionCount() const {
    return static_cast<int>(caption_cues_.size());
  }
  [[nodiscard]] QString status() const { return {}; }
  [[nodiscard]] int clipPropertyReads() const { return clip_property_reads_; }
  [[nodiscard]] int snapshotCalls() const { return snapshot_calls_; }
  [[nodiscard]] int sequenceTrackPropertyReads() const {
    return sequence_track_property_reads_;
  }
  [[nodiscard]] int sequenceTrackSnapshotCalls() const {
    return sequence_track_snapshot_calls_;
  }

  Q_INVOKABLE QVariantList captionCueSnapshot() const {
    ++snapshot_calls_;
    return caption_cues_;
  }
  Q_INVOKABLE QVariantList snapshotSequenceTracks() const {
    ++sequence_track_snapshot_calls_;
    return sequence_tracks_;
  }
  Q_INVOKABLE QVariantMap inspectTimelineClipById(const QString& clip_id) const {
    if (clip_id != QStringLiteral("caption-1"))
      return {{QStringLiteral("ok"), false}};
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("clip"), caption_cues_.first()}};
  }
  Q_INVOKABLE bool addTitleClipFromTemplate(const QString&) { return false; }
  Q_INVOKABLE bool addGraphicShape(const QString&) { return false; }
  Q_INVOKABLE bool seekSequenceSeconds(double) { return true; }
  Q_INVOKABLE bool selectClip(int) { return true; }
  Q_INVOKABLE bool updateSelectedTitle(const QVariantMap&) { return true; }
  Q_INVOKABLE bool seekTranscriptWord(const QString&, int, const QString&) { return true; }
  Q_INVOKABLE bool updateTranscriptWord(
      const QString&, int, const QString&, const QString&) { return true; }
  Q_INVOKABLE void undo() {}
  void notifyClipsChanged() { emit clipsChanged(); }
  void setCaptionTrackLocked(bool locked) {
    if (sequence_tracks_.isEmpty()) return;
    auto track = sequence_tracks_.first().toMap();
    track.insert(QStringLiteral("locked"), locked);
    sequence_tracks_[0] = track;
    emit sequenceChanged();
  }

 signals:
  void clipsChanged();
  void sequenceChanged();

 private:
  QVariantList clips_;
  QVariantList sequence_tracks_;
  QVariantList caption_cues_;
  mutable int clip_property_reads_ = 0;
  mutable int snapshot_calls_ = 0;
  mutable int sequence_track_property_reads_ = 0;
  mutable int sequence_track_snapshot_calls_ = 0;
};

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void settle(int milliseconds = 100) {
  const auto steps = std::max(1, milliseconds / 5);
  for (int index = 0; index < steps; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QObject* find_object(QObject* root, const QString& object_name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == object_name)
    return root;
  for (auto* child : root->children()) {
    if (auto* found = find_object(child, object_name))
      return found;
  }
  return nullptr;
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr)
    return nullptr;
  if (root->objectName() == object_name)
    return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name))
      return found;
  }
  return nullptr;
}

void click_center(QQuickWindow* window, QQuickItem* item) {
  const QPointF center(item->width() / 2.0, item->height() / 2.0);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(center).toPoint());
}

bool tooltip_bounds_cover_scene_point(QQuickItem* tooltip,
                                      const QPointF& scene_point) {
  if (tooltip == nullptr || !tooltip->isVisible())
    return false;
  return tooltip->boundingRect().contains(tooltip->mapFromScene(scene_point));
}

bool tooltip_is_topmost_at(QQuickWindow* window, const QPointF& scene_point) {
  if (window == nullptr || window->contentItem() == nullptr)
    return false;
  QQuickItem* item = window->contentItem();
  while (item != nullptr && item->isVisible() &&
         item->contains(item->mapFromScene(scene_point))) {
    if (item->objectName() == QStringLiteral("AppToolTip"))
      return true;
    QQuickItem* next = nullptr;
    for (auto* child : item->childItems()) {
      if (child != nullptr && child->isVisible() &&
          child->contains(child->mapFromScene(scene_point)) &&
          (next == nullptr || child->z() >= next->z())) {
        next = child;
      }
    }
    item = next;
  }
  return false;
}

void report_item_geometry(const char* label, QQuickWindow* window, QQuickItem* item) {
  if (item == nullptr) {
    std::cerr << label << "=<missing>\n";
    return;
  }
  const QPointF origin = item->mapToScene(QPointF(0.0, 0.0));
  std::cerr << label << " bounds=x:" << origin.x() << ",y:" << origin.y()
            << ",w:" << item->width() << ",h:" << item->height()
            << " visible:" << item->isVisible()
            << " focus:" << item->hasActiveFocus()
            << " window:" << (window != nullptr ? window->width() : 0)
            << 'x' << (window != nullptr ? window->height() : 0) << '\n';
}

bool item_fits_window(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible() ||
      item->width() <= 0.0 || item->height() <= 0.0)
    return false;
  const QPointF origin = item->mapToScene(QPointF(0.0, 0.0));
  return origin.x() >= 0.0 && origin.y() >= 0.0 &&
         origin.x() + item->width() <= window->width() + 1.0 &&
         origin.y() + item->height() <= window->height() + 1.0;
}

void report_pointer_hit_stack(QQuickWindow* window, const QPointF& scene_point) {
  if (window == nullptr || window->contentItem() == nullptr)
    return;
  QQuickItem* item = window->contentItem();
  const QPointF local = item->mapFromScene(scene_point);
  std::cerr << "caption pointer hit path:";
  while (item != nullptr) {
    if (!item->isVisible() || !item->contains(item->mapFromScene(scene_point)))
      break;
    std::cerr << ' ' << item->metaObject()->className() << '['
              << item->objectName().toStdString() << ']';
    QQuickItem* next = nullptr;
    for (auto* child : item->childItems()) {
      if (child != nullptr && child->isVisible() &&
          child->contains(child->mapFromScene(scene_point)) &&
          (next == nullptr || child->z() >= next->z())) {
        next = child;
      }
    }
    item = next;
  }
  std::cerr << " local=" << local.x() << ',' << local.y() << '\n';
  const auto windows = QGuiApplication::topLevelWindows();
  for (auto* top_level : windows) {
    if (top_level == nullptr || !top_level->isVisible())
      continue;
    std::cerr << "visible window=" << top_level->metaObject()->className()
              << '[' << top_level->objectName().toStdString() << "] title="
              << top_level->title().toStdString();
    if (auto* quick_window = qobject_cast<QQuickWindow*>(top_level)) {
      auto* grabber = quick_window->mouseGrabberItem();
      std::cerr << " grabber="
                << (grabber != nullptr ? grabber->metaObject()->className() : "<none>")
                << '[' << (grabber != nullptr ? grabber->objectName().toStdString() : "")
                << ']';
    }
    std::cerr << '\n';
  }
}

void capture_failure_window(QQuickWindow* window) {
  if (window == nullptr)
    return;
  const QImage capture = window->grabWindow();
  if (capture.isNull()) {
    std::cerr << "QML capture unavailable for the caption edit failure\n";
    return;
  }
  const QString path = QDir(QDir::tempPath()).filePath(
      QStringLiteral("timeline-editorial-caption-failure.png"));
  if (capture.save(path))
    std::cerr << "QML capture: " << path.toStdString() << '\n';
}

void key_type(QQuickWindow* window, const QString& text, int delay = 5) {
  for (const QChar character : text) {
    if (character.unicode() > 0xff)
      continue;
    QTest::keyClick(window, character.toLatin1(), Qt::NoModifier, delay);
  }
}

QVariantMap clip_of_type(const QVariantList& clips, const QString& media_type) {
  for (const auto& value : clips) {
    const auto clip = value.toMap();
    if (clip.value(QStringLiteral("mediaType")).toString() == media_type)
      return clip;
  }
  return {};
}

QVariantMap transcript_word(const QVariantMap& segment, int index) {
  for (const auto& value : segment.value(QStringLiteral("words")).toList()) {
    const auto word = value.toMap();
    if (word.value(QStringLiteral("index")).toInt() == index)
      return word;
  }
  return {};
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);
  QAccessible::setActive(true);

  QTemporaryDir workspace;
  if (!workspace.isValid())
    return fail("An isolated editorial panel workspace should be available");
  const QString source_path = workspace.filePath(QStringLiteral("editorial-panel-source.png"));
  QImage source(128, 72, QImage::Format_RGB32);
  source.fill(qRgb(78, 112, 146));
  if (!source.save(source_path))
    return fail("The editorial controller fixture should have a real media source");

  TimelineDocumentController controller;
  QQmlPropertyMap app_shell;
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  app_shell.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  engine.rootContext()->setContextProperty(QStringLiteral("testDocument"), &controller);
  CountingEditorialDocument counting_document;
  engine.rootContext()->setContextProperty(QStringLiteral("countingDocument"),
                                           &counting_document);

  QQmlComponent bounded_snapshot_component(&engine);
  const QByteArray bounded_snapshot_qml =
      R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    width: 560
    height: 900
    visible: true
    color: theme.ink950
    Theme { id: theme }
    TimelineEditorialPanel {
        id: panel
        objectName: "boundedSnapshotPanel"
        anchors.fill: parent
        theme: theme
        documentController: countingDocument
        currentClip: countingDocument.currentClip
        kind: "captions"
        titleTemplates: []
        transcriptSegments: []
    }
}
)qml";
  bounded_snapshot_component.setData(
      bounded_snapshot_qml,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/timeline_editorial_panel_ui_test.qml")));
  std::unique_ptr<QObject> bounded_snapshot_fixture(
      bounded_snapshot_component.create());
  if (!bounded_snapshot_fixture) {
    std::cerr << bounded_snapshot_component.errorString().toStdString() << '\n';
    return fail("The editorial panel should accept a stable caption snapshot source");
  }
  auto* bounded_snapshot_window =
      qobject_cast<QQuickWindow*>(bounded_snapshot_fixture.get());
  if (bounded_snapshot_window == nullptr)
    return fail("The bounded caption snapshot fixture should create a QQuickWindow");
  bounded_snapshot_window->show();
  settle();
  auto* bounded_snapshot_panel = find_object(
      bounded_snapshot_window, QStringLiteral("boundedSnapshotPanel"));
  auto* bounded_caption_field = find_item(
      bounded_snapshot_window->contentItem(), QStringLiteral("timelineEditorialCaptionText_caption-1"));
  const auto snapshot_call_count = counting_document.snapshotCalls();
  if (bounded_snapshot_panel == nullptr || snapshot_call_count < 1 ||
      bounded_caption_field == nullptr || !bounded_caption_field->isEnabled() ||
      bounded_snapshot_panel->property("captionCues").toList().size() != 1 ||
      counting_document.clipPropertyReads() != 0 ||
      counting_document.sequenceTrackSnapshotCalls() < 1 ||
      counting_document.sequenceTrackPropertyReads() != 0) {
    return fail("The caption panel should use owned track snapshots without projecting full timeline properties");
  }
  const auto sequence_snapshot_count = counting_document.sequenceTrackSnapshotCalls();
  counting_document.setCaptionTrackLocked(true);
  settle();
  if (counting_document.sequenceTrackSnapshotCalls() != sequence_snapshot_count + 1 ||
      counting_document.sequenceTrackPropertyReads() != 0 ||
      bounded_caption_field->isEnabled()) {
    return fail("A sequenceChanged lock update should refresh one owned snapshot and disable caption editing");
  }
  counting_document.setCaptionTrackLocked(false);
  settle();
  if (counting_document.sequenceTrackSnapshotCalls() != sequence_snapshot_count + 2 ||
      counting_document.sequenceTrackPropertyReads() != 0 ||
      !bounded_caption_field->isEnabled()) {
    return fail("A sequenceChanged unlock update should refresh one owned snapshot and restore caption editing");
  }
  counting_document.notifyClipsChanged();
  settle();
  if (counting_document.snapshotCalls() != snapshot_call_count + 1 ||
      bounded_snapshot_panel->property("captionCues").toList().size() != 1 ||
      counting_document.clipPropertyReads() != 0 ||
      counting_document.sequenceTrackPropertyReads() != 0) {
    return fail("Clip notifications should refresh bounded caption and owned track snapshots without reading all clips");
  }
  bounded_snapshot_fixture.reset();
  settle(20);

  QQmlComponent component(&engine);
  const QByteArray qml =
      R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "timelineEditorialFixture"
    width: 560
    height: 1240
    visible: true
    color: theme.ink950
    property string panelKind: "titles"

    Theme { id: theme }

    TimelineEditorialPanel {
        id: editorialPanel
        objectName: "testedTimelineEditorialPanel"
        anchors.fill: parent
        theme: theme
        documentController: testDocument
        currentClip: testDocument.currentClip
        kind: fixture.panelKind
    }
}
)qml";
  component.setData(qml, QUrl::fromLocalFile(
                            QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                           "/tests/timeline_editorial_panel_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("TimelineEditorialPanel should load with production theme and controls");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (window == nullptr)
    return fail("The editorial panel fixture should create an application window");
  window->show();
  settle();

  auto* panel = find_object(window, QStringLiteral("testedTimelineEditorialPanel"));
  auto* add_headline = find_item(window->contentItem(),
                                 QStringLiteral("timelineEditorialAddTitle_headline"));
  auto* title_picker = find_item(window->contentItem(),
                                 QStringLiteral("timelineEditorialOpenTitlePicker"));
  if (panel == nullptr || add_headline == nullptr || title_picker == nullptr ||
      add_headline->property("enabled").toBool() ||
      panel->property("hasSelectedTranscriptWord").toBool()) {
    return fail("With no timeline, title creation and transcript correction should be safely unavailable");
  }
  fixture->setProperty("panelKind", QStringLiteral("transcript"));
  settle();
  auto* empty_transcript_editor = find_item(
      window->contentItem(), QStringLiteral("timelineEditorialTranscriptCorrection"));
  if (empty_transcript_editor == nullptr || empty_transcript_editor->isVisible() ||
      panel->property("canEditSelectedTranscriptWord").toBool()) {
    return fail("An empty transcript without a selected word should not expose an editable correction");
  }
  fixture->setProperty("panelKind", QStringLiteral("titles"));
  settle();

  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Editorial Panel UI")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 72},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(QStringLiteral("photo:editorial-panel-ui"), source_path,
                                QStringLiteral("Editorial source"), 2.0, source_path);
  settle();
  add_headline = find_item(window->contentItem(),
                           QStringLiteral("timelineEditorialAddTitle_headline"));
  title_picker = find_item(window->contentItem(),
                           QStringLiteral("timelineEditorialOpenTitlePicker"));
  if (add_headline == nullptr || !add_headline->property("enabled").toBool())
    return fail("Real controller title templates should become available after creating a timeline");

  QSignalSpy deep_edit(panel, SIGNAL(openEditorRequested(QString)));
  click_center(window, add_headline);
  settle();
  QVariantMap selected = controller.currentClip();
  if (selected.value(QStringLiteral("mediaType")).toString() != QStringLiteral("title") ||
      selected.value(QStringLiteral("titleTemplateKey")).toString() != QStringLiteral("headline") ||
      selected.value(QStringLiteral("titleText")).toString().isEmpty()) {
    return fail("Choosing a title template should create a selected editable title clip through the controller");
  }

  const QString title_track_id = selected.value(QStringLiteral("trackId")).toString();
  int title_track_index = -1;
  const auto title_tracks = controller.sequenceTracks();
  for (int index = 0; index < title_tracks.size(); ++index) {
    const auto track = title_tracks.at(index).toMap();
    if (track.value(QStringLiteral("trackId")).toString() == title_track_id &&
        track.value(QStringLiteral("trackRole")).toString() == QStringLiteral("graphics")) {
      title_track_index = index;
      break;
    }
  }
  auto* add_ellipse = find_item(window->contentItem(),
                                QStringLiteral("timelineEditorialAddEllipse"));
  if (title_track_index < 0 || add_ellipse == nullptr)
    return fail("The selected title should resolve to its graphics track and shape actions");
  controller.toggleTrackLocked(title_track_index);
  settle();
  if (add_ellipse->property("enabled").toBool())
    return fail("Locked graphics tracks should prevent additional title and shape creation");
  controller.toggleTrackLocked(title_track_index);
  settle();

  auto* add_rectangle = find_item(window->contentItem(),
                                  QStringLiteral("timelineEditorialAddRectangle"));
  if (add_rectangle == nullptr || !add_rectangle->property("enabled").toBool())
    return fail("The graphics panel should expose an enabled real rectangle operation");
  click_center(window, add_rectangle);
  settle();
  selected = controller.currentClip();
  if (selected.value(QStringLiteral("mediaType")).toString() != QStringLiteral("title") ||
      selected.value(QStringLiteral("titleShapeType")).toString() != QStringLiteral("rectangle")) {
    return fail("Choosing a shape should create a real editable graphic clip");
  }
  controller.undo();
  settle();
  const QVariantList clips_after_shape_undo = controller.clips();
  if (controller.currentClip().value(QStringLiteral("titleShapeType")).toString() ==
          QStringLiteral("rectangle") ||
      std::count_if(clips_after_shape_undo.cbegin(), clips_after_shape_undo.cend(),
                     [](const QVariant& value) {
                       return value.toMap().value(QStringLiteral("mediaType")).toString() ==
                              QStringLiteral("title");
                     }) != 1) {
    return fail("The real timeline undo stack should remove the newly added graphic while preserving the title");
  }

  click_center(window, title_picker);
  settle();
  if (deep_edit.size() != 1 || deep_edit.takeFirst().at(0).toString() != QStringLiteral("titles"))
    return fail("The dock title shortcut should preserve the full existing title editor route");

  controller.generateCaptionsFromTranscript(QStringLiteral("Alpha beta gamma."));
  settle();
  QVariantMap caption = clip_of_type(controller.clips(), QStringLiteral("caption"));
  if (caption.isEmpty() || controller.transcriptSegments().isEmpty())
    return fail("The caption and transcript views should use the controller's timed cue model");
  const auto caption_snapshot = controller.captionCueSnapshot();
  if (caption_snapshot.size() != 1 ||
      caption_snapshot.first().toMap().value(QStringLiteral("clipId")).toString() !=
          caption.value(QStringLiteral("clipId")).toString() ||
      caption_snapshot.first().toMap().keys().size() != 7 ||
      caption_snapshot.first().toMap().contains(QStringLiteral("keyframes"))) {
    return fail("The caption projection should preserve cue identity while excluding unrelated clip details");
  }
  const QString caption_id = caption.value(QStringLiteral("clipId")).toString();
  auto* fixture_object = fixture.get();
  fixture_object->setProperty("width", 320);
  fixture_object->setProperty("panelKind", QStringLiteral("captions"));
  settle();

  auto* caption_seek = find_item(window->contentItem(),
                                 QStringLiteral("timelineEditorialSeekCaption_") + caption_id);
  if (caption_seek == nullptr || !caption_seek->property("enabled").toBool())
    return fail("Each timed caption cue should expose a usable seek action");
  click_center(window, caption_seek);
  settle();
  if (controller.currentClip().value(QStringLiteral("clipId")).toString() != caption_id ||
      std::abs(controller.sequencePlayheadSeconds() -
               caption.value(QStringLiteral("startSeconds")).toDouble()) > 0.001) {
    return fail("Selecting a caption cue should select that real clip and seek its sequence timecode");
  }

  auto* caption_text = find_item(window->contentItem(),
                                 QStringLiteral("timelineEditorialCaptionText_") + caption_id);
  if (caption_text == nullptr || !caption_text->property("enabled").toBool())
    return fail("An unlocked caption cue should have an editable text field");
  click_center(window, caption_text);
  settle(30);
  if (!caption_text->hasActiveFocus()) {
    report_item_geometry("caption text field", window, caption_text);
    return fail("Pointer selection should give the visible caption field keyboard focus");
  }
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  key_type(window, QStringLiteral("Edited cue text"));
  settle(20);
  if (caption_text->property("text").toString() != QStringLiteral("Edited cue text")) {
    report_item_geometry("caption text field after typing", window, caption_text);
    std::cerr << "field text=" << caption_text->property("text").toString().toStdString() << '\n';
    capture_failure_window(window);
    return fail("Real QWindow key events should edit the focused caption text field");
  }
  QTest::keyClick(window, Qt::Key_Return);
  settle();
  if (controller.currentClip().value(QStringLiteral("clipId")).toString() != caption_id ||
      controller.currentClip().value(QStringLiteral("titleText")).toString() !=
          QStringLiteral("Edited cue text")) {
    report_item_geometry("caption text field after commit", window, caption_text);
    std::cerr << "field text=" << caption_text->property("text").toString().toStdString()
              << " controller text="
              << controller.currentClip().value(QStringLiteral("titleText")).toString().toStdString()
              << " controller status=" << controller.status().toStdString() << '\n';
    capture_failure_window(window);
    return fail("Keyboard caption edits should write through and read back from the timeline controller");
  }

  auto* caption_undo = find_item(window->contentItem(),
                                 QStringLiteral("timelineEditorialUndoButton"));
  if (caption_undo == nullptr || !caption_undo->property("enabled").toBool())
    return fail("The caption panel should expose the controller's active undo state");
  click_center(window, caption_undo);
  settle();
  if (controller.currentClip().value(QStringLiteral("titleText")).toString() !=
      QStringLiteral("Alpha beta gamma.")) {
    return fail("Caption panel undo should restore the original cue text through timeline history");
  }

  caption = clip_of_type(controller.clips(), QStringLiteral("caption"));
  const auto tracks = controller.sequenceTracks();
  int caption_track_index = -1;
  for (int index = 0; index < tracks.size(); ++index) {
    if (tracks.at(index).toMap().value(QStringLiteral("trackId")).toString() ==
        caption.value(QStringLiteral("trackId")).toString()) {
      caption_track_index = index;
      break;
    }
  }
  if (caption_track_index < 0)
    return fail("The generated caption cue should resolve to its real timeline track");
  controller.toggleTrackLocked(caption_track_index);
  settle();
  caption_text = find_item(window->contentItem(),
                           QStringLiteral("timelineEditorialCaptionText_") +
                               caption.value(QStringLiteral("clipId")).toString());
  if (caption_text == nullptr || caption_text->property("enabled").toBool())
    return fail("A locked caption track should disable inline cue edits");
  controller.toggleTrackLocked(caption_track_index);
  settle();

  auto* caption_tools = find_item(window->contentItem(),
                                  QStringLiteral("timelineEditorialOpenCaptionTools"));
  auto* caption_styles = find_item(window->contentItem(),
                                   QStringLiteral("timelineEditorialOpenCaptionStyles"));
  auto* caption_generator = find_item(window->contentItem(),
                                      QStringLiteral("timelineEditorialOpenCaptionGenerator"));
  if (!item_fits_window(window, caption_tools) ||
      !item_fits_window(window, caption_styles) ||
      !item_fits_window(window, caption_generator)) {
    report_item_geometry("caption tools", window, caption_tools);
    report_item_geometry("caption styles", window, caption_styles);
    report_item_geometry("caption generator", window, caption_generator);
    capture_failure_window(window);
    return fail("Caption generation and styling should route to their existing complete editors");
  }
  QSignalSpy caption_tools_clicked(caption_tools, SIGNAL(clicked()));
  const QPointF caption_tools_center =
      caption_tools->mapToScene(QPointF(caption_tools->width() / 2.0,
                                        caption_tools->height() / 2.0));
  const QPointF caption_undo_center =
      caption_undo->mapToScene(QPointF(caption_undo->width() / 2.0,
                                       caption_undo->height() / 2.0));
  QTest::mouseMove(window, caption_undo_center.toPoint());
  settle(350);
  auto* caption_undo_tooltip = find_item(window->contentItem(), QStringLiteral("AppToolTip"));
  if (caption_undo_tooltip == nullptr || !caption_undo_tooltip->isVisible())
    return fail("Hovering the undo control should show its tooltip before the click-through regression");
  QTest::mouseMove(window, caption_tools_center.toPoint());
  settle(20);
  if (!caption_undo_tooltip->isVisible() ||
      caption_undo_tooltip->property("opacity").toReal() <= 0.0 ||
      !tooltip_bounds_cover_scene_point(caption_undo_tooltip, caption_tools_center) ||
      tooltip_is_topmost_at(window, caption_tools_center)) {
    return fail("The fading tooltip should visibly overlap the caption-tools target while remaining outside its hit path");
  }
  click_center(window, caption_tools);
  settle(20);
  if (caption_tools_clicked.size() != 1 ||
      deep_edit.size() == 0 ||
      deep_edit.last().at(0).toString() != QStringLiteral("captions")) {
    report_pointer_hit_stack(window, caption_tools_center);
    std::cerr << "caption tools click probe: enabled="
              << caption_tools->property("enabled").toBool()
              << " containsCenter="
              << caption_tools->contains(
                     QPointF(caption_tools->width() / 2.0,
                             caption_tools->height() / 2.0))
              << " sceneCenter=" << caption_tools_center.x() << ','
              << caption_tools_center.y()
              << " clickedSignals=" << caption_tools_clicked.size()
              << " routeSignals=" << deep_edit.size() << '\n';
  }
  click_center(window, caption_styles);
  settle(20);
  click_center(window, caption_generator);
  settle();
  if (deep_edit.size() != 3 || deep_edit.at(0).at(0).toString() != QStringLiteral("captions") ||
      deep_edit.at(1).at(0).toString() != QStringLiteral("caption-styles") ||
      deep_edit.at(2).at(0).toString() != QStringLiteral("caption-generator")) {
    report_item_geometry("caption tools", window, caption_tools);
    report_item_geometry("caption styles", window, caption_styles);
    report_item_geometry("caption generator", window, caption_generator);
    std::cerr << "caption route signals=" << deep_edit.size();
    for (const auto& arguments : deep_edit)
      std::cerr << ' ' << arguments.at(0).toString().toStdString();
    std::cerr << '\n';
    capture_failure_window(window);
    return fail("The compact caption dock should preserve all three existing caption workflows");
  }

  fixture_object->setProperty("width", 560);
  fixture_object->setProperty("panelKind", QStringLiteral("transcript"));
  settle();
  const QVariantMap original_segment = controller.transcriptSegments().first().toMap();
  const auto original_word = transcript_word(original_segment, 1);
  const QString segment_id = original_segment.value(QStringLiteral("segmentId")).toString();
  if (original_word.isEmpty() || segment_id.isEmpty())
    return fail("The transcript controller model should provide timed editable words");

  auto* transcript_search = find_item(window->contentItem(),
                                      QStringLiteral("timelineEditorialTranscriptSearch"));
  if (transcript_search == nullptr)
    return fail("The transcript dock should expose searchable segment content");
  click_center(window, transcript_search);
  key_type(window, QStringLiteral("no matching transcript phrase"), 2);
  settle();
  if (panel->property("matchingTranscriptSegmentCount").toInt() != 0)
    return fail("Transcript search should remove nonmatching segments from the visible result set");
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(window, Qt::Key_Backspace);
  settle();
  if (panel->property("matchingTranscriptSegmentCount").toInt() != 1)
    return fail("Clearing transcript search should restore the matching timed segment");

  auto* transcript_word_button = find_item(
      window->contentItem(),
      QStringLiteral("timelineEditorialTranscriptWord_") + segment_id + QStringLiteral("_1"));
  if (transcript_word_button == nullptr)
    return fail("Every timed transcript word should be directly reachable from its segment");
  click_center(window, transcript_word_button);
  settle();
  if (std::abs(controller.sequencePlayheadSeconds() -
               original_word.value(QStringLiteral("startSeconds")).toDouble()) > 0.001) {
    return fail("Choosing a transcript word should move Program to the controller's timed position");
  }

  auto* transcript_editor = find_item(window->contentItem(),
                                      QStringLiteral("timelineEditorialTranscriptWordEditor"));
  if (transcript_editor == nullptr || !transcript_editor->property("enabled").toBool())
    return fail("Choosing a real transcript word should reveal an editable correction field");
  controller.toggleTrackLocked(caption_track_index);
  settle();
  if (transcript_editor->property("enabled").toBool() ||
      panel->property("canEditSelectedTranscriptWord").toBool()) {
    return fail("Transcript corrections should be disabled while their caption track is locked");
  }
  QVariant locked_edit_saved;
  if (!QMetaObject::invokeMethod(panel, "saveTranscriptWordCorrection",
                                 Q_RETURN_ARG(QVariant, locked_edit_saved)) ||
      locked_edit_saved.toBool() ||
      transcript_word(controller.transcriptSegments().first().toMap(), 1)
              .value(QStringLiteral("text")).toString() !=
          original_word.value(QStringLiteral("text")).toString()) {
    return fail("A locked transcript correction should be rejected without mutating controller state");
  }
  controller.toggleTrackLocked(caption_track_index);
  settle();
  if (!transcript_editor->property("enabled").toBool())
    return fail("Unlocking the caption track should restore transcript correction availability");
  QSignalSpy transcript_corrections(panel, SIGNAL(transcriptWordEdited(QString,int)));
  click_center(window, transcript_editor);
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  key_type(window, QStringLiteral("BETA"), 10);
  QTest::keyClick(window, Qt::Key_Return);
  settle();
  QVariantMap updated_segment;
  for (const auto& value : controller.transcriptSegments()) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("segmentId")).toString() == segment_id) {
      updated_segment = candidate;
      break;
    }
  }
  if (transcript_word(updated_segment, 1).value(QStringLiteral("text")).toString() !=
          QStringLiteral("BETA") || transcript_corrections.size() != 1) {
    std::cerr << "transcript correction signals=" << transcript_corrections.size()
              << " readback="
              << transcript_word(updated_segment, 1).value(QStringLiteral("text")).toString().toStdString()
              << '\n';
    return fail("Keyboard transcript corrections should persist through the real controller model");
  }

  auto* transcript_undo = find_item(window->contentItem(),
                                    QStringLiteral("timelineEditorialUndoButton"));
  if (!item_fits_window(window, transcript_undo) ||
      !transcript_undo->property("enabled").toBool()) {
    report_item_geometry("transcript undo", window, transcript_undo);
    return fail("Transcript corrections should enable the shared timeline undo action");
  }
  QSignalSpy undo_state_changes(&controller, SIGNAL(undoStateChanged()));
  click_center(window, transcript_undo);
  settle();
  QVariantMap restored_segment;
  for (const auto& value : controller.transcriptSegments()) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("segmentId")).toString() == segment_id) {
      restored_segment = candidate;
      break;
    }
  }
  if (transcript_word(restored_segment, 1).value(QStringLiteral("text")).toString() !=
      original_word.value(QStringLiteral("text")).toString()) {
    report_item_geometry("transcript undo after click", window, transcript_undo);
    std::cerr << "original word=" << original_word.value(QStringLiteral("text")).toString().toStdString()
              << " restored word="
              << transcript_word(restored_segment, 1).value(QStringLiteral("text")).toString().toStdString()
              << " restored segment="
              << restored_segment.value(QStringLiteral("text")).toString().toStdString()
              << " current clip="
              << controller.currentClip().value(QStringLiteral("titleText")).toString().toStdString()
              << " canUndo=" << controller.canUndo()
              << " undo state signals=" << undo_state_changes.size()
              << " status=" << controller.status().toStdString() << '\n';
    if (undo_state_changes.size() > 0 && controller.canUndo()) {
      controller.undo();
      const auto after_diagnostic_undo = controller.transcriptSegments();
      const auto after_diagnostic_segment = after_diagnostic_undo.isEmpty()
          ? QVariantMap{} : after_diagnostic_undo.first().toMap();
      std::cerr << "diagnostic second undo word="
                << transcript_word(after_diagnostic_segment, 1)
                       .value(QStringLiteral("text")).toString().toStdString()
                << " segment="
                << after_diagnostic_segment.value(QStringLiteral("text")).toString().toStdString()
                << '\n';
    }
    capture_failure_window(window);
    return fail("Transcript panel undo should restore the source word and its timed segment projection");
  }

  auto* transcript_full_editor = find_item(window->contentItem(),
                                           QStringLiteral("timelineEditorialOpenTranscriptEditor"));
  if (transcript_full_editor == nullptr)
    return fail("The transcript dock should expose its existing full editor");
  click_center(window, transcript_full_editor);
  settle();
  if (deep_edit.size() != 4 || deep_edit.last().at(0).toString() != QStringLiteral("transcript"))
    return fail("The transcript dock should keep its deep editor route connected");

  auto* source_seek = find_item(window->contentItem(),
                                QStringLiteral("timelineEditorialSeekSourceWord"));
  if (source_seek == nullptr || !source_seek->property("enabled").toBool())
    return fail("Source seeking should remain available as a safe controller-validated action");
  click_center(window, source_seek);
  settle();
  if (!controller.status().contains(QStringLiteral("No online Source clip")))
    return fail("Unsupported Source seeking should fail safely with a controller explanation");

  fixture.reset();
  settle(20);
  return EXIT_SUCCESS;
}

#include "timeline_editorial_panel_ui_test.moc"
