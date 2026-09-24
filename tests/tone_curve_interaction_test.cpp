#include <QGuiApplication>
#include <QJSValue>
#include <QMouseEvent>
#include <QtTest/qtestkeyboard.h>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/qtestmouse.h>
#include <QtTest/qtestsystem.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
QVariantList points(QObject* editor) {
  const auto value = editor->property("pointsModel");
  return value.metaType() == QMetaType::fromType<QJSValue>()
             ? value.value<QJSValue>().toVariant().toList() : value.toList();
}
double ordinate(QObject* editor, int index) {
  return points(editor).at(index).toMap().value(QStringLiteral("y")).toDouble();
}
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
ToneCurveEditor {
    width: 340; height: 430
    property int editCount: 0
    property int starts: 0
    property int finishes: 0
    onEditStarted: starts++
    onEditFinished: finishes++
    pointsModel: [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    onPointsEdited: function(points) { pointsModel = points; editCount += 1 }
}
)", QUrl());
  if (component.isError()) {
    std::cerr << component.errorString().toStdString();
    return EXIT_FAILURE;
  }
  std::unique_ptr<QObject> editor(component.create());
  auto* item = qobject_cast<QQuickItem*>(editor.get());
  if (!item) return fail("Could not create curve editor");
  QQuickWindow window;
  window.setColor(QColor("#151619"));
  window.resize(340, 430);
  item->setParentItem(window.contentItem());
  window.show();
  if (!QTest::qWaitForWindowExposed(&window)) return fail("Curve window was not exposed");
  auto* graph = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveGraph"));
  if (!graph) return fail("Curve graph missing");
  for (bool compact : {false, true}) {
    editor->setProperty("compactMode", compact);
    item->setHeight(compact ? 240 : 430);
    window.resize(340, compact ? 240 : 430);
    QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
    QTest::qWait(60);
    const auto inset = editor->property("graphInset").toDouble();
    const auto height = graph->height() - inset * 2;
    const auto width = graph->width() - inset * 2;
    const auto origin = graph->mapToScene(QPointF(inset, inset + height));
    const auto near_handle = (origin + QPointF(14, -12)).toPoint();
    const auto edits_before = editor->property("editCount").toInt();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, near_handle);
    if (editor->property("editCount").toInt() != edits_before || ordinate(editor.get(), 0) != 0)
      return fail("Pressing near a handle must select without changing the curve");
    QTest::mouseMove(&window, near_handle + QPoint(0, -5));
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, near_handle + QPoint(0, -5));
    if (std::abs(ordinate(editor.get(), 0) - 5.0 / height) > 1e-5)
      return fail("Handle movement must preserve the initial grab offset");
    QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
    const auto off_curve = graph->mapToScene(QPointF(inset + width * 0.5, inset + height * 0.85)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, off_curve);
    const auto added = points(editor.get());
    if (added.size() != 3 || std::abs(ordinate(editor.get(), 1) -
        added.at(1).toMap().value(QStringLiteral("x")).toDouble()) > 1e-5)
      return fail("Adding a point away from the curve must start on the neutral diagonal");
    // Clicking either endpoint without dragging must remain non-destructive.
    const auto before_selection = points(editor.get());
    const auto white_handle = graph->mapToScene(QPointF(inset + width - 10, inset + 10)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, white_handle);
    if (points(editor.get()) != before_selection)
      return fail("Selecting the white endpoint must not crush highlights");
    const auto move = [&](QPoint position, Qt::KeyboardModifiers modifiers) {
      QMouseEvent event(QEvent::MouseMove, position, window.mapToGlobal(position),
                        Qt::NoButton, Qt::LeftButton, modifiers);
      QGuiApplication::sendEvent(&window, &event);
    };
    QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
    QTest::mousePress(&window, Qt::LeftButton, Qt::ShiftModifier, near_handle);
    move(near_handle + QPoint(0, -10), Qt::ShiftModifier);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::ShiftModifier, near_handle + QPoint(0, -10));
    if (std::abs(ordinate(editor.get(), 0) - 1.0 / height) > 1e-5)
      return fail("Curve fine movement must be one tenth of normal movement");
    const auto before_nudge = ordinate(editor.get(), 0);
    QTest::keyClick(&window, Qt::Key_Up);
    QTest::keyClick(&window, Qt::Key_Up, Qt::ShiftModifier);
    if (std::abs(ordinate(editor.get(), 0) - before_nudge - 1.1 / 255) > 1e-5)
      return fail("Curve arrow nudges must support one level and one tenth level");
    auto* numeric_output = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveOutputField"));
    if (!numeric_output || !numeric_output->isVisible())
      return fail("Numeric output must remain visible in both layouts");
    for (int attempt = 0; attempt < 5; ++attempt) {
      const auto before_three = ordinate(editor.get(), 0);
      for (int nudge = 0; nudge < 3; ++nudge)
        QTest::keyClick(&window, Qt::Key_Up);
      if (std::abs(ordinate(editor.get(), 0) - before_three - 3.0 / 255) > 1e-5)
        return fail("Three-level keyboard corrections must not overshoot");
    }
    const auto before_cancel = points(editor.get());
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, near_handle);
    move(near_handle + QPoint(0, -10), Qt::NoModifier);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, near_handle + QPoint(0, -10));
    if (points(editor.get()) != before_cancel)
      return fail("Curve Escape must restore the exact pre-gesture points");
    QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
    const auto middle = graph->mapToScene(QPointF(inset + width * 0.5, inset + height * 0.5)).toPoint();
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, middle);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, middle);
    if (points(editor.get()).size() != 2)
      return fail("Canceling a new point must remove the insertion");
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, middle);
    const auto middle_before = points(editor.get()).at(1).toMap();
    QTest::mousePress(&window, Qt::LeftButton, Qt::ControlModifier, middle);
    move(middle + QPoint(10, -2), Qt::ControlModifier);
    move(middle + QPoint(15, -10), Qt::ControlModifier);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::ControlModifier, middle + QPoint(15, -10));
    const auto middle_after = points(editor.get()).at(1).toMap();
    if (middle_after.value("y") != middle_before.value("y") ||
        middle_after.value("x").toDouble() <= middle_before.value("x").toDouble())
      return fail("Curve horizontal lock must preserve output");
    QTest::keyClick(&window, Qt::Key_Delete);
    if (points(editor.get()).size() != 2) return fail("Delete key must remove the selected interior point");
    if (editor->property("starts").toInt() != editor->property("finishes").toInt())
      return fail("Curve gestures must balance edit transactions");
    QTest::qWait(60);
    const auto capture_dir = qEnvironmentVariable("PHOTO_EDITOR_CURVE_CAPTURE_DIR");
    if (!capture_dir.isEmpty() && !window.grabWindow().save(
            capture_dir + (compact ? "/curve-compact.png" : "/curve-normal.png")))
      return fail("Could not capture curve UI proof");
  }
  auto* expand = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveExpandButton"));
  if (!expand || !expand->isVisible()) return fail("Compact curve must expose expansion");
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    expand->mapToScene(QPointF(expand->width() / 2, expand->height() / 2)).toPoint());
  if (!editor->property("expanded").toBool() || editor->property("compactLayout").toBool() ||
      editor->property("implicitHeight").toDouble() < 400)
    return fail("Expand must request the full graph layout");
  std::cout << "Native curve offset, insertion, fine drag, keyboard nudge, axis lock, deletion and cancel passed in both sizes\n";
  return EXIT_SUCCESS;
}
