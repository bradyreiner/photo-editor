#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QImage>
#include <QTest>
#include <iostream>
#include <memory>

namespace {
int binding_loops = 0;
QtMessageHandler previous_handler = nullptr;
void message_handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (message.contains("Binding loop")) ++binding_loops;
  if (previous_handler) previous_handler(type, context, message);
}
int fail(const char* reason) { std::cerr << reason << '\n'; return 1; }
QQuickItem* visual_item(QQuickItem* parent, const QString& name) {
  if (!parent) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* found = visual_item(child, name)) return found;
  }
  return nullptr;
}
}

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  previous_handler = qInstallMessageHandler(message_handler);
  const bool baseline = app.arguments().contains("--baseline");
  const bool reference = app.arguments().contains("--reference");
  QQmlEngine engine;
  QQmlComponent component(&engine);
  const auto persistence = reference ? QString{} : baseline ? QStringLiteral(R"QML(
        contentY: Math.max(0, Math.min(root.saved, Math.max(0, contentHeight - height)))
        onContentYChanged: {
            if (visible && moving && Math.abs(root.saved - contentY) > 0.5) {
                root.writes++
                root.saved = contentY
            }
        }
)QML") : QStringLiteral(R"QML(
        C.TimelineInspectorScrollState {
            view: list
            position: root.saved
            onPositionSettled: function(position) { root.writes++; root.saved = position }
        }
)QML");
  const auto qml = QStringLiteral(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Window {
    id: root
    width: 400; height: 320; visible: true
    property real saved: 0
    property int writes: 0
    function start() { list.flick(0, -1900) }
    function offset() { return list.contentY - list.originY }
    function seek(value) { list.contentY = value }
    function hideList() { list.visible = false }
    function showList() { list.visible = true }
    function shorten() { list.model = 6 }
    ListView {
        id: list
        anchors.fill: parent
        model: 42
        boundsBehavior: Flickable.StopAtBounds
        delegate: Rectangle { required property int index; width: 400; height: 30; color: index % 2 ? "#222" : "#333" }
%1
    }
})QML").arg(persistence);
  component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/effects-discovery-inline.qml")));
  std::unique_ptr<QObject> root(component.create());
  if (!root) { std::cerr << component.errorString().toStdString(); return fail("Effects scroll fixture must load"); }
  QTest::qWait(100);
  const int initial_loops = binding_loops;
  QElapsedTimer timer;
  timer.start();
  QMetaObject::invokeMethod(root.get(), "start");
  QTest::qWait(1800);
  const auto writes = root->property("writes").toInt();
  QVariant offset;
  QMetaObject::invokeMethod(root.get(), "offset", Q_RETURN_ARG(QVariant, offset));
  QJsonObject report{{"baseline", baseline}, {"reference", reference}, {"scrollPersistenceWrites", writes},
      {"bindingLoops", binding_loops - initial_loops}, {"settledOffset", offset.toDouble()},
      {"scenarioElapsedMs", timer.elapsed()}};
  std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).toStdString() << '\n';
  if (baseline || reference) return 0;
  if (writes < 1 || writes > 2 || binding_loops != 0 || offset.toDouble() < 846 ||
      qAbs(offset.toDouble() - root->property("saved").toDouble()) > 0.5)
    return fail("Flicking must remain free of feedback and persist once after settling");
  root->setProperty("saved", 330);
  QTest::qWait(40);
  QMetaObject::invokeMethod(root.get(), "offset", Q_RETURN_ARG(QVariant, offset));
  if (qAbs(offset.toDouble() - 330) > 0.5) return fail("Changing context must restore its saved scroll offset");
  QMetaObject::invokeMethod(root.get(), "hideList");
  root->setProperty("saved", 570);
  QTest::qWait(20);
  QMetaObject::invokeMethod(root.get(), "showList");
  QTest::qWait(40);
  QMetaObject::invokeMethod(root.get(), "offset", Q_RETURN_ARG(QVariant, offset));
  if (qAbs(offset.toDouble() - 570) > 0.5) return fail("A hidden panel must restore when opened");
  QMetaObject::invokeMethod(root.get(), "shorten");
  QTest::qWait(40);
  QMetaObject::invokeMethod(root.get(), "offset", Q_RETURN_ARG(QVariant, offset));
  if (qAbs(offset.toDouble()) > 0.5) return fail("Filtering to a short list must clamp safely");

  QTemporaryDir images;
  QImage sample(192, 120, QImage::Format_RGB32);
  sample.fill(QColor("#cc6644"));
  const auto image_path = images.filePath("rendered-example.png");
  if (!sample.save(image_path)) return fail("Example fixture must be available");
  QQmlComponent rows_component(&engine);
  rows_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Window {
    id: root
    width: 320; height: 120; visible: true
    property string exampleUrl: ""
    property int requests: 0
    property int adds: 0
    function effectExampleThumbnailSource(type, look) {
        if (type !== "oil_paint" || look) throw new Error("Wrong engine example request")
        return exampleUrl
    }
    function exampleThumbnailOriginalSource() { return exampleUrl }
    C.TimelineEffectLibraryRow {
        id: shared
        objectName: "sharedRow"
        width: parent.width
        modelData: ({name: "Oil paint", sharedPhotoType: "oil_paint"})
        exampleController: root
        actionEnabled: true
        onAddRequested: root.adds++
    }
    C.TimelineEffectLibraryRow {
        id: legacy
        objectName: "legacyRow"
        width: parent.width; y: 40
        modelData: ({name: "Blur"})
        previewSource: root.exampleUrl
        actionEnabled: true
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/effects-rows-inline.qml")));
  std::unique_ptr<QObject> rows(rows_component.create());
  if (!rows) { std::cerr << rows_component.errorString().toStdString(); return fail("Real effect rows must load"); }
  rows->setProperty("exampleUrl", QUrl::fromLocalFile(image_path).toString());
  QTest::qWait(150);
  auto* shared = rows->findChild<QQuickItem*>("sharedRow");
  auto* legacy = rows->findChild<QQuickItem*>("legacyRow");
  if (!shared || !legacy || shared->property("exampleSource").toString() != QUrl::fromLocalFile(image_path).toString())
    return fail("Shared rows must request the named engine example");
  auto* shared_shader = shared->findChild<QObject*>("timelineEffectLegacyThumbnailShader");
  auto* legacy_shader = legacy->findChild<QObject*>("timelineEffectLegacyThumbnailShader");
  if (!shared_shader || shared_shader->property("active").toBool() ||
      !legacy_shader || !legacy_shader->property("active").toBool())
    return fail("Only legacy effects may instantiate live thumbnail shaders");
  shared->forceActiveFocus();
  QTest::keyClick(qobject_cast<QQuickWindow*>(rows.get()), Qt::Key_Return);
  if (rows->property("adds").toInt() != 1) return fail("Keyboard Add must remain available");
  if (binding_loops != 0) return fail("Effect rows and scroll restoration must have no binding loops");
  QQmlComponent controls_component(&engine);
  controls_component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components" as C
Window {
    id: root
    width: 320; height: 760; visible: true
    signal assetChanged()
    property var photoLayers: [{id: "layer-a", name: "Clip effects", kind: "adjustment", revision: 0}]
    property real amount: 0.75
    property int ends: 0
    property int resets: 0
    property bool extraEffect: false
    property bool hasWorkingSelection: false
    function photoLayerEffects(index) {
        const effects = [{id: "blur-a", type: "gaussian_blur", version: 1, sigma: 8, amount: amount,
                          enabled: true, maskId: "", edgeMode: 0}]
        if (extraEffect) effects.push({id: "blur-b", type: "gaussian_blur", version: 1, sigma: 12,
            amount: 1, enabled: true, maskId: "", edgeMode: 0})
        return effects
    }
    function availablePhotoEffects() { return [{type: "gaussian_blur", name: "Gaussian blur", category: "Blur"}] }
    function availablePhotoEffectLooks() { return [] }
    function availableFilmGrainStocks() { return [] }
    function finishPhotoLayerEffectPreview(apply) { return true }
    function beginCoalescedEdit() {}
    function endCoalescedEdit() { ends++ }
    function resetPhotoLayerEffect(index, id) { if (index === 0 && id === "blur-a") resets++ }
    function addAnotherEffect() {
        extraEffect = true
        photoLayers = [{id: "layer-a", name: "Clip effects", kind: "adjustment", revision: 2}]
    }
    function changeAmount() {
        amount = 0.5
        photoLayers = [{id: "layer-a", name: "Clip effects", kind: "adjustment", revision: 1}]
    }
    QtObject {
        id: demoBridge
        property var editor: root
        property bool ready: true
        property bool loading: false
        property bool editable: true
        property string error: ""
    }
    C.TimelineSharedEffectControls {
        objectName: "compactControls"
        width: parent.width
        bridge: demoBridge
    }
})QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/shared-effect-controls-inline.qml")));
  std::unique_ptr<QObject> controls(controls_component.create());
  if (!controls) { std::cerr << controls_component.errorString().toStdString(); return fail("Compact native effect controls must load"); }
  QTest::qWait(100);
  auto* controls_window = qobject_cast<QQuickWindow*>(controls.get());
  auto* stack = visual_item(controls_window->contentItem(), "timelineSharedLayerEffectStack");
  if (!stack || stack->property("headerVisible").toBool() || stack->property("addButtonVisible").toBool() ||
      !stack->property("timelineStyle").toBool() || stack->property("expandedEffectId").toString() != "blur-a")
    return fail("Timeline must directly expose the new effect without duplicate galleries or workspace chrome");
  const auto controls_height = controls->findChild<QQuickItem*>("compactControls")->implicitHeight();
  std::cout << "Compact effect stack width=" << stack->width() << " height=" << controls_height << '\n';
  if (stack->width() < 280)
    return fail("The compact effect controls must use the available inspector width");
  if (controls_height <= 0 || controls_height > 700)
    return fail("A single effect must fit in the compact inspector");
  QMetaObject::invokeMethod(controls.get(), "changeAmount");
  QTest::qWait(60);
  if (visual_item(controls_window->contentItem(), "timelineSharedLayerEffectStack") != stack ||
      stack->property("expandedEffectId").toString() != "blur-a" || controls->property("ends").toInt() != 0)
    return fail("Publishing an effect edit must preserve the live controls and active gesture");
  for (const int panel_width : {240, 320, 440}) {
    controls_window->setWidth(panel_width);
    QTest::qWait(30);
    auto* amount = visual_item(stack, "photoGaussianAmountField");
    if (qAbs(stack->width() - panel_width) > 1 || !amount || amount->width() < 150 ||
        amount->mapToScene(QPointF(amount->width(), 0)).x() > panel_width + 1)
      return fail("Effect controls must remain readable and contained when the inspector is resized");
  }
  auto* actions = visual_item(stack, "timelineEffectActions_blur-a");
  if (!actions || !actions->isVisible() || !QMetaObject::invokeMethod(actions, "menuItemTriggered",
      Q_ARG(QString, QStringLiteral("reset"))) || controls->property("resets").toInt() != 1)
    return fail("Compact effect actions must retain the real parameter reset action");
  stack->setProperty("expandedEffectId", "");
  QMetaObject::invokeMethod(controls.get(), "changeAmount");
  QTest::qWait(60);
  if (!stack->property("expandedEffectId").toString().isEmpty())
    return fail("A deliberately collapsed effect must stay collapsed after a live value publication");
  QMetaObject::invokeMethod(controls.get(), "addAnotherEffect");
  QTest::qWait(60);
  if (stack->property("expandedEffectId").toString() != "blur-b")
    return fail("Adding an effect must open its own controls without selecting an older effect");
  return 0;
}
