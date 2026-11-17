#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"

#include <QCoreApplication>
#include <QColor>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <functional>
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

void settle(int milliseconds = 50) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
}

void capture_canvas_proof(QQuickWindow& window, const QString& filename) {
  const auto directory = qEnvironmentVariable("PHOTO_EDITOR_STYLE_CANVAS_PROOF_DIR").trimmed();
  if (directory.isEmpty()) return;
  require(QDir().mkpath(directory), "The requested style canvas proof directory must exist.");
  settle(80);
  const auto image = window.grabWindow();
  const auto path = QDir(directory).filePath(filename);
  require(!image.isNull() && image.save(path, "PNG"),
          "The requested style canvas proof image must be captured.");
}

QVariant invoke_point(QObject* fixture, const char* method, double x, double y) {
  QVariant result;
  const bool invoked = QMetaObject::invokeMethod(
      fixture, method, Q_RETURN_ARG(QVariant, result),
      Q_ARG(QVariant, QVariant(x)), Q_ARG(QVariant, QVariant(y)));
  require(invoked && result.canConvert<QVariantMap>(),
          "The QML fixture must map the requested canvas point.");
  return result;
}

QPointF view_point(QObject* fixture, const char* method, double x, double y) {
  const auto mapped = invoke_point(fixture, method, x, y).toMap();
  require(mapped.value(QStringLiteral("valid")).toBool(),
          "The fixture point must remain inside its cropped canvas view.");
  return {mapped.value(QStringLiteral("x")).toDouble(),
          mapped.value(QStringLiteral("y")).toDouble()};
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

QString fixture_qml() {
  const auto components = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString();
  return QStringLiteral(R"QML(
import QtQuick
import "%1" as Components

Item {
    id: harness
    objectName: "photoLayerStyleCanvasFixture"
    width: 520
    height: 420
    property real sourceWidth: 1000
    property real sourceHeight: 800
    property real authoredWidth: 240
    property real authoredHeight: 160
    property real parentScaleX: 0.92
    property real parentScaleY: 1.08
    property real parentRotation: -19
    property real documentCenterX: 500
    property real documentCenterY: 400
    property string targetId: "layer-one"
    property var parameters: ({distance: 24, angleDegrees: 0, blur: 8, spread: 0,
        useGlobalLight: false, color: {r: 0, g: 0, b: 0, a: 0.65}})
    property var committedParameters: parameters
    property var latestPatch: ({})
    property int beginCount: 0
    property int previewCount: 0
    property int finishCount: 0
    property int historyCount: 0
    property bool lastCommit: false
    property bool transactionOpen: false

    function mapStyleToDocument(x, y) {
        const centerX = (authoredWidth - 1) * 0.5
        const centerY = (authoredHeight - 1) * 0.5
        const radians = parentRotation * Math.PI / 180
        const localX = (Number(x) - centerX) * parentScaleX
        const localY = (Number(y) - centerY) * parentScaleY
        return {
            x: documentCenterX + localX * Math.cos(radians) - localY * Math.sin(radians),
            y: documentCenterY + localX * Math.sin(radians) + localY * Math.cos(radians)
        }
    }
    function mapDocumentToStyle(x, y) {
        const radians = parentRotation * Math.PI / 180
        const dx = Number(x) - documentCenterX
        const dy = Number(y) - documentCenterY
        const localX = dx * Math.cos(radians) + dy * Math.sin(radians)
        const localY = -dx * Math.sin(radians) + dy * Math.cos(radians)
        return {
            x: (authoredWidth - 1) * 0.5 + localX / parentScaleX,
            y: (authoredHeight - 1) * 0.5 + localY / parentScaleY
        }
    }
    function mapStylePointToView(x, y) {
        const documentPoint = mapStyleToDocument(x, y)
        const mapped = canvasMapper.imageToViewPoint(
            documentPoint.x / (sourceWidth - 1), documentPoint.y / (sourceHeight - 1))
        return {valid: mapped.valid, x: mapped.x, y: mapped.y}
    }
    function mapViewPointToStyle(x, y) {
        const source = canvasMapper.viewToImagePoint(x, y)
        if (!source.valid)
            return {valid: false, x: 0, y: 0}
        const documentX = source.x * (sourceWidth - 1)
        const documentY = source.y * (sourceHeight - 1)
        const style = mapDocumentToStyle(documentX, documentY)
        return {valid: true, x: style.x, y: style.y}
    }

    Components.CanvasCoordinateMapper {
        id: canvasMapper
        contentX: harness.width * 0.08
        contentY: harness.height * 0.08
        contentWidth: harness.width * 0.84
        contentHeight: harness.height * 0.84
        cropX: 0.15
        cropY: 0.10
        cropWidth: 0.70
        cropHeight: 0.80
        rotationDegrees: 13
        coordinateSpace: "source"
    }

    Components.PhotoLayerStyleOverlay {
        id: styleOverlay
        objectName: "photoLayerStyleOverlay"
        anchors.fill: parent
        active: true
        editable: true
        layerIndex: 0
        targetLayerId: harness.targetId
        documentToken: "document-one"
        styleId: "style-one"
        styleType: "drop_shadow"
        styleParameters: harness.parameters
        styleRevision: "fixture-revision"
        globalLightAngleDegrees: 31
        styleWidth: harness.authoredWidth
        styleHeight: harness.authoredHeight
        styleScaleX: 1.25
        styleScaleY: 0.75
        imageX: 0
        imageY: 0
        imageWidth: harness.width
        imageHeight: harness.height
        mapViewToStylePoint: function(x, y) { return harness.mapViewPointToStyle(x, y) }
        mapStylePointToView: function(x, y) { return harness.mapStylePointToView(x, y) }
        beginGesture: function(index, layerId, targetStyleId) {
            if (index !== 0 || layerId !== harness.targetId || targetStyleId !== "style-one" ||
                    harness.transactionOpen)
                return false
            harness.beginCount += 1
            harness.transactionOpen = true
            harness.committedParameters = JSON.parse(JSON.stringify(harness.parameters))
            return true
        }
        previewGesture: function(index, layerId, targetStyleId, patch) {
            if (!harness.transactionOpen || index !== 0 || layerId !== harness.targetId ||
                    targetStyleId !== "style-one" || !patch.parameters)
                return false
            harness.previewCount += 1
            harness.latestPatch = patch
            return true
        }
        finishGesture: function(index, layerId, targetStyleId, commit) {
            if (!harness.transactionOpen || index !== 0 || targetStyleId !== "style-one")
                return false
            harness.finishCount += 1
            harness.lastCommit = commit
            if (commit) {
                harness.historyCount += 1
                harness.parameters = JSON.parse(JSON.stringify(harness.latestPatch.parameters))
            } else {
                harness.parameters = JSON.parse(JSON.stringify(harness.committedParameters))
            }
            harness.transactionOpen = false
            return true
        }
    }
}
)QML").arg(components);
}

QString controller_fixture_qml() {
  const auto components = QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components")).toString();
  return QStringLiteral(R"QML(
import QtQuick
import "%1" as Components

Item {
    id: harness
    objectName: "photoLayerStyleControllerFixture"
    width: 600
    height: 440
    property var controller: styleController
    property int layerIndex: 0
    property string styleId: targetStyleId
    property int contextRevision: 0
    readonly property var styleContext: {
        const revision = contextRevision
        return controller && styleId.length > 0
            ? controller.photoLayerStyleCanvasGeometry(layerIndex, styleId)
            : ({valid: false})
    }

    function mapStylePointToView(x, y) {
        if (!controller || width <= 0 || height <= 0)
            return {valid: false, x: 0, y: 0}
        const mapped = controller.mapPhotoLayerStyleCoordinate(layerIndex, x, y, true)
        return {valid: mapped.valid === true, x: Number(mapped.x) * width,
                y: Number(mapped.y) * height}
    }
    function mapViewPointToStyle(x, y) {
        if (!controller || width <= 0 || height <= 0)
            return {valid: false, x: 0, y: 0}
        return controller.mapPhotoLayerStyleCoordinate(layerIndex, x / width, y / height, false)
    }

    Connections {
        target: harness.controller
        ignoreUnknownSignals: true
        function onControlsChanged() { harness.contextRevision += 1 }
        function onPreviewChanged() { harness.contextRevision += 1 }
    }

    Components.PhotoLayerStyleOverlay {
        id: styleOverlay
        objectName: "photoLayerStyleOverlay"
        anchors.fill: parent
        active: styleContext.valid === true
        editable: styleContext.editable === true
        locked: styleContext.editable !== true
        documentToken: harness.controller ? String(harness.controller.assetId) : ""
        layerIndex: harness.layerIndex
        targetLayerId: String(styleContext.layerId || "")
        styleId: harness.styleId
        styleType: String(styleContext.styleType || "")
        styleParameters: styleContext.styleParameters || ({})
        styleRevision: String(styleContext.revision || "")
        globalLightAngleDegrees: Number(styleContext.globalLightAngleDegrees || 0)
        styleWidth: Number(styleContext.styleWidth || 0)
        styleHeight: Number(styleContext.styleHeight || 0)
        styleScaleX: Number(styleContext.styleScaleX || 1)
        styleScaleY: Number(styleContext.styleScaleY || 1)
        imageX: 0
        imageY: 0
        imageWidth: harness.width
        imageHeight: harness.height
        mapViewToStylePoint: function(x, y) { return harness.mapViewPointToStyle(x, y) }
        mapStylePointToView: function(x, y) { return harness.mapStylePointToView(x, y) }
        beginGesture: function(index, layerId, targetStyleId) {
            return harness.controller.beginPhotoLayerStyleGesture(index, layerId, targetStyleId)
        }
        previewGesture: function(index, layerId, targetStyleId, patch) {
            return harness.controller.previewPhotoLayerStyleGesture(index, layerId, targetStyleId, patch)
        }
        finishGesture: function(index, layerId, targetStyleId, commit) {
            return harness.controller.finishPhotoLayerStyleGesture(index, layerId, targetStyleId, commit)
        }
    }
}
)QML").arg(components);
}

void verify_shadow_gesture(QQuickWindow& window, QObject* fixture,
                           QObject* overlay, bool compact) {
  const auto center = QPointF(119.5, 79.5);
  const double startX = center.x() + 24.0 * 1.25;
  const double startY = center.y();
  const auto start = view_point(fixture, "mapStylePointToView", startX, startY);
  const double finishX = center.x() + 30.0 * 1.25;
  const double finishY = center.y() + 40.0 * 0.75;
  const auto finish = view_point(fixture, "mapStylePointToView", finishX, finishY);
  drag(window, start, finish);

  const auto patch = fixture->property("latestPatch").toMap();
  const auto parameters = patch.value(QStringLiteral("parameters")).toMap();
  const double expectedDistance = std::hypot(30.0, 40.0);
  const double expectedAngle = std::atan2(40.0, 30.0) * 180.0 / std::acos(-1.0);
  require(fixture->property("beginCount").toInt() == 1,
          "A shadow pointer gesture must open exactly one controller transaction.");
  require(fixture->property("previewCount").toInt() >= 1,
          "A shadow pointer gesture must preview parameter changes while dragging.");
  require(std::abs(parameters.value(QStringLiteral("distance")).toDouble() - expectedDistance) < 0.2 &&
              std::abs(parameters.value(QStringLiteral("angleDegrees")).toDouble() - expectedAngle) < 0.3,
          compact ? "Compact transformed/cropped input must author local shadow pixels."
                  : "Transformed/cropped input must author local shadow pixels.");
  require(parameters.value(QStringLiteral("blur")).toDouble() == 8.0 &&
              parameters.value(QStringLiteral("useGlobalLight")).toBool() == false,
          "A shadow gesture must preserve the remaining complete style parameters.");
  require(fixture->property("finishCount").toInt() == 1 &&
              fixture->property("lastCommit").toBool() &&
              fixture->property("historyCount").toInt() == 1,
          "A released shadow gesture must finish as one history transaction.");
  require(!overlay->property("interactionActive").toBool(),
          "A released shadow gesture must release pointer ownership.");
}

QVariantMap shadow_parameters(double distance, double angle, bool use_global_light = false) {
  return {{QStringLiteral("distance"), distance},
          {QStringLiteral("angleDegrees"), angle},
          {QStringLiteral("blur"), 8.0},
          {QStringLiteral("spread"), 0.0},
          {QStringLiteral("useGlobalLight"), use_global_light},
          {QStringLiteral("color"), QVariantMap{
              {QStringLiteral("r"), 0.0}, {QStringLiteral("g"), 0.0},
              {QStringLiteral("b"), 0.0}, {QStringLiteral("a"), 0.65}}}};
}

QVariantList gradient_test_stops() {
  return {QVariantMap{{QStringLiteral("position"), 0.0},
                      {QStringLiteral("color"), QVariantMap{
                          {QStringLiteral("r"), 0.9}, {QStringLiteral("g"), 0.1},
                          {QStringLiteral("b"), 0.2}, {QStringLiteral("a"), 1.0}}}},
          QVariantMap{{QStringLiteral("position"), 1.0},
                      {QStringLiteral("color"), QVariantMap{
                          {QStringLiteral("r"), 0.1}, {QStringLiteral("g"), 0.3},
                          {QStringLiteral("b"), 0.95}, {QStringLiteral("a"), 0.8}}}}};
}

bool wait_until(const std::function<bool()>& condition, int timeout_milliseconds = 15000) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_milliseconds) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (condition()) return true;
    QThread::msleep(2);
  }
  return condition();
}

QImage wait_for_preview(photo_editor::desktop::EditorPreviewImageProvider& provider) {
  QImage image;
  const bool received = wait_until([&] {
    image = provider.image(QStringLiteral("edited"));
    return !image.isNull();
  });
  require(received, "The controller must publish its rendered image to the preview provider.");
  return image;
}

QImage wait_for_preview_change(photo_editor::desktop::EditorPreviewImageProvider& provider,
                               const QImage& previous) {
  QImage image;
  const bool changed = wait_until([&] {
    image = provider.image(QStringLiteral("edited"));
    return !image.isNull() && image != previous;
  });
  require(changed, "The controller must publish the changed style render before commit.");
  return image;
}

void wait_for_preview_image(photo_editor::desktop::EditorPreviewImageProvider& provider,
                            const QImage& expected) {
  require(wait_until([&] {
            return provider.image(QStringLiteral("edited")) == expected;
          }),
          "The controller render must match the expected undo or redo image.");
}

QString layer_style_id(photo_editor::desktop::EditorPreviewController& controller,
                       int index, const QString& type) {
  for (const auto& value : controller.photoLayerStyles(index)) {
    const auto style = value.toMap();
    if (style.value(QStringLiteral("typeId")).toString() == type)
      return style.value(QStringLiteral("id")).toString();
  }
  return {};
}

QVariantMap layer_style_by_id(photo_editor::desktop::EditorPreviewController& controller,
                              int index, const QString& id) {
  for (const auto& value : controller.photoLayerStyles(index)) {
    const auto style = value.toMap();
    if (style.value(QStringLiteral("id")).toString() == id) return style;
  }
  return {};
}

bool set_style_parameters_and_verify(
    photo_editor::desktop::EditorPreviewController& controller, int index,
    const QString& id, const QVariantMap& parameters) {
  const auto current = layer_style_by_id(controller, index, id)
                           .value(QStringLiteral("parameters")).toMap();
  if (current != parameters &&
      !controller.setPhotoLayerStyleParameters(index, id,
          {{QStringLiteral("parameters"), parameters}}))
    return false;
  return layer_style_by_id(controller, index, id)
             .value(QStringLiteral("parameters")).toMap() == parameters;
}

int layer_index_by_id(const QVariantList& layers, const QString& id) {
  for (int index = 0; index < layers.size(); ++index) {
    if (layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QPointF controller_view_point(photo_editor::desktop::EditorPreviewController& controller,
                              int index, double x, double y,
                              const QSize& viewport) {
  const auto mapped = controller.mapPhotoLayerStyleCoordinate(index, x, y, true);
  require(mapped.value(QStringLiteral("valid")).toBool(),
          "The production controller must map the target-local point to the viewport.");
  return {mapped.value(QStringLiteral("x")).toDouble() * viewport.width(),
          mapped.value(QStringLiteral("y")).toDouble() * viewport.height()};
}

void wait_for_history_restore(photo_editor::desktop::EditorPreviewController& controller,
                              bool redo) {
  const auto asset_id = controller.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &controller, &photo_editor::desktop::EditorPreviewController::historyRestoreFinished,
      &controller, [&asset_id, &finished, &succeeded](const QString& restored_asset, bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) controller.redo();
  else controller.undo();
  const bool restored = wait_until([&] { return finished; });
  QObject::disconnect(connection);
  require(restored && succeeded, "The controller must restore style history successfully.");
}

class ScopedPhotoEditorData final {
 public:
  explicit ScopedPhotoEditorData(const QString& path)
      : was_set_(qEnvironmentVariableIsSet("PHOTO_EDITOR_APP_DATA_DIR")),
        previous_(qgetenv("PHOTO_EDITOR_APP_DATA_DIR")) {
    QDir().mkpath(path);
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", path.toUtf8());
  }
  ~ScopedPhotoEditorData() {
    if (was_set_) qputenv("PHOTO_EDITOR_APP_DATA_DIR", previous_);
    else qunsetenv("PHOTO_EDITOR_APP_DATA_DIR");
  }

 private:
  bool was_set_ = false;
  QByteArray previous_;
};

void verify_real_controller_gesture() {
  using namespace photo_editor::desktop;
  QTemporaryDir temporary;
  require(temporary.isValid(), "The controller fixture must create isolated image storage.");
  QImage source(96, 72, QImage::Format_ARGB32);
  source.fill(QColor(QStringLiteral("#536679")));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto sourcePath = temporary.filePath(QStringLiteral("style-canvas-source.png"));
  require(source.save(sourcePath, "PNG"), "The controller fixture source image must save.");
  ScopedPhotoEditorData isolatedData(temporary.filePath(QStringLiteral("app-data")));

  EditorPreviewImageProvider provider;
  EditorPreviewController editor;
  editor.setImageProvider(&provider);
  editor.loadAsset(QStringLiteral("photo-layer-style-canvas-real"), sourcePath,
                   {}, {}, {}, {}, {}, true);
  require(wait_until([&] { return editor.sourceImageWidth() == source.width(); }),
          "The controller must load the source before layer setup.");
  require(editor.addPaintLayer(), "The controller fixture must add a transparent pixel layer.");
  const QVariantMap paintSettings{{QStringLiteral("color"), QStringLiteral("#f1bc4e")},
      {QStringLiteral("size"), 12.0}, {QStringLiteral("hardness"), 0.95},
      {QStringLiteral("opacity"), 1.0}, {QStringLiteral("flow"), 1.0}};
  require(editor.beginPhotoPaintStroke(0, paintSettings),
          "The controller fixture must begin its painted sample.");
  editor.addPhotoPaintPoint(48, 36);
  require(editor.finishPhotoPaintStroke(true),
          "The controller fixture must commit its painted sample.");
  require(wait_until([&] { return !editor.photoPaintBusy(); }),
          "The controller fixture paint worker must settle.");
  settle(250);
  require(editor.addPhotoLayerStyle(0, QStringLiteral("drop_shadow")),
          "The controller fixture must add a live drop shadow.");
  const auto styleId = layer_style_id(editor, 0, QStringLiteral("drop_shadow"));
  require(!styleId.isEmpty(), "The real controller style must have a stable identity.");
  auto style = editor.photoLayerStyles(0).first().toMap();
  auto parameters = style.value(QStringLiteral("parameters")).toMap();
  parameters.insert(QStringLiteral("distance"), 4.0);
  parameters.insert(QStringLiteral("angleDegrees"), 0.0);
  parameters.insert(QStringLiteral("blur"), 3.0);
  parameters.insert(QStringLiteral("spread"), 0.0);
  parameters.insert(QStringLiteral("useGlobalLight"), false);
  require(set_style_parameters_and_verify(editor, 0, styleId, parameters),
          "The controller fixture must seed an exact shadow geometry.");
  settle(250);
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The baseline layer-style preview must settle before pointer authoring.");
  settle(100);
  const auto startingHistory = editor.editHistoryCurrentIndex();
  const auto startingStyles = editor.photoLayerStyles(0);
  const auto startingEnvelope = editor.photoLayerStyleEnvelope(0);
  const auto startingRender = wait_for_preview(provider);
  const auto geometry = editor.photoLayerStyleCanvasGeometry(0, styleId);
  require(geometry.value(QStringLiteral("valid")).toBool() &&
              geometry.value(QStringLiteral("editable")).toBool(),
          "The controller must expose editable raster geometry for its style target.");

  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(controller_fixture_qml().toUtf8(), QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/tests/photo_layer_style_controller_fixture.qml")));
  if (component.isError()) {
    for (const auto& error : component.errors())
      std::cerr << error.toString().toStdString() << '\n';
  }
  require(!component.isError(), "The real controller overlay fixture must compile.");
  QQmlContext context(engine.rootContext());
  context.setContextProperty(QStringLiteral("styleController"), &editor);
  context.setContextProperty(QStringLiteral("targetStyleId"), styleId);
  std::unique_ptr<QObject> object(component.create(&context));
  require(object != nullptr, "The real controller overlay fixture must instantiate.");
  auto* fixture = qobject_cast<QQuickItem*>(object.get());
  auto* overlay = object->findChild<QObject*>(QStringLiteral("photoLayerStyleOverlay"));
  require(fixture && overlay,
          "The real controller fixture must expose its production canvas overlay.");

  QQuickWindow window;
  window.resize(600, 440);
  window.setColor(QColor(QStringLiteral("#1b1b1b")));
  fixture->setParentItem(window.contentItem());
  fixture->setSize(QSizeF(600, 440));
  window.show();
  window.requestActivate();
  settle(200);
  require(window.isExposed(), "The real controller overlay window must accept pointer input.");

  const double width = geometry.value(QStringLiteral("styleWidth")).toDouble();
  const double height = geometry.value(QStringLiteral("styleHeight")).toDouble();
  const double scaleX = geometry.value(QStringLiteral("styleScaleX")).toDouble();
  const double scaleY = geometry.value(QStringLiteral("styleScaleY")).toDouble();
  const double centerX = (width - 1.0) * 0.5;
  const double centerY = (height - 1.0) * 0.5;
  const QPointF start = controller_view_point(editor, 0, centerX + 4.0 * scaleX,
                                              centerY, window.size());
  const QPointF finish = controller_view_point(editor, 0, centerX + 24.0 * scaleX,
                                               centerY + 7.0 * scaleY, window.size());
  pointer(window, QEvent::MouseButtonPress, start);
  pointer(window, QEvent::MouseMove, finish);
  require(overlay->property("interactionActive").toBool(),
          "The real controller gesture must remain open before pointer release.");
  const auto livePreview = wait_for_preview_change(provider, startingRender);
  capture_canvas_proof(window, QStringLiteral("controller-shadow-preview.png"));
  require(editor.photoLayerStyles(0) == startingStyles &&
              editor.photoLayerStyleEnvelope(0) == startingEnvelope &&
              editor.editHistoryCurrentIndex() == startingHistory,
          "A live preview must leave committed serialization and history unchanged.");
  pointer(window, QEvent::MouseButtonRelease, finish);
  require(wait_until([&] { return !overlay->property("interactionActive").toBool(); }),
          "Pointer release must close the real controller gesture.");
  const auto committedHistory = startingHistory + 1;
  require(editor.editHistoryCurrentIndex() == committedHistory,
          "One pointer gesture must append exactly one history entry.");
  const auto committedStyles = editor.photoLayerStyles(0);
  const auto committedEnvelope = editor.photoLayerStyleEnvelope(0);
  require(committedStyles != startingStyles && committedEnvelope != startingEnvelope,
          "Pointer release must commit the style into the serializable recipe.");
  const auto committedRender = wait_for_preview(provider);
  require(committedRender != startingRender,
          "The committed style must remain visible in the rendered output.");
  require(!livePreview.isNull(), "The real controller must expose its live preview image.");

  wait_for_history_restore(editor, false);
  require(editor.editHistoryCurrentIndex() == startingHistory &&
              editor.photoLayerStyles(0) == startingStyles &&
              editor.photoLayerStyleEnvelope(0) == startingEnvelope,
          "Undo must restore the starting style recipe in one step.");
  wait_for_preview_image(provider, startingRender);
  wait_for_history_restore(editor, true);
  require(editor.editHistoryCurrentIndex() == committedHistory &&
              editor.photoLayerStyles(0) == committedStyles &&
              editor.photoLayerStyleEnvelope(0) == committedEnvelope,
          "Redo must restore the committed style recipe in one step.");
  wait_for_preview_image(provider, committedRender);

  const auto afterRedoGeometry = editor.photoLayerStyleCanvasGeometry(0, styleId);
  const auto afterRedoParameters = afterRedoGeometry.value(QStringLiteral("styleParameters")).toMap();
  const double afterRedoDistance = afterRedoParameters.value(QStringLiteral("distance")).toDouble();
  const double afterRedoAngle = afterRedoParameters.value(QStringLiteral("angleDegrees")).toDouble() *
      std::acos(-1.0) / 180.0;
  const auto lockStart = controller_view_point(editor, 0,
      centerX + std::cos(afterRedoAngle) * afterRedoDistance * scaleX,
      centerY + std::sin(afterRedoAngle) * afterRedoDistance * scaleY, window.size());
  const auto lockFinish = controller_view_point(editor, 0,
      centerX - 16.0 * scaleX, centerY + 18.0 * scaleY, window.size());
  pointer(window, QEvent::MouseButtonPress, lockStart);
  pointer(window, QEvent::MouseMove, lockFinish);
  require(overlay->property("interactionActive").toBool(),
          "The lock scenario must begin with an active style draft.");
  require(editor.setPhotoLayerLocked(0, true),
          "Locking the target during a draft must be accepted as its own edit.");
  settle(120);
  require(!overlay->property("interactionActive").toBool() &&
              editor.photoLayers().first().toMap().value(QStringLiteral("locked")).toBool() &&
              editor.photoLayerStyleEnvelope(0) == committedEnvelope &&
              editor.editHistoryCurrentIndex() == committedHistory + 1,
          "Locking mid-gesture must cancel only the style preview and preserve the accepted lock.");
  pointer(window, QEvent::MouseButtonRelease, lockFinish);

  // The original target must be unlocked before the linked-light fixture.
  if (editor.photoLayers().at(0).toMap().value(QStringLiteral("locked")).toBool())
    require(editor.setPhotoLayerLocked(0, false),
            "The controller fixture must unlock before authoring the stroke gradient.");
  require(!editor.photoLayers().at(0).toMap().value(QStringLiteral("locked")).toBool(),
          "The original target must be unlocked before the linked-light fixture.");

  // Link the existing shadow and a second styled paint layer to document
  // lighting. The canvas angle drag previews both consumers without mutating
  // committed recipes or history until release.
  const QString primaryLayerId = geometry.value(QStringLiteral("layerId")).toString();
  const auto layersBeforeSecondary = editor.photoLayers();
  require(editor.addPaintLayer(),
          "The linked-light fixture must add a second transparent paint layer.");
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The second paint layer must settle before it is painted.");
  const auto layersAfterSecondary = editor.photoLayers();
  QString secondaryLayerId;
  for (const auto& value : layersAfterSecondary) {
    const auto candidate = value.toMap().value(QStringLiteral("id")).toString();
    if (candidate != primaryLayerId &&
        layer_index_by_id(layersBeforeSecondary, candidate) < 0) {
      secondaryLayerId = candidate;
      break;
    }
  }
  require(!secondaryLayerId.isEmpty(),
          "The linked-light fixture must identify its new paint layer.");
  int secondaryIndex = layer_index_by_id(editor.photoLayers(), secondaryLayerId);
  int primaryIndex = layer_index_by_id(editor.photoLayers(), primaryLayerId);
  require(secondaryIndex >= 0 && primaryIndex >= 0,
          "Both linked-light consumers must remain addressable by identity.");
  require(editor.beginPhotoPaintStroke(secondaryIndex, paintSettings),
          "The second linked paint layer must accept its visual sample.");
  editor.addPhotoPaintPoint(22, 20);
  require(editor.finishPhotoPaintStroke(true),
          "The second linked paint sample must commit.");
  require(wait_until([&] { return !editor.photoPaintBusy(); }),
          "The second linked paint worker must settle.");

  auto primaryShadow = layer_style_by_id(editor, primaryIndex, styleId);
  auto primaryShadowParameters = primaryShadow.value(QStringLiteral("parameters")).toMap();
  const double storedPrimaryAngle =
      primaryShadowParameters.value(QStringLiteral("angleDegrees")).toDouble();
  primaryShadowParameters.insert(QStringLiteral("useGlobalLight"), true);
  require(set_style_parameters_and_verify(editor, primaryIndex, styleId,
              primaryShadowParameters),
          "The original target shadow must enable shared lighting.");
  if (std::abs(editor.photoLayerStyleLightAngle(primaryIndex) - 12.0) >= 1e-9)
    require(editor.setPhotoLayerStyleLightAngle(primaryIndex, 12.0),
            "The original target shadow must set its initial light angle.");
  require(std::abs(editor.photoLayerStyleLightAngle(primaryIndex) - 12.0) < 1e-9,
          "The original target shadow must retain its initial light angle.");
  if (!editor.photoLayerStyleLightLinked(primaryIndex))
    require(editor.setPhotoLayerStyleLightLinked(primaryIndex, true),
            "The original target shadow must link to the document light.");
  require(editor.photoLayerStyleLightLinked(primaryIndex),
          "The original target shadow layer must be linked to document lighting.");

  require(editor.addPhotoLayerStyle(secondaryIndex, QStringLiteral("drop_shadow")),
          "The second linked consumer must receive a shadow style.");
  const auto secondaryStyleId = layer_style_id(editor, secondaryIndex, QStringLiteral("drop_shadow"));
  auto secondaryShadow = layer_style_by_id(editor, secondaryIndex, secondaryStyleId);
  auto secondaryShadowParameters = secondaryShadow.value(QStringLiteral("parameters")).toMap();
  secondaryShadowParameters.insert(QStringLiteral("useGlobalLight"), true);
  require(!secondaryStyleId.isEmpty() &&
              set_style_parameters_and_verify(editor, secondaryIndex, secondaryStyleId,
                  secondaryShadowParameters),
          "The second shadow must enable its shared-light policy.");
  if (!editor.photoLayerStyleLightLinked(secondaryIndex)) {
    if (std::abs(editor.photoLayerStyleLightAngle(secondaryIndex) + 70.0) >= 1e-9)
      require(editor.setPhotoLayerStyleLightAngle(secondaryIndex, -70.0),
              "The second shadow must set its initial local light angle.");
    require(editor.setPhotoLayerStyleLightLinked(secondaryIndex, true),
            "The second shadow must link to the same document light.");
  }
  require(editor.photoLayerStyleLightLinked(secondaryIndex),
          "The second shadow layer must be linked to document lighting.");
  if (std::abs(editor.documentPhotoLayerStyleLightAngle() - 23.0) >= 1e-9)
    require(editor.setDocumentPhotoLayerStyleLightAngle(23.0),
            "The linked-light fixture must establish a non-default document angle.");
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - 23.0) < 1e-9,
          "The linked-light fixture must retain its baseline document angle.");
  fixture->setProperty("layerIndex", primaryIndex);
  fixture->setProperty("styleId", styleId);
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The linked-light baseline render must settle before pointer authoring.");
  settle(100);
  const auto linkedGeometry = editor.photoLayerStyleCanvasGeometry(primaryIndex, styleId);
  require(linkedGeometry.value(QStringLiteral("valid")).toBool() &&
              linkedGeometry.value(QStringLiteral("editable")).toBool() &&
              editor.photoLayerStyleLightLinked(primaryIndex) &&
              editor.photoLayerStyleLightLinked(secondaryIndex),
          "The real canvas target and its second consumer must both be linked and editable.");
  const auto linkedStartingHistory = editor.editHistoryCurrentIndex();
  const auto linkedStartingPrimaryStyles = editor.photoLayerStyles(primaryIndex);
  const auto linkedStartingPrimaryEnvelope = editor.photoLayerStyleEnvelope(primaryIndex);
  const auto linkedStartingSecondaryStyles = editor.photoLayerStyles(secondaryIndex);
  const auto linkedStartingSecondaryEnvelope = editor.photoLayerStyleEnvelope(secondaryIndex);
  const double linkedStartingAngle = editor.documentPhotoLayerStyleLightAngle();
  const auto linkedStartingRender = wait_for_preview(provider);
  const auto linkedParameters = linkedGeometry.value(QStringLiteral("styleParameters")).toMap();
  const double linkedWidth = linkedGeometry.value(QStringLiteral("styleWidth")).toDouble();
  const double linkedHeight = linkedGeometry.value(QStringLiteral("styleHeight")).toDouble();
  const double linkedScaleX = linkedGeometry.value(QStringLiteral("styleScaleX")).toDouble();
  const double linkedScaleY = linkedGeometry.value(QStringLiteral("styleScaleY")).toDouble();
  const double linkedCenterX = (linkedWidth - 1.0) * 0.5;
  const double linkedCenterY = (linkedHeight - 1.0) * 0.5;
  const double linkedDistance = linkedParameters.value(QStringLiteral("distance")).toDouble();
  // Keep the requested global angle within the controller's no-op epsilon
  // while changing only distance. The draft should still render and commit
  // one undoable style change without a spurious document-light edit.
  require(editor.beginPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId),
          "The distance-only linked gesture must begin at the current light angle.");
  const double epsilonAngle = linkedStartingAngle + 5e-10;
  const double distanceOnlyTarget = linkedDistance + 5.0;
  const QVariantMap distanceOnlyPatch{{QStringLiteral("parameters"), QVariantMap{
      {QStringLiteral("distance"), distanceOnlyTarget},
      {QStringLiteral("angleDegrees"), epsilonAngle}}}};
  require(editor.previewPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId,
              distanceOnlyPatch),
          "A linked shadow distance-only draft must preview at the current effective angle.");
  const auto distanceOnlyPreview = wait_for_preview_change(provider, linkedStartingRender);
  require(editor.photoLayerStyles(primaryIndex) == linkedStartingPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == linkedStartingPrimaryEnvelope &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedStartingAngle) < 1e-12 &&
              editor.editHistoryCurrentIndex() == linkedStartingHistory,
          "The distance-only linked draft must leave committed light and history unchanged.");
  require(editor.finishPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId, true),
          "A same-angle linked distance change must commit successfully.");
  require(editor.editHistoryCurrentIndex() == linkedStartingHistory + 1 &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedStartingAngle) < 1e-12 &&
              std::abs(layer_style_by_id(editor, primaryIndex, styleId)
                           .value(QStringLiteral("parameters")).toMap()
                           .value(QStringLiteral("distance")).toDouble() - distanceOnlyTarget) < 0.01 &&
              !distanceOnlyPreview.isNull(),
          "A same-angle distance-only gesture must commit one history step without moving the light.");
  wait_for_history_restore(editor, false);
  require(editor.editHistoryCurrentIndex() == linkedStartingHistory &&
              editor.photoLayerStyles(primaryIndex) == linkedStartingPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == linkedStartingPrimaryEnvelope &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedStartingAngle) < 1e-12,
          "Undo must restore the linked distance-only draft to its exact baseline.");
  wait_for_preview_image(provider, linkedStartingRender);
  const double linkedStartAngle = linkedStartingAngle * std::acos(-1.0) / 180.0;
  const double linkedTargetAngleDegrees = 124.0;
  const double linkedTargetAngle = linkedTargetAngleDegrees * std::acos(-1.0) / 180.0;
  const double linkedTargetDistance = 34.0;
  const auto linkedStart = view_point(fixture, "mapStylePointToView",
      linkedCenterX + std::cos(linkedStartAngle) * linkedDistance * linkedScaleX,
      linkedCenterY + std::sin(linkedStartAngle) * linkedDistance * linkedScaleY);
  const auto linkedEnd = view_point(fixture, "mapStylePointToView",
      linkedCenterX + std::cos(linkedTargetAngle) * linkedTargetDistance * linkedScaleX,
      linkedCenterY + std::sin(linkedTargetAngle) * linkedTargetDistance * linkedScaleY);
  pointer(window, QEvent::MouseButtonPress, linkedStart);
  pointer(window, QEvent::MouseMove, linkedEnd);
  require(overlay->property("interactionActive").toBool(),
          "The linked-light canvas draft must stay active before pointer release.");
  const auto linkedLivePreview = wait_for_preview_change(provider, linkedStartingRender);
  require(editor.photoLayerStyles(primaryIndex) == linkedStartingPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == linkedStartingPrimaryEnvelope &&
              editor.photoLayerStyles(secondaryIndex) == linkedStartingSecondaryStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == linkedStartingSecondaryEnvelope &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedStartingAngle) < 1e-9 &&
              editor.photoLayerStyleLightLinked(primaryIndex) &&
              editor.photoLayerStyleLightLinked(secondaryIndex) &&
              editor.editHistoryCurrentIndex() == linkedStartingHistory,
          "A linked-light preview must leave persisted angles, links, recipes, and history unchanged.");
  capture_canvas_proof(window, QStringLiteral("controller-linked-shadow-preview.png"));
  pointer(window, QEvent::MouseButtonRelease, linkedEnd);
  require(wait_until([&] { return !overlay->property("interactionActive").toBool(); }),
          "Pointer release must close the linked-light shadow gesture.");
  const int linkedCommittedHistory = linkedStartingHistory + 1;
  const auto linkedCommittedPrimaryStyles = editor.photoLayerStyles(primaryIndex);
  const auto linkedCommittedPrimaryEnvelope = editor.photoLayerStyleEnvelope(primaryIndex);
  const auto linkedCommittedSecondaryStyles = editor.photoLayerStyles(secondaryIndex);
  const auto linkedCommittedSecondaryEnvelope = editor.photoLayerStyleEnvelope(secondaryIndex);
  require(editor.editHistoryCurrentIndex() == linkedCommittedHistory &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedTargetAngleDegrees) < 0.5 &&
              editor.photoLayerStyles(primaryIndex) != linkedStartingPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) != linkedStartingPrimaryEnvelope &&
              editor.photoLayerStyles(secondaryIndex) == linkedStartingSecondaryStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) != linkedStartingSecondaryEnvelope &&
              editor.photoLayerStyleLightLinked(primaryIndex) &&
              editor.photoLayerStyleLightLinked(secondaryIndex),
          "One linked shadow drag must commit its distance and document angle in one history step.");
  const auto committedLinkedShadow = layer_style_by_id(editor, primaryIndex, styleId)
                                        .value(QStringLiteral("parameters")).toMap();
  require(std::abs(committedLinkedShadow.value(QStringLiteral("distance")).toDouble() -
                   linkedTargetDistance) < 0.5 &&
              std::abs(committedLinkedShadow.value(QStringLiteral("angleDegrees")).toDouble() -
                       storedPrimaryAngle) < 1e-9 &&
              committedLinkedShadow.value(QStringLiteral("useGlobalLight")).toBool(),
          "A linked shadow drag must store the new distance while preserving linked angle policy.");
  const auto linkedCommittedRender = wait_for_preview(provider);
  require(linkedCommittedRender != linkedStartingRender && !linkedLivePreview.isNull(),
          "The linked document angle must render both shadow consumers during preview and commit.");
  wait_for_history_restore(editor, false);
  require(editor.editHistoryCurrentIndex() == linkedStartingHistory &&
              editor.photoLayerStyles(primaryIndex) == linkedStartingPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == linkedStartingPrimaryEnvelope &&
              editor.photoLayerStyles(secondaryIndex) == linkedStartingSecondaryStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == linkedStartingSecondaryEnvelope &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedStartingAngle) < 1e-9 &&
              editor.photoLayerStyleLightLinked(primaryIndex) &&
              editor.photoLayerStyleLightLinked(secondaryIndex),
          "One undo must restore the linked shadow distance and shared document angle.");
  wait_for_preview_image(provider, linkedStartingRender);
  wait_for_history_restore(editor, true);
  require(editor.editHistoryCurrentIndex() == linkedCommittedHistory &&
              std::abs(editor.documentPhotoLayerStyleLightAngle() - linkedTargetAngleDegrees) < 0.5 &&
              editor.photoLayerStyles(primaryIndex) == linkedCommittedPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == linkedCommittedPrimaryEnvelope &&
              editor.photoLayerStyles(secondaryIndex) == linkedCommittedSecondaryStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == linkedCommittedSecondaryEnvelope,
          "One redo must restore both linked style geometry and the shared document angle.");
  wait_for_preview_image(provider, linkedCommittedRender);

  if (!editor.photoLayers().at(secondaryIndex).toMap()
          .value(QStringLiteral("locked")).toBool())
    require(editor.setPhotoLayerLocked(secondaryIndex, true),
            "The second linked consumer must accept its lock before rejection coverage.");
  require(editor.photoLayers().at(secondaryIndex).toMap()
              .value(QStringLiteral("locked")).toBool(),
          "The second linked consumer must be locked before rejection coverage.");
  const int lockedConsumerHistory = editor.editHistoryCurrentIndex();
  const auto lockedConsumerRecipe = editor.currentAssetBoundAdjustmentPatchJson();
  const auto lockedConsumerPrimaryStyles = editor.photoLayerStyles(primaryIndex);
  const auto lockedConsumerPrimaryEnvelope = editor.photoLayerStyleEnvelope(primaryIndex);
  const auto lockedConsumerSecondaryStyles = editor.photoLayerStyles(secondaryIndex);
  const auto lockedConsumerSecondaryEnvelope = editor.photoLayerStyleEnvelope(secondaryIndex);
  const double lockedConsumerAngle = editor.documentPhotoLayerStyleLightAngle();
  const auto lockedConsumerRender = wait_for_preview(provider);
  const double rejectedTargetAngleDegrees = 211.0;
  const double rejectedTargetAngle = rejectedTargetAngleDegrees * std::acos(-1.0) / 180.0;
  const auto rejectedStart = view_point(fixture, "mapStylePointToView",
      linkedCenterX + std::cos(linkedTargetAngle) * linkedTargetDistance * linkedScaleX,
      linkedCenterY + std::sin(linkedTargetAngle) * linkedTargetDistance * linkedScaleY);
  const auto rejectedEnd = view_point(fixture, "mapStylePointToView",
      linkedCenterX + std::cos(rejectedTargetAngle) * 38.0 * linkedScaleX,
      linkedCenterY + std::sin(rejectedTargetAngle) * 38.0 * linkedScaleY);
  pointer(window, QEvent::MouseButtonPress, rejectedStart);
  pointer(window, QEvent::MouseMove, rejectedEnd);
  require(!overlay->property("interactionActive").toBool(),
          "A locked linked consumer must reject the shared-light canvas preview.");
  pointer(window, QEvent::MouseButtonRelease, rejectedEnd);
  capture_canvas_proof(window, QStringLiteral("controller-linked-shadow-locked-rejected.png"));
  require(editor.beginPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId),
          "A direct linked gesture must begin before testing commit rejection.");
  const QVariantMap rejectedPatch{{QStringLiteral("parameters"), QVariantMap{
      {QStringLiteral("distance"), 38.0},
      {QStringLiteral("angleDegrees"), rejectedTargetAngleDegrees}}}};
  require(!editor.previewPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId, rejectedPatch),
          "The controller must reject a shared-light preview with a locked linked consumer.");
  require(!editor.finishPhotoLayerStyleGesture(primaryIndex, primaryLayerId, styleId, true),
          "The controller must reject committing the unaccepted shared-light draft.");
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The accepted linked-light render must settle after a rejected draft.");
  settle(80);
  require(editor.editHistoryCurrentIndex() == lockedConsumerHistory,
          "Rejected shared-light preview and commit must preserve history.");
  require(std::abs(editor.documentPhotoLayerStyleLightAngle() - lockedConsumerAngle) < 1e-9,
          "Rejected shared-light preview and commit must preserve the accepted document angle.");
  require(editor.currentAssetBoundAdjustmentPatchJson() == lockedConsumerRecipe,
          "Rejected shared-light preview and commit must preserve the serialized authored recipe.");
  require(editor.photoLayerStyles(primaryIndex) == lockedConsumerPrimaryStyles &&
              editor.photoLayerStyleEnvelope(primaryIndex) == lockedConsumerPrimaryEnvelope,
          "Rejected shared-light preview and commit must preserve the primary style recipe.");
  require(editor.photoLayerStyles(secondaryIndex) == lockedConsumerSecondaryStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == lockedConsumerSecondaryEnvelope,
          "Rejected shared-light preview and commit must preserve the secondary style recipe.");
  require(editor.photoLayerStyleLightLinked(primaryIndex) &&
              editor.photoLayerStyleLightLinked(secondaryIndex),
          "Rejected shared-light preview and commit must preserve both link policies.");
  require(editor.photoLayers().at(secondaryIndex).toMap()
              .value(QStringLiteral("locked")).toBool(),
          "Rejected shared-light preview and commit must preserve the accepted consumer lock.");
  require(provider.image(QStringLiteral("edited")) == lockedConsumerRender,
          "A rejected shared-light draft must not replace the accepted rendered state.");

  if (editor.photoLayers().at(secondaryIndex).toMap()
          .value(QStringLiteral("locked")).toBool())
    require(editor.setPhotoLayerLocked(secondaryIndex, false),
            "The linked consumer must unlock before the independent gradient cases.");
  require(!editor.photoLayers().at(secondaryIndex).toMap()
              .value(QStringLiteral("locked")).toBool(),
          "The linked consumer must be unlocked before the independent gradient cases.");
  fixture->setProperty("layerIndex", secondaryIndex);

  // Run a production gradient-overlay pointer gesture through the same
  // controller bridge so the proof set covers all supported handle types.
  require(editor.addPhotoLayerStyle(secondaryIndex, QStringLiteral("gradient_overlay")),
          "The real controller must add a gradient overlay for canvas authoring.");
  const auto gradientStyleId = layer_style_id(editor, secondaryIndex, QStringLiteral("gradient_overlay"));
  auto gradientStyle = layer_style_by_id(editor, secondaryIndex, gradientStyleId);
  auto gradientParameters = gradientStyle.value(QStringLiteral("parameters")).toMap();
  const QVariantMap controllerGradient{
      {QStringLiteral("angleDegrees"), 0.0}, {QStringLiteral("scale"), 1.0},
      {QStringLiteral("reverse"), false}, {QStringLiteral("stops"), gradient_test_stops()}};
  gradientParameters.insert(QStringLiteral("gradient"), controllerGradient);
  require(!gradientStyleId.isEmpty(),
          "The real gradient overlay must have a stable identity.");
  require(set_style_parameters_and_verify(editor, secondaryIndex, gradientStyleId,
              gradientParameters),
          "The real gradient overlay must accept valid authored controls.");
  fixture->setProperty("styleId", gradientStyleId);
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The gradient overlay baseline render must settle before pointer authoring.");
  const auto controllerGradientGeometry =
      editor.photoLayerStyleCanvasGeometry(secondaryIndex, gradientStyleId);
  require(controllerGradientGeometry.value(QStringLiteral("valid")).toBool() &&
              controllerGradientGeometry.value(QStringLiteral("styleType")).toString() ==
                  QStringLiteral("gradient_overlay"),
          "The real controller must expose its gradient overlay as a canvas target.");
  const auto gradientStartingHistory = editor.editHistoryCurrentIndex();
  const auto gradientStartingStyles = editor.photoLayerStyles(secondaryIndex);
  const auto gradientStartingEnvelope = editor.photoLayerStyleEnvelope(secondaryIndex);
  const auto gradientStartingRender = wait_for_preview(provider);
  const double controllerGradientWidth =
      controllerGradientGeometry.value(QStringLiteral("styleWidth")).toDouble();
  const double controllerGradientHeight =
      controllerGradientGeometry.value(QStringLiteral("styleHeight")).toDouble();
  const double controllerGradientCenterX = (controllerGradientWidth - 1.0) * 0.5;
  const double controllerGradientCenterY = (controllerGradientHeight - 1.0) * 0.5;
  const double controllerGradientAngle = 46.0 * std::acos(-1.0) / 180.0;
  const double controllerGradientScale = 0.62;
  const auto controllerGradientExtent = [controllerGradientWidth, controllerGradientHeight](double angle) {
    return std::abs(std::cos(angle)) * std::max(0.5, (controllerGradientWidth - 1.0) * 0.5) +
        std::abs(std::sin(angle)) * std::max(0.5, (controllerGradientHeight - 1.0) * 0.5);
  };
  const auto controllerGradientStart = view_point(fixture, "mapStylePointToView",
      controllerGradientCenterX + std::max(0.5, controllerGradientCenterX),
      controllerGradientCenterY);
  const auto controllerGradientEnd = view_point(fixture, "mapStylePointToView",
      controllerGradientCenterX + std::cos(controllerGradientAngle) *
          controllerGradientExtent(controllerGradientAngle) * controllerGradientScale,
      controllerGradientCenterY + std::sin(controllerGradientAngle) *
          controllerGradientExtent(controllerGradientAngle) * controllerGradientScale);
  pointer(window, QEvent::MouseButtonPress, controllerGradientStart);
  pointer(window, QEvent::MouseMove, controllerGradientEnd);
  require(overlay->property("interactionActive").toBool(),
          "The real gradient overlay gesture must remain open before pointer release.");
  const auto controllerGradientPreview = wait_for_preview_change(provider, gradientStartingRender);
  capture_canvas_proof(window, QStringLiteral("controller-gradient-overlay-preview.png"));
  require(editor.photoLayerStyles(secondaryIndex) == gradientStartingStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == gradientStartingEnvelope &&
              editor.editHistoryCurrentIndex() == gradientStartingHistory,
          "A live gradient overlay preview must not change its committed recipe or history.");
  pointer(window, QEvent::MouseButtonRelease, controllerGradientEnd);
  require(wait_until([&] { return !overlay->property("interactionActive").toBool(); }),
          "Pointer release must close the real gradient overlay gesture.");
  require(editor.editHistoryCurrentIndex() == gradientStartingHistory + 1,
          "The real gradient overlay gesture must commit one history step.");
  const auto committedControllerGradient =
      layer_style_by_id(editor, secondaryIndex, gradientStyleId)
          .value(QStringLiteral("parameters")).toMap().value(QStringLiteral("gradient")).toMap();
  require(std::abs(committedControllerGradient.value(QStringLiteral("angleDegrees")).toDouble() - 46.0) < 0.5 &&
              std::abs(committedControllerGradient.value(QStringLiteral("scale")).toDouble() -
                       controllerGradientScale) < 0.025 &&
              committedControllerGradient.value(QStringLiteral("stops")).toList() ==
                  gradient_test_stops(),
          "The real gradient overlay must commit angle and scale while retaining all stops.");
  require(!controllerGradientPreview.isNull() &&
              wait_for_preview(provider) != gradientStartingRender,
          "The production gradient overlay draft must appear in its preview render.");

  require(editor.addPhotoLayerStyle(secondaryIndex, QStringLiteral("stroke")),
          "The real controller must add a stroke for the gradient gesture.");
  const auto strokeStyleId = layer_style_id(editor, secondaryIndex, QStringLiteral("stroke"));
  require(!strokeStyleId.isEmpty(), "The controller stroke must have a stable identity.");
  auto strokeStyle = layer_style_by_id(editor, secondaryIndex, strokeStyleId);
  auto strokeParameters = strokeStyle.value(QStringLiteral("parameters")).toMap();
  const QVariantList strokeStops = gradient_test_stops();
  const QVariantMap strokeGradient{
      {QStringLiteral("angleDegrees"), 0.0}, {QStringLiteral("scale"), 1.0},
      {QStringLiteral("reverse"), false}, {QStringLiteral("stops"), strokeStops}};
  strokeParameters.insert(QStringLiteral("fill"), QVariantMap{
      {QStringLiteral("kind"), QStringLiteral("gradient")},
      {QStringLiteral("gradient"), strokeGradient}});
  strokeParameters.insert(QStringLiteral("width"), 12.0);
  require(set_style_parameters_and_verify(editor, secondaryIndex, strokeStyleId,
              strokeParameters),
          "The controller stroke must accept a valid nested gradient fill.");
  fixture->setProperty("styleId", strokeStyleId);
  require(wait_until([&] { return !editor.processing() && !editor.fullRenderRunning(); }),
          "The baseline stroke gradient render must settle before canvas input.");
  settle(100);
  const auto strokeGeometry = editor.photoLayerStyleCanvasGeometry(secondaryIndex, strokeStyleId);
  require(strokeGeometry.value(QStringLiteral("valid")).toBool() &&
              strokeGeometry.value(QStringLiteral("editable")).toBool() &&
              strokeGeometry.value(QStringLiteral("styleType")).toString() == QStringLiteral("stroke"),
          "The real controller must expose a gradient-filled stroke as a canvas style target.");
  const auto strokeStartingHistory = editor.editHistoryCurrentIndex();
  const auto strokeStartingStyles = editor.photoLayerStyles(secondaryIndex);
  const auto strokeStartingEnvelope = editor.photoLayerStyleEnvelope(secondaryIndex);
  const auto strokeStartingRender = wait_for_preview(provider);
  const double strokeWidth = strokeGeometry.value(QStringLiteral("styleWidth")).toDouble();
  const double strokeHeight = strokeGeometry.value(QStringLiteral("styleHeight")).toDouble();
  const double strokeCenterX = (strokeWidth - 1.0) * 0.5;
  const double strokeCenterY = (strokeHeight - 1.0) * 0.5;
  const double strokeHandleRadius = std::max(0.5, strokeCenterX);
  const double strokeTargetAngle = 52.0 * std::acos(-1.0) / 180.0;
  const double strokeTargetScale = 0.68;
  const auto extentFor = [strokeWidth, strokeHeight](double angle) {
    return std::abs(std::cos(angle)) * std::max(0.5, (strokeWidth - 1.0) * 0.5) +
        std::abs(std::sin(angle)) * std::max(0.5, (strokeHeight - 1.0) * 0.5);
  };
  const auto strokeStart = view_point(fixture, "mapStylePointToView",
      strokeCenterX + strokeHandleRadius, strokeCenterY);
  const auto strokeEnd = view_point(fixture, "mapStylePointToView",
      strokeCenterX + std::cos(strokeTargetAngle) * extentFor(strokeTargetAngle) * strokeTargetScale,
      strokeCenterY + std::sin(strokeTargetAngle) * extentFor(strokeTargetAngle) * strokeTargetScale);
  pointer(window, QEvent::MouseButtonPress, strokeStart);
  pointer(window, QEvent::MouseMove, strokeEnd);
  require(overlay->property("interactionActive").toBool(),
          "The real stroke gradient pointer gesture must remain open before release.");
  const auto strokeLivePreview = wait_for_preview_change(provider, strokeStartingRender);
  capture_canvas_proof(window, QStringLiteral("controller-gradient-stroke-preview.png"));
  require(editor.photoLayerStyles(secondaryIndex) == strokeStartingStyles &&
              editor.photoLayerStyleEnvelope(secondaryIndex) == strokeStartingEnvelope &&
              editor.editHistoryCurrentIndex() == strokeStartingHistory,
          "A live stroke gradient preview must not change its serialized recipe or history.");
  pointer(window, QEvent::MouseButtonRelease, strokeEnd);
  require(wait_until([&] { return !overlay->property("interactionActive").toBool(); }),
          "Pointer release must close the real stroke gradient gesture.");
  require(editor.editHistoryCurrentIndex() == strokeStartingHistory + 1,
          "One stroke gradient gesture must append exactly one history entry.");
  const auto committedStrokeStyle = layer_style_by_id(editor, secondaryIndex, strokeStyleId);
  const auto committedStrokeParameters = committedStrokeStyle.value(QStringLiteral("parameters")).toMap();
  const auto committedStrokeFill = committedStrokeParameters.value(QStringLiteral("fill")).toMap();
  const auto committedStrokeGradient = committedStrokeFill.value(QStringLiteral("gradient")).toMap();
  require(committedStrokeFill.value(QStringLiteral("kind")).toString() == QStringLiteral("gradient") &&
              std::abs(committedStrokeGradient.value(QStringLiteral("angleDegrees")).toDouble() - 52.0) < 0.5 &&
              std::abs(committedStrokeGradient.value(QStringLiteral("scale")).toDouble() -
                       strokeTargetScale) < 0.025 &&
              committedStrokeGradient.value(QStringLiteral("stops")).toList() == strokeStops &&
              committedStrokeGradient.value(QStringLiteral("reverse")).toBool() == false &&
              committedStrokeParameters.value(QStringLiteral("width")).toDouble() == 12.0,
          "The controller must serialize stroke handle edits only to fill.gradient geometry.");
  require(editor.photoLayerStyleEnvelope(secondaryIndex) != strokeStartingEnvelope &&
              !strokeLivePreview.isNull() && wait_for_preview(provider) != strokeStartingRender,
          "The committed stroke gradient must remain visible in the rendered output.");
}

}  // namespace

int main(int argc, char** argv) {
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
    qputenv("QT_QUICK_BACKEND", QByteArrayLiteral("software"));
  QGuiApplication app(argc, argv);
  try {
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(fixture_qml().toUtf8(), QUrl::fromLocalFile(QStringLiteral(
        PHOTO_EDITOR_SOURCE_DIR "/tests/photo_layer_style_canvas_ui_test.qml")));
    if (component.isError()) {
      for (const auto& error : component.errors())
        std::cerr << error.toString().toStdString() << '\n';
    }
    require(!component.isError(), "The production style overlay fixture must compile.");
    std::unique_ptr<QObject> object(component.create());
    require(object != nullptr, "The production style overlay fixture must instantiate.");
    auto* fixture = qobject_cast<QQuickItem*>(object.get());
    auto* overlay = object->findChild<QObject*>(QStringLiteral("photoLayerStyleOverlay"));
    require(fixture && overlay,
            "The production overlay and QML fixture must expose their objects.");

    QQuickWindow window;
    window.setColor(QColor(QStringLiteral("#1b1b1b")));
    window.resize(520, 420);
    fixture->setParentItem(window.contentItem());
    fixture->setSize(QSizeF(520, 420));
    window.show();
    window.requestActivate();
    settle(200);
    require(window.isExposed(), "The offscreen canvas window must be exposed for pointer input.");

    verify_shadow_gesture(window, fixture, overlay, false);
    capture_canvas_proof(window, QStringLiteral("mock-shadow-ordinary.png"));

    // The compact case exercises the same production component and exact
    // CanvasCoordinateMapper round trip at a smaller viewport size.
    fixture->setProperty("parameters", shadow_parameters(24.0, 0.0));
    fixture->setSize(QSizeF(310, 250));
    window.resize(310, 250);
    settle(100);
    fixture->setProperty("beginCount", 0);
    fixture->setProperty("previewCount", 0);
    fixture->setProperty("finishCount", 0);
    fixture->setProperty("historyCount", 0);
    fixture->setProperty("latestPatch", QVariantMap{});
    verify_shadow_gesture(window, fixture, overlay, true);
    capture_canvas_proof(window, QStringLiteral("mock-shadow-compact.png"));

    // Inner shadows linked to global light display the effective global angle.
    // A direct drag updates the linked angle while preserving the link flag.
    fixture->setProperty("parameters", shadow_parameters(24.0, 0.0, true));
    overlay->setProperty("styleType", QStringLiteral("inner_shadow"));
    fixture->setProperty("beginCount", 0);
    fixture->setProperty("previewCount", 0);
    fixture->setProperty("finishCount", 0);
    fixture->setProperty("historyCount", 0);
    const double innerAngle = 31.0 * std::acos(-1.0) / 180.0;
    const auto innerStart = view_point(fixture, "mapStylePointToView",
        119.5 + std::cos(innerAngle) * 24.0 * 1.25,
        79.5 + std::sin(innerAngle) * 24.0 * 0.75);
    const auto innerEnd = view_point(fixture, "mapStylePointToView",
        119.5 - 30.0 * 1.25, 79.5 + 40.0 * 0.75);
    drag(window, innerStart, innerEnd);
    const auto innerPatch = fixture->property("latestPatch").toMap()
                                .value(QStringLiteral("parameters")).toMap();
    require(std::abs(innerPatch.value(QStringLiteral("distance")).toDouble() - 50.0) < 0.2 &&
                std::abs(innerPatch.value(QStringLiteral("angleDegrees")).toDouble() -
                         std::atan2(40.0, -30.0) * 180.0 / std::acos(-1.0)) < 0.3 &&
                innerPatch.value(QStringLiteral("useGlobalLight")).toBool(),
            "An inner shadow drag must preserve global-light linking and authored angle/distance.");
    require(fixture->property("finishCount").toInt() == 1 &&
                fixture->property("historyCount").toInt() == 1,
            "An inner shadow drag must finish as one history transaction.");

    // Escape cancels a preview and restores the controller's captured state.
    fixture->setProperty("parameters", shadow_parameters(50.0, 0.0));
    overlay->setProperty("styleType", QStringLiteral("drop_shadow"));
    fixture->setProperty("beginCount", 0);
    fixture->setProperty("previewCount", 0);
    fixture->setProperty("finishCount", 0);
    fixture->setProperty("historyCount", 0);
    fixture->setProperty("latestPatch", QVariantMap{});
    const auto center = QPointF(119.5, 79.5);
    const auto escapeStart = view_point(fixture, "mapStylePointToView",
        center.x() + 50.0 * 1.25, center.y() + 0.0);
    const auto escapeEnd = view_point(fixture, "mapStylePointToView",
        center.x() + 0.0, center.y() + 30.0 * 0.75);
    pointer(window, QEvent::MouseButtonPress, escapeStart);
    pointer(window, QEvent::MouseMove, escapeEnd);
    require(overlay->property("interactionActive").toBool(),
            "The Escape fixture must be inside a live pointer transaction.");
    QTest::keyClick(&window, Qt::Key_Escape);
    settle();
    pointer(window, QEvent::MouseButtonRelease, escapeEnd);
    require(!overlay->property("interactionActive").toBool() &&
                !fixture->property("lastCommit").toBool() &&
                fixture->property("historyCount").toInt() == 0,
            "Escape must cancel the pointer transaction without a history entry.");
    require(std::abs(fixture->property("parameters").toMap()
                         .value(QStringLiteral("distance")).toDouble() - 50.0) < 0.2,
            "Escape must restore the style values captured at gesture start.");

    // Pan activation models lost pointer ownership; lock disables new gestures.
    pointer(window, QEvent::MouseButtonPress, escapeStart);
    pointer(window, QEvent::MouseMove, escapeEnd);
    overlay->setProperty("panActive", true);
    settle();
    pointer(window, QEvent::MouseButtonRelease, escapeEnd);
    require(!overlay->property("interactionActive").toBool() &&
                !fixture->property("lastCommit").toBool(),
            "A lost pointer during pan activation must cancel the style gesture.");
    overlay->setProperty("panActive", false);
    overlay->setProperty("locked", true);
    const auto lockedBeginCount = fixture->property("beginCount").toInt();
    drag(window, escapeStart, escapeEnd);
    require(fixture->property("beginCount").toInt() == lockedBeginCount,
            "A locked style target must reject pointer authoring.");
    overlay->setProperty("locked", false);

    // A target identity change while the mouse is held cancels the captured
    // transaction before any later pointer update can reach the new target.
    pointer(window, QEvent::MouseButtonPress, escapeStart);
    pointer(window, QEvent::MouseMove, escapeEnd);
    const auto previewsBeforeTargetChange = fixture->property("previewCount").toInt();
    fixture->setProperty("targetId", QStringLiteral("layer-replaced"));
    settle();
    pointer(window, QEvent::MouseButtonRelease, escapeEnd);
    require(!overlay->property("interactionActive").toBool() &&
                !fixture->property("lastCommit").toBool(),
            "Changing the target layer during a gesture must cancel it.");
    pointer(window, QEvent::MouseMove, escapeStart + QPointF(10, 0));
    require(fixture->property("previewCount").toInt() == previewsBeforeTargetChange,
            "A stale target must not receive pointer updates after cancellation.");

    // Gradient angle and scale use renderer-raster geometry. Shift constrains
    // the angle while the mapper still accounts for crop, rotation and parent
    // transform in the input position.
    fixture->setProperty("targetId", QStringLiteral("layer-one"));
    fixture->setProperty("parameters", QVariantMap{{QStringLiteral("gradient"), QVariantMap{
        {QStringLiteral("angleDegrees"), 0.0}, {QStringLiteral("scale"), 1.0},
        {QStringLiteral("reverse"), true}, {QStringLiteral("stops"), QVariantList{
            QVariantMap{{QStringLiteral("position"), 0.0}},
            QVariantMap{{QStringLiteral("position"), 1.0}}}}}}});
    overlay->setProperty("styleType", QStringLiteral("gradient_overlay"));
    fixture->setProperty("beginCount", 0);
    fixture->setProperty("previewCount", 0);
    fixture->setProperty("finishCount", 0);
    fixture->setProperty("historyCount", 0);
    settle();
    const double gradientCenterX = 119.5;
    const double gradientCenterY = 79.5;
    const auto gradientStart = view_point(fixture, "mapStylePointToView",
        gradientCenterX + 119.5, gradientCenterY);
    const double rawAngle = 43.0 * std::acos(-1.0) / 180.0;
    const double rawRadius = 0.7 * (std::abs(std::cos(rawAngle)) * 119.5 +
                                     std::abs(std::sin(rawAngle)) * 79.5);
    const auto gradientEnd = view_point(fixture, "mapStylePointToView",
        gradientCenterX + std::cos(rawAngle) * rawRadius,
        gradientCenterY + std::sin(rawAngle) * rawRadius);
    drag(window, gradientStart, gradientEnd, Qt::ShiftModifier);
    const auto gradientPatch = fixture->property("latestPatch").toMap()
                                   .value(QStringLiteral("parameters")).toMap();
    const auto gradient = gradientPatch.value(QStringLiteral("gradient")).toMap();
    require(std::abs(gradient.value(QStringLiteral("angleDegrees")).toDouble() - 45.0) < 0.01 &&
                std::abs(gradient.value(QStringLiteral("scale")).toDouble() -
                         rawRadius / (std::abs(std::cos(45.0 * std::acos(-1.0) / 180.0)) * 119.5 +
                                      std::abs(std::sin(45.0 * std::acos(-1.0) / 180.0)) * 79.5)) < 0.02,
            "Shift must constrain the gradient angle while retaining exact raster scale.");
    require(gradient.value(QStringLiteral("reverse")).toBool() &&
                gradient.value(QStringLiteral("stops")).toList().size() == 2,
            "Gradient manipulation must preserve reversal and every stop.");
    require(fixture->property("finishCount").toInt() == 1 &&
                fixture->property("historyCount").toInt() == 1,
            "A gradient handle drag must finish as one history transaction.");
    capture_canvas_proof(window, QStringLiteral("mock-gradient-overlay.png"));

    // Gradient-filled strokes use the same controls through fill.gradient.
    // Retain the complete fill, stop data, and stroke settings around the edit.
    const auto strokeStops = gradient_test_stops();
    const QVariantMap strokeGradient{
        {QStringLiteral("angleDegrees"), 0.0}, {QStringLiteral("scale"), 1.0},
        {QStringLiteral("reverse"), true}, {QStringLiteral("stops"), strokeStops}};
    fixture->setProperty("parameters", QVariantMap{
        {QStringLiteral("width"), 9.0}, {QStringLiteral("alignment"), QStringLiteral("center")},
        {QStringLiteral("fill"), QVariantMap{
            {QStringLiteral("kind"), QStringLiteral("gradient")},
            {QStringLiteral("gradient"), strokeGradient}}}});
    overlay->setProperty("styleType", QStringLiteral("stroke"));
    fixture->setProperty("beginCount", 0);
    fixture->setProperty("previewCount", 0);
    fixture->setProperty("finishCount", 0);
    fixture->setProperty("historyCount", 0);
    fixture->setProperty("latestPatch", QVariantMap{});
    settle();
    const double strokeGradientAngle = 38.0 * std::acos(-1.0) / 180.0;
    const double strokeGradientScale = 0.64;
    const auto strokeGradientExtent = [](double angle) {
      return std::abs(std::cos(angle)) * 119.5 + std::abs(std::sin(angle)) * 79.5;
    };
    const auto strokeGradientStart = view_point(fixture, "mapStylePointToView",
        gradientCenterX + 119.5, gradientCenterY);
    const auto strokeGradientEnd = view_point(fixture, "mapStylePointToView",
        gradientCenterX + std::cos(strokeGradientAngle) *
            strokeGradientExtent(strokeGradientAngle) * strokeGradientScale,
        gradientCenterY + std::sin(strokeGradientAngle) *
            strokeGradientExtent(strokeGradientAngle) * strokeGradientScale);
    drag(window, strokeGradientStart, strokeGradientEnd);
    const auto strokePatch = fixture->property("latestPatch").toMap()
                                 .value(QStringLiteral("parameters")).toMap();
    const auto strokeFill = strokePatch.value(QStringLiteral("fill")).toMap();
    const auto strokeGradientPatch = strokeFill.value(QStringLiteral("gradient")).toMap();
    require(std::abs(strokeGradientPatch.value(QStringLiteral("angleDegrees")).toDouble() - 38.0) < 0.3 &&
                std::abs(strokeGradientPatch.value(QStringLiteral("scale")).toDouble() -
                         strokeGradientScale) < 0.02,
            "A gradient stroke handle must edit fill.gradient angle and scale.");
    require(strokeFill.value(QStringLiteral("kind")).toString() == QStringLiteral("gradient") &&
                strokeGradientPatch.value(QStringLiteral("reverse")).toBool() &&
                strokeGradientPatch.value(QStringLiteral("stops")).toList() == strokeStops &&
                strokePatch.value(QStringLiteral("width")).toDouble() == 9.0 &&
                strokePatch.value(QStringLiteral("alignment")).toString() == QStringLiteral("center") &&
                !strokePatch.contains(QStringLiteral("gradient")),
            "Stroke gradient edits must preserve fill kind, stops, stroke settings, and nesting.");
    require(fixture->property("finishCount").toInt() == 1 &&
                fixture->property("historyCount").toInt() == 1,
            "A gradient stroke drag must finish as one history transaction.");
    capture_canvas_proof(window, QStringLiteral("mock-gradient-stroke.png"));

    verify_real_controller_gesture();

    std::cout << "Photo layer style canvas: transformed/cropped shadow, compact mapping, linked inner shadow, gradient overlay/stroke controls, cancel and stale-target safety, and real controller preview/history passed\n";
  } catch (const std::exception& error) {
    std::cerr << "Photo layer style canvas failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
