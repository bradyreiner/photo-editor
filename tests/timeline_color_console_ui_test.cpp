#include "timeline_document_controller.h"

#include <QAccessible>
#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QImage>
#include <QMetaObject>
#include <QRectF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPointer>
#include <QSet>
#include <QSizeF>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <algorithm>
#include <cmath>

using photo_editor::desktop::TimelineDocumentController;

class FakeTrackSnapshotDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList sequenceTracks READ sequenceTracks NOTIFY sequenceChanged)
  Q_PROPERTY(QVariantMap currentClip READ currentClip NOTIFY selectionChanged)
  Q_PROPERTY(int selectedClipCount READ selectedClipCount NOTIFY selectionChanged)
  Q_PROPERTY(double sequencePlayheadSeconds READ sequencePlayheadSeconds NOTIFY playbackStateChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

 public:
  explicit FakeTrackSnapshotDocument(bool first_track_locked, QObject* parent = nullptr)
      : QObject(parent), first_track_locked_(first_track_locked) {
    tracks_.reserve(518);
    for (int index = 0; index < 518; ++index) {
      const auto track_id = QStringLiteral("track-%1").arg(index, 3, 10, QLatin1Char('0'));
      tracks_.push_back(QVariantMap{
          {QStringLiteral("trackId"), track_id},
          {QStringLiteral("trackName"), QStringLiteral("Track %1").arg(index)},
          {QStringLiteral("trackKind"), QStringLiteral("video")},
          {QStringLiteral("locked"), index == 0 ? first_track_locked_ : false},
          {QStringLiteral("clips"), QVariantList{}},
      });
    }
    current_clip_ = {
        {QStringLiteral("clipId"), QStringLiteral("selected-video")},
        {QStringLiteral("trackId"), QStringLiteral("track-000")},
        {QStringLiteral("displayLabel"), QStringLiteral("Selected video")},
        {QStringLiteral("trackKind"), QStringLiteral("video")},
        {QStringLiteral("mediaType"), QStringLiteral("video")},
        {QStringLiteral("startSeconds"), 0.0},
        {QStringLiteral("trimmedDurationSeconds"), 2.0},
        {QStringLiteral("keyframes"), QVariantList{}},
        {QStringLiteral("colorGradeParameters"), QVariantMap{}},
        {QStringLiteral("colorGradeCurves"), QVariantMap{}},
    };
  }

  QVariantList sequenceTracks() const {
    ++sequence_tracks_read_count_;
    return tracks_;
  }
  Q_INVOKABLE QVariantList snapshotSequenceTracks() const {
    ++snapshot_read_count_;
    return tracks_;
  }
  QVariantMap currentClip() const { return current_clip_; }
  int selectedClipCount() const { return 1; }
  double sequencePlayheadSeconds() const { return 0.0; }
  QString status() const { return QStringLiteral("Ready"); }
  int sequenceTracksReadCount() const { return sequence_tracks_read_count_; }
  int snapshotReadCount() const { return snapshot_read_count_; }

  void notifyClipsAndSequenceChanged() {
    emit clipsChanged();
    emit sequenceChanged();
  }
  void notifySelectionChanged() { emit selectionChanged(); }
  void notifyClipsChanged() { emit clipsChanged(); }
  void notifySequenceChanged() { emit sequenceChanged(); }

  Q_INVOKABLE QVariantMap editClipKeyframeForAutomation(
      const QString&, const QString&, const QString&, double, double, const QString&) {
    return {{QStringLiteral("ok"), false}};
  }
  Q_INVOKABLE QVariantMap setClipColorGradeParameterForAutomation(
      const QString&, const QString&, double) {
    return {{QStringLiteral("ok"), false}};
  }
  Q_INVOKABLE QVariantMap setClipColorGradeCurveForAutomation(
      const QString&, const QString&, const QString&, const QVariantList&) {
    return {{QStringLiteral("ok"), false}};
  }
  Q_INVOKABLE bool setSelectedClipVideoInputTransform(const QString&) { return false; }
  Q_INVOKABLE bool applySelectedClipColorLut(const QString&) { return false; }
  Q_INVOKABLE bool clearSelectedClipColorLut() { return false; }
  Q_INVOKABLE QVariantMap clearClipColorGradeRecipeForAutomation(const QString&) {
    return {{QStringLiteral("ok"), false}};
  }
  Q_INVOKABLE void beginContinuousEdit() {}
  Q_INVOKABLE void endContinuousEdit() {}

 signals:
  void clipsChanged();
  void sequenceChanged();
  void selectionChanged();
  void playbackStateChanged();
  void statusChanged();

 private:
  QVariantList tracks_;
  QVariantMap current_clip_;
  bool first_track_locked_ = false;
  mutable int sequence_tracks_read_count_ = 0;
  mutable int snapshot_read_count_ = 0;
};

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void settle(int milliseconds = 120) {
  const int steps = std::max(1, milliseconds / 5);
  for (int index = 0; index < steps; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
}

QObject* find_object_recursive(QObject* root, const QString& object_name,
                               QSet<QObject*>& visited) {
  if (root == nullptr || visited.contains(root)) {
    return nullptr;
  }
  visited.insert(root);
  if (root->objectName() == object_name) {
    return root;
  }
  for (auto* child : root->children()) {
    if (auto* found = find_object_recursive(child, object_name, visited);
        found != nullptr) {
      return found;
    }
  }
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child : item->childItems()) {
      if (auto* found = find_object_recursive(child, object_name, visited);
          found != nullptr) {
        return found;
      }
    }
  }
  return nullptr;
}

QObject* find_object(QObject* root, const QString& object_name) {
  QSet<QObject*> visited;
  return find_object_recursive(root, object_name, visited);
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) {
    return nullptr;
  }
  if (root->objectName() == object_name) {
    return root;
  }
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name); found != nullptr) {
      return found;
    }
  }
  return nullptr;
}

void click(QQuickWindow* window, QQuickItem* item, const QPointF& position) {
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(position).toPoint());
}

void type_key_text(QQuickWindow* window, const QString& text) {
  for (const QChar character : text) {
    Qt::Key key = Qt::Key_unknown;
    if (character >= QLatin1Char('0') && character <= QLatin1Char('9')) {
      key = static_cast<Qt::Key>(Qt::Key_0 + character.digitValue());
    } else if (character == QLatin1Char('.')) {
      key = Qt::Key_Period;
    }
    if (key != Qt::Key_unknown) {
      QTest::keyClick(window, key);
    }
  }
}

double grade_value(const QVariantMap& clip, const QString& parameter) {
  return clip.value(QStringLiteral("colorGradeParameters"))
      .toMap()
      .value(parameter)
      .toDouble();
}

bool run_color_console_snapshot_lifecycle_test(QQmlPropertyMap& app_shell) {
  FakeTrackSnapshotDocument first_document(false);
  FakeTrackSnapshotDocument replacement_document(true);
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  engine.rootContext()->setContextProperty(QStringLiteral("snapshotDocumentOne"),
                                           &first_document);
  engine.rootContext()->setContextProperty(QStringLiteral("snapshotDocumentTwo"),
                                           &replacement_document);
  QQmlComponent component(&engine);
  const QByteArray qml =
      R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "trackSnapshotColorConsoleFixture"
    width: 420
    height: 700
    visible: true
    color: "#111315"
    property bool panelVisible: false
    property var activeDocument: snapshotDocumentOne

    function useFirstDocument() { activeDocument = snapshotDocumentOne }
    function useSecondDocument() { activeDocument = snapshotDocumentTwo }

    Theme { id: theme }

    TimelineColorConsole {
        id: colorConsole
        objectName: "snapshotTimelineColorConsole"
        anchors.fill: parent
        visible: fixture.panelVisible
        theme: theme
        timelineDocument: fixture.activeDocument
        currentClip: fixture.activeDocument.currentClip
    }
}
)qml";
  component.setData(qml, QUrl::fromLocalFile(
                            QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                           "/tests/timeline_color_console_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (fixture == nullptr) {
    std::cerr << component.errorString().toStdString() << '\n';
    return false;
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (window == nullptr) return false;
  window->show();
  settle();
  auto* panel = find_object(window, QStringLiteral("snapshotTimelineColorConsole"));
  if (panel == nullptr || first_document.snapshotReadCount() != 0 ||
      first_document.sequenceTracksReadCount() != 0) {
    std::cerr << "The hidden production Color Console should not project sequence tracks.\n";
    return false;
  }

  window->setProperty("panelVisible", true);
  settle();
  if (first_document.snapshotReadCount() != 1 ||
      first_document.sequenceTracksReadCount() != 0 ||
      panel->property("selectedTrackLocked").toBool()) {
    std::cerr << "Opening the production Color Console should snapshot once and read the unlocked row from the value.\n";
    return false;
  }
  for (int index = 0; index < 518; ++index) {
    first_document.notifySelectionChanged();
  }
  settle();
  if (first_document.sequenceTracksReadCount() != 0) {
    std::cerr << "Repeated production Color Console binding reads must not call the QObject sequenceTracks property.\n";
    return false;
  }

  first_document.notifyClipsAndSequenceChanged();
  settle();
  if (first_document.snapshotReadCount() != 2 ||
      first_document.sequenceTracksReadCount() != 0) {
    std::cerr << "A production clips/sequence notification cluster should take one track snapshot.\n";
    return false;
  }
  window->setProperty("panelVisible", false);
  first_document.notifyClipsAndSequenceChanged();
  settle();
  if (first_document.snapshotReadCount() != 2) {
    std::cerr << "A retained hidden production Color Console should skip track snapshots.\n";
    return false;
  }

  if (!QMetaObject::invokeMethod(fixture.get(), "useSecondDocument")) {
    std::cerr << "The production fixture should replace its active Timeline document.\n";
    return false;
  }
  settle();
  if (replacement_document.snapshotReadCount() != 0) {
    std::cerr << "Replacing the document while hidden should not project its tracks.\n";
    return false;
  }
  window->setProperty("panelVisible", true);
  settle();
  if (replacement_document.snapshotReadCount() != 1 ||
      replacement_document.sequenceTracksReadCount() != 0 ||
      !panel->property("selectedTrackLocked").toBool()) {
    std::cerr << "Reopening after document replacement should snapshot the replacement and read its locked row.\n";
    return false;
  }

  if (!QMetaObject::invokeMethod(fixture.get(), "useFirstDocument")) {
    std::cerr << "The visible production Color Console should accept a second document replacement.\n";
    return false;
  }
  settle();
  if (first_document.snapshotReadCount() != 3 ||
      replacement_document.snapshotReadCount() != 1 ||
      panel->property("selectedTrackLocked").toBool()) {
    std::cerr << "A visible document replacement should refresh once and switch back to the first document's unlocked row.\n";
    return false;
  }
  replacement_document.notifyClipsAndSequenceChanged();
  settle();
  if (replacement_document.snapshotReadCount() != 1) {
    std::cerr << "The old document should no longer refresh the production Color Console after replacement.\n";
    return false;
  }
  first_document.notifyClipsChanged();
  settle();
  first_document.notifySequenceChanged();
  settle();
  if (first_document.snapshotReadCount() != 5 ||
      first_document.sequenceTracksReadCount() != 0 ||
      replacement_document.sequenceTracksReadCount() != 0) {
    std::cerr << "Each production notifier should refresh one visible value snapshot and never read the sequenceTracks property.\n";
    return false;
  }
  window->close();
  settle(30);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  QApplication app(argc, argv);
  QAccessible::setActive(true);

  QTemporaryDir workspace;
  if (!workspace.isValid()) {
    return fail("A temporary timeline workspace should be available");
  }
  const QString source_path = workspace.filePath(QStringLiteral("color-console-source.png"));
  QImage source(96, 64, QImage::Format_RGB32);
  source.fill(qRgb(84, 126, 176));
  if (!source.save(source_path)) {
    return fail("A real image source should be written for the controller fixture");
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Color Console UI")},
      {QStringLiteral("width"), 96},
      {QStringLiteral("height"), 64},
      {QStringLiteral("frameRate"), 24.0},
  });
  controller.addClipFromGallery(QStringLiteral("photo:color-console-ui"), source_path,
                                QStringLiteral("Color console source"), 2.0, source_path);
  const QVariantMap adjustment =
      controller.addAdjustmentClipForAutomation(0.0, 1.5);
  if (!adjustment.value(QStringLiteral("ok")).toBool() ||
      adjustment.value(QStringLiteral("clipId")).toString().isEmpty()) {
    return fail("The real controller should create a selectable adjustment grade target");
  }

  QQmlPropertyMap app_shell;
  app_shell.insert(QStringLiteral("interfaceScale"), 1.0);
  app_shell.insert(QStringLiteral("interfaceDensity"), QStringLiteral("balanced"));
  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), &app_shell);
  engine.rootContext()->setContextProperty(QStringLiteral("testDocument"), &controller);
  QQmlComponent component(&engine);
  const QByteArray qml =
      R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"

ApplicationWindow {
    id: fixture
    objectName: "timelineColorConsoleFixture"
    width: 360
    height: 740
    visible: true
    color: "#111315"

    Theme { id: theme }

    TimelineColorConsole {
        id: colorConsole
        objectName: "testedTimelineColorConsole"
        anchors.fill: parent
        theme: theme
        timelineDocument: testDocument
        currentClip: testDocument.currentClip
    }
}
)qml";
  component.setData(qml, QUrl::fromLocalFile(
                            QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                           "/tests/timeline_color_console_ui_test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("TimelineColorConsole should load its production QML dependencies");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  if (window == nullptr) {
    return fail("The color console fixture should be a visible QQuickWindow");
  }
  window->show();
  settle();

  auto* panel = find_object(window, QStringLiteral("testedTimelineColorConsole"));
  auto* exposure_slider =
      find_object(window, QStringLiteral("timelineColorConsole_exposure"));
  auto* exposure_field =
      find_item(window->contentItem(),
                QStringLiteral("timelineColorConsole_exposureValueField"));
  auto* more_tools =
      find_item(window->contentItem(), QStringLiteral("timelineColorMoreToolsButton"));
  auto* close_button =
      find_item(window->contentItem(), QStringLiteral("timelineColorConsoleCloseButton"));
  if (panel == nullptr || exposure_slider == nullptr || exposure_field == nullptr ||
      more_tools == nullptr || close_button == nullptr) {
    return fail("The production color console should expose the grade, typed input, and dock actions");
  }
  if (!exposure_slider->property("enabled").toBool() ||
      !exposure_field->isVisible() || exposure_field->width() < 50.0) {
    return fail("A selected unlocked adjustment clip should have a usable exposure control");
  }

  click(window, exposure_field,
        QPointF(exposure_field->width() / 2.0, exposure_field->height() / 2.0));
  settle(30);
  if (!exposure_field->hasActiveFocus()) {
    return fail("The numeric exposure field should accept keyboard focus");
  }
  QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
  type_key_text(window, QStringLiteral("1.25"));
  QTest::keyClick(window, Qt::Key_Return);
  settle();
  const double edited_exposure = grade_value(controller.currentClip(),
                                             QStringLiteral("exposure"));
  if (std::abs(edited_exposure - 1.25) > 0.011 ||
      std::abs(exposure_slider->property("value").toDouble() - 1.25) > 0.011) {
    return fail("Typed exposure should write the selected clip and read back the real controller value");
  }

  controller.undo();
  settle();
  if (std::abs(grade_value(controller.currentClip(), QStringLiteral("exposure"))) >
          0.001 ||
      std::abs(exposure_slider->property("value").toDouble()) > 0.001) {
    return fail("One controller undo should restore the real grade and refresh the UI value");
  }
  controller.redo();
  settle();
  if (std::abs(grade_value(controller.currentClip(), QStringLiteral("exposure")) -
               1.25) > 0.011) {
    return fail("Redo should restore the typed exposure grade");
  }

  auto* curves_tab =
      find_item(window->contentItem(),
                QStringLiteral("timelineColorConsolePage_curves"));
  if (curves_tab == nullptr) {
    return fail("The compact console should keep tone curves one click away");
  }
  click(window, curves_tab,
        QPointF(curves_tab->width() / 2.0, curves_tab->height() / 2.0));
  settle();
  auto* curve_editor = find_item(window->contentItem(),
                                 QStringLiteral("timelineColorConsoleToneCurve"));
  auto* curve_graph = curve_editor == nullptr
                          ? nullptr
                          : find_item(curve_editor, QStringLiteral("toneCurveGraph"));
  if (curve_graph == nullptr || curve_graph->width() < 120.0 ||
      curve_graph->height() < 120.0) {
    return fail("Tone curves should remain directly editable at the narrow dock width");
  }
  QPointer<QQuickItem> curve_editor_identity(curve_editor);
  const QPoint curve_press =
      curve_graph->mapToScene(QPointF(curve_graph->width() * 0.62,
                                     curve_graph->height() * 0.27))
          .toPoint();
  const QPoint curve_finish =
      curve_graph->mapToScene(QPointF(curve_graph->width() * 0.62,
                                     curve_graph->height() * 0.18))
          .toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, curve_press);
  QTest::mouseMove(window, curve_finish, 80);
  settle();
  if (curve_editor_identity.isNull() ||
      find_item(window->contentItem(),
                QStringLiteral("timelineColorConsoleToneCurve")) !=
          curve_editor_identity.data()) {
    return fail("A controller curve readback should keep the actively edited curve editor alive");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, curve_finish);
  settle();
  auto rgb_curve = controller.currentClip()
                       .value(QStringLiteral("colorGradeCurves"))
                       .toMap()
                       .value(QStringLiteral("RGB"))
                       .toList();
  if (rgb_curve.size() != 3 ||
      std::abs(rgb_curve.at(1).toMap().value(QStringLiteral("x")).toDouble() -
               rgb_curve.at(1).toMap().value(QStringLiteral("y")).toDouble()) <
          0.03) {
    return fail("Dragging a new point on the production curve graph should persist a non-identity curve point");
  }
  controller.undo();
  settle();
  rgb_curve = controller.currentClip()
                  .value(QStringLiteral("colorGradeCurves"))
                  .toMap()
                  .value(QStringLiteral("RGB"))
                  .toList();
  if (!rgb_curve.isEmpty()) {
    return fail("One undo should remove the actual curve point written from the console");
  }

  const QVariantList reset_source_points{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.04}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.68}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 0.96}},
  };
  const auto selected_clip_id =
      controller.currentClip().value(QStringLiteral("clipId")).toString();
  if (!controller.setClipColorGradeCurveForAutomation(
           selected_clip_id, QString{}, QStringLiteral("RGB"), reset_source_points)
           .value(QStringLiteral("ok")).toBool()) {
    return fail("The production console should have a non-identity curve to reset");
  }
  auto* reset_curve_button = find_item(
      window->contentItem(), QStringLiteral("timelineColorConsoleResetCurve"));
  if (!reset_curve_button || !reset_curve_button->isEnabled()) {
    return fail("The production curve reset action should remain reachable while a grade is editable");
  }
  click(window, reset_curve_button,
        QPointF(reset_curve_button->width() / 2.0, reset_curve_button->height() / 2.0));
  settle();
  rgb_curve = controller.currentClip()
                  .value(QStringLiteral("colorGradeCurves"))
                  .toMap()
                  .value(QStringLiteral("RGB"))
                  .toList();
  if (!rgb_curve.isEmpty()) {
    return fail("Reset should restore the identity curve");
  }
  controller.undo();
  settle();
  rgb_curve = controller.currentClip()
                  .value(QStringLiteral("colorGradeCurves"))
                  .toMap()
                  .value(QStringLiteral("RGB"))
                  .toList();
  if (rgb_curve.size() != 3 ||
      std::abs(rgb_curve.at(1).toMap().value(QStringLiteral("y")).toDouble() - 0.68) > 1e-5) {
    return fail("One undo should restore the curve cleared by the console Reset action");
  }

  const int track_row = controller.currentClip()
                            .value(QStringLiteral("trackRowIndex"))
                            .toInt();
  controller.toggleTrackLocked(track_row);
  settle();
  if (exposure_slider->property("enabled").toBool()) {
    return fail("A locked picture track should disable grade writes in the console");
  }
  QVariant rejected_write;
  const bool invoked = QMetaObject::invokeMethod(
      panel, "setGradeValue", Qt::DirectConnection,
      Q_RETURN_ARG(QVariant, rejected_write),
      Q_ARG(QVariant, QStringLiteral("contrast")),
      Q_ARG(QVariant, 35.0));
  if (!invoked || rejected_write.toBool() ||
      std::abs(grade_value(controller.currentClip(), QStringLiteral("contrast"))) >
          0.001) {
    return fail("A locked track should reject a write without changing the selected grade");
  }

  controller.toggleTrackLocked(track_row);
  controller.selectClip(-1);
  settle();
  if (exposure_slider->property("enabled").toBool() ||
      more_tools->property("enabled").toBool() ||
      controller.selectedClipCount() != 0) {
    return fail("The console should show and enforce its no-selection state");
  }

  window->setWidth(320);
  settle();
  const QPointF more_origin = more_tools->mapToItem(
      qobject_cast<QQuickItem*>(panel), QPointF(0, 0));
  const QPointF close_origin = close_button->mapToItem(
      qobject_cast<QQuickItem*>(panel), QPointF(0, 0));
  const QRectF more_rect(more_origin, QSizeF(more_tools->width(), more_tools->height()));
  const QRectF close_rect(close_origin,
                          QSizeF(close_button->width(), close_button->height()));
  if (std::abs(qobject_cast<QQuickItem*>(panel)->width() - 320.0) > 1.0 ||
      more_rect.intersects(close_rect) ||
      more_rect.left() < 0 || close_rect.right() > 320.0) {
    return fail("The retained console header should stay inside a 320-pixel dock");
  }

  window->hide();
  if (!run_color_console_snapshot_lifecycle_test(app_shell)) {
    return fail("The production Color Console should use lazy owned sequence-track snapshots across document lifecycle changes");
  }

  std::cout << "Color Console pointer/keyboard edits, controller readback, undo, locked state, and narrow layout passed\n";
  return EXIT_SUCCESS;
}

#include "timeline_color_console_ui_test.moc"
