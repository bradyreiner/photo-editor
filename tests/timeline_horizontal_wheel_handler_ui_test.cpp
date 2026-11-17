#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QGuiApplication>
#include <QMetaMethod>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QUrl>
#include <QVariant>
#include <QWheelEvent>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

void settle_events() {
  for (int index = 0; index < 4; ++index) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

bool near(qreal first, qreal second, qreal epsilon = 0.01) {
  return std::abs(first - second) <= epsilon;
}

QQuickItem* find_item(QQuickItem* root, const QString& object_name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == object_name) return root;
  for (auto* child : root->childItems()) {
    if (auto* found = find_item(child, object_name)) return found;
  }
  return nullptr;
}

void send_wheel(QQuickWindow* window,
                const QPoint& pixel_delta,
                const QPoint& angle_delta,
                Qt::KeyboardModifiers modifiers,
                const QPointF& position,
                Qt::ScrollPhase phase = Qt::NoScrollPhase) {
  const auto global_position = window->mapToGlobal(position.toPoint());
  QWheelEvent event(position, QPointF(global_position), pixel_delta, angle_delta,
                    Qt::NoButton, modifiers, phase, false);
  QCoreApplication::sendEvent(window, &event);
  settle_events();
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  QQmlEngine engine;

  const QByteArray harness_qml = R"QML(
import QtQuick
import QtQuick.Window

Window {
    id: root
    objectName: "timelineWheelHarness"
    width: 1000
    height: 240
    visible: false

    Item {
        id: headerColumn
        objectName: "timelineWheelHeaderColumn"
        x: 30
        y: 20
        width: 150
        height: parent.height - 20
    }

    Flickable {
        id: viewport
        objectName: "timelineWheelViewport"
        x: 180
        y: 20
        width: 390
        height: parent.height - 20
        contentWidth: 1000
        contentHeight: 700
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalAndVerticalFlick

        Rectangle {
            width: viewport.contentWidth
            height: viewport.contentHeight
            color: "#202830"
        }
    }

    Flickable {
        id: nativeVerticalControl
        objectName: "timelineWheelNativeVerticalControl"
        x: 600
        width: 390
        height: parent.height
        contentWidth: width
        contentHeight: 700
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Rectangle {
            width: nativeVerticalControl.contentWidth
            height: nativeVerticalControl.contentHeight
            color: "#303840"
        }
    }
}
)QML";
  QQmlComponent harness_component(&engine);
  harness_component.setData(
      harness_qml,
      QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR
                                         "/tests/TimelineHorizontalWheelHandlerHarness.qml")));
  std::unique_ptr<QObject> harness_object(harness_component.create());
  if (!harness_object) {
    qCritical().noquote() << harness_component.errorString();
    return fail("The horizontal wheel test fixture should load.");
  }
  auto* window = qobject_cast<QQuickWindow*>(harness_object.get());
  if (window == nullptr) return fail("The wheel fixture should create a QQuickWindow.");
  auto* viewport = find_item(window->contentItem(), QStringLiteral("timelineWheelViewport"));
  if (viewport == nullptr) return fail("The test Flickable should exist.");
  auto* header_column = find_item(window->contentItem(), QStringLiteral("timelineWheelHeaderColumn"));
  auto* header_column_item = qobject_cast<QQuickItem*>(header_column);
  if (header_column_item == nullptr) {
    return fail("The sibling test track-header column should be a QQuickItem.");
  }
  auto* native_vertical_control = find_item(
      window->contentItem(), QStringLiteral("timelineWheelNativeVerticalControl"));
  if (native_vertical_control == nullptr) {
    return fail("The plain Flickable wheel-control should exist.");
  }

  const auto horizontal_handler_url = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineViewportWheelHandler.qml"));
  auto* viewport_item = qobject_cast<QQuickItem*>(viewport);
  if (viewport_item == nullptr) return fail("The test Flickable should be a QQuickItem.");
  if (!near(viewport_item->x(), 180.0) || !near(viewport_item->y(), 20.0) ||
      !near(header_column_item->x(), 30.0) || !near(header_column_item->width(), 150.0) ||
      header_column_item->parentItem() != viewport_item->parentItem()) {
    return fail("The viewport and track-header column should be adjacent sibling items.");
  }

  QQmlComponent horizontal_handler_component(&engine, horizontal_handler_url);
  std::unique_ptr<QObject> horizontal_handler(horizontal_handler_component.create());
  if (!horizontal_handler) {
    qCritical().noquote() << horizontal_handler_component.errorString();
    return fail("The production horizontal TimelineViewportWheelHandler should load.");
  }
  if (!horizontal_handler->setProperty("viewport",
                                       QVariant::fromValue<QObject*>(viewport)) ||
      !horizontal_handler->setProperty("parent",
                                       QVariant::fromValue(viewport_item))) {
    return fail("The production horizontal wheel handler should attach to the test Flickable.");
  }

  const auto vertical_handler_url = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineVerticalWheelHandler.qml"));
  QQmlComponent vertical_handler_component(&engine, vertical_handler_url);
  std::unique_ptr<QObject> vertical_handler(vertical_handler_component.create());
  if (!vertical_handler) {
    qCritical().noquote() << vertical_handler_component.errorString();
    return fail("The production vertical TimelineVerticalWheelHandler should load.");
  }
  if (!vertical_handler->setProperty("viewport",
                                     QVariant::fromValue<QObject*>(viewport)) ||
      !vertical_handler->setProperty("parent",
                                     QVariant::fromValue(viewport_item)) ||
      !vertical_handler->setProperty("timelineZoom", 2.0)) {
    return fail("The production vertical wheel handler should attach to the test Flickable.");
  }

  std::unique_ptr<QObject> header_horizontal_handler(horizontal_handler_component.create());
  if (!header_horizontal_handler ||
      !header_horizontal_handler->setProperty("viewport",
                                              QVariant::fromValue<QObject*>(viewport)) ||
      !header_horizontal_handler->setProperty(
          "parent", QVariant::fromValue<QObject*>(header_column_item))) {
    return fail("The production horizontal handler should attach to a sibling header and control the same viewport.");
  }

  std::unique_ptr<QObject> header_vertical_handler(vertical_handler_component.create());
  if (!header_vertical_handler ||
      !header_vertical_handler->setProperty("viewport",
                                            QVariant::fromValue<QObject*>(viewport)) ||
      !header_vertical_handler->setProperty(
          "parent", QVariant::fromValue<QObject*>(header_column_item)) ||
      !header_vertical_handler->setProperty("timelineZoom", 2.0)) {
    return fail("The production vertical handler should attach to a sibling header and control the same viewport.");
  }

  int zoom_signal_index = -1;
  for (int index = 0; index < vertical_handler->metaObject()->methodCount(); ++index) {
    const auto method = vertical_handler->metaObject()->method(index);
    if (method.methodType() == QMetaMethod::Signal &&
        method.name() == QByteArrayLiteral("zoomRequested")) {
      zoom_signal_index = index;
      break;
    }
  }
  if (zoom_signal_index < 0) return fail("The production handler should expose zoomRequested.");
  QSignalSpy zoom_spy(vertical_handler.get(),
                      vertical_handler->metaObject()->method(zoom_signal_index));
  if (!zoom_spy.isValid()) return fail("The production zoom signal should be observable.");
  QSignalSpy header_zoom_spy(
      header_vertical_handler.get(),
      header_vertical_handler->metaObject()->method(zoom_signal_index));
  if (!header_zoom_spy.isValid()) {
    return fail("The sibling-header production zoom signal should be observable.");
  }

  window->show();
  settle_events();
  viewport->setProperty("contentX", 240.0);
  viewport->setProperty("contentY", 160.0);
  native_vertical_control->setProperty("contentY", 160.0);
  settle_events();
  const QPointF pointer_in_viewport(90.0, 80.0);
  const QPointF pointer = viewport_item->mapToScene(pointer_in_viewport);

  auto* native_vertical_control_item = qobject_cast<QQuickItem*>(native_vertical_control);
  if (native_vertical_control_item == nullptr) {
    return fail("The plain Flickable control should be a QQuickItem.");
  }
  send_wheel(window, QPoint(0, 25), QPoint(), Qt::NoModifier,
             native_vertical_control_item->mapToScene(QPointF(90.0, 80.0)));
  const auto native_vertical_delta =
      native_vertical_control->property("contentY").toReal() - 160.0;
  qInfo().noquote() << "plain_flickable_vertical_pixel_delta="
                    << native_vertical_delta;
  viewport->setProperty("contentY", 160.0);
  native_vertical_control->setProperty("contentY", 160.0);
  settle_events();

  send_wheel(window, QPoint(36, 0), QPoint(), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), 204.0) ||
      !near(viewport->property("contentY").toReal(), 160.0) || zoom_spy.count() != 0) {
    return fail("Horizontal trackpad pixels should scroll only the timeline horizontally.");
  }
  const auto horizontal_diagnostic =
      horizontal_handler->property("lastWheelEvent").toMap();
  const auto horizontal_state =
      horizontal_handler->property("diagnosticState").toMap();
  const auto horizontal_viewport_diagnostic =
      horizontal_diagnostic.value("viewport").toMap();
  if (horizontal_state.value("objectName").toString() !=
          QStringLiteral("timelineViewportHorizontalWheelHandler") ||
      horizontal_state.value("orientation").toString() !=
          QStringLiteral("horizontal") ||
      !horizontal_state.value("enabled").toBool() ||
      horizontal_state.value("acceptedDevices").toInt() == 0 ||
      !horizontal_state.value("lastEvent").toMap().value("received").toBool() ||
      !horizontal_diagnostic.value("received").toBool() ||
      horizontal_diagnostic.value("sequence").toInt() != 1 ||
      horizontal_diagnostic.value("accepted").toBool() != true ||
      horizontal_diagnostic.value("deviceTypeName").toString().isEmpty() ||
      !near(horizontal_diagnostic.value("pixelDeltaX").toReal(), 36.0) ||
      !near(horizontal_diagnostic.value("angleDeltaX").toReal(), 0.0) ||
      !near(horizontal_diagnostic.value("logicalDeltaX").toReal(), 36.0) ||
      !near(horizontal_diagnostic.value("sceneX").toReal(), pointer.x()) ||
      !near(horizontal_diagnostic.value("sceneY").toReal(), pointer.y()) ||
      !near(horizontal_diagnostic.value("mappedViewportX").toReal(),
            pointer_in_viewport.x()) ||
      !near(horizontal_diagnostic.value("mappedViewportY").toReal(),
            pointer_in_viewport.y()) ||
      !near(horizontal_viewport_diagnostic.value("sceneX").toReal(), 180.0) ||
      !near(horizontal_viewport_diagnostic.value("sceneY").toReal(), 20.0) ||
      !near(horizontal_viewport_diagnostic.value("width").toReal(), 390.0) ||
      !near(horizontal_viewport_diagnostic.value("contentXBefore").toReal(), 240.0) ||
      !near(horizontal_viewport_diagnostic.value("contentXAfter").toReal(), 204.0) ||
      !near(horizontal_viewport_diagnostic.value("contentYBefore").toReal(), 160.0) ||
      !near(horizontal_viewport_diagnostic.value("contentYAfter").toReal(), 160.0)) {
    return fail("The production horizontal handler should expose a bounded last-event diagnostic with real coordinates and offsets.");
  }

  send_wheel(window, QPoint(), QPoint(120, 0), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), 156.0)) {
    return fail("Horizontal wheel notches should use the bounded logical-pixel fallback.");
  }

  send_wheel(window, QPoint(0, 18), QPoint(), Qt::ShiftModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), 138.0) ||
      !near(viewport->property("contentY").toReal(), 160.0)) {
    return fail("Shift plus vertical wheel should scroll the timeline horizontally only.");
  }

  send_wheel(window, QPoint(12, 9), QPoint(), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), 126.0) ||
      !near(viewport->property("contentY").toReal(), 151.0)) {
    return fail("A diagonal trackpad event should preserve movement on both axes.");
  }

  const auto diagonal_x = viewport->property("contentX").toReal();
  const auto diagonal_y = viewport->property("contentY").toReal();
  send_wheel(window, QPoint(0, 25), QPoint(), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), diagonal_x) ||
      !(viewport->property("contentY").toReal() < diagonal_y)) {
    return fail("An ordinary vertical wheel gesture should remain a vertical scroll.");
  }

  const auto vertical_after_pixels = viewport->property("contentY").toReal();
  send_wheel(window, QPoint(), QPoint(0, 120), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentY").toReal(), vertical_after_pixels - 48.0)) {
    return fail("A vertical wheel notch should use the same bounded pixel fallback.");
  }
  const auto vertical_diagnostic = vertical_handler->property("lastWheelEvent").toMap();
  const auto vertical_viewport_diagnostic =
      vertical_diagnostic.value("viewport").toMap();
  if (!vertical_diagnostic.value("received").toBool() ||
      vertical_diagnostic.value("sequence").toInt() < 1 ||
      vertical_diagnostic.value("accepted").toBool() != true ||
      vertical_diagnostic.value("deviceTypeName").toString().isEmpty() ||
      !near(vertical_diagnostic.value("angleDeltaY").toReal(), 120.0) ||
      !near(vertical_diagnostic.value("logicalDeltaY").toReal(), 48.0) ||
      !near(vertical_viewport_diagnostic.value("contentYBefore").toReal(),
            vertical_after_pixels) ||
      !near(vertical_viewport_diagnostic.value("contentYAfter").toReal(),
            vertical_after_pixels - 48.0)) {
    return fail("The production vertical handler should expose the accepted notch event and before/after offsets.");
  }
  const auto vertical_before_momentum = viewport->property("contentY").toReal();
  send_wheel(window, QPoint(0, 7), QPoint(), Qt::NoModifier, pointer,
             Qt::ScrollMomentum);
  if (!near(viewport->property("contentY").toReal(), vertical_before_momentum - 7.0)) {
    return fail("Nonzero OS momentum packets should continue to pan the viewport.");
  }

  const auto before_zoom_x = viewport->property("contentX").toReal();
  const auto before_zoom_y = viewport->property("contentY").toReal();
  send_wheel(window, QPoint(), QPoint(0, 120), Qt::ControlModifier, pointer);
  const auto zoom_event_count = zoom_spy.count();
  const auto zoom_arguments = zoom_event_count > 0 ? zoom_spy.takeFirst() : QList<QVariant>{};
  const auto actual_zoom = zoom_arguments.size() > 0 ? zoom_arguments.at(0).toReal() : -1.0;
  const auto actual_anchor = zoom_arguments.size() > 1 ? zoom_arguments.at(1).toReal() : -1.0;
  const auto actual_content_x = viewport->property("contentX").toReal();
  const auto actual_content_y = viewport->property("contentY").toReal();
  const auto expected_anchor = before_zoom_x + pointer_in_viewport.x();
  if (zoom_event_count != 1 || zoom_arguments.size() != 2 || !near(actual_zoom, 2.3) ||
      !near(actual_anchor, expected_anchor) || !near(actual_content_x, before_zoom_x) ||
      !near(actual_content_y, before_zoom_y)) {
    qCritical().noquote()
        << QStringLiteral("Ctrl-wheel diagnostic: zoom=%1 anchor=%2 expectedAnchor=%3 contentX=%4 beforeX=%5 contentY=%6 beforeY=%7 pointerViewportX=%8 pointerWindowX=%9")
               .arg(actual_zoom, 0, 'f', 6)
               .arg(actual_anchor, 0, 'f', 6)
               .arg(expected_anchor, 0, 'f', 6)
               .arg(actual_content_x, 0, 'f', 6)
               .arg(before_zoom_x, 0, 'f', 6)
               .arg(actual_content_y, 0, 'f', 6)
               .arg(before_zoom_y, 0, 'f', 6)
               .arg(pointer_in_viewport.x(), 0, 'f', 6)
               .arg(pointer.x(), 0, 'f', 6);
    return fail("Ctrl-wheel zoom should preserve its 1.15 step and pointer anchor.");
  }

  send_wheel(window, QPoint(), QPoint(), Qt::ControlModifier, pointer,
             Qt::ScrollEnd);
  if (zoom_spy.count() != 0 ||
      !near(viewport->property("contentY").toReal(), before_zoom_y)) {
    return fail("A zero-delta scroll-end event must not zoom or scroll.");
  }

  send_wheel(window, QPoint(0, 12), QPoint(), Qt::ControlModifier, pointer);
  if (zoom_spy.count() != 1 ||
      !near(zoom_spy.takeFirst().at(0).toReal(), 2.3)) {
    return fail("Pixel-only Ctrl trackpad input should zoom according to its vertical sign.");
  }

  QSignalSpy lane_scale_spy(vertical_handler.get(), SIGNAL(laneScaleRequested(double,double)));
  QSignalSpy header_lane_scale_spy(header_vertical_handler.get(), SIGNAL(laneScaleRequested(double,double)));
  if (!lane_scale_spy.isValid() || !header_lane_scale_spy.isValid())
    return fail("Lane scaling must be available from the timeline and sibling headers.");
  viewport->setProperty("contentX", 240.0);
  viewport->setProperty("contentY", 160.0);
  settle_events();
  send_wheel(window, QPoint(5, 18), QPoint(), Qt::AltModifier, pointer);
  if (lane_scale_spy.count() != 1 || header_lane_scale_spy.count() != 0 ||
      !near(lane_scale_spy.first().at(0).toReal(), 1.08) ||
      !near(lane_scale_spy.first().at(1).toReal(), 80.0) ||
      !near(viewport->property("contentX").toReal(), 240.0) ||
      !near(viewport->property("contentY").toReal(), 160.0))
    return fail("Option-wheel must scale lanes at the pointer without also panning either axis.");
  send_wheel(window, QPoint(), QPoint(0, -120), Qt::AltModifier,
             header_column_item->mapToScene(QPointF(60.0, 80.0)));
  if (header_lane_scale_spy.count() != 1 ||
      !near(header_lane_scale_spy.first().at(0).toReal(), 1.0 / 1.08) ||
      !near(viewport->property("contentX").toReal(), 240.0) ||
      !near(viewport->property("contentY").toReal(), 160.0))
    return fail("Option-wheel on headers must scale the same lanes without panning.");

  viewport->setProperty("contentX", 1.0);
  send_wheel(window, QPoint(12, 0), QPoint(), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), 0.0)) {
    return fail("Horizontal wheel movement should clamp at the timeline start.");
  }
  const auto maximum_x = viewport->property("contentWidth").toReal() -
                         viewport->property("width").toReal();
  viewport->setProperty("contentX", maximum_x - 2.0);
  send_wheel(window, QPoint(-12, 0), QPoint(), Qt::NoModifier, pointer);
  if (!near(viewport->property("contentX").toReal(), maximum_x)) {
    return fail("Horizontal wheel movement should clamp at the timeline end.");
  }

  const QPointF header_pointer =
      header_column_item->mapToScene(QPointF(75.0, 80.0));
  const auto reset_header_viewport = [&]() {
    viewport->setProperty("contentX", 240.0);
    viewport->setProperty("contentY", 160.0);
    settle_events();
  };

  reset_header_viewport();
  send_wheel(window, QPoint(0, 25), QPoint(), Qt::NoModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 240.0) ||
      !near(viewport->property("contentY").toReal(), 135.0) ||
      zoom_spy.count() != 0 || header_zoom_spy.count() != 0) {
    return fail("A vertical wheel over the sibling track headers should scroll the shared viewport up on Y only.");
  }
  const auto header_vertical_event =
      header_vertical_handler->property("lastWheelEvent").toMap();
  if (!header_vertical_event.value("received").toBool() ||
      header_vertical_event.value("accepted").toBool() != true ||
      !near(header_vertical_event.value("sceneX").toReal(), header_pointer.x()) ||
      !near(header_vertical_event.value("sceneY").toReal(), header_pointer.y()) ||
      !near(header_vertical_event.value("mappedViewportX").toReal(), -75.0) ||
      !near(header_vertical_event.value("mappedViewportY").toReal(), 80.0)) {
    return fail("The sibling header vertical handler should map its pointer into the shared viewport.");
  }

  send_wheel(window, QPoint(0, -11), QPoint(), Qt::NoModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 240.0) ||
      !near(viewport->property("contentY").toReal(), 146.0)) {
    return fail("A downward wheel over the sibling track headers should move only the shared viewport Y offset down.");
  }

  reset_header_viewport();
  send_wheel(window, QPoint(0, 18), QPoint(), Qt::ShiftModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 222.0) ||
      !near(viewport->property("contentY").toReal(), 160.0)) {
    return fail("Shift plus vertical wheel over sibling headers should pan the shared viewport on X only.");
  }

  reset_header_viewport();
  send_wheel(window, QPoint(24, 0), QPoint(), Qt::NoModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 216.0) ||
      !near(viewport->property("contentY").toReal(), 160.0)) {
    return fail("A horizontal wheel over sibling headers should move the shared viewport on X only.");
  }

  reset_header_viewport();
  send_wheel(window, QPoint(12, 9), QPoint(), Qt::NoModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 228.0) ||
      !near(viewport->property("contentY").toReal(), 151.0)) {
    return fail("A diagonal wheel over sibling headers should apply each axis exactly once.");
  }

  reset_header_viewport();
  header_zoom_spy.clear();
  zoom_spy.clear();
  send_wheel(window, QPoint(), QPoint(0, 120), Qt::ControlModifier,
             header_pointer);
  const auto header_zoom_arguments =
      header_zoom_spy.count() > 0 ? header_zoom_spy.takeFirst() : QList<QVariant>{};
  if (header_zoom_spy.count() != 0 || header_zoom_arguments.size() != 2 ||
      !near(header_zoom_arguments.at(0).toReal(), 2.3) || zoom_spy.count() != 0 ||
      !near(header_zoom_arguments.at(1).toReal(), 240.0) ||
      !near(viewport->property("contentX").toReal(), 240.0) ||
      !near(viewport->property("contentY").toReal(), 160.0)) {
    return fail("Ctrl-wheel over sibling headers should zoom the shared viewport exactly once without panning.");
  }

  viewport->setProperty("contentX", 1.0);
  viewport->setProperty("contentY", 1.0);
  send_wheel(window, QPoint(12, 0), QPoint(), Qt::NoModifier, header_pointer);
  send_wheel(window, QPoint(0, 12), QPoint(), Qt::NoModifier, header_pointer);
  if (!near(viewport->property("contentX").toReal(), 0.0) ||
      !near(viewport->property("contentY").toReal(), 0.0)) {
    return fail("Horizontal and vertical wheel movement over sibling headers should clamp at the shared viewport start.");
  }
  viewport->setProperty("contentX", maximum_x - 2.0);
  viewport->setProperty("contentY", viewport->property("contentHeight").toReal() -
                         viewport->property("height").toReal() - 2.0);
  send_wheel(window, QPoint(-12, 0), QPoint(), Qt::NoModifier, header_pointer);
  send_wheel(window, QPoint(0, -12), QPoint(), Qt::NoModifier, header_pointer);
  const auto maximum_y = viewport->property("contentHeight").toReal() -
                         viewport->property("height").toReal();
  if (!near(viewport->property("contentX").toReal(), maximum_x) ||
      !near(viewport->property("contentY").toReal(), maximum_y)) {
    return fail("Horizontal and vertical wheel movement over sibling headers should clamp at the shared viewport end.");
  }

  return EXIT_SUCCESS;
}
