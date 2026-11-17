#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QVariant>

#include <cstdlib>
#include <iostream>
#include <memory>

namespace {
QString invoke(QObject* root, const char* method) {
  QVariant result;
  if (!QMetaObject::invokeMethod(root, method, Q_RETURN_ARG(QVariant, result)))
    return QStringLiteral("Failed to invoke QML check");
  return result.toString();
}
QString guides(QObject* root, int count, const QString& axis, double position) {
  QVariant result;
  QMetaObject::invokeMethod(root, "assertGuides", Q_RETURN_ARG(QVariant, result),
                           Q_ARG(QVariant, count), Q_ARG(QVariant, axis), Q_ARG(QVariant, position));
  return result.toString();
}
bool check(const QString& result) {
  if (result == QStringLiteral("ok")) return true;
  std::cerr << result.toStdString() << '\n';
  return false;
}
void drag(QQuickWindow* window, const QPoint& start, const QPoint& end,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  if (modifiers.testFlag(Qt::ShiftModifier)) QTest::keyPress(window, Qt::Key_Shift);
  QTest::mousePress(window, Qt::LeftButton, modifiers, start);
  QTest::mouseMove(window, end, 20);
  QTest::mouseRelease(window, Qt::LeftButton, modifiers, end);
  if (modifiers.testFlag(Qt::ShiftModifier)) QTest::keyRelease(window, Qt::Key_Shift);
  QCoreApplication::processEvents();
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyGuideTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CanvasGuides"));
  QTemporaryDir settings_dir;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/CanvasGuidesTestHarness.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) {
    std::cerr << component.errorString().toStdString();
    return EXIT_FAILURE;
  }
  auto* window = qobject_cast<QQuickWindow*>(root.get());
  if (!window || !QTest::qWaitForWindowExposed(window) || !check(invoke(root.get(), "modelChecks")))
    return EXIT_FAILURE;
  drag(window, QPoint(13, 100), QPoint(277, 150), Qt::ShiftModifier);
  if (!check(guides(root.get(), 1, QStringLiteral("x"), 435))) return EXIT_FAILURE;
  drag(window, QPoint(150, 13), QPoint(250, 218), Qt::ShiftModifier);
  if (!check(guides(root.get(), 2, QStringLiteral("y"), 315))) return EXIT_FAILURE;
  drag(window, QPoint(278, 180), QPoint(342, 180), Qt::ShiftModifier);
  if (!check(guides(root.get(), 2, QStringLiteral("x"), 565))) return EXIT_FAILURE;
  drag(window, QPoint(343, 180), QPoint(13, 180));
  if (!check(guides(root.get(), 1, QStringLiteral("y"), 315))) return EXIT_FAILURE;
  // Rulers follow both pan and zoom; stored guides stay in document pixels.
  invoke(root.get(), "zoomAndPan");
  drag(window, QPoint(13, 100), QPoint(513, 150), Qt::ShiftModifier);
  if (!check(guides(root.get(), 2, QStringLiteral("x"), 435))) return EXIT_FAILURE;
  // Pointer cancellation must never commit a new guide.
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, QPoint(13, 100));
  QTest::mouseMove(window, QPoint(700, 200));
  QTest::keyClick(window, Qt::Key_Escape);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(700, 200));
  if (!check(guides(root.get(), 2, QStringLiteral("x"), 435))) return EXIT_FAILURE;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(13, 13));
  QTest::qWait(150);
  auto* popup = root->findChild<QObject*>(QStringLiteral("canvasGuideSettingsPopup"));
  if (!popup || !popup->property("visible").toBool()) {
    std::cerr << "Ruler corner should open the units and guides popup\n";
    return EXIT_FAILURE;
  }
  auto* unit_picker = root->findChild<QQuickItem*>(QStringLiteral("canvasGuideUnitPicker"));
  if (!unit_picker) return EXIT_FAILURE;
  const auto picker_center = unit_picker->mapToScene(QPointF(unit_picker->width() / 2, unit_picker->height() / 2));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, picker_center.toPoint());
  QTest::keyClick(window, Qt::Key_Down);
  QTest::keyClick(window, Qt::Key_Return);
  auto* state = root->findChild<QObject*>(QStringLiteral("guideState"));
  if (!state || state->property("unit").toString() != QStringLiteral("in")) {
    std::cerr << "Unit picker should switch rulers to inches through keyboard selection\n";
    return EXIT_FAILURE;
  }
  QTest::keyClick(window, Qt::Key_Escape);
  // Reload the production component against the same settings to prove persistence.
  root.reset();
  QCoreApplication::processEvents();
  root.reset(component.create());
  if (!root || !check(guides(root.get(), 2, QStringLiteral("x"), 435))) return EXIT_FAILURE;
  window = qobject_cast<QQuickWindow*>(root.get());
  if (const auto capture_path = qEnvironmentVariable("LOOKSY_GUIDES_CAPTURE"); !capture_path.isEmpty()) {
    invoke(root.get(), "prepareCapture");
    QTest::qWait(200);
    QTest::mouseMove(window, QPoint(900, 580));
    QTest::qWait(100);
    if (!window->grabWindow().save(capture_path)) return EXIT_FAILURE;
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, QPoint(13, 13));
    QTest::qWait(200);
    if (!window->grabWindow().save(capture_path + QStringLiteral(".settings.png"))) return EXIT_FAILURE;
  }
  std::cout << "Guide unit, drag, snapping, zoom, lock, cancel, settings and persistence checks passed\n";
  return EXIT_SUCCESS;
}
