#include <QCoreApplication>
#include <QGuiApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
void settle() { for (int i = 0; i < 8; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 10); }
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QQuickItem* findQuickItem(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (QQuickItem* child : parent->childItems()) {
    if (QQuickItem* found = findQuickItem(child, name)) return found;
  }
  return nullptr;
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/LayerTransformOverlay.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* overlay = qobject_cast<QQuickItem*>(object.get());
  if (!overlay) { std::cerr << component.errorString().toStdString(); return fail("Transform overlay must load"); }
  QQuickWindow window;
  window.resize(2400, 1800);
  overlay->setParentItem(window.contentItem());
  overlay->setSize(QSizeF(2400, 1800));
  overlay->setProperty("active", true);
  overlay->setProperty("imageX", 100);
  overlay->setProperty("imageY", 100);
  overlay->setProperty("canvasWidth", 1000);
  overlay->setProperty("canvasHeight", 800);
  overlay->setProperty("layerX", 100);
  overlay->setProperty("layerY", 200);
  overlay->setProperty("layerWidth", 200);
  overlay->setProperty("layerHeight", 100);
  window.show();
  settle();
  QSignalSpy transformed(overlay, SIGNAL(transformEdited(int,int,double,double)));
  if (!transformed.isValid()) return fail("Transform signal must be observable");
  quint64 timestamp = 1000;
  const auto pointer = [&](QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, point, QPointF(window.mapToGlobal(point.toPoint())),
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
        type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
    event.setTimestamp(timestamp); timestamp += 16;
    QCoreApplication::sendEvent(&window, &event); settle();
  };
  const auto drag = [&](double scale, double x, double y, Qt::KeyboardModifiers modifiers) {
    transformed.clear();
    overlay->setProperty("imageWidth", 1000 * scale);
    overlay->setProperty("imageHeight", 800 * scale);
    settle();
    const QPointF start(100 + 200 * scale, 100 + 250 * scale);
    const QPointF end = start + QPointF((x - 100) * scale, (y - 200) * scale);
    timestamp += 500;
    pointer(QEvent::MouseButtonPress, start);
    pointer(QEvent::MouseMove, end, modifiers);
    return end;
  };
  for (double scale : {0.25, 0.5, 2.0}) {
    const auto end = drag(scale, 400 + 2 / scale, 200, Qt::NoModifier);
    if (transformed.isEmpty() || transformed.last().at(0).toInt() != 400 ||
        std::abs(overlay->property("snapGuideX").toDouble() - 500) > 0.01)
      return fail("Centers must snap at the same screen distance at every zoom");
    pointer(QEvent::MouseButtonRelease, end);
    if (std::isfinite(overlay->property("snapGuideX").toDouble()) || overlay->property("interactionActive").toBool())
      return fail("Snap guides must disappear at the end of a drag");
  }
  auto end = drag(0.5, 404, 200, Qt::AltModifier);
  if (transformed.isEmpty() || transformed.last().at(0).toInt() != 404 ||
      std::isfinite(overlay->property("snapGuideX").toDouble())) return fail("Alt must bypass snapping");
  pointer(QEvent::MouseButtonRelease, end, Qt::AltModifier);
  end = drag(0.5, 404, 260, Qt::ShiftModifier);
  if (transformed.isEmpty() || transformed.last().at(0).toInt() != 400 || transformed.last().at(1).toInt() != 200)
    return fail("Shift must constrain movement while preserving snapping on the moving axis");
  pointer(QEvent::MouseButtonRelease, end, Qt::ShiftModifier);
  overlay->setProperty("layerRotation", 90);
  end = drag(0.5, -46, 200, Qt::NoModifier);
  if (transformed.isEmpty() || transformed.last().at(0).toInt() != -50)
    return fail("Rotated bounding edges must snap without changing scale or angle");
  pointer(QEvent::MouseButtonRelease, end);
  overlay->setProperty("layerRotation", 0);
  overlay->setProperty("snappingEnabled", false);
  end = drag(0.5, 404, 200, Qt::NoModifier);
  if (transformed.isEmpty() || transformed.last().at(0).toInt() != 404) return fail("Disabling guides must preserve free movement");
  pointer(QEvent::MouseButtonRelease, end);
  overlay->setProperty("locked", true);
  end = drag(0.5, 404, 200, Qt::NoModifier);
  if (!transformed.isEmpty()) return fail("Locked layers must reject drag input");
  pointer(QEvent::MouseButtonRelease, end);
  std::cout << "Layer snapping: zoom, guides, Alt, Shift, rotated edges, free movement and locking passed\n";

  overlay->setProperty("locked", false);
  overlay->setProperty("snappingEnabled", false);
  overlay->setProperty("imageWidth", 1000);
  overlay->setProperty("imageHeight", 800);
  overlay->setProperty("layerX", 100);
  overlay->setProperty("layerY", 200);
  overlay->setProperty("layerWidth", 200);
  overlay->setProperty("layerHeight", 100);
  overlay->setProperty("layerScale", 0.8);
  overlay->setProperty("layerRotation", 37);
  overlay->setProperty("textBoxResize", true);
  settle();
  transformed.clear();
  QSignalSpy textBoxEdited(overlay, SIGNAL(textBoxEdited(int,int,int,int)));
  if (!textBoxEdited.isValid()) return fail("Text box resize signal must be observable");
  auto* rightHandle = findQuickItem(overlay, QStringLiteral("photoLayerTransformRightHandle"));
  auto* bottomRightHandle = findQuickItem(overlay, QStringLiteral("photoLayerTransformBottomRightHandle"));
  if (!rightHandle || !bottomRightHandle)
    return fail("Text boxes must expose edge and corner resize handles");
  const auto handleCenter = [](QQuickItem* item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
  };
  const auto rotatedDelta = [](double localX, double localY, double degrees) {
    constexpr double pi = 3.14159265358979323846;
    const double radians = degrees * pi / 180.0;
    return QPointF(localX * std::cos(radians) - localY * std::sin(radians),
                   localX * std::sin(radians) + localY * std::cos(radians));
  };

  // The right edge grows only the paragraph width, even when the text is
  // rotated and its editor scale is not 1.0. The opposite edge stays anchored.
  auto* frame = findQuickItem(overlay, QStringLiteral("photoLayerTransformFrame"));
  if (!frame) return fail("Text box frame must remain addressable during resize");
  const QPointF originalLeftCenter = frame->mapToScene(QPointF(0, frame->height() / 2));
  QPointF start = handleCenter(rightHandle);
  end = start + rotatedDelta(40, 0, 37);
  timestamp += 500;
  pointer(QEvent::MouseButtonPress, start);
  pointer(QEvent::MouseMove, end);
  if (textBoxEdited.size() != 1 || textBoxEdited.last().at(2).toInt() != 250 ||
      textBoxEdited.last().at(3).toInt() != 100 || !transformed.isEmpty())
    return fail("Text edge drag must resize only its box dimension without scaling text");
  pointer(QEvent::MouseButtonRelease, end);
  overlay->setProperty("layerX", textBoxEdited.last().at(0));
  overlay->setProperty("layerY", textBoxEdited.last().at(1));
  overlay->setProperty("layerWidth", textBoxEdited.last().at(2));
  overlay->setProperty("layerHeight", textBoxEdited.last().at(3));
  settle();
  frame = findQuickItem(overlay, QStringLiteral("photoLayerTransformFrame"));
  const QPointF resizedLeftCenter = frame->mapToScene(QPointF(0, frame->height() / 2));
  if (QLineF(originalLeftCenter, resizedLeftCenter).length() > 1.5 ||
      std::abs(overlay->property("layerScale").toDouble() - 0.8) > 0.001 ||
      std::abs(overlay->property("layerRotation").toDouble() - 37) > 0.001)
    return fail("Text edge resize must preserve its opposite anchor, scale, and rotation");

  // A free corner drag reflows both dimensions independently.
  bottomRightHandle = findQuickItem(overlay, QStringLiteral("photoLayerTransformBottomRightHandle"));
  start = handleCenter(bottomRightHandle);
  end = start + rotatedDelta(40, 16, 37);
  textBoxEdited.clear();
  timestamp += 500;
  pointer(QEvent::MouseButtonPress, start);
  pointer(QEvent::MouseMove, end);
  if (textBoxEdited.size() != 1 || textBoxEdited.last().at(2).toInt() != 300 ||
      textBoxEdited.last().at(3).toInt() != 120 || !transformed.isEmpty())
    return fail("Text corner drag must resize width and height independently");
  pointer(QEvent::MouseButtonRelease, end);

  // The compatibility scale helper is explicitly unavailable in text mode.
  QVariant helperResult = true;
  const bool invoked = QMetaObject::invokeMethod(overlay, "scaleFromViewportPoint",
      Q_RETURN_ARG(QVariant, helperResult),
      Q_ARG(QVariant, QVariant(200.0)), Q_ARG(QVariant, QVariant(200.0)));
  if (!invoked || helperResult.toBool() || !transformed.isEmpty())
    return fail("Text mode must reject direct scale-helper requests");

  auto* label = findQuickItem(overlay, QStringLiteral("photoLayerTransformLabel"));
  if (!label || !label->property("text").toString().contains(QStringLiteral("W ")) ||
      label->property("text").toString().contains(QStringLiteral("0.80x")))
    return fail("Text resize status must show box dimensions rather than a scale multiplier");
  std::cout << "Text box resize: rotated edge and corner drags preserve font scale and anchors\n";
  return 0;
}
