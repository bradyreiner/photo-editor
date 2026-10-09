#include <QGuiApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QKeySequence>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/qtestmouse.h>
#include <QtTest/qtestkeyboard.h>
#include <QtTest/qtestsystem.h>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import "file:)" + QByteArray(PHOTO_EDITOR_SOURCE_DIR) + R"(/apps/desktop/qml/components"
InspectorSlider {
    objectName: "exposureTest"
    width: 500; height: 60
    label: "Exposure"
    from: -5; to: 5; stepSize: 0.01
    precisionResponse: true
    valueText: value.toFixed(2) + " EV"
    property int starts: 0
    property int finishes: 0
    property int commits: 0
    onInteractionStarted: starts++
    onInteractionFinished: finishes++
    onValueDragged: function(next) { commits++; value = next }
}
)", QUrl());
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return EXIT_FAILURE; };
  std::unique_ptr<QObject> object(component.create());
  auto* item = qobject_cast<QQuickItem*>(object.get());
  if (!item) { std::cerr << component.errorString().toStdString(); return EXIT_FAILURE; }
  QQuickWindow window;
  window.resize(500, 60);
  window.setColor(QColor("#141617"));
  item->setParentItem(window.contentItem());
  window.show();
  if (!QTest::qWaitForWindowExposed(&window)) return fail("Exposure window unavailable");
  QTest::qWait(60);
  auto* pointer = item->findChild<QQuickItem*>(QStringLiteral("exposureTestPrecisionPointer"));
  if (!pointer) return fail("Precision pointer missing");
  const auto value = [&]() { return item->property("value").toDouble(); };
  const auto move = [&](QPoint point, Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(QEvent::MouseMove, point, window.mapToGlobal(point),
                      Qt::NoButton, Qt::LeftButton, modifiers);
    QGuiApplication::sendEvent(&window, &event);
  };
  auto* numeric = item->findChild<QQuickItem*>(QStringLiteral("exposureTestValueField"));
  if (!numeric) return fail("Exposure numeric field missing");
  // Type through the actual text field, including Return followed by focus loss.
  for (const double ev : {-5.0, -3.0, 3.0, 5.0, 0.01, -0.01}) {
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      numeric->mapToScene(QPointF(numeric->width() / 2, numeric->height() / 2)).toPoint());
    const auto select_all = QKeySequence(QKeySequence::SelectAll)[0];
    QTest::keyClick(&window, select_all.key(), select_all.keyboardModifiers());
    QKeyEvent text(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, QString::number(ev));
    QGuiApplication::sendEvent(&window, &text);
    QTest::keyClick(&window, Qt::Key_Return);
    item->forceActiveFocus();
    QTest::qWait(20);
    if (std::abs(value() - ev) > 1e-8) {
      std::cerr << "expected " << ev << " value " << value()
                << " text " << numeric->property("text").toString().toStdString() << '\n';
      return fail("Numeric EV was clipped or rounded");
    }
    QVariant position;
    QMetaObject::invokeMethod(item, "positionForValue", Q_RETURN_ARG(QVariant, position), Q_ARG(QVariant, ev));
    QVariant roundtrip;
    QMetaObject::invokeMethod(item, "valueForPosition", Q_RETURN_ARG(QVariant, roundtrip), Q_ARG(QVariant, position));
    if (std::abs(roundtrip.toDouble() - ev) > 1e-8) return fail("Exposure track mapping is not reversible");
  }
  const auto type_value = [&](const QString& text) {
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                      numeric->mapToScene(QPointF(numeric->width() / 2, numeric->height() / 2)).toPoint());
    const auto select_all = QKeySequence(QKeySequence::SelectAll)[0];
    QTest::keyClick(&window, select_all.key(), select_all.keyboardModifiers());
    QKeyEvent input(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QGuiApplication::sendEvent(&window, &input);
  };
  const int beforeFocusLossCommit = item->property("commits").toInt();
  type_value(QStringLiteral("0.04 EV"));
  item->forceActiveFocus(Qt::OtherFocusReason);
  QTest::qWait(20);
  if (std::abs(value() - 0.04) > 1e-8 ||
      item->property("commits").toInt() != beforeFocusLossCommit + 1)
    return fail("A valid unit-suffixed value must commit once when focus leaves the numeric field");

  const int beforeInvalidCommit = item->property("commits").toInt();
  numeric->setProperty("text", QString());
  item->setProperty("valueInputDirty", true);
  QVariant invalidCommitted;
  if (!QMetaObject::invokeMethod(item, "commitValueText", Q_RETURN_ARG(QVariant, invalidCommitted)) ||
      invalidCommitted.toBool() || item->property("commits").toInt() != beforeInvalidCommit ||
      std::abs(value() - 0.04) > 1e-8)
    return fail("Blank numeric input must be rejected instead of silently committing zero");
  item->setProperty("value", 0);
  const auto center = pointer->mapToScene(QPointF(pointer->width() / 2, pointer->height() / 2)).toPoint();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(3, 0));
  if (value() != 0) return fail("Grabbing exposure handle changed its value");
  move(center + QPoint(13, 0), Qt::NoModifier);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(13, 0));
  const auto near_zero = value();
  const auto travel = pointer->property("travel").toDouble();
  if (near_zero <= 0 || near_zero > 40.0 / travel)
    return fail("Wider exposure range must retain at least the old central drag precision");
  item->setProperty("value", 0);
  QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, center);
  move(center + QPoint(100, 0), Qt::ShiftModifier);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, center + QPoint(100, 0));
  if (std::abs(value() - near_zero) > 0.011) return fail("Shift must reduce track movement to one tenth");
  for (const int direction : {-1, 1}) {
    item->setProperty("value", 0);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
    const auto edge = pointer->mapToScene(QPointF(direction < 0 ? 0 : pointer->width(), pointer->height() / 2)).toPoint();
    move(edge, Qt::NoModifier);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, edge);
    if (value() != direction * 5) return fail("Pointer drag must reach both five-stop endpoints");
  }
  item->setProperty("value", 0.01);
  QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, center);
  move(center + QPoint(60, 0), Qt::ShiftModifier);
  QTest::keyClick(&window, Qt::Key_Escape);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, center + QPoint(60, 0));
  QTest::qWait(60);
  if (std::abs(value() - 0.01) > 1e-8) return fail("Cancel must retain the exact starting EV");
  if (item->property("starts") != item->property("finishes")) return fail("Exposure transactions are unbalanced");
  const auto capture = qEnvironmentVariable("PHOTO_EDITOR_EXPOSURE_CAPTURE_PATH");
  if (!capture.isEmpty() && !window.grabWindow().save(capture)) return fail("Exposure capture failed");
  std::cout << "Exposure numeric range, mapping, central precision, fine drag, endpoints and cancel passed\n";
  return EXIT_SUCCESS;
}
