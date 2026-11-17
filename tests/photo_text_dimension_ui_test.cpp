#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QJSValue>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QTest>
#include <QThread>
#include <QVariant>
#include <QVariantMap>
#include <QUrl>
#include <QSizeF>
#include <cmath>

#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"

#include <iostream>
#include <memory>
#include <stdexcept>

#ifndef PHOTO_EDITOR_SOURCE_DIR
#define PHOTO_EDITOR_SOURCE_DIR "."
#endif

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void settle(int milliseconds = 40) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

QQuickItem* find_item(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems())
    if (auto* match = find_item(child, name)) return match;
  return nullptr;
}

bool inside_panel(QQuickItem* panel, QQuickItem* item) {
  if (!panel || !item) return false;
  const auto bounds = item->mapRectToItem(panel,
      QRectF(0, 0, item->width(), item->height()));
  return bounds.left() >= -1 && bounds.right() <= panel->width() + 1;
}

QString bounds_description(QQuickItem* panel, QQuickItem* item) {
  if (!panel || !item) return QStringLiteral("missing item");
  const auto bounds = item->mapRectToItem(panel, item->boundingRect());
  return QStringLiteral("width=%1 bounds=[%2,%3 %4x%5]")
      .arg(item->width()).arg(bounds.x()).arg(bounds.y())
      .arg(bounds.width()).arg(bounds.height());
}

void capture_inspector_proofs(QQuickWindow& window, QObject* fixtureObject,
                              QObject* panel, QObject* settings) {
  const auto outputPath = qEnvironmentVariable("PHOTO_EDITOR_TEXT_DIMENSION_PROOF_DIR");
  if (outputPath.isEmpty()) return;
  QDir outputDirectory(outputPath);
  require(QDir().mkpath(outputPath), "Could not create the text dimension proof directory.");

  auto* fixture = qobject_cast<QQuickItem*>(fixtureObject);
  auto* overlay = fixtureObject
      ? fixtureObject->findChild<QObject*>(QStringLiteral("photoTextDimensionOverlay"))
      : nullptr;
  require(fixture && panel && settings && overlay,
          "Proof captures require the complete text dimension fixture.");

  const auto originalWindowSize = window.size();
  const auto originalScale = settings->property("interfaceScale");
  const auto originalPanelWidth = panel->property("width");
  const auto originalPanelHeight = panel->property("height");
  const auto originalFixtureWidth = fixtureObject->property("panelWidth");
  const auto originalFixtureSize = fixture->size();
  const auto originalOverlayActive = overlay->property("active");
  const auto originalShapeExpanded = panel->property("shapeExpanded");
  const auto originalViewExpanded = panel->property("viewExpanded");
  const auto originalSurfaceExpanded = panel->property("surfaceExpanded");

  overlay->setProperty("active", false);
  const struct CaptureCase {
    qreal width;
    qreal scale;
    const char* section;
  } cases[] = {
      {360.0, 1.0, "shape"}, {360.0, 1.0, "view"}, {360.0, 1.0, "surface"},
      {280.0, 2.0, "shape"}, {280.0, 2.0, "view"}, {280.0, 2.0, "surface"}};
  for (const auto& capture : cases) {
    settings->setProperty("interfaceScale", capture.scale);
    fixtureObject->setProperty("panelWidth", capture.width);
    panel->setProperty("width", capture.width);
    fixture->setSize(QSizeF(capture.width, 2400.0));
    window.resize(static_cast<int>(capture.width), 2400);
    panel->setProperty("shapeExpanded", QString::fromLatin1(capture.section) == QStringLiteral("shape"));
    panel->setProperty("viewExpanded", QString::fromLatin1(capture.section) == QStringLiteral("view"));
    panel->setProperty("surfaceExpanded", QString::fromLatin1(capture.section) == QStringLiteral("surface"));
    settle(80);
    const auto panelHeight = panel->property("implicitHeight").toReal();
    const int captureHeight = static_cast<int>(std::ceil(
        panelHeight > 0 ? panelHeight + 24.0 : 1500.0));
    panel->setProperty("height", captureHeight);
    fixture->setSize(QSizeF(capture.width, captureHeight));
    window.resize(static_cast<int>(capture.width), captureHeight);
    settle(40);
    const auto image = window.grabWindow();
    const auto fileName = QStringLiteral("text-dimension-%1-%2-%3.png")
        .arg(capture.width).arg(capture.scale).arg(QString::fromLatin1(capture.section));
    require(!image.isNull() && image.save(outputDirectory.filePath(fileName)),
            "Could not save a text dimension proof capture.");
  }

  settings->setProperty("interfaceScale", originalScale);
  fixtureObject->setProperty("panelWidth", originalFixtureWidth);
  panel->setProperty("width", originalPanelWidth);
  panel->setProperty("height", originalPanelHeight);
  fixture->setSize(originalFixtureSize);
  window.resize(originalWindowSize);
  panel->setProperty("shapeExpanded", originalShapeExpanded);
  panel->setProperty("viewExpanded", originalViewExpanded);
  panel->setProperty("surfaceExpanded", originalSurfaceExpanded);
  overlay->setProperty("active", originalOverlayActive);
  settle();
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

void drag(QQuickWindow& window, QPointF from, QPointF to,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  pointer(window, QEvent::MouseButtonPress, from, modifiers);
  for (int step = 1; step <= 5; ++step)
    pointer(window, QEvent::MouseMove, from + (to - from) * (step / 5.0), modifiers);
  pointer(window, QEvent::MouseButtonRelease, to, modifiers);
}

bool invoke_qml(QObject* object, const char* method, QVariant argument,
                QVariant* result = nullptr) {
  if (!object) return false;
  QVariant returnValue;
  const bool invoked = QMetaObject::invokeMethod(
      object, method, Q_RETURN_ARG(QVariant, returnValue),
      Q_ARG(QVariant, argument));
  if (invoked && result) *result = returnValue;
  return invoked;
}

bool invoke_qml_noarg(QObject* object, const char* method, QVariant* result = nullptr) {
  if (!object) return false;
  QVariant returnValue;
  const bool invoked = QMetaObject::invokeMethod(
      object, method, Q_RETURN_ARG(QVariant, returnValue));
  if (invoked && result) *result = returnValue;
  return invoked;
}

bool invoke_qml_void(QObject* object, const char* method) {
  return object && QMetaObject::invokeMethod(object, method);
}

bool activate(QQuickItem* item) {
  return item && QMetaObject::invokeMethod(item, "click");
}

QVariant unpack(QVariant value) {
  return value.metaType() == QMetaType::fromType<QJSValue>()
             ? value.value<QJSValue>().toVariant()
             : value;
}

QVariantMap dimension_map(QObject* controller) {
  return controller ? unpack(controller->property("textDimension")).toMap()
                    : QVariantMap{};
}

QString fixture_qml() {
  const auto components = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString();
  return QStringLiteral(R"QML(
import QtQuick
import QtQuick.Layouts
import "%1" as Components

Item {
    id: harness
    objectName: "photoTextDimensionFixture"
    width: panelWidth
    height: 1500
    property real panelWidth: 480
    property string targetId: "layer-one"
    property string documentToken: "document-one"
    property int panelRevision: 1
    property int authoritativeRevision: 1
    property bool hasAuthoredEnvelope: false
    property string overlayRevision: "canvas-revision-one"
    property string lastRequestedMode: ""
    property string lastTargetMode: ""
    property string mappedMode: ""
    property int mappedModifiers: 0
    property int beginCount: 0
    property int previewCount: 0
    property int finishCount: 0
    property int historyCount: 0
    property bool transactionOpen: false
    property bool lastCommit: false
    property var previewPatch: ({})
    property var unrelatedText: "Keep this title editable"
    property var activeController: controller
    property var geometry: ({
        valid: true,
        outline: [{x: 90, y: 80}, {x: 310, y: 80}, {x: 310, y: 150}, {x: 90, y: 150}],
        handles: {
            rotate: {x: 200, y: 55},
            depth: {x: 340, y: 115},
            corners: {
                topLeft: {x: 90, y: 80}, topRight: {x: 310, y: 80},
                bottomRight: {x: 310, y: 150}, bottomLeft: {x: 90, y: 150}
            }
        },
        overlayImageX: 0, overlayImageY: 0, overlayImageWidth: 520, overlayImageHeight: 360
    })

    QtObject {
        id: controller
        objectName: "dimensionController"
        property string assetId: harness.documentToken
        property bool locked: false
        property int setCalls: 0
        property int beginCoalescedCount: 0
        property int endCoalescedCount: 0
        property var lastPatch: ({})
        property var textDimension: ({
            version: 1, enabled: false, depth: 32, bevelWidth: 2, bevelSegments: 3,
            rotationX: 0, rotationY: 0, rotationZ: 0,
            projection: "perspective", focalLength: 1000,
            useTextColor: true, frontColor: "#fff6f1e8", sideColor: "#ff353941",
            bevelColor: "#ffe6e8ec", roughness: 0.42, metallic: 0.08,
            lightAzimuth: -38, lightElevation: 35, lightIntensity: 1.1, ambient: 0.28,
            cornerPinEnabled: false,
            cornerPins: [
                {x: 0, y: 0}, {x: 1, y: 0},
                {x: 1, y: 1}, {x: 0, y: 1}
            ]
        })
        function photoLayerTextDimension(index) {
            return index === 0 ? textDimension : ({})
        }
        function photoTextDimensionTargetRevision(index) {
            return index === 0 ? String(harness.authoritativeRevision) : ""
        }
        function setPhotoLayerTextDimension(index, patch) {
            if (index !== 0 || locked || !patch || typeof patch !== "object") return false
            setCalls += 1
            lastPatch = patch
            textDimension = Object.assign({}, textDimension, patch)
            harness.hasAuthoredEnvelope = true
            harness.authoritativeRevision += 1
            Qt.callLater(function() { harness.panelRevision = harness.authoritativeRevision })
            return true
        }
        function beginCoalescedEdit() { beginCoalescedCount += 1 }
        function endCoalescedEdit() { endCoalescedCount += 1 }
    }

    QtObject {
        id: replacementController
        objectName: "replacementDimensionController"
        property string assetId: harness.documentToken
        property int setCalls: 0
        property int endCoalescedCount: 0
        property var textDimension: controller.textDimension
        function photoLayerTextDimension(index) { return index === 0 ? textDimension : ({}) }
        function photoTextDimensionTargetRevision(index) {
            return index === 0 ? String(harness.authoritativeRevision) : ""
        }
        function setPhotoLayerTextDimension(index, patch) { setCalls += 1; return false }
        function beginCoalescedEdit() { }
        function endCoalescedEdit() { endCoalescedCount += 1 }
    }

    property var layerData: hasAuthoredEnvelope
        ? ({id: targetId, textDimension: controller.textDimension})
        : ({id: targetId})

    Components.PhotoTextDimensionPanel {
        id: panel
        objectName: "photoTextDimensionPanel"
        width: harness.panelWidth
        controller: harness.activeController
        layerIndex: 0
        layerData: harness.layerData
        locked: controller.locked
        documentToken: harness.documentToken
        revision: String(harness.panelRevision)
        onCanvasEditRequested: function(mode) { harness.lastRequestedMode = mode }
        onCanvasTargetModeRequested: function(mode) { harness.lastTargetMode = mode }
    }

    Components.PhotoTextDimensionOverlay {
        id: overlay
        objectName: "photoTextDimensionOverlay"
        anchors.fill: parent
        z: 20
        active: true
        editable: true
        locked: controller.locked
        panActive: false
        controller: controller
        documentToken: harness.documentToken
        layerIndex: 0
        targetLayerId: harness.targetId
        revision: harness.overlayRevision
        mode: "rotate"
        dimension: controller.textDimension
        geometryProvider: function(index, layerId, documentToken, revision) {
            return Object.assign({}, harness.geometry, {
                valid: index === 0,
                layerId: layerId,
                documentToken: documentToken,
                revision: revision,
                dimension: controller.textDimension
            })
        }
        imageX: 0
        imageY: 0
        imageWidth: 520
        imageHeight: 360
        mapViewToDimensionPatch: function(mode, x, y, startDimension, geometry, modifiers) {
            harness.mappedMode = mode
            harness.mappedModifiers = modifiers
            const dx = x - Number(geometry.pressViewX)
            const dy = y - Number(geometry.pressViewY)
            if (mode === "rotate") {
                let rotationX = Number(startDimension.rotationX) + dy
                let rotationY = Number(startDimension.rotationY) + dx
                if (modifiers & Qt.ShiftModifier) {
                    rotationX = Math.round(rotationX / 15) * 15
                    rotationY = Math.round(rotationY / 15) * 15
                }
                return {rotationX: rotationX, rotationY: rotationY}
            }
            if (mode === "depth")
                return {depth: Math.max(0, Number(startDimension.depth) + Math.hypot(dx, dy))}
            if (mode.indexOf("corner-") === 0) {
                const key = mode.slice("corner-".length)
                const pins = JSON.parse(JSON.stringify(startDimension.cornerPins))
                const index = ["topLeft", "topRight", "bottomRight", "bottomLeft"].indexOf(key)
                pins[index] = {x: (x - 90) / 220, y: (y - 80) / 70}
                return {cornerPins: pins}
            }
            return null
        }
        beginGesture: function(index, layerId, token, revision) {
            if (harness.transactionOpen || index !== 0 || layerId !== harness.targetId ||
                    token !== harness.documentToken || revision !== harness.overlayRevision)
                return false
            harness.beginCount += 1
            harness.transactionOpen = true
            harness.previewPatch = ({})
            return true
        }
        previewGesture: function(index, layerId, patch, token, revision) {
            if (!harness.transactionOpen || index !== 0 || layerId !== harness.targetId ||
                    token !== harness.documentToken || revision !== harness.overlayRevision)
                return false
            harness.previewCount += 1
            harness.previewPatch = patch
            return true
        }
        finishGesture: function(index, layerId, commit, token, revision) {
            if (!harness.transactionOpen || index !== 0 || token !== harness.documentToken ||
                    revision !== harness.overlayRevision || (commit && layerId !== harness.targetId))
                return false
            harness.finishCount += 1
            harness.lastCommit = commit
            if (commit) {
                controller.textDimension = Object.assign({}, controller.textDimension,
                                                        harness.previewPatch)
                harness.historyCount += 1
            }
            harness.transactionOpen = false
            return true
        }
    }
}
)QML").arg(components);
}

QString real_controller_fixture_qml() {
  const auto components = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString();
  return QStringLiteral(R"QML(
import QtQuick
import QtQuick.Layouts
import "%1" as Components

Item {
    id: harness
    objectName: "realTextDimensionFixture"
    width: 480
    height: 1800
    property int controllerRevision: 0
    readonly property var targetLayerData: {
        const refresh = controllerRevision
        const layers = dimensionController.photoLayers || []
        return layers.length > 0 ? layers[0] : ({})
    }
    readonly property string targetRevision: {
        const refresh = controllerRevision
        return String(dimensionController.photoTextDimensionTargetRevision(0) || "")
    }

    Connections {
        target: dimensionController
        function onControlsChanged() { harness.controllerRevision += 1 }
        function onHistoryChanged() { harness.controllerRevision += 1 }
    }

    Components.PhotoTextDimensionPanel {
        id: panel
        objectName: "realPhotoTextDimensionPanel"
        width: harness.width
        controller: dimensionController
        layerIndex: 0
        layerData: harness.targetLayerData
        locked: harness.targetLayerData.locked === true
        documentToken: String(dimensionController.assetId || "")
        revision: harness.targetRevision
    }
}
)QML").arg(components);
}

bool verify_real_controller_async_inspector(QQmlEngine& engine, const QString& imagePath) {
  QImage source(1000, 500, QImage::Format_RGBA8888);
  source.fill(QColor(QStringLiteral("#9d7654")));
  if (!source.save(imagePath))
    return false;

  photo_editor::desktop::EditorPreviewImageProvider imageProvider;
  photo_editor::desktop::EditorPreviewController controller;
  controller.setImageProvider(&imageProvider);
  controller.loadAsset(QStringLiteral("text-dimension-real-inspector"), imagePath,
                       {}, {}, {}, {}, {}, true);
  QElapsedTimer loadTimer;
  loadTimer.start();
  while ((controller.sourceImageWidth() <= 0 || controller.processing()) &&
         loadTimer.elapsed() < 10000)
    settle(10);
  if (controller.sourceImageWidth() <= 0 || controller.processing())
    return false;
  if (!controller.addTextLayer(QStringLiteral("Revision guarded"), QStringLiteral("Guard test")) ||
      controller.photoLayerCount() != 1 ||
      controller.photoLayers().value(0).toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("text"))
    return false;

  engine.rootContext()->setContextProperty(QStringLiteral("dimensionController"), &controller);
  QQmlComponent component(&engine);
  component.setData(real_controller_fixture_qml().toUtf8(), QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/photo-text-dimension-real-fixture.qml")));
  if (component.isError()) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
    engine.rootContext()->setContextProperty(QStringLiteral("dimensionController"),
                                               static_cast<QObject*>(nullptr));
    return false;
  }
  std::unique_ptr<QObject> fixtureObject(component.create());
  auto* fixture = qobject_cast<QQuickItem*>(fixtureObject.get());
  auto* panel = fixtureObject
      ? fixtureObject->findChild<QObject*>(QStringLiteral("realPhotoTextDimensionPanel"))
      : nullptr;
  if (!fixture || !panel) {
    std::cerr << component.errorString().toStdString() << '\n';
    fixtureObject.reset();
    engine.rootContext()->setContextProperty(QStringLiteral("dimensionController"),
                                               static_cast<QObject*>(nullptr));
    return false;
  }

  QQuickWindow window;
  window.resize(480, 1000);
  fixture->setParentItem(window.contentItem());
  fixture->setSize(QSizeF(480, 1800));
  window.show();
  window.requestActivate();
  settle(80);

  const auto historyBefore = controller.editHistoryCurrentIndex();
  QVariant result;
  const bool began = invoke_qml_noarg(panel, "beginDimensionEdit", &result) && result.toBool();
  const bool firstAccepted = began && invoke_qml(panel, "writePatch",
      QVariantMap{{QStringLiteral("depth"), 37.0}}, &result) && result.toBool();
  settle(80);  // Deliver scheduleControlsChanged after the setter has returned.
  const bool delayedNotificationPreservedSession = panel->property("editSessionOpen").toBool() &&
      !panel->property("editSessionInvalid").toBool() &&
      panel->property("revision").toString() ==
          controller.photoTextDimensionTargetRevision(0);
  const auto afterFirst = controller.photoLayerTextDimension(0);
  const bool firstValueAccepted = afterFirst.value(QStringLiteral("depth")).toDouble() == 37.0;
  const bool secondAccepted = delayedNotificationPreservedSession && invoke_qml(panel, "writePatch",
      QVariantMap{{QStringLiteral("depth"), 38.0}}, &result) && result.toBool();
  const bool thirdAccepted = secondAccepted && invoke_qml(panel, "writePatch",
      QVariantMap{{QStringLiteral("bevelWidth"), 2.5}}, &result) && result.toBool();
  const bool valuesAcceptedBeforeSignals = thirdAccepted &&
      controller.photoLayerTextDimension(0).value(QStringLiteral("depth")).toDouble() == 38.0 &&
      controller.photoLayerTextDimension(0).value(QStringLiteral("bevelWidth")).toDouble() == 2.5;
  const bool ended = invoke_qml_void(panel, "endDimensionEdit");
  settle(100);
  const auto finalDimension = controller.photoLayerTextDimension(0);
  const bool oneHistoryEntry = controller.editHistoryCurrentIndex() == historyBefore + 1;
  const bool accepted = began && firstAccepted && delayedNotificationPreservedSession &&
      firstValueAccepted && secondAccepted && thirdAccepted && valuesAcceptedBeforeSignals && ended &&
      !panel->property("editSessionOpen").toBool() && !panel->property("editSessionInvalid").toBool() &&
      finalDimension.value(QStringLiteral("depth")).toDouble() == 38.0 &&
      finalDimension.value(QStringLiteral("bevelWidth")).toDouble() == 2.5 && oneHistoryEntry;
  if (!accepted) {
    std::cerr << "Real inspector transaction failed: began=" << began
              << " first=" << firstAccepted << " delayed-session=" << delayedNotificationPreservedSession
              << " second=" << secondAccepted << " third=" << thirdAccepted
              << " ended=" << ended << " history=" << historyBefore << "->"
              << controller.editHistoryCurrentIndex() << " revision="
              << panel->property("revision").toString().toStdString() << "/"
              << controller.photoTextDimensionTargetRevision(0).toStdString() << '\n';
  }

  window.hide();
  fixtureObject.reset();
  engine.rootContext()->setContextProperty(QStringLiteral("dimensionController"),
                                             static_cast<QObject*>(nullptr));
  return accepted;
}

void verify_panel(QQuickWindow& window, QObject* fixture, QObject* controller,
                  QObject* panel, QObject* settings) {
  QVariant result;
  auto* enable3d = find_item(qobject_cast<QQuickItem*>(panel),
                             QStringLiteral("photoTextDimensionEnabled"));
  require(activate(enable3d), "The explicit 3D opt-in switch must activate.");
  auto dimension = dimension_map(controller);
  require(dimension.value(QStringLiteral("enabled")).toBool() &&
              dimension.value(QStringLiteral("depth")).toDouble() == 32.0 &&
              dimension.value(QStringLiteral("bevelWidth")).toDouble() == 2.0 &&
              dimension.value(QStringLiteral("rotationX")).toDouble() == -12.0 &&
              dimension.value(QStringLiteral("rotationY")).toDouble() == -18.0,
          "First 3D opt-in writes the safe depth, bevel, and tilted view defaults.");
  require(dimension.value(QStringLiteral("version")).toInt() == 1 &&
              !unpack(controller->property("lastPatch")).toMap().contains(QStringLiteral("version")),
          "Dimension patches preserve the versioned state without rewriting its schema marker.");
  auto* focalDistance = find_item(qobject_cast<QQuickItem*>(panel),
                                  QStringLiteral("photoTextDimensionFocalLength"));
  require(focalDistance && focalDistance->property("from").toDouble() == 100.0 &&
              focalDistance->property("to").toDouble() == 10000.0 &&
              focalDistance->property("defaultValue").toDouble() == 1000.0,
          "Perspective uses the controller's document-pixel focal-distance scale.");

  require(invoke_qml(panel, "applyPreset", 1, &result) && result.toBool(),
          "The editable beveled metal recipe must apply through the controller patch path.");
  dimension = dimension_map(controller);
  require(dimension.value(QStringLiteral("enabled")).toBool() &&
              std::abs(dimension.value(QStringLiteral("metallic")).toDouble() - 0.88) < 0.001 &&
              dimension.value(QStringLiteral("bevelProfile")).toString() == QStringLiteral("chamfer") &&
              fixture->property("unrelatedText").toString() ==
                  QStringLiteral("Keep this title editable"),
          "Presets update only the dimensional recipe while retaining editable text content.");

  auto* resetShape = find_item(qobject_cast<QQuickItem*>(panel),
                               QStringLiteral("photoTextDimensionResetShape"));
  require(activate(resetShape), "The shape reset action must be available and activatable.");
  dimension = dimension_map(controller);
  require(dimension.value(QStringLiteral("depth")).toDouble() == 32.0 &&
              dimension.value(QStringLiteral("bevelWidth")).toDouble() == 2.0 &&
              std::abs(dimension.value(QStringLiteral("metallic")).toDouble() - 0.88) < 0.001 &&
              fixture->property("unrelatedText").toString() ==
                  QStringLiteral("Keep this title editable"),
          "Shape reset restores depth and bevel without resetting surface finish or text.");

  auto* rotateMode = find_item(qobject_cast<QQuickItem*>(panel),
                               QStringLiteral("photoTextDimensionCanvasRotate"));
  require(activate(rotateMode) && fixture->property("lastRequestedMode").toString() ==
              QStringLiteral("rotate"),
          "The inspector exposes a visible rotate-on-canvas action.");
  auto* targetMode = find_item(qobject_cast<QQuickItem*>(panel),
                               QStringLiteral("photoTextDimensionTargetModeAction"));
  require(activate(targetMode) && fixture->property("lastTargetMode").toString() ==
              QStringLiteral("text"),
          "The target strip offers a direct route back to editable text.");

  require(invoke_qml(panel, "writePatch",
                     QVariantMap{{QStringLiteral("depth"), 77.0},
                                 {QStringLiteral("rotationX"), 15.0},
                                 {QStringLiteral("rotationY"), -25.0}}, &result) && result.toBool(),
          "The mock can author a custom envelope before toggling dimensional rendering off.");
  require(activate(enable3d), "The 3D switch must turn rendering off without deleting its envelope.");
  dimension = dimension_map(controller);
  require(!dimension.value(QStringLiteral("enabled")).toBool() &&
              dimension.value(QStringLiteral("depth")).toDouble() == 77.0 &&
              dimension.value(QStringLiteral("rotationX")).toDouble() == 15.0,
          "Disabling 3D retains its configured depth and rotation.");
  require(activate(enable3d), "The 3D switch must re-enable an authored envelope.");
  dimension = dimension_map(controller);
  require(dimension.value(QStringLiteral("enabled")).toBool() &&
              dimension.value(QStringLiteral("depth")).toDouble() == 77.0 &&
              dimension.value(QStringLiteral("rotationX")).toDouble() == 15.0 &&
              dimension.value(QStringLiteral("rotationY")).toDouble() == -25.0,
          "Re-enabling 3D preserves the exact authored envelope.");

  const auto beginCount = controller->property("beginCoalescedCount").toInt();
  const auto endCount = controller->property("endCoalescedCount").toInt();
  const auto setCallsBeforeCoalesced = controller->property("setCalls").toInt();
  require(invoke_qml_noarg(panel, "beginDimensionEdit", &result) && result.toBool() &&
              invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 41.0}}, &result) &&
              result.toBool(),
          "The asynchronous inspector transaction must accept its first write.");
  settle();
  require(panel->property("editSessionOpen").toBool() &&
              !panel->property("editSessionInvalid").toBool() &&
              panel->property("revision").toString() ==
                  fixture->property("authoritativeRevision").toString(),
          "A delayed own-revision notification must keep the captured inspector edit alive.");
  require(invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 42.0}}, &result) &&
              result.toBool() &&
              invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 43.0}}, &result) &&
              result.toBool() && invoke_qml_void(panel, "endDimensionEdit"),
          "Rapid writes before the next notification must share one coalesced controller transaction.");
  settle();
  require(controller->property("beginCoalescedCount").toInt() == beginCount + 1 &&
              controller->property("endCoalescedCount").toInt() == endCount + 1 &&
              controller->property("setCalls").toInt() == setCallsBeforeCoalesced + 3 &&
              dimension_map(controller).value(QStringLiteral("depth")).toDouble() == 43.0,
          "Scheduled and coalesced own revisions must preserve every accepted inspector update.");

  auto* replacementController = fixture->findChild<QObject*>(
      QStringLiteral("replacementDimensionController"));
  require(replacementController != nullptr,
          "The controller replacement fixture must be available for captured-target checks.");
  const auto endsBeforeControllerSwitch = controller->property("endCoalescedCount").toInt();
  const auto replacementEndsBeforeSwitch = replacementController->property("endCoalescedCount").toInt();
  const auto replacementWritesBeforeSwitch = replacementController->property("setCalls").toInt();
  require(invoke_qml_noarg(panel, "beginDimensionEdit", &result) && result.toBool() &&
              invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 44.0}}, &result) &&
              result.toBool(),
          "A controller replacement scenario must begin with one accepted captured-controller write.");
  fixture->setProperty("activeController", QVariant::fromValue(replacementController));
  settle();
  require(invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 45.0}}, &result) &&
              !result.toBool() && invoke_qml_void(panel, "endDimensionEdit") &&
              controller->property("endCoalescedCount").toInt() == endsBeforeControllerSwitch + 1 &&
              replacementController->property("endCoalescedCount").toInt() == replacementEndsBeforeSwitch &&
              replacementController->property("setCalls").toInt() == replacementWritesBeforeSwitch,
          "A controller switch closes the captured controller transaction and rejects redirected writes.");
  fixture->setProperty("activeController", QVariant::fromValue(controller));
  settle();

  const auto setCallsBeforeStaleDrag = controller->property("setCalls").toInt();
  require(invoke_qml_noarg(panel, "beginDimensionEdit", &result) && result.toBool(),
          "A second inspector gesture must capture its original target.");
  fixture->setProperty("targetId", QStringLiteral("layer-two"));
  settle();
  require(invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 99.0}}, &result) &&
              !result.toBool() && invoke_qml_void(panel, "endDimensionEdit") &&
              controller->property("setCalls").toInt() == setCallsBeforeStaleDrag,
          "A delayed inspector release cannot write into a newly selected layer.");
  fixture->setProperty("targetId", QStringLiteral("layer-one"));
  settle();

  const auto setCallsBeforeExternalRevision = controller->property("setCalls").toInt();
  require(invoke_qml_noarg(panel, "beginDimensionEdit", &result) && result.toBool(),
          "A revision guard scenario captures the current inspector target.");
  fixture->setProperty("authoritativeRevision",
                       fixture->property("authoritativeRevision").toInt() + 1);
  fixture->setProperty("panelRevision", fixture->property("panelRevision").toInt() + 1);
  settle();
  require(invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("depth"), 101.0}}, &result) &&
              !result.toBool() && invoke_qml_void(panel, "endDimensionEdit") &&
              controller->property("setCalls").toInt() == setCallsBeforeExternalRevision,
          "An external same-layer revision rejects a stale in-flight inspector update.");

  require(invoke_qml(panel, "captureColorDialogTarget", QStringLiteral("sideColor"), &result) &&
              result.toBool(),
          "The color chooser must capture a target identity before it opens.");
  fixture->setProperty("targetId", QStringLiteral("layer-two"));
  settle();
  require(invoke_qml_noarg(panel, "dialogTargetMatches", &result) && !result.toBool(),
          "A held color dialog must reject a stale layer target.");
  fixture->setProperty("targetId", QStringLiteral("layer-one"));
  settle();

  const auto setCallsBeforeLock = controller->property("setCalls").toInt();
  controller->setProperty("locked", true);
  settle();
  require(invoke_qml(panel, "setEnabled", false, &result) && !result.toBool() &&
              controller->property("setCalls").toInt() == setCallsBeforeLock,
          "Locking the text layer must make dimension writes inert.");
  controller->setProperty("locked", false);
  settle();

  require(invoke_qml(panel, "writePatch", QVariantMap{{QStringLiteral("cornerPinEnabled"), true}}, &result) &&
              result.toBool(),
          "The responsive review enables the four normalized corner fields.");
  panel->setProperty("shapeExpanded", true);
  panel->setProperty("viewExpanded", true);
  panel->setProperty("surfaceExpanded", true);
  settle();
  for (const qreal scale : {1.0, 2.0}) {
    settings->setProperty("interfaceScale", scale);
    for (const qreal width : {280.0, 360.0, 480.0}) {
      fixture->setProperty("panelWidth", width);
      panel->setProperty("width", width);
      settle();
      auto* depth = find_item(qobject_cast<QQuickItem*>(panel),
                              QStringLiteral("photoTextDimension_depth"));
      auto* color = find_item(qobject_cast<QQuickItem*>(panel),
                              QStringLiteral("photoTextDimensionColor_sideColor"));
      auto* pin = find_item(qobject_cast<QQuickItem*>(panel),
                            QStringLiteral("photoTextDimensionPin_bottomRightY"));
      const bool fits = depth && color && pin && depth->width() <= width + 1 &&
          color->width() <= width + 1 && pin->width() <= width + 1 &&
          inside_panel(qobject_cast<QQuickItem*>(panel), depth) &&
          inside_panel(qobject_cast<QQuickItem*>(panel), color) &&
          inside_panel(qobject_cast<QQuickItem*>(panel), pin);
      if (!fits) {
        const auto diagnostic = QStringLiteral(
            "Responsive bounds failed at scale=%1 width=%2: depth {%3}, side color {%4}, pin {%5}")
            .arg(scale).arg(width)
            .arg(bounds_description(qobject_cast<QQuickItem*>(panel), depth))
            .arg(bounds_description(qobject_cast<QQuickItem*>(panel), color))
            .arg(bounds_description(qobject_cast<QQuickItem*>(panel), pin));
        const auto bytes = diagnostic.toUtf8();
        std::cerr << bytes.constData() << '\n';
        require(false,
                "Shape, surface, and corner controls must fit 280, 360, and 480 px at 1x and 2x.");
      }
    }
  }
  settings->setProperty("interfaceScale", 1.0);
  panel->setProperty("shapeExpanded", true);
  panel->setProperty("viewExpanded", false);
  panel->setProperty("surfaceExpanded", false);
  settle();
  capture_inspector_proofs(window, fixture, panel, settings);
}

void verify_overlay(QQuickWindow& window, QObject* fixture, QObject* controller,
                    QObject* overlay) {
  auto dimension = dimension_map(controller);
  const auto originalEnabled = dimension.value(QStringLiteral("enabled")).toBool();
  dimension.insert(QStringLiteral("enabled"), false);
  controller->setProperty("textDimension", QVariant::fromValue(dimension));
  overlay->setProperty("geometryRevision", overlay->property("geometryRevision").toInt() + 1);
  settle();
  require(!overlay->property("canEdit").toBool(),
          "The canvas overlay must not edit dormant 3D settings while dimensional rendering is off.");
  dimension.insert(QStringLiteral("enabled"), originalEnabled);
  controller->setProperty("textDimension", QVariant::fromValue(dimension));
  overlay->setProperty("geometryRevision", overlay->property("geometryRevision").toInt() + 1);
  settle();
  dimension = dimension_map(controller);
  const auto originalRotationX = dimension.value(QStringLiteral("rotationX")).toDouble();
  pointer(window, QEvent::MouseButtonPress, QPointF(200, 55));
  pointer(window, QEvent::MouseMove, QPointF(226, 80), Qt::ShiftModifier);
  require(overlay->property("interactionActive").toBool() &&
              fixture->property("transactionOpen").toBool() &&
              fixture->property("previewCount").toInt() > 0 &&
              dimension_map(controller)
                      .value(QStringLiteral("rotationX")).toDouble() == originalRotationX,
          "Canvas rotation previews remain draft-only and use the controller mapper.");
  require(fixture->property("mappedMode").toString() == QStringLiteral("rotate") &&
              (fixture->property("mappedModifiers").toInt() &
               static_cast<int>(Qt::ShiftModifier)) != 0,
          "The mapper receives the selected rotation mode and Shift constraint state.");
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window, &escape);
  settle();
  require(!overlay->property("interactionActive").toBool() &&
              !fixture->property("lastCommit").toBool() &&
              dimension_map(controller)
                      .value(QStringLiteral("rotationX")).toDouble() == originalRotationX,
          "Escape cancels the preview without changing committed text dimensions.");
  pointer(window, QEvent::MouseButtonRelease, QPointF(226, 80));
  require(!overlay->property("interactionActive").toBool() &&
              !fixture->property("lastCommit").toBool() &&
              fixture->property("historyCount").toInt() == 0 &&
              dimension_map(controller)
                      .value(QStringLiteral("rotationX")).toDouble() == originalRotationX,
          "Releasing the canceled pointer must leave the draft and history unchanged.");

  drag(window, QPointF(200, 55), QPointF(230, 88));
  dimension = dimension_map(controller);
  const bool rotationCommitted = !overlay->property("interactionActive").toBool() &&
      fixture->property("historyCount").toInt() == 1 &&
      dimension.value(QStringLiteral("rotationX")).toDouble() != originalRotationX;
  if (!rotationCommitted) {
    const auto diagnostic = QStringLiteral(
        "Rotation gesture state: active=%1 canEdit=%2 mapped=%3 transaction=%4 previews=%5 finishes=%6 "
        "history=%7 commit=%8 mode=%9 rotationX=%10 original=%11")
        .arg(overlay->property("interactionActive").toBool())
        .arg(overlay->property("canEdit").toBool())
        .arg(fixture->property("mappedMode").toString())
        .arg(fixture->property("transactionOpen").toBool())
        .arg(fixture->property("previewCount").toInt())
        .arg(fixture->property("finishCount").toInt())
        .arg(fixture->property("historyCount").toInt())
        .arg(fixture->property("lastCommit").toBool())
        .arg(overlay->property("mode").toString())
        .arg(dimension.value(QStringLiteral("rotationX")).toDouble())
        .arg(originalRotationX);
    const auto bytes = diagnostic.toUtf8();
    std::cerr << bytes.constData() << '\n';
    require(false, "One committed rotation drag writes one history transaction.");
  }

  overlay->setProperty("mode", QStringLiteral("depth"));
  settle();
  const auto originalDepth = dimension_map(controller)
                                .value(QStringLiteral("depth")).toDouble();
  drag(window, QPointF(340, 115), QPointF(366, 132));
  dimension = dimension_map(controller);
  require(dimension.value(QStringLiteral("depth")).toDouble() > originalDepth &&
              fixture->property("historyCount").toInt() == 2,
          "The dedicated depth handle changes extrusion depth in one gesture.");

  overlay->setProperty("mode", QStringLiteral("corner-pin"));
  settle();
  dimension = dimension_map(controller);
  dimension.insert(QStringLiteral("cornerPinEnabled"), false);
  controller->setProperty("textDimension", QVariant::fromValue(dimension));
  overlay->setProperty("geometryRevision", overlay->property("geometryRevision").toInt() + 1);
  settle();
  require(!overlay->property("canEdit").toBool(),
          "Corner drag targets must be inert while direct corner pinning is disabled.");
  dimension.insert(QStringLiteral("cornerPinEnabled"), true);
  controller->setProperty("textDimension", QVariant::fromValue(dimension));
  overlay->setProperty("geometryRevision", overlay->property("geometryRevision").toInt() + 1);
  settle();
  drag(window, QPointF(90, 80), QPointF(76, 68));
  dimension = dimension_map(controller);
  const auto pins = dimension.value(QStringLiteral("cornerPins")).toList();
  require(fixture->property("mappedMode").toString() == QStringLiteral("corner-topLeft") &&
              pins.value(0).toMap().value(QStringLiteral("x")).toDouble() < 0 &&
              fixture->property("historyCount").toInt() == 3,
          "Four-corner placement edits the selected normalized pin through the mapper.");

  overlay->setProperty("mode", QStringLiteral("rotate"));
  settle();
  pointer(window, QEvent::MouseButtonPress, QPointF(200, 55));
  pointer(window, QEvent::MouseMove, QPointF(215, 72));
  require(overlay->property("interactionActive").toBool(),
          "A target change scenario starts with an active preview draft.");
  fixture->setProperty("targetId", QStringLiteral("layer-two"));
  settle();
  require(!overlay->property("interactionActive").toBool() &&
              !fixture->property("lastCommit").toBool() &&
              fixture->property("historyCount").toInt() == 3,
          "Changing the selected layer during a drag cancels the captured target safely.");

  pointer(window, QEvent::MouseButtonPress, QPointF(200, 55));
  pointer(window, QEvent::MouseMove, QPointF(218, 70));
  controller->setProperty("locked", true);
  settle();
  require(!overlay->property("interactionActive").toBool() &&
              !fixture->property("lastCommit").toBool() &&
              fixture->property("historyCount").toInt() == 3,
          "Locking the target during a canvas gesture cancels its preview.");
  controller->setProperty("locked", false);
  settle();

  pointer(window, QEvent::MouseButtonPress, QPointF(200, 55));
  pointer(window, QEvent::MouseMove, QPointF(216, 69));
  overlay->setProperty("panActive", true);
  settle();
  require(!overlay->property("interactionActive").toBool() &&
              !fixture->property("lastCommit").toBool() &&
              fixture->property("historyCount").toInt() == 3,
          "Starting canvas pan cancels the dimension preview without a commit.");
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtimeRoot;
  if (!runtimeRoot.isValid())
    return 1;
  const auto appDataRoot = runtimeRoot.filePath(QStringLiteral("app-data"));
  const auto cacheRoot = runtimeRoot.filePath(QStringLiteral("cache"));
  const auto logRoot = runtimeRoot.filePath(QStringLiteral("logs"));
  const auto observabilityRoot = runtimeRoot.filePath(QStringLiteral("observability"));
  const auto settingsRoot = runtimeRoot.filePath(QStringLiteral("settings"));
  if (!QDir().mkpath(appDataRoot) || !QDir().mkpath(cacheRoot) ||
      !QDir().mkpath(logRoot) || !QDir().mkpath(observabilityRoot) ||
      !QDir().mkpath(settingsRoot))
    return 1;
  QStandardPaths::setTestModeEnabled(true);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyTextDimensionUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoTextDimensionUiTest"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsRoot);
  QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settingsRoot);
  if (!qputenv("PHOTO_EDITOR_APP_DATA_DIR", appDataRoot.toUtf8()) ||
      !qputenv("PHOTO_EDITOR_APP_DATA_ROOT", appDataRoot.toUtf8()) ||
      !qputenv("PHOTO_EDITOR_CACHE_DIR", cacheRoot.toUtf8()) ||
      !qputenv("PHOTO_EDITOR_LOG_DIR", logRoot.toUtf8()) ||
      !qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", observabilityRoot.toUtf8()) ||
      !qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1") ||
      !qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", "1"))
    return 1;

  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent settingsComponent(&engine);
  settingsComponent.setData(R"QML(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
})QML", QUrl());
  std::unique_ptr<QObject> settings(settingsComponent.create());
  if (!settings) {
    std::cerr << settingsComponent.errorString().toStdString() << '\n';
    return 1;
  }
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), settings.get());

  QQmlComponent component(&engine);
  component.setData(fixture_qml().toUtf8(), QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/photo-text-dimension-fixture.qml")));
  if (component.isError()) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
    return 1;
  }
  std::unique_ptr<QObject> object(component.create());
  if (!object) {
    std::cerr << component.errorString().toStdString() << '\n';
    return 1;
  }
  auto* fixture = qobject_cast<QQuickItem*>(object.get());
  auto* controller = object->findChild<QObject*>(QStringLiteral("dimensionController"));
  auto* panel = object->findChild<QObject*>(QStringLiteral("photoTextDimensionPanel"));
  auto* overlay = object->findChild<QObject*>(QStringLiteral("photoTextDimensionOverlay"));
  require(fixture && controller && panel && overlay,
          "The production inspector and direct canvas overlay must instantiate with the mock controller.");

  QQuickWindow window;
  window.resize(520, 360);
  window.setColor(QColor(QStringLiteral("#191919")));
  fixture->setParentItem(window.contentItem());
  fixture->setSize(QSizeF(520, 1500));
  window.show();
  window.requestActivate();
  settle(100);

  try {
    verify_panel(window, object.get(), controller, panel, settings.get());
    controller->setProperty("locked", false);
    object->setProperty("targetId", QStringLiteral("layer-one"));
    panel->setProperty("width", 480.0);
    object->setProperty("panelWidth", 480.0);
    settle();
    verify_overlay(window, object.get(), controller, overlay);
    require(verify_real_controller_async_inspector(engine,
                runtimeRoot.filePath(QStringLiteral("text-dimension-real-source.png"))),
            "The production controller must preserve a coalesced inspector edit through delayed revision notifications.");
  } catch (const std::exception& exception) {
    std::cerr << exception.what() << '\n';
    return 1;
  }

  return 0;
}
