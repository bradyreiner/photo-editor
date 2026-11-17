#include <QCoreApplication>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include <iostream>
#include <memory>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

namespace {

void settle() {
  for (int i = 0; i < 5; ++i)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
}

int fail(const char* message) {
  std::cerr << "FAIL: " << message << '\n';
  return 1;
}

QQuickItem* find_item(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = find_item(child, name)) return found;
  }
  return nullptr;
}

void pointer(QQuickWindow& window, QEvent::Type type, QPointF point,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
  const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
  QMouseEvent event(type, point, window.mapToGlobal(point.toPoint()), button,
                    buttons, modifiers);
  QCoreApplication::sendEvent(&window, &event);
  settle();
}

void click(QQuickWindow& window, QPointF point,
           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  pointer(window, QEvent::MouseButtonPress, point, modifiers);
  pointer(window, QEvent::MouseButtonRelease, point, modifiers);
}

void drag(QQuickWindow& window, QPointF start, QPointF finish,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  pointer(window, QEvent::MouseButtonPress, start, modifiers);
  for (int step = 1; step <= 12; ++step)
    pointer(window, QEvent::MouseMove,
            start + (finish - start) * (step / 12.0), modifiers);
  pointer(window, QEvent::MouseButtonRelease, finish, modifiers);
  settle();
}

QPointF center(QQuickItem* item) {
  return item->mapToScene(item->boundingRect().center());
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  const QString component_root = QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/");

  QQmlEngine engine;
  QObject::connect(&engine, &QQmlEngine::warnings,
                   [](const QList<QQmlError>& errors) {
    for (const auto& warning : errors)
      std::cerr << warning.toString().toStdString() << '\n';
  });

  const QString fixture = QStringLiteral(R"QML(
pragma ComponentBehavior: Bound
import QtQuick
import "." as Components

Item {
    id: root
    width: 440
    height: 270
    property int activationCount: 0
    property int dropCount: 0
    property string lastSourceLayerId: ""
    property string lastMaskSetId: ""
    property string lastTargetLayerId: ""
    property bool lastCopyRequested: false
    property bool lastVectorMask: false

    function routeDrop(drop, targetLayerId) {
        const source = drop.source
        if (!source) return
        lastSourceLayerId = String(source.sourceLayerId || "")
        lastMaskSetId = String(source.maskSetId || "")
        lastTargetLayerId = targetLayerId
        lastCopyRequested = source.copyRequested === true
        lastVectorMask = source.vectorMask === true
        dropCount += 1
        drop.acceptProposedAction()
    }

    Rectangle { anchors.fill: parent; color: "#14191f" }

    Components.LayerMaskThumbnail {
        objectName: "vectorMaskSource"
        x: 20; y: 18; width: 36; height: 36
        maskSetId: "vector-source-mask"
        sourceLayerId: "vector-source-layer"
        vectorMask: true
        maskName: "Vector mask"
        onActivated: root.activationCount += 1
    }
    Components.LayerMaskThumbnail {
        objectName: "rasterMaskSource"
        x: 20; y: 86; width: 36; height: 36
        maskSetId: "raster-source-mask"
        sourceLayerId: "raster-source-layer"
        maskName: "Layer mask"
        onActivated: root.activationCount += 1
    }

    Rectangle {
        x: 190; y: 16; width: 220; height: 62
        color: "#232b35"; border.color: "#64758b"; radius: 6
        Text { anchors.centerIn: parent; text: "Both mask types"; color: "white" }
        DropArea {
            objectName: "combinedMaskDropArea"
            anchors.fill: parent
            keys: ["looksy-layer-mask", "looksy-vector-mask"]
            onDropped: function(drop) { root.routeDrop(drop, "combined-target-layer") }
        }
    }
    Rectangle {
        x: 190; y: 96; width: 220; height: 62
        color: "#232b35"; border.color: "#64758b"; radius: 6
        Text { anchors.centerIn: parent; text: "Vector key only"; color: "white" }
        DropArea {
            objectName: "vectorMaskDropArea"
            anchors.fill: parent
            keys: ["looksy-vector-mask"]
            onDropped: function(drop) { root.routeDrop(drop, "vector-key-target-layer") }
        }
    }
    Rectangle {
        x: 190; y: 176; width: 220; height: 62
        color: "#232b35"; border.color: "#64758b"; radius: 6
        Text { anchors.centerIn: parent; text: "Raster key only"; color: "white" }
        DropArea {
            objectName: "rasterMaskDropArea"
            anchors.fill: parent
            keys: ["looksy-layer-mask"]
            onDropped: function(drop) { root.routeDrop(drop, "raster-key-target-layer") }
        }
    }
}
)QML");

  QQmlComponent component(&engine);
  component.setData(fixture.toUtf8(),
                    QUrl::fromLocalFile(component_root + QStringLiteral("LayerMaskThumbnailDragFixture.qml")));
  if (component.isError()) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Load fixture importing the production LayerMaskThumbnail component");
  }
  std::unique_ptr<QObject> root_object(component.create());
  auto* root = qobject_cast<QQuickItem*>(root_object.get());
  if (!root) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Create the production-component mask drag fixture");
  }

  QQuickWindow window;
  window.resize(440, 270);
  root->setParentItem(window.contentItem());
  root->setSize(QSizeF(440, 270));
  window.show();
  window.requestActivate();
  settle();

  auto* vector_source = find_item(root, QStringLiteral("vectorMaskSource"));
  auto* raster_source = find_item(root, QStringLiteral("rasterMaskSource"));
  auto* combined_drop = find_item(root, QStringLiteral("combinedMaskDropArea"));
  auto* vector_drop = find_item(root, QStringLiteral("vectorMaskDropArea"));
  auto* raster_drop = find_item(root, QStringLiteral("rasterMaskDropArea"));
  if (!vector_source || !raster_source || !combined_drop || !vector_drop || !raster_drop)
    return fail("Find production sources and routing DropAreas in the QQuickWindow fixture");

  const auto combined_keys = combined_drop->property("keys").toStringList();
  if (combined_keys != QStringList{QStringLiteral("looksy-layer-mask"),
                                   QStringLiteral("looksy-vector-mask")})
    return fail("The combined target accepts the production raster and vector mask keys");

  QSignalSpy vector_activated(vector_source, SIGNAL(activated()));
  QSignalSpy raster_activated(raster_source, SIGNAL(activated()));
  if (!vector_activated.isValid() || !raster_activated.isValid())
    return fail("Observe production mask-thumbnail activation signals");

  click(window, center(vector_source));
  if (vector_activated.size() != 1 || root->property("dropCount").toInt() != 0 ||
      vector_source->property("copyRequested").toBool())
    return fail("Click activates the vector-mask thumbnail without starting a copy drag");

  drag(window, center(vector_source), center(combined_drop));
  if (root->property("dropCount").toInt() != 1 ||
      !root->property("lastVectorMask").toBool() ||
      root->property("lastSourceLayerId").toString() != QStringLiteral("vector-source-layer") ||
      root->property("lastMaskSetId").toString() != QStringLiteral("vector-source-mask") ||
      root->property("lastTargetLayerId").toString() != QStringLiteral("combined-target-layer") ||
      root->property("lastCopyRequested").toBool())
    return fail("Pointer drag routes vector source identity and target through the combined key DropArea");

  drag(window, center(vector_source), center(vector_drop), Qt::AltModifier);
  if (root->property("dropCount").toInt() != 2 ||
      !root->property("lastVectorMask").toBool() ||
      root->property("lastTargetLayerId").toString() != QStringLiteral("vector-key-target-layer") ||
      !root->property("lastCopyRequested").toBool())
    return fail("Alt-drag sends the vector key and marks the source as a copy request");

  drag(window, center(vector_source), center(raster_drop));
  if (root->property("dropCount").toInt() != 2)
    return fail("A vector-mask source does not enter a raster-key-only DropArea");

  vector_source->setProperty("locked", true);
  drag(window, center(vector_source), center(combined_drop), Qt::AltModifier);
  if (root->property("dropCount").toInt() != 2 || vector_source->property("dragging").toBool())
    return fail("A locked production thumbnail cannot start a mask drag");
  vector_source->setProperty("locked", false);

  drag(window, center(raster_source), center(vector_drop));
  if (root->property("dropCount").toInt() != 2)
    return fail("A raster mask does not enter a vector-key-only DropArea");

  click(window, center(raster_source));
  if (raster_activated.size() != 1)
    return fail("Click activates the raster-mask thumbnail");
  drag(window, center(raster_source), center(raster_drop));
  if (root->property("dropCount").toInt() != 3 ||
      root->property("lastVectorMask").toBool() ||
      root->property("lastSourceLayerId").toString() != QStringLiteral("raster-source-layer") ||
      root->property("lastMaskSetId").toString() != QStringLiteral("raster-source-mask") ||
      root->property("lastTargetLayerId").toString() != QStringLiteral("raster-key-target-layer") ||
      root->property("lastCopyRequested").toBool())
    return fail("Default raster masks use the raster transfer key and preserve source identity");

  drag(window, center(raster_source), center(combined_drop));
  if (root->property("dropCount").toInt() != 4 ||
      root->property("lastVectorMask").toBool() ||
      root->property("lastTargetLayerId").toString() != QStringLiteral("combined-target-layer"))
    return fail("The combined production-style target continues to accept raster mask drags");

  std::cout << "Vector mask drag UI: activation, combined and typed key routing, source/target identity, Alt-copy and lock protection passed\n";
  return 0;
}
