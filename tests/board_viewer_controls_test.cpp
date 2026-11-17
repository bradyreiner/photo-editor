#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <iostream>
#include <memory>

namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
void settle() { for (int i = 0; i < 8; ++i) { QCoreApplication::processEvents(); QTest::qWait(5); } }
QQuickItem* find_item(QQuickItem* item, const QString& name) {
  if (item->objectName() == name) return item;
  for (auto* child : item->childItems()) if (auto* found = find_item(child, name)) return found;
  return nullptr;
}
QString production_function(const QString& source, const QString& name) {
  const auto start = source.indexOf("    function " + name + "(");
  if (start < 0) return {};
  const auto open = source.indexOf('{', start);
  int depth = 1;
  for (auto at = open + 1; at < source.size(); ++at) {
    if (source[at] == '{') ++depth;
    if (source[at] == '}' && --depth == 0) return source.mid(start, at - start + 1);
  }
  return {};
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent stage_component(&engine);
  stage_component.setData("import QtQuick\nItem { property real panX: 0; property real panY: 0 }", QUrl());
  std::unique_ptr<QObject> stage_object(stage_component.create());
  auto* stage = qobject_cast<QQuickItem*>(stage_object.get());
  if (!stage) return fail("Create a canvas viewport fixture");
  QQmlComponent target_component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/BoardCanvasTarget.qml")));
  std::unique_ptr<QObject> target_object(target_component.createWithInitialProperties(
      {{"canvasViewport", QVariant::fromValue(stage)}, {"x", 100}, {"y", 80}, {"width", 200}, {"height", 120}}));
  auto* target = qobject_cast<QQuickItem*>(target_object.get());
  if (!target) return fail(qPrintable(target_component.errorString()));
  QQuickWindow window; window.resize(800, 600); stage->setParentItem(window.contentItem());
  stage->setSize({800, 600}); target->setParentItem(stage); window.show(); settle();
  QSignalSpy activation(target, SIGNAL(activateRequested()));
  const QPoint press(180, 130);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, press); settle();
  if (activation.size() != 1) return fail("A single click activates the board exactly once");
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, press);
  QTest::mouseMove(&window, press + QPoint(60, 40)); settle();
  if (std::abs(stage->property("panX").toDouble() - 60) > .01 ||
      std::abs(stage->property("panY").toDouble() - 40) > .01)
    return fail("Dragging an inactive board pans the canvas in both axes");
  // The board moves under the pointer as the canvas pans. Further movement
  // must stay in viewport coordinates rather than accumulating local deltas.
  target->setPosition({160, 120});
  QTest::mouseMove(&window, press + QPoint(90, 50)); settle();
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, press + QPoint(90, 50)); settle();
  if (activation.size() != 1 || std::abs(stage->property("panX").toDouble() - 90) > .01 ||
      std::abs(stage->property("panY").toDouble() - 50) > .01)
    return fail("Panning stays stable as boards move and never opens a board on release");
  target->setEnabled(false);
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(200, 160)); settle();
  if (activation.size() != 1) return fail("Busy boards reject overlapping activations");

  QQmlComponent transform_component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/LayerTransformOverlay.qml")));
  std::unique_ptr<QObject> transform_object(transform_component.createWithInitialProperties(
      {{"width", 800}, {"height", 600}, {"active", true}, {"imageWidth", 800}, {"imageHeight", 600},
       {"canvasWidth", 800}, {"canvasHeight", 600}, {"layerX", 80}, {"layerY", 80},
       {"layerWidth", 200}, {"layerHeight", 100}, {"moveSelection", true}, {"snappingEnabled", false}}));
  auto* transform = qobject_cast<QQuickItem*>(transform_object.get());
  if (!transform) return fail(qPrintable(transform_component.errorString()));
  transform->setParentItem(stage);
  engine.globalObject().setProperty("moveOverlay", engine.newQObject(transform));
  transform->setProperty("pickForMove", QVariant::fromValue(engine.evaluate(
      "(function(x, y, modifiers) { moveOverlay.layerX = 100; moveOverlay.layerY = 100; return true; })")));
  QSignalSpy moved(transform, SIGNAL(positionEdited(int,int)));
  QSignalSpy finished(transform, SIGNAL(interactionFinished()));
  settle();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(150, 130));
  QTest::mouseMove(&window, QPoint(190, 160)); settle();
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(190, 160)); settle();
  if (moved.isEmpty() || moved.last().at(0).toInt() != 140 || moved.last().at(1).toInt() != 130 || finished.size() != 1)
    return fail("One press selects a different photo and drags it in the same gesture");
  auto* corner = find_item(transform, QStringLiteral("photoLayerTransformBottomRightHandle"));
  if (!corner) return fail("Photos expose a corner resize handle");
  QSignalSpy resized(transform, SIGNAL(transformEdited(int,int,double,double)));
  const auto corner_point = corner->mapToScene({corner->width() / 2, corner->height() / 2}).toPoint();
  QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, corner_point);
  QTest::mouseMove(&window, corner_point + QPoint(200, 100)); settle();
  QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, corner_point + QPoint(200, 100)); settle();
  if (resized.isEmpty() || std::abs(resized.last().at(2).toDouble() - 2) > .01)
    return fail("Dragging a corner resizes a photo across a board seam without stretching its aspect");

  QTemporaryDir images;
  QImage red(200, 200, QImage::Format_RGB32); red.fill(Qt::red);
  const auto image_path = images.filePath("full-photo.png");
  if (!red.save(image_path)) return fail("Create an uncropped positioning fixture");
  QQmlComponent overflow_component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/PhotoLayerOverflowPreview.qml")));
  std::unique_ptr<QObject> overflow_object(overflow_component.createWithInitialProperties({
      {"width", 400}, {"height", 300}, {"imageSource", QUrl::fromLocalFile(image_path)},
      {"canvasRect", QRectF(100, 100, 200, 100)}, {"photoRect", QRectF(100, 60, 200, 200)}}));
  auto* overflow = qobject_cast<QQuickItem*>(overflow_object.get());
  if (!overflow) return fail(qPrintable(overflow_component.errorString()));
  QQuickWindow overflow_window; overflow_window.resize(400, 300); overflow_window.setColor(Qt::black);
  overflow->setParentItem(overflow_window.contentItem()); overflow_window.show();
  auto* overflow_image = find_item(overflow, QStringLiteral("photoLayerOutsideCanvasImage"));
  QElapsedTimer loading; loading.start();
  while (overflow_image && overflow_image->property("status").toInt() != 1 && loading.elapsed() < 5000) {
    QCoreApplication::processEvents(); QTest::qWait(10);
  }
  settle();
  const auto outside_pixels = overflow_window.grabWindow();
  if (outside_pixels.isNull()) return fail("Capture the real outside-canvas positioning surface");
  const auto sample = [&overflow_window](const QImage& image, int x, int y) {
    return image.pixelColor(x * image.width() / overflow_window.width(), y * image.height() / overflow_window.height());
  };
  const auto outside = sample(outside_pixels, 150, 80);
  const auto inside = sample(outside_pixels, 150, 120);
  if (outside.red() < 70 || outside.red() > 110 || outside.green() > 5 || inside != QColor(Qt::black)) {
    std::cerr << "Overflow sample " << outside.name().toStdString() << "; canvas sample " << inside.name().toStdString() << '\n';
    return fail("Full photo remains visible dimmed outside the canvas; the aid never paints over output pixels");
  }
  overflow->setProperty("photoRect", QRectF(100, 110, 200, 200)); settle();
  const auto moved_pixels = overflow_window.grabWindow();
  if (sample(moved_pixels, 150, 80) != QColor(Qt::black) || sample(moved_pixels, 150, 230).red() < 70)
    return fail("Outside-canvas pixels follow a photo move without being destructively cropped");

  QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/StudioImageViewport.qml"));
  if (!file.open(QIODevice::ReadOnly)) return fail("Read the production viewport methods");
  const auto source = QString::fromUtf8(file.readAll());
  if (source.contains("    BoardNavigator {")) return fail("The canvas must have no navigation strip");
  QJSEngine js;
  auto setup = js.evaluate(R"JS(
    var zoom = 1, panX = 37, panY = -16, boardStageIndex = 0;
    var boardStageActive = true, maxInspectionZoom = 64, width = 800, height = 600, boardStageCount = 17, wheelZoomStepBase = 1.22;
    var boardStageModel = [];
    for (var i = 0; i < 17; i++) boardStageModel.push({imageSpanStart: i, imageSpanCount: 1});
    boardStageModel[12] = {imageSpanStart: 12, imageSpanCount: 2};
    boardStageModel[13] = {imageSpanStart: 12, imageSpanCount: 2};
    var viewport = this;
    var calls = 0;
    var workspaceContext = {
      clamp: function(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); },
      editBoardSwitchBusy: false, editBoardExportBusy: false,
      selectEditBoard: function(i) {},
      editBoardSelected: function(i) { boardStageIndex = i; calls++; }
    };
    Object.defineProperty(this, 'imageSpanStart', { get: function() { return boardStageModel[boardStageIndex].imageSpanStart; } });
    Object.defineProperty(this, 'imageSpanCount', { get: function() { return boardStageModel[boardStageIndex].imageSpanCount; } });
    Object.defineProperty(this, 'boardUnitWidth', { get: function() { return 200 / imageSpanCount; } });
    function boardBoundaryX(i) { return 100 - imageSpanStart * boardUnitWidth * zoom + panX - 200 * (zoom - 1) / 2 + i * boardUnitWidth * zoom; }
    Object.defineProperty(this, 'imageFrameY', { get: function() { return 80 + panY - 120 * (zoom - 1) / 2; } });
  )JS");
  if (setup.isError() || js.evaluate(production_function(source, "boardCanvasStart")).isError() ||
      js.evaluate(production_function(source, "boardInActiveCanvas")).isError() ||
      js.evaluate(production_function(source, "boardCanvasCount")).isError() ||
      js.evaluate(production_function(source, "setZoom")).isError() ||
      js.evaluate(production_function(source, "activateBoardInPlace")).isError() ||
      js.evaluate(production_function(source, "zoomIn")).isError() ||
      js.evaluate(production_function(source, "zoomOut")).isError())
    return fail("Load the actual production zoom and board activation methods");
  // The anchor lies outside the active photo: zoom must still hold that world
  // point under the pointer instead of clamping the anchor to the photo edge.
  for (double next : {2.0, .02, .8, 4.0}) {
    js.globalObject().setProperty("next", next);
    if (!js.evaluate(R"JS((function() {
      var oldX = (20 - boardBoundaryX(0)) / zoom;
      var oldY = (25 - imageFrameY) / zoom;
      setZoom(next, 20, 25);
      return Math.abs((20 - boardBoundaryX(0)) / zoom - oldX) < 0.00001 &&
             Math.abs((25 - imageFrameY) / zoom - oldY) < 0.00001 && zoom === next;
    })())JS").toBool()) return fail("Free zoom preserves the pointer anchor, including empty canvas and distant overview");
  }
  if (!js.evaluate(R"JS((function() {
    var x = boardBoundaryX(0), y = imageFrameY, scale = boardUnitWidth * zoom;
    activateBoardInPlace(13);
    var stable = calls === 1 && boardStageIndex === 12 && Math.abs(x - boardBoundaryX(0)) < .00001 &&
                 Math.abs(y - imageFrameY) < .00001 && scale === boardUnitWidth * zoom;
    activateBoardInPlace(13); activateBoardInPlace(12); activateBoardInPlace(-1);
    return stable && calls === 1;
  })())JS").toBool()) return fail("A connected span opens once as one canvas, preserving view and scale across different span widths");
  if (!js.evaluate(R"JS((function() {
    setZoom(1); zoomOut();
    var belowFit = zoom < 1;
    zoomIn();
    return belowFit && Math.abs(zoom - 1) < .00001;
  })())JS").toBool()) return fail("Zoom buttons move freely below Fit and back without a dead end");
  QFile workspace(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  if (!workspace.open(QIODevice::ReadOnly)) return fail("Read the production photo selection handler");
  const auto workspace_source = QString::fromUtf8(workspace.readAll());
  js.evaluate(R"JS(
    var picks = 0, Qt = {ControlModifier: 1, MetaModifier: 2, ShiftModifier: 4};
    viewport.boardIndexAtPoint = function(x, y) { return x < 400 ? 13 : 14; };
    var root = {editBoardMode: true, editBoardIndex: 12, editViewportHandle: viewport,
      photoLayerAtViewportPoint: function(x, y) { return 0; },
      normalizedPhotoLayerSelection: function() { return []; },
      setActivePhotoLayer: function(i) { picks++; return true; }};
  )JS");
  if (js.evaluate(production_function(workspace_source, "selectPhotoLayerAtViewportPoint")).isError() ||
      !js.evaluate("selectPhotoLayerAtViewportPoint(300, 60, 0) && !selectPhotoLayerAtViewportPoint(700, 60, 0) && picks === 1").toBool())
    return fail("Photos on the other half of the active span select directly; foreign canvases retain navigation");
  js.evaluate(R"JS(
    viewport.paintedContentX = 100; viewport.paintedContentY = 100;
    viewport.paintedContentWidth = 200; viewport.paintedContentHeight = 100;
    root.normalizedActivePhotoLayerIndex = function() { return 0; };
    root.currentPhotoLayerData = function() { return {kind: 'source', enabled: true, opacity: 1}; };
    root.editorPreviewController = {sourceImageWidth: 200, sourceImageHeight: 100,
      photoLayerContainsPoint: function(i, x, y) { return x >= 0 && x <= 200 && y >= -50 && y <= 150; }};
  )JS");
  if (js.evaluate(production_function(workspace_source, "photoLayerAtViewportPoint")).isError() ||
      !js.evaluate(R"JS((function() {
        if (photoLayerAtViewportPoint(150, 75) !== 0 || photoLayerAtViewportPoint(150, 25) !== -1) return false;
        root.editBoardMode = false;
        return photoLayerAtViewportPoint(150, 75) === -1;
      })())JS").toBool()) return fail("Selected photo overflow stays draggable while background and ordinary Edit keep their picking bounds");
  js.evaluate(R"JS(
    var imageFrameX = 100, boardUnitHeight = 60;
    function boardOffset(index, unit, scale) { return index * unit; }
  )JS");
  if (js.evaluate(production_function(source, "boardDropPlacement")).isError() ||
      !js.evaluate(R"JS((function() {
        var point = {x: imageFrameX + boardUnitWidth * zoom * 1.5, y: imageFrameY + boardUnitHeight * zoom / 2};
        var placed = boardDropPlacement(13, point);
        return Math.abs(placed.x - .75) < .00001 && Math.abs(placed.y - .5) < .00001;
      })())JS").toBool()) return fail("Drops on the second tile preserve their position within the whole shared canvas");
  std::cout << "Direct board click, free canvas pan, pointer-anchored zoom and stable editing view passed\n";
  return 0;
}
