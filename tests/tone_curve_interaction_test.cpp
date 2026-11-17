#include <QGuiApplication>
#include <QFontMetricsF>
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
    objectName: "toneCurveTestEditor"
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
    auto* numeric_input = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveInputField"));
    if (!numeric_output || !numeric_output->isVisible() || !numeric_input || !numeric_input->isVisible())
      return fail("Numeric input and output must remain visible in both layouts");
    const auto numeric_font = numeric_output->property("font").value<QFont>();
    const auto numeric_width = numeric_output->width() - numeric_output->property("leftPadding").toDouble()
                               - numeric_output->property("rightPadding").toDouble();
    if (QFontMetricsF(numeric_font).horizontalAdvance(QStringLiteral("255.000")) > numeric_width + 1)
      return fail("Curve point fields must fit the full accepted numeric precision");
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

  item->setWidth(250);
  item->setHeight(430);
  window.resize(250, 430);
  QTest::qWait(60);
  const auto narrowInset = editor->property("graphInset").toDouble();
  const auto narrowHeight = graph->height() - narrowInset * 2;
  if (graph->width() < 220 || narrowHeight < 120 ||
      graph->mapToItem(item, QPointF(0, 0)).x() + graph->width() > item->width() + 1)
    return fail("The tone graph must preserve usable geometry when its inspector narrows");
  auto* narrow_input = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveInputField"));
  auto* narrow_output = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveOutputField"));
  if (!narrow_input || !narrow_output || narrow_input->width() < 72 || narrow_output->width() < 72 ||
      narrow_input->mapToItem(item, QPointF(0, 0)).x() + narrow_input->width() > item->width() + 1 ||
      narrow_output->mapToItem(item, QPointF(0, 0)).x() + narrow_output->width() > item->width() + 1)
    return fail("Curve point fields must wrap and remain reachable in a narrow inspector");

  QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
  const auto starts_before_numeric = editor->property("starts").toInt();
  const auto finishes_before_numeric = editor->property("finishes").toInt();
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    narrow_output->mapToScene(QPointF(narrow_output->width() / 2,
                                                     narrow_output->height() / 2)).toPoint());
  if (!narrow_output->hasActiveFocus())
    return fail("The resized numeric field must receive pointer focus at its mapped location");
  QTest::keyClick(&window, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClick(&window, Qt::Key_6);
  QTest::keyClick(&window, Qt::Key_4);
  QTest::keyClick(&window, Qt::Key_Return);
  QTest::qWait(20);
  if (std::abs(ordinate(editor.get(), 0) - 64.0 / 255.0) > 1e-5 ||
      editor->property("starts").toInt() != starts_before_numeric + 1 ||
      editor->property("finishes").toInt() != finishes_before_numeric + 1)
    return fail("Narrow curve numeric entry must edit the selected point in one transaction");

  QMetaObject::invokeMethod(editor.get(), "testResetActiveCurve");
  const auto narrowOrigin = graph->mapToScene(QPointF(narrowInset, narrowInset + narrowHeight));
  const auto narrowPress = (narrowOrigin + QPointF(14, -12)).toPoint();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, narrowPress);
  QTest::mouseMove(&window, narrowPress + QPoint(0, -5));
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, narrowPress + QPoint(0, -5));
  if (std::abs(ordinate(editor.get(), 0) - 5.0 / narrowHeight) > 1e-5)
    return fail("Curve handle mapping must stay accurate after a narrow resize");
  const auto capture_dir = qEnvironmentVariable("PHOTO_EDITOR_CURVE_CAPTURE_DIR");
  if (!capture_dir.isEmpty() && !window.grabWindow().save(capture_dir + "/curve-narrow.png"))
    return fail("Could not capture the resized curve UI proof");

  const QVariantList editable_points{
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.25}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}}};
  editor->setProperty("pointsModel", editable_points);
  QVariant selected_index;
  if (!QMetaObject::invokeMethod(editor.get(), "testSelectPoint", Q_RETURN_ARG(QVariant, selected_index),
                                 Q_ARG(QVariant, 1)))
    return fail("The selected curve point should be addressable for button interaction proof");
  auto* delete_button = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveTestEditorDeletePointButton"));
  if (!delete_button || !delete_button->isVisible())
    return fail("The selected interior curve point must expose its Delete action");
  const auto starts_before_delete = editor->property("starts").toInt();
  const auto finishes_before_delete = editor->property("finishes").toInt();
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    delete_button->mapToScene(QPointF(delete_button->width() / 2,
                                                     delete_button->height() / 2)).toPoint());
  if (points(editor.get()).size() != 2 || editor->property("starts").toInt() != starts_before_delete + 1 ||
      editor->property("finishes").toInt() != finishes_before_delete + 1)
    return fail("The Delete button must remove one point inside one edit transaction");

  editor->setProperty("pointsModel", editable_points);
  auto* reset_button = editor->findChild<QQuickItem*>(QStringLiteral("toneCurveTestEditorResetCurveButton"));
  if (!reset_button || !reset_button->isVisible())
    return fail("The full curve layout must expose its Reset action");
  const auto starts_before_reset = editor->property("starts").toInt();
  const auto finishes_before_reset = editor->property("finishes").toInt();
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier,
                    reset_button->mapToScene(QPointF(reset_button->width() / 2,
                                                    reset_button->height() / 2)).toPoint());
  if (points(editor.get()).size() != 2 || editor->property("starts").toInt() != starts_before_reset + 1 ||
      editor->property("finishes").toInt() != finishes_before_reset + 1)
    return fail("The Reset button must restore the identity curve inside one edit transaction");
  std::cout << "Native curve offset, insertion, fine drag, keyboard nudge, axis lock, button deletion/reset, narrow resize and cancel passed\n";
  return EXIT_SUCCESS;
}
