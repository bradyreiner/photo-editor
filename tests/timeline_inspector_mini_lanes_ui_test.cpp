#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QThread>
#include <QtTest/QTest>
#include <QVariant>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void settle() {
  for (int index = 0; index < 25; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(4);
  }
}

QObject* find_named(QObject* root, const QString& name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->children()) {
    if (auto* found = find_named(child, name)) return found;
  }
  if (auto* item = qobject_cast<QQuickItem*>(root)) {
    for (auto* child : item->childItems()) {
      if (auto* found = find_named(child, name)) return found;
    }
  }
  return nullptr;
}

QPoint point_in_window(QQuickWindow* window, QQuickItem* item, qreal x, qreal y) {
  return item == nullptr || window == nullptr
      ? QPoint{}
      : item->mapToScene(QPointF(x, y)).toPoint();
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  const auto qml = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import "file:)qml") + QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)
      + QStringLiteral(R"qml(/apps/desktop/qml/components" as C

Window {
    objectName: "inspectorMiniLanesWindow"
    width: 260
    height: 220
    visible: true
    color: "#16191d"

    C.Theme { id: sharedTheme }
    C.TimelineInspectorMiniLanes {
        id: lanes
        objectName: "inspectorMiniLanes"
        anchors.fill: parent
        theme: sharedTheme
        channels: [
            {parameter: "frameScale", label: "Scale", keys: [
                {seconds: 0.0, value: 1.0},
                {seconds: 1.0, value: 1.5},
                {seconds: 2.0, value: 2.0}]},
            {parameter: "effect:Blur", label: "Blur Amount", keys: []}
        ]
        duration: 2.0
        playhead: 0.5
        framesPerSecond: 30
        rowHeight: 34
        rulerHeight: 28
    }
}
)qml");
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/timeline-inspector-mini-lanes-ui-test.qml")));
  std::unique_ptr<QObject> fixture(component.create());
  if (!fixture) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("The inspector keyframe lane fixture must load.");
  }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* lanes = find_named(fixture.get(), QStringLiteral("inspectorMiniLanes"));
  auto* scale_lane = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniLane-frameScale")));
  auto* blur_lane = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniLane-effect:Blur")));
  auto* ruler_pointer = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniRulerPointer")));
  auto* scale_pointer = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniLanePointer-frameScale")));
  if (window == nullptr || lanes == nullptr || scale_lane == nullptr || blur_lane == nullptr ||
      ruler_pointer == nullptr || scale_pointer == nullptr) {
    return fail("The inspector must expose its ruler and aligned property lanes.");
  }
  window->requestActivate();
  settle();
  if (!scale_lane->isVisible() || !blur_lane->isVisible() ||
      scale_lane->width() != blur_lane->width() ||
      qAbs(scale_lane->y() - (blur_lane->y() - 34.0)) > 1.0 ||
      scale_lane->height() != 34.0 ||
      lanes->property("implicitContentHeight").toDouble() != 96.0) {
    return fail("Property lanes should align to one ruler and share a compact row height.");
  }

  QVariant x_at_midpoint;
  if (!QMetaObject::invokeMethod(lanes, "xAtTime", Q_RETURN_ARG(QVariant, x_at_midpoint),
                                 Q_ARG(QVariant, QVariant(1.0)),
                                 Q_ARG(QVariant, QVariant(scale_pointer->width())))) {
    return fail("The mini ruler must expose its seconds-to-pixel mapping.");
  }
  QVariant time_at_midpoint;
  if (!QMetaObject::invokeMethod(lanes, "timeAtX", Q_RETURN_ARG(QVariant, time_at_midpoint),
                                 Q_ARG(QVariant, x_at_midpoint),
                                 Q_ARG(QVariant, QVariant(scale_pointer->width()))) ||
      qAbs(time_at_midpoint.toDouble() - 1.0) > 0.02) {
    return fail("Ruler positions should map back to frame-snapped local clip time.");
  }

  QSignalSpy seek_spy(lanes, SIGNAL(seekRequested(double)));
  QSignalSpy started_spy(lanes, SIGNAL(seekGestureStarted()));
  QSignalSpy finished_spy(lanes, SIGNAL(seekGestureFinished()));
  QSignalSpy channel_spy(lanes, SIGNAL(channelActivated(QString)));
  const auto keyframe_point = point_in_window(
      window, scale_pointer, x_at_midpoint.toDouble(), scale_pointer->height() / 2.0);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, keyframe_point);
  settle();
  if (seek_spy.count() != 1 || started_spy.count() != 1 || finished_spy.count() != 1 ||
      channel_spy.count() != 1 || channel_spy.constFirst().constFirst().toString() != "frameScale" ||
      qAbs(seek_spy.constFirst().constFirst().toDouble() - 1.0) > 0.02) {
    return fail("Clicking a visible keyframe should select its property and seek to its exact local time.");
  }

  const auto ruler_point = point_in_window(window, ruler_pointer, ruler_pointer->width() * 0.75,
                                          ruler_pointer->height() / 2.0);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, ruler_point);
  settle();
  if (seek_spy.count() != 2 || started_spy.count() != 2 || finished_spy.count() != 2 ||
      qAbs(seek_spy.constLast().constFirst().toDouble() - 1.5) > 0.04) {
    return fail("Scrubbing the inspector ruler should emit a bounded, frame-snapped local seek gesture.");
  }

  QSignalSpy keyframe_started_spy(lanes, SIGNAL(keyframeMoveStarted()));
  QSignalSpy keyframe_move_spy(
      lanes, SIGNAL(keyframeMoveRequested(QString,double,double)));
  QSignalSpy keyframe_finished_spy(lanes, SIGNAL(keyframeMoveFinished()));
  const QPoint keyframe_start = point_in_window(
      window, scale_pointer, x_at_midpoint.toDouble(), scale_pointer->height() / 2.0);
  QVariant x_at_next_key;
  if (!QMetaObject::invokeMethod(lanes, "xAtTime", Q_RETURN_ARG(QVariant, x_at_next_key),
                                 Q_ARG(QVariant, QVariant(1.5)),
                                 Q_ARG(QVariant, QVariant(scale_pointer->width())))) {
    return fail("The inspector lane must map a dragged keyframe to clip-local time.");
  }
  const QPoint keyframe_end = point_in_window(
      window, scale_pointer, x_at_next_key.toDouble(), scale_pointer->height() / 2.0);
  const QPointF keyframe_start_local(keyframe_start);
  const QPointF keyframe_end_local(keyframe_end);
  const QPointF keyframe_start_global(window->mapToGlobal(keyframe_start));
  const QPointF keyframe_end_global(window->mapToGlobal(keyframe_end));
  QMouseEvent press(QEvent::MouseButtonPress, keyframe_start_local,
                    keyframe_start_global, Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(window, &press);
  QMouseEvent move(QEvent::MouseMove, keyframe_end_local,
                   keyframe_end_global, Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
  QCoreApplication::sendEvent(window, &move);
  QMouseEvent release(QEvent::MouseButtonRelease, keyframe_end_local,
                      keyframe_end_global, Qt::LeftButton, Qt::NoButton,
                      Qt::NoModifier);
  QCoreApplication::sendEvent(window, &release);
  settle();
  if (keyframe_started_spy.count() != 1 || keyframe_move_spy.count() != 1 ||
      keyframe_finished_spy.count() != 1 ||
      qAbs(keyframe_move_spy.constFirst().at(1).toDouble() - 1.0) > 0.02 ||
      qAbs(keyframe_move_spy.constFirst().at(2).toDouble() - 1.5) > 0.04) {
    return fail("Dragging a keyframe diamond should emit one frame-snapped local move inside one edit gesture.");
  }

  QVariant quarter_x;
  if (!QMetaObject::invokeMethod(lanes, "xAtTime", Q_RETURN_ARG(QVariant, quarter_x),
                                 Q_ARG(QVariant, QVariant(0.25)),
                                 Q_ARG(QVariant, QVariant(scale_pointer->width())))) {
    return fail("The inspector lane should remain addressable during a live keyframe snapshot update.");
  }
  const QPoint live_gesture_point = point_in_window(
      window, scale_pointer, quarter_x.toDouble(), scale_pointer->height() / 2.0);
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, live_gesture_point);
  settle();
  if (!scale_pointer->property("pressed").toBool()) {
    return fail("The lane pointer should remain pressed before replacing its keyframe snapshot.");
  }

  QVariantMap updated_key;
  updated_key.insert(QStringLiteral("seconds"), 1.1);
  updated_key.insert(QStringLiteral("value"), 1.6);
  QVariantMap first_updated_key;
  first_updated_key.insert(QStringLiteral("seconds"), 0.0);
  first_updated_key.insert(QStringLiteral("value"), 1.0);
  QVariantList updated_keys;
  updated_keys.append(first_updated_key);
  updated_keys.append(updated_key);
  QVariantMap updated_scale_channel;
  updated_scale_channel.insert(QStringLiteral("parameter"), QStringLiteral("frameScale"));
  updated_scale_channel.insert(QStringLiteral("label"), QStringLiteral("Scale"));
  updated_scale_channel.insert(QStringLiteral("keys"), updated_keys);
  QVariantMap updated_blur_channel;
  updated_blur_channel.insert(QStringLiteral("parameter"), QStringLiteral("effect:Blur"));
  updated_blur_channel.insert(QStringLiteral("label"), QStringLiteral("Blur Amount"));
  updated_blur_channel.insert(QStringLiteral("keys"), QVariantList{});
  QVariantList updated_channels;
  updated_channels.append(updated_scale_channel);
  updated_channels.append(updated_blur_channel);
  lanes->setProperty("channels", updated_channels);
  settle();

  auto* retained_scale_lane = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniLane-frameScale")));
  auto* retained_scale_pointer = qobject_cast<QQuickItem*>(
      find_named(fixture.get(), QStringLiteral("timelineInspectorMiniLanePointer-frameScale")));
  if (retained_scale_lane != scale_lane || retained_scale_pointer != scale_pointer ||
      !retained_scale_pointer->property("pressed").toBool()) {
    return fail("Replacing keyframe data must keep the active lane delegate and pointer gesture intact.");
  }
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, live_gesture_point);
  settle();
  if (seek_spy.count() != 3 || started_spy.count() != 3 || finished_spy.count() != 3) {
    return fail("A lane gesture should finish normally after keyframe data refreshes in place.");
  }
  return EXIT_SUCCESS;
}
