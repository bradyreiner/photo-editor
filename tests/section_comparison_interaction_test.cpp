#include <QGuiApplication>
#include <QStyleHints>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QtTest/qtestmouse.h>
#include <QtTest/qtestsystem.h>
#include <cstdlib>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Layouts
import "file:)" + QByteArray(PHOTO_EDITOR_SOURCE_DIR) + R"(/apps/desktop/qml/components"
Item {
    id: host
    width: 400; height: 180
    property color mint: "#d4ad6a"
    property string scope: ""
    property string previousScope: ""
    property int toggles: 0
    property int holds: 0
    function activeAdjustmentLayerIndex() { return -1 }
    function sectionComparisonActive(key) { return scope === key }
    function toggleSectionComparison(key) { toggles++; scope = scope === key ? "" : key }
    function holdSectionComparison(key, held) {
        if (held) { holds++; previousScope = scope; scope = key }
        else { scope = previousScope }
    }
    function resetPrimaryAdjustmentGroup(key) {}
    ColumnLayout {
        anchors.fill: parent
        StudioInspectorGroup {
            objectName: "whiteBalanceGroup"
            workspace: host
            title: "White Balance"
            subtitle: "Temperature and tint"
            compareGroup: "white-balance"
            beforeAfterObjectName: "wbCompare"
        }
        StudioInspectorGroup {
            workspace: host
            title: "Curves"
            subtitle: "Curves and input levels"
            compareGroup: "curves"
            beforeAfterObjectName: "curveCompare"
        }
        StudioCollapsibleGroup {
            title: "Color Mix"
            expanded: false
            compareVisible: true
            compareObjectName: "mixCompare"
            compareActive: host.scope === "color-mix"
            onCompareRequested: host.toggleSectionComparison("color-mix")
            onCompareHoldRequested: function(held) { host.holdSectionComparison("color-mix", held) }
        }
    }
}
)", QUrl());
  const auto fail = [](const char* message) { std::cerr << message << '\n'; return EXIT_FAILURE; };
  std::unique_ptr<QObject> object(component.create());
  auto* host = qobject_cast<QQuickItem*>(object.get());
  if (!host) { std::cerr << component.errorString().toStdString(); return EXIT_FAILURE; }
  QQuickWindow window;
  window.setColor(QColor("#141617"));
  window.resize(400, 180);
  host->setParentItem(window.contentItem());
  window.show();
  if (!QTest::qWaitForWindowExposed(&window)) return fail("Comparison window unavailable");
  QTest::qWait(80);
  const auto position = [&](const char* name) {
    auto* button = host->findChild<QQuickItem*>(QString::fromLatin1(name));
    return button ? button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint() : QPoint(-1, -1);
  };
  const auto wb = position("wbCompare");
  const auto curve = position("curveCompare");
  const auto mix = position("mixCompare");
  if (wb.x() < 0 || curve.x() < 0 || mix.x() < 0) return fail("Visible comparison buttons missing");
  const auto scope = [&]() { return host->property("scope").toString(); };
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, wb);
  if (scope() != QStringLiteral("white-balance")) return fail("White Balance click routed to the wrong scope");
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, curve);
  QTest::qWait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 100);
  if (scope() != QStringLiteral("curves")) return fail("Holding Curves must temporarily replace comparison");
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, curve);
  if (scope() != QStringLiteral("white-balance") || host->property("toggles").toInt() != 1)
    return fail("Releasing a hold must restore the previous scope without toggling it");
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, wb);
  if (!scope().isEmpty()) return fail("Second click must restore all current edits");
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, mix);
  if (scope() != QStringLiteral("color-mix")) return fail("Collapsed Color Mix must remain independently comparable");
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, curve);
  QTest::qWait(QGuiApplication::styleHints()->mousePressAndHoldInterval() + 100);
  if (!window.mouseGrabberItem()) return fail("Comparison pointer grab missing");
  window.mouseGrabberItem()->ungrabMouse();
  if (scope() != QStringLiteral("color-mix")) return fail("Canceled comparison must restore the previous scope");
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, wb);
  auto* wb_group = host->findChild<QQuickItem*>(QStringLiteral("whiteBalanceGroup"));
  wb_group->setVisible(false);
  if (!scope().isEmpty()) return fail("Hiding a compared section must restore the current image");
  wb_group->setVisible(true);
  QTest::mouseMove(&window, QPoint(0, window.height() - 1));
  QTest::qWait(100);
  const auto capture = qEnvironmentVariable("PHOTO_EDITOR_SECTION_CAPTURE_PATH");
  if (!capture.isEmpty() && !window.grabWindow().save(capture)) return fail("Comparison capture failed");
  std::cout << "Native section comparison toggle, hold, release, cancel and collapsed Color Mix passed\n";
  return EXIT_SUCCESS;
}
