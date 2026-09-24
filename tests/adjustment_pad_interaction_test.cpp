#include <QGuiApplication>
#include <QMouseEvent>
#include <QDir>
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

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import "file:)" + QByteArray(PHOTO_EDITOR_SOURCE_DIR) + R"(/apps/desktop/qml/components"
StudioAdjustmentPad2D {
    width: 340; height: 180
    property int starts: 0
    property int finishes: 0
    onValueEdited: function(x, y) { xValue = x; yValue = y }
    onInteractionStarted: starts++
    onInteractionFinished: finishes++
}
)", QUrl());
  if (component.isError()) {
    std::cerr << component.errorString().toStdString();
    return EXIT_FAILURE;
  }
  std::unique_ptr<QObject> editor(component.create());
  auto* item = qobject_cast<QQuickItem*>(editor.get());
  if (!item) return fail("Could not create adjustment pad");
  QQuickWindow window;
  window.setColor(QColor("#151619"));
  window.resize(340, 180);
  item->setParentItem(window.contentItem());
  window.show();
  if (!QTest::qWaitForWindowExposed(&window)) return fail("Pad window was not exposed");
  auto* field = editor->findChild<QQuickItem*>(QStringLiteral("adjustmentPadField"));
  if (!field) return fail("Pad field missing");
  QTest::qWait(60);
  const auto center = field->mapToScene(QPointF(field->width() / 2 + 5, field->height() / 2)).toPoint();
  const auto value = [&]() { return editor->property("xValue").toDouble(); };
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
  if (value() != 0) return fail("Press jumped");
  QTest::mouseMove(&window, center + QPoint(10, 0));
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(10, 0));
  const double normal = value();
  if (std::abs(normal - 2000 / field->width()) > 1e-5) return fail("Incorrect relative movement");
  editor->setProperty("xValue", 0);
  QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, center);
  const auto fine_position = center + QPoint(10, 0);
  QMouseEvent fine_move(QEvent::MouseMove, fine_position,
                        window.mapToGlobal(fine_position), Qt::NoButton,
                        Qt::LeftButton, Qt::ShiftModifier);
  QGuiApplication::sendEvent(&window, &fine_move);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, center + QPoint(10, 0));
  if (std::abs(value() - normal * 0.1) > 1e-5) return fail("Fine movement must be one tenth");
  const double before = value();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
  QTest::mouseMove(&window, center + QPoint(10, 5));
  QTest::mouseMove(&window, center + QPoint(20, 10));
  QTest::keyClick(&window, Qt::Key_Escape);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(20, 10));
  QTest::qWait(60);
  if (value() != before || editor->property("yValue").toDouble() != 0)
    return fail("Cancel must restore both values and discard pending edits");
  if (editor->property("starts").toInt() != 3 || editor->property("finishes").toInt() != 3)
    return fail("Gestures must finish exactly once");
  const auto move = [&](QPoint position, Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(QEvent::MouseMove, position, window.mapToGlobal(position),
                      Qt::NoButton, Qt::LeftButton, modifiers);
    QGuiApplication::sendEvent(&window, &event);
  };
  editor->setProperty("xValue", 0);
  editor->setProperty("yValue", 0);
  QTest::mousePress(&window, Qt::LeftButton, Qt::ControlModifier, center);
  move(center + QPoint(10, 3), Qt::ControlModifier);
  move(center + QPoint(15, 20), Qt::ControlModifier);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::ControlModifier, center + QPoint(15, 20));
  if (std::abs(value() - 3000 / field->width()) > 1e-5 ||
      editor->property("yValue").toDouble() != 0)
    return fail("Horizontal axis lock must persist when later vertical movement is larger");
  editor->setProperty("xValue", 0);
  QTest::mousePress(&window, Qt::LeftButton, Qt::ControlModifier, center);
  move(center + QPoint(3, 10), Qt::ControlModifier);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::ControlModifier, center + QPoint(3, 10));
  if (value() != 0 || std::abs(editor->property("yValue").toDouble() + 2000 / field->height()) > 1e-5)
    return fail("Vertical lock must leave the horizontal parameter unchanged");

  editor->setProperty("xValue", 0);
  editor->setProperty("yValue", 0);
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
  move(center + QPoint(10, 0), Qt::NoModifier);
  move(center + QPoint(10, 0), Qt::ShiftModifier);
  if (std::abs(value() - normal) > 1e-5) return fail("Entering fine mode jumped");
  move(center + QPoint(20, 0), Qt::ShiftModifier);
  move(center + QPoint(20, 0), Qt::NoModifier);
  move(center + QPoint(30, 0), Qt::NoModifier);
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, center + QPoint(30, 0));
  if (std::abs(value() - normal * 2.1) > 1e-5)
    return fail("Modifier transitions must retain accumulated fine and normal movement");

  const double before_ungrab = value();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, center);
  move(center + QPoint(12, 15), Qt::NoModifier);
  if (!window.mouseGrabberItem()) return fail("Expected pad mouse grab");
  window.mouseGrabberItem()->ungrabMouse();
  QTest::qWait(60);
  if (value() != before_ungrab || editor->property("yValue").toDouble() != 0)
    return fail("Losing pointer grab must roll back both values");
  if (editor->property("starts").toInt() != 7 || editor->property("finishes").toInt() != 7)
    return fail("Every completed or canceled gesture must finish exactly once");
  const auto capture_dir = qEnvironmentVariable("PHOTO_EDITOR_PAD_CAPTURE_DIR");
  if (!capture_dir.isEmpty()) {
    QDir().mkpath(capture_dir);
    editor->setProperty("title", "Temp / Tint");
    editor->setProperty("xLabel", "Cool / Warm");
    editor->setProperty("yLabel", "Magenta / Green");
    QTest::qWait(60);
    if (!window.grabWindow().save(capture_dir + "/adjustment-pad.png"))
      return fail("Could not capture pad UI");
  }
  std::cout << "Native pad offset, fine movement, axis locks, modifier transitions, Escape and pointer cancellation passed\n";
  return EXIT_SUCCESS;
}
