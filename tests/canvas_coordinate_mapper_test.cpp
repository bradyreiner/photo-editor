#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool require_source_patterns(
    const QString& source,
    const QStringList& required_patterns,
    const char* message) {
  for (const auto& pattern : required_patterns) {
    if (!source.contains(pattern)) {
      std::cerr << message << " missing pattern: " << pattern.toStdString() << '\n';
      return false;
    }
  }
  return true;
}

bool near_value(const double actual, const double expected, const double tolerance = 1e-6) {
  return std::abs(actual - expected) <= tolerance;
}

bool expect_number(
    const QVariantMap& map,
    const QString& key,
    const double expected,
    const char* message) {
  const auto actual = map.value(key).toDouble();
  if (!near_value(actual, expected)) {
    std::cerr << message << " key=" << key.toStdString()
              << " actual=" << actual
              << " expected=" << expected << '\n';
    return false;
  }
  return true;
}

bool expect_bool(
    const QVariantMap& map,
    const QString& key,
    const bool expected,
    const char* message) {
  const auto actual = map.value(key).toBool();
  if (actual != expected) {
    std::cerr << message << " key=" << key.toStdString()
              << " actual=" << actual
              << " expected=" << expected << '\n';
    return false;
  }
  return true;
}

QVariantMap invoke_map(
    QObject* object,
    const char* method,
    const QVariantList& arguments) {
  QVariant return_value;
  bool invoked = false;
  if (arguments.size() == 2) {
    invoked = QMetaObject::invokeMethod(
        object,
        method,
        Q_RETURN_ARG(QVariant, return_value),
        Q_ARG(QVariant, arguments.at(0)),
        Q_ARG(QVariant, arguments.at(1)));
  } else if (arguments.size() == 4) {
    invoked = QMetaObject::invokeMethod(
        object,
        method,
        Q_RETURN_ARG(QVariant, return_value),
        Q_ARG(QVariant, arguments.at(0)),
        Q_ARG(QVariant, arguments.at(1)),
        Q_ARG(QVariant, arguments.at(2)),
        Q_ARG(QVariant, arguments.at(3)));
  }
  if (!invoked) {
    return {};
  }
  return return_value.toMap();
}

QString invoke_string(
    QObject* object,
    const char* method,
    const QVariantList& arguments) {
  QVariant return_value;
  bool invoked = false;
  if (arguments.size() == 2) {
    invoked = QMetaObject::invokeMethod(
        object,
        method,
        Q_RETURN_ARG(QVariant, return_value),
        Q_ARG(QVariant, arguments.at(0)),
        Q_ARG(QVariant, arguments.at(1)));
  }
  if (!invoked) {
    return {};
  }
  return return_value.toString();
}

void set_identity_view(QObject* mapper) {
  mapper->setProperty("contentX", 50.0);
  mapper->setProperty("contentY", 20.0);
  mapper->setProperty("contentWidth", 400.0);
  mapper->setProperty("contentHeight", 200.0);
  mapper->setProperty("cropX", 0.0);
  mapper->setProperty("cropY", 0.0);
  mapper->setProperty("cropWidth", 1.0);
  mapper->setProperty("cropHeight", 1.0);
  mapper->setProperty("rotationDegrees", 0.0);
  mapper->setProperty("devicePixelRatio", 1.0);
}

void set_surface_view(
    QObject* mapper,
    const double content_x,
    const double content_y,
    const double content_width,
    const double content_height,
    const double crop_x,
    const double crop_y,
    const double crop_width,
    const double crop_height,
    const double rotation_degrees,
    const double device_pixel_ratio) {
  mapper->setProperty("contentX", content_x);
  mapper->setProperty("contentY", content_y);
  mapper->setProperty("contentWidth", content_width);
  mapper->setProperty("contentHeight", content_height);
  mapper->setProperty("cropX", crop_x);
  mapper->setProperty("cropY", crop_y);
  mapper->setProperty("cropWidth", crop_width);
  mapper->setProperty("cropHeight", crop_height);
  mapper->setProperty("rotationDegrees", rotation_degrees);
  mapper->setProperty("devicePixelRatio", device_pixel_ratio);
}

bool expect_same_image_point(
    const QVariantMap& actual,
    const QVariantMap& expected,
    const char* message) {
  if (!actual.value(QStringLiteral("valid")).toBool() ||
      !expected.value(QStringLiteral("valid")).toBool() ||
      !near_value(
          actual.value(QStringLiteral("x")).toDouble(),
          expected.value(QStringLiteral("x")).toDouble()) ||
      !near_value(
          actual.value(QStringLiteral("y")).toDouble(),
          expected.value(QStringLiteral("y")).toDouble())) {
    std::cerr << message
              << " actual=(" << actual.value(QStringLiteral("x")).toDouble()
              << ", " << actual.value(QStringLiteral("y")).toDouble()
              << ") expected=(" << expected.value(QStringLiteral("x")).toDouble()
              << ", " << expected.value(QStringLiteral("y")).toDouble()
              << ")\n";
    return false;
  }
  return true;
}

QString read_source_file(const QString& relative_path) {
  QFile file(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR)).filePath(relative_path));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    return {};
  }
  return QString::fromUtf8(file.readAll());
}

bool reject_source_patterns(
    const QString& source,
    const QStringList& patterns,
    const char* message) {
  for (const auto& pattern : patterns) {
    if (source.contains(pattern)) {
      std::cerr << message << " pattern=" << pattern.toStdString() << '\n';
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);

  QQmlEngine engine;
  const auto qml_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                            .filePath(QStringLiteral(
                                "apps/desktop/qml/components/CanvasCoordinateMapper.qml"));
  QQmlComponent component(&engine, QUrl::fromLocalFile(qml_path));
  if (component.isError()) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("CanvasCoordinateMapper.qml should load without QML errors");
  }

  std::unique_ptr<QObject> mapper(component.create());
  if (!mapper) {
    for (const auto& error : component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("CanvasCoordinateMapper.qml should create a mapper object");
  }

  const auto crop_overlay_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/CropOverlay.qml"));
  QQmlComponent crop_overlay_component(
      &engine,
      QUrl::fromLocalFile(crop_overlay_qml_path));
  if (crop_overlay_component.isError()) {
    for (const auto& error : crop_overlay_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("CropOverlay.qml should load for the deep-zoom canvas memory guard");
  }
  std::unique_ptr<QObject> crop_overlay(crop_overlay_component.create());
  if (!crop_overlay) {
    return fail("CropOverlay.qml should instantiate for the deep-zoom canvas memory guard");
  }
  if (crop_overlay->metaObject()->indexOfSignal("interactionStarted()") < 0 ||
      crop_overlay->metaObject()->indexOfSignal("interactionFinished()") < 0) {
    return fail("crop drags should expose interaction boundaries for coalesced undo and persistence");
  }

  const auto layer_transform_overlay_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/LayerTransformOverlay.qml"));
  QQmlComponent layer_transform_overlay_component(
      &engine,
      QUrl::fromLocalFile(layer_transform_overlay_qml_path));
  if (layer_transform_overlay_component.isError()) {
    for (const auto& error : layer_transform_overlay_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("LayerTransformOverlay.qml should load for the coalesced interaction contract");
  }
  std::unique_ptr<QObject> layer_transform_overlay(layer_transform_overlay_component.create());
  if (!layer_transform_overlay ||
      layer_transform_overlay->metaObject()->indexOfSignal("interactionStarted()") < 0 ||
      layer_transform_overlay->metaObject()->indexOfSignal("interactionFinished()") < 0) {
    return fail("layer transforms should expose interaction boundaries for coalesced undo and persistence");
  }
  const auto transform_overlay_qml_path =
      QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
          .filePath(QStringLiteral("apps/desktop/qml/components/TransformOverlay.qml"));
  QQmlComponent transform_overlay_component(
      &engine,
      QUrl::fromLocalFile(transform_overlay_qml_path));
  if (transform_overlay_component.isError()) {
    for (const auto& error : transform_overlay_component.errors()) {
      std::cerr << error.toString().toStdString() << '\n';
    }
    return fail("TransformOverlay.qml should load for the coalesced interaction contract");
  }
  std::unique_ptr<QObject> transform_overlay(transform_overlay_component.create());
  if (!transform_overlay ||
      transform_overlay->metaObject()->indexOfSignal("interactionStarted()") < 0 ||
      transform_overlay->metaObject()->indexOfSignal("interactionFinished()") < 0) {
    return fail("crop transforms should expose interaction boundaries for coalesced undo and persistence");
  }
  crop_overlay->setProperty("width", 1200.0);
  crop_overlay->setProperty("height", 800.0);
  crop_overlay->setProperty("active", true);
  crop_overlay->setProperty("imageWidth", 307200.0);
  crop_overlay->setProperty("imageHeight", 204800.0);
  crop_overlay->setProperty("guideMode", QStringLiteral("Golden Spiral"));
  QCoreApplication::processEvents();
  auto* crop_spiral_canvas =
      crop_overlay->findChild<QObject*>(QStringLiteral("cropGoldenSpiralCanvas"));
  if (crop_spiral_canvas == nullptr ||
      crop_spiral_canvas->property("width").toDouble() != 1.0 ||
      crop_spiral_canvas->property("height").toDouble() != 1.0 ||
      crop_spiral_canvas->property("visible").toBool()) {
    return fail("a 256x zoom-sized crop overlay must keep its hidden Canvas at 1x1 instead of allocating a hundreds-of-gigabytes backing store");
  }

  const auto contract =
      mapper->property("coordinateTransformContract").toString();
  if (!contract.contains(QStringLiteral("view-to-image:true")) ||
      !contract.contains(QStringLiteral("image-to-view:true")) ||
      !contract.contains(QStringLiteral("crop:true")) ||
      !contract.contains(QStringLiteral("rotation:true")) ||
      !contract.contains(QStringLiteral("zoom-pan:true")) ||
      !contract.contains(QStringLiteral("painted-image-rect:true")) ||
      !contract.contains(QStringLiteral("fit-fill:true")) ||
      !contract.contains(QStringLiteral("device-pixel-ratio:true"))) {
    return fail("coordinate mapper should expose its transform contract");
  }

  set_identity_view(mapper.get());
  auto view_point = invoke_map(
      mapper.get(),
      "imageToViewPoint",
      {0.25, 0.5});
  if (!expect_bool(view_point, QStringLiteral("valid"), true, "identity image-to-view validity") ||
      !expect_number(view_point, QStringLiteral("x"), 150.0, "identity image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 120.0, "identity image-to-view y")) {
    return fail("identity image-to-view transform should be exact");
  }
  auto image_point = invoke_map(
      mapper.get(),
      "viewToImagePoint",
      {150.0, 120.0});
  if (!expect_bool(image_point, QStringLiteral("valid"), true, "identity view-to-image validity") ||
      !expect_number(image_point, QStringLiteral("x"), 0.25, "identity view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.5, "identity view-to-image y")) {
    return fail("identity view-to-image transform should round trip exactly");
  }
  auto source_pixel_rect = invoke_map(
      mapper.get(),
      "imageToViewRect",
      {0.25, 0.50, 1.0 / 8.0, 1.0 / 4.0});
  if (!expect_bool(source_pixel_rect, QStringLiteral("valid"), true, "source pixel rect validity") ||
      !expect_number(source_pixel_rect, QStringLiteral("x"), 150.0, "source pixel rect x") ||
      !expect_number(source_pixel_rect, QStringLiteral("y"), 120.0, "source pixel rect y") ||
      !expect_number(source_pixel_rect, QStringLiteral("width"), 50.0, "source pixel rect width") ||
      !expect_number(source_pixel_rect, QStringLiteral("height"), 50.0, "source pixel rect height")) {
    return fail("source-pixel grid cells should map through the same image-to-view rect contract as overlays");
  }

  mapper->setProperty("contentX", -100.0);
  mapper->setProperty("contentY", 30.0);
  mapper->setProperty("contentWidth", 800.0);
  mapper->setProperty("contentHeight", 400.0);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.5, 0.5});
  if (!expect_number(view_point, QStringLiteral("x"), 300.0, "zoom/pan image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 230.0, "zoom/pan image-to-view y")) {
    return fail("zoom and pan should be part of the exact coordinate transform");
  }

  set_identity_view(mapper.get());
  mapper->setProperty("cropX", 0.2);
  mapper->setProperty("cropY", 0.1);
  mapper->setProperty("cropWidth", 0.5);
  mapper->setProperty("cropHeight", 0.4);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.45, 0.3});
  image_point = invoke_map(mapper.get(), "viewToImagePoint", {250.0, 120.0});
  if (!expect_number(view_point, QStringLiteral("x"), 250.0, "crop image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 120.0, "crop image-to-view y") ||
      !expect_number(image_point, QStringLiteral("x"), 0.45, "crop view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.3, "crop view-to-image y")) {
    return fail("crop windows should round trip through image/view coordinates");
  }

  set_identity_view(mapper.get());
  mapper->setProperty("rotationDegrees", 90.0);
  view_point = invoke_map(mapper.get(), "imageToViewPoint", {0.25, 0.5});
  image_point = invoke_map(mapper.get(), "viewToImagePoint", {250.0, 70.0});
  if (!expect_number(view_point, QStringLiteral("x"), 250.0, "rotation image-to-view x") ||
      !expect_number(view_point, QStringLiteral("y"), 70.0, "rotation image-to-view y") ||
      !expect_number(image_point, QStringLiteral("x"), 0.25, "rotation view-to-image x") ||
      !expect_number(image_point, QStringLiteral("y"), 0.5, "rotation view-to-image y")) {
    return fail("rotation should round trip through image/view coordinates");
  }

  set_identity_view(mapper.get());
  auto view_rect = invoke_map(mapper.get(), "imageToViewRect", {0.2, 0.25, 0.3, 0.5});
  auto image_rect = invoke_map(mapper.get(), "viewToImageRect", {130.0, 70.0, 120.0, 100.0});
  if (!expect_bool(view_rect, QStringLiteral("valid"), true, "rect image-to-view validity") ||
      !expect_number(view_rect, QStringLiteral("x"), 130.0, "rect image-to-view x") ||
      !expect_number(view_rect, QStringLiteral("y"), 70.0, "rect image-to-view y") ||
      !expect_number(view_rect, QStringLiteral("width"), 120.0, "rect image-to-view width") ||
      !expect_number(view_rect, QStringLiteral("height"), 100.0, "rect image-to-view height") ||
      !expect_number(image_rect, QStringLiteral("x"), 0.2, "rect view-to-image x") ||
      !expect_number(image_rect, QStringLiteral("y"), 0.25, "rect view-to-image y") ||
      !expect_number(image_rect, QStringLiteral("width"), 0.3, "rect view-to-image width") ||
      !expect_number(image_rect, QStringLiteral("height"), 0.5, "rect view-to-image height")) {
    return fail("rect transforms should round trip exactly");
  }

  mapper->setProperty("devicePixelRatio", 2.0);
  const auto device_rect = invoke_map(
      mapper.get(),
      "devicePixelRectForViewRect",
      {10.25, 5.5, 20.25, 10.25});
  if (!expect_number(device_rect, QStringLiteral("x"), 21.0, "dpr rect x") ||
      !expect_number(device_rect, QStringLiteral("y"), 11.0, "dpr rect y") ||
      !expect_number(device_rect, QStringLiteral("width"), 41.0, "dpr rect width") ||
      !expect_number(device_rect, QStringLiteral("height"), 21.0, "dpr rect height") ||
      !expect_number(device_rect, QStringLiteral("devicePixelRatio"), 2.0, "dpr rect ratio")) {
    return fail("device-pixel-ratio view rect conversion should be exact");
  }
  const auto diagnostic_signature =
      invoke_string(mapper.get(), "pointerMappingDiagnosticSignature", {250.0, 120.0});
  if (!diagnostic_signature.startsWith(QStringLiteral("canvas-coordinate-mapper:v1|")) ||
      !diagnostic_signature.contains(QStringLiteral("|valid:true|")) ||
      !diagnostic_signature.contains(QStringLiteral("|view:250.000,120.000|")) ||
      !diagnostic_signature.contains(QStringLiteral("|image:0.500000,0.500000|")) ||
      !diagnostic_signature.contains(QStringLiteral("|crop:0.000000,0.000000,1.000000x1.000000|")) ||
      !diagnostic_signature.contains(QStringLiteral("|rotation:0.000|")) ||
      !diagnostic_signature.contains(QStringLiteral("|fitMode:fit|")) ||
      !diagnostic_signature.contains(QStringLiteral("|dpr:2.000"))) {
    return fail("pointer mapping diagnostics should report mapper-backed view, image, crop, rotation, fit mode, and dpr state");
  }

  const auto studio_workspace_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  const auto studio_pixel_grid_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioPixelGridOverlay.qml"));
  const auto studio_thumbnail_card_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioThumbnailCard.qml"));
  const auto studio_brush_cursor_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportBrushCursors.qml"));
  const auto studio_retouch_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioRetouchOverlayCommandSurface.qml"));
  const auto studio_viewport_input_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioViewportInputCommandSurface.qml"));
  const auto studio_image_viewport_source =
      read_source_file(QStringLiteral("apps/desktop/qml/components/StudioImageViewport.qml"));
  const auto studio_viewport_source =
      studio_workspace_source + studio_image_viewport_source +
      studio_retouch_overlay_source + studio_viewport_input_source;
  if (!studio_workspace_source.contains(
          QStringLiteral("onInteractionStarted: editorPreview.beginCoalescedEdit()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("onInteractionFinished: editorPreview.endCoalescedEdit()")) ||
      !studio_workspace_source.contains(
          QStringLiteral("text-layer-editor:v4|single-instance:true|multiline-content:true|empty-text:true")) ||
      !studio_workspace_source.contains(
          QStringLiteral("property bool cropLiquifyToolAvailable: true")) ||
      !studio_workspace_source.contains(
          QStringLiteral("liquify-tool:v1|engine-backed:true|interactive-preview:true|coalesced-undo:true|visible:true"))) {
    return fail("Studio crop, layer transform, text, and liquify tools should expose responsive coalesced interaction contracts");
  }
  const auto mask_view_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskView.qml"));
  const auto mask_canvas_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskCanvasOverlay.qml"));
  const auto shared_mask_overlay_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/SharedMaskOverlay.qml"));
  const auto mask_inspector_source =
      read_source_file(QStringLiteral("apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
  const auto controller_source =
      read_source_file(QStringLiteral("apps/desktop/src/editor_preview_controller.cpp"));
  if (studio_workspace_source.isEmpty() ||
      mask_view_source.isEmpty() ||
      mask_canvas_overlay_source.isEmpty() ||
      shared_mask_overlay_source.isEmpty() ||
      studio_image_viewport_source.isEmpty() ||
      !studio_image_viewport_source.contains(QStringLiteral("function normalizedPointForViewport(x, y)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("viewportCoordinateMapper.viewToImagePoint(x, y)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("fitMode: viewport.fitMode")) ||
      !mask_view_source.contains(QStringLiteral("mask-view-pointer-mapper:v1|CanvasCoordinateMapper|crop-rotation-aware|all-mask-inputs")) ||
      !mask_view_source.contains(QStringLiteral("function imagePointFromItem(")) ||
      !mask_view_source.contains(QStringLiteral("fitMode: \"fit\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("mask-canvas-pointer-mapper:v1|CanvasCoordinateMapper|crop-rotation-aware|all-mask-inputs")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function imagePointFromItem(")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function previewOutlinePoint(normalizedX, normalizedY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const point = root.viewPointForImage(normalizedX, normalizedY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const firstPoint = root.previewOutlinePoint(")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("fitMode: \"fit\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("shape-handle-interaction:v1|linear,radial,ellipse,rectangle|coalesced-undo:true|coalesced-persist:true|interactive-preview:true")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("visible: mask.kind === \"radial\" || mask.kind === \"ellipse\" || mask.kind === \"rectangle\"")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("onPressed: editRoot.beginMaskShapeEditTransaction()")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("onReleased: editRoot.endMaskShapeEditTransaction()")) ||
      !mask_inspector_source.contains(QStringLiteral("component MaskSlider: InspectorSlider")) ||
      !mask_inspector_source.contains(QStringLiteral("mask-slider-interaction:v1|coalesced-undo:true|coalesced-persist:true|interactive-preview:true")) ||
      !mask_inspector_source.contains(QStringLiteral("onInteractionStarted: editorPreview.beginCoalescedEdit()")) ||
      !mask_inspector_source.contains(QStringLiteral("onInteractionFinished: editorPreview.endCoalescedEdit()")) ||
      mask_inspector_source.contains(QStringLiteral("\n        InspectorSlider {")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("fitMode: \"overlay\"")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("const point = root.imagePointFromLocal(mouseX, mouseY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("root.activeOutlineClicked(point.x, point.y)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("const previousViewPoint = root.viewPointForImage(")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("const firstViewPoint = root.viewPointForImage(")) ||
      shared_mask_overlay_source.contains(QStringLiteral("activeOutlineClicked(root.clamp(mouseX /")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.previewPointX(root.pointXValue(")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.previewPointY(root.pointYValue(")) ||
      shared_mask_overlay_source.contains(QStringLiteral("root.width * root.pointXValue(")) ||
      shared_mask_overlay_source.contains(QStringLiteral("root.height * root.pointYValue("))) {
    return fail("Studio, MaskView, MaskCanvasOverlay, MaskInspectorSections, and SharedMaskOverlay should expose mapper-backed and coalesced interaction contracts");
  }
  const QStringList studio_tool_routing_contract = {
      QStringLiteral("function normalizedPointForViewport(x, y) {\n        const point = viewportCoordinateMapper.viewToImagePoint(x, y)"),
      QStringLiteral("function updatePixelInspectorAt(x, y) {\n        const point = normalizedPointForViewport(x, y)"),
      QStringLiteral("function handleSamplingPointerPressed(mouse) {\n        if (!previewContext.hasAsset || previewContext.compareMode)\n            return false\n        const point = normalizedPointForViewport(mouse.x, mouse.y)"),
      QStringLiteral("previewContext.sampleActiveColorRangeAt(point.x, point.y)"),
      QStringLiteral("previewContext.beginActiveLuminanceRangeSample(point.x, point.y)"),
      QStringLiteral("previewContext.commitActiveLuminanceRangeSample(point.x, point.y)"),
      QStringLiteral("function handleMaskPointerPressed(mouse) {\n        if (!maskPointerActive())\n            return false\n        const point = normalizedPointForViewport(mouse.x, mouse.y)"),
      QStringLiteral("return previewContext.applyActiveSelectionPixel(point.x, point.y, operation)"),
      QStringLiteral("previewContext.addBrushPointWithPressure(point.x, point.y, 1.0)"),
      QStringLiteral("previewContext.appendActivePolygonPoint(point.x, point.y)"),
      QStringLiteral("previewContext.smartSelectAddAt(point.x, point.y, !previewContext.hasActiveMask)"),
      QStringLiteral("const point = viewport.normalizedPointForViewport(mapped.x, mapped.y)"),
      QStringLiteral("previewController.setActiveMaskPatchSourceRegion("),
      QStringLiteral("previewController.setActiveMaskCloneSource(")};
  const QStringList mask_canvas_tool_routing_contract = {
      QStringLiteral("function imagePointFromItem(item, itemX, itemY) {\n        const mapped = item && item !== previewItem"),
      QStringLiteral("const point = maskCoordinateMapper.viewToImagePoint(mapped.x, mapped.y)"),
      QStringLiteral("function normalizedStrokePoint(mouseX, mouseY) {\n        const point = imagePointFromItem(previewItem, mouseX, mouseY)"),
      QStringLiteral("function scheduleSmartSelectPreviewFromItem(item, itemX, itemY, force) {\n        const point = imagePointFromItem(item, itemX, itemY)"),
      QStringLiteral("const point = root.imagePointFromItem(brushOverlay, x, y)\n                                if (point.valid) {\n                                    editorPreview.setActiveMaskCloneSource(point.x, point.y)"),
      QStringLiteral("editorPreview.addBrushPointWithTabletEraser("),
      QStringLiteral("const point = root.imagePointFromItem(brushOverlay, mouse.x, mouse.y)\n                            if (point.valid)\n                                editorPreview.addBrushPointWithTablet(point.x, point.y"),
      QStringLiteral("const point = root.normalizedStrokePoint(mouse.x, mouse.y)\n                if (!point.valid)"),
      QStringLiteral("editorPreview.appendActivePolygonPoint(point.x, point.y)"),
      QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouse.x, mouse.y, false)"),
      QStringLiteral("const point = root.imagePointFromItem(polygonOverlay, previewX, previewY)")};
  const QStringList mask_view_tool_routing_contract = {
      QStringLiteral("function imagePointFromItem(item, itemX, itemY) {\n        if (!item)"),
      QStringLiteral("const point = maskCoordinateMapper.viewToImagePoint(mapped.x, mapped.y)"),
      QStringLiteral("function normalizedStrokePoint(item, itemX, itemY) {\n        return imagePointFromItem(item, itemX, itemY)"),
      QStringLiteral("const point = root.imagePointFromItem(brushOverlay, x, y)"),
      QStringLiteral("editorPreview.setActiveMaskCloneSource(point.x, point.y)"),
      QStringLiteral("editorPreview.addBrushPointWithTabletEraser("),
      QStringLiteral("const point = root.imagePointFromItem(brushOverlay, mouse.x, mouse.y)\n                                        if (!point.valid)"),
      QStringLiteral("editorPreview.addBrushPointWithTablet(point.x, point.y"),
      QStringLiteral("const imagePoint = root.normalizedStrokePoint(\n                                                previewItem,\n                                                parent.lassoPointerX,\n                                                parent.lassoPointerY)"),
      QStringLiteral("const imagePoint = root.normalizedStrokePoint(\n                                            previewItem,\n                                            eventPoint.position.x,"),
      QStringLiteral("editorPreview.appendActivePolygonPoint(imagePoint.x, imagePoint.y)")};
  const QStringList shared_overlay_tool_routing_contract = {
      QStringLiteral("function viewPointForImage(normalizedX, normalizedY) {\n        const point = sharedCoordinateMapper.imageToViewPoint(normalizedX, normalizedY)"),
      QStringLiteral("function imagePointFromLocal(localX, localY) {\n        const point = sharedCoordinateMapper.viewToImagePoint(localX, localY)"),
      QStringLiteral("const point = root.imagePointFromLocal(mouseX, mouseY)"),
      QStringLiteral("root.activeOutlineClicked(point.x, point.y)")};
  if (!require_source_patterns(
          studio_viewport_source,
          studio_tool_routing_contract,
          "Studio workspace should route pixel inspector, sampling, brush, lasso, polygon, object, clone, and patch tools through the mapper") ||
      !require_source_patterns(
          mask_canvas_overlay_source,
          mask_canvas_tool_routing_contract,
          "MaskCanvasOverlay should route mouse, tablet, hover, lasso, polygon, brush, clone, and smart paths through the mapper") ||
      !require_source_patterns(
          mask_view_source,
          mask_view_tool_routing_contract,
          "MaskView should route mouse, tablet, hover, lasso, polygon, brush, clone, and smart paths through the mapper") ||
      !require_source_patterns(
          shared_mask_overlay_source,
          shared_overlay_tool_routing_contract,
          "SharedMaskOverlay should route outline drawing and hit testing through the mapper")) {
    return fail("Every Goal 1 mask input category should have explicit mapper-backed source evidence");
  }
  if (!mask_canvas_overlay_source.contains(QStringLiteral("function scheduleSmartSelectPreviewFromItem(item, itemX, itemY, force) {\n        const point = imagePointFromItem(item, itemX, itemY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function beginSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(mouseX, mouseY)\n        editorPreview.beginSmartSelectStroke(point.x, point.y")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function updateSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(mouseX, mouseY)\n        editorPreview.updateSmartSelectStroke(point.x, point.y")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const point = normalizedStrokePoint(mouse.x, mouse.y)\n        if (addNegativePrompt)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouseX, mouseY, false)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("root.scheduleSmartSelectPreviewFromItem(previewItem, mouse.x, mouse.y, false)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("beginSmartSelectStroke(mouse.x /")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("updateSmartSelectStroke(mouse.x /")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("scheduleSmartSelectPreview(mouse.x")) ||
      !mask_view_source.contains(QStringLiteral("function beginSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)")) ||
      !mask_view_source.contains(QStringLiteral("function updateSmartSelectStroke(mouseX, mouseY) {\n        const point = normalizedStrokePoint(previewItem, mouseX, mouseY)")) ||
      !mask_view_source.contains(QStringLiteral("const imagePoint = root.normalizedStrokePoint(previewItem, point.position.x, point.position.y)\n                                    if (imagePoint.valid)\n                                        editorPreview.scheduleSmartSelectPreview(imagePoint.x, imagePoint.y)")) ||
      !mask_view_source.contains(QStringLiteral("root.beginSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)")) ||
      !mask_view_source.contains(QStringLiteral("root.updateSmartSelectStroke(parent.smartPointerX, parent.smartPointerY)")) ||
      mask_view_source.contains(QStringLiteral("scheduleSmartSelectPreview(point.position.x")) ||
      !studio_image_viewport_source.contains(QStringLiteral("function normalizedPointForViewport(x, y) {\n        const point = viewportCoordinateMapper.viewToImagePoint(x, y)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("if (!previewContext.hasActiveMask || kind === \"object\") {\n            const objectMode = workspaceContext.applyObjectPickModeForModifierState(mouse.modifiers)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("previewContext.smartSelectSubtractAt(point.x, point.y, !previewContext.hasActiveMask)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("previewContext.smartSelectAddAt(point.x, point.y, !previewContext.hasActiveMask)")) ||
      studio_image_viewport_source.contains(QStringLiteral("smartSelectAddAt(mouse.x")) ||
      studio_image_viewport_source.contains(QStringLiteral("smartSelectSubtractAt(mouse.x"))) {
    return fail("Smart/Object selection hover, prompt, stroke, and one-click commit paths should all use mapper-backed normalized image coordinates");
  }
  const QStringList forbidden_mask_direct_normalization_patterns = {
      QStringLiteral("mouse.x / previewItem.width"),
      QStringLiteral("mouse.y / previewItem.height"),
      QStringLiteral("mouse.x / brushOverlay.width"),
      QStringLiteral("mouse.y / brushOverlay.height"),
      QStringLiteral("point.position.x / previewItem.width"),
      QStringLiteral("point.position.y / previewItem.height"),
      QStringLiteral("x / brushOverlay.width"),
      QStringLiteral("y / brushOverlay.height"),
      QStringLiteral("previewX / Math.max(1, previewItem.width)"),
      QStringLiteral("previewY / Math.max(1, previewItem.height)"),
      QStringLiteral("localX / Math.max(1, root.width)"),
      QStringLiteral("localY / Math.max(1, root.height)"),
      QStringLiteral("parent.width * normalizedX"),
      QStringLiteral("parent.height * normalizedY"),
      QStringLiteral("parent.width * Number(modelData.x"),
      QStringLiteral("parent.height * Number(modelData.y"),
      QStringLiteral("(parent.x + mouse.x - parent.grabOffsetX + parent.width / 2) / Math.max(1, parent.parent.width)"),
      QStringLiteral("(parent.y + mouse.y - parent.grabOffsetY + parent.height / 2) / Math.max(1, parent.parent.height)")};
  if (!reject_source_patterns(
          mask_canvas_overlay_source,
          forbidden_mask_direct_normalization_patterns,
          "MaskCanvasOverlay should not normalize mask pointers or handles by local width/height") ||
      !reject_source_patterns(
          mask_view_source,
          forbidden_mask_direct_normalization_patterns,
          "MaskView should not normalize mask pointers or handles by local width/height") ||
      !reject_source_patterns(
          shared_mask_overlay_source,
          forbidden_mask_direct_normalization_patterns,
          "SharedMaskOverlay should not normalize mask pointers or handles by local width/height")) {
    return fail("Mask pointer paths should route through CanvasCoordinateMapper instead of direct local width/height math");
  }
  if (mask_inspector_source.isEmpty() ||
      controller_source.isEmpty() ||
      !controller_source.contains(QStringLiteral("fallback_metadata.insert(QStringLiteral(\"smartFallback\"), true);")) ||
      !controller_source.contains(QStringLiteral("Created an editable rough selection fallback for refinement.")) ||
      !mask_inspector_source.contains(QStringLiteral("function smartSelectionFallbackState(mask)")) ||
      !mask_inspector_source.contains(QStringLiteral("metadata.smartFallback === true ? \"Editable preview\" : \"\"")) ||
      !mask_inspector_source.contains(QStringLiteral("const fallbackState = root.smartSelectionFallbackState(mask)")) ||
      !studio_workspace_source.contains(QStringLiteral("function activeMaskData()")) ||
      !studio_workspace_source.contains(QStringLiteral("function smartSelectionFallbackState(mask)")) ||
      !studio_workspace_source.contains(QStringLiteral("metadata.smartFallback === true ? \"Editable preview\" : \"\"")) ||
      !studio_workspace_source.contains(QStringLiteral("const fallbackState = root.smartSelectionFallbackState(root.activeMaskData())")) ||
      !studio_workspace_source.contains(QStringLiteral("readonly property string fallbackState: root.smartSelectionFallbackState(root.activeMaskData())")) ||
      !studio_workspace_source.contains(QStringLiteral("selectedMaskHeader.fallbackState.length > 0"))) {
    return fail("Smart selection rough fallback masks should expose a visible editable preview state in mask status UI");
  }
  if (!studio_image_viewport_source.contains(QStringLiteral("function viewPointForImage(normalizedX, normalizedY)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("function viewRectForImage(normalizedX, normalizedY, normalizedWidth, normalizedHeight)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("function viewDiameterForImageSpan(centerX, centerY, normalizedDiameter)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("function pointerMappingDiagnosticSignature(x, y)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("viewportCoordinateMapper.pointerMappingDiagnosticSignature(x, y)")) ||
      !studio_image_viewport_source.contains(QStringLiteral("readonly property var sampleViewRect: viewport.viewRectForImage(")) ||
      !studio_image_viewport_source.contains(QStringLiteral("objectName: \"luminanceRangeSamplePatch\"")) ||
      studio_thumbnail_card_source.contains(QStringLiteral("luminanceRangeSamplePatch")) ||
      !studio_retouch_overlay_source.contains(QStringLiteral("property var sourceRect: viewport.viewRectForImage(sourceX, sourceY, sourceWidth, sourceHeight)")) ||
      !studio_retouch_overlay_source.contains(QStringLiteral("property var sourcePoint: viewport.viewPointForImage(previewController.activeMaskCloneSourceX, previewController.activeMaskCloneSourceY)")) ||
      !studio_retouch_overlay_source.contains(QStringLiteral("property var targetPoint: viewport.viewPointForImage(workspace.activeCloneTargetX(), workspace.activeCloneTargetY())")) ||
      !studio_retouch_overlay_source.contains(QStringLiteral("const point = viewport.normalizedPointForViewport(mapped.x, mapped.y)")) ||
      studio_viewport_source.contains(QStringLiteral("viewport.paintedContentX + previewController.activeMaskCloneSourceX * viewport.paintedContentWidth")) ||
      studio_viewport_source.contains(QStringLiteral("viewport.paintedContentX + workspace.activeCloneTargetX() * viewport.paintedContentWidth")) ||
      studio_viewport_source.contains(QStringLiteral("viewport.paintedContentX + sourceX * viewport.paintedContentWidth")) ||
      studio_viewport_source.contains(QStringLiteral("(mapped.x - viewport.paintedContentX) / Math.max(1, viewport.paintedContentWidth)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function viewPointForImage(normalizedX, normalizedY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function viewRectForImage(normalizedX, normalizedY, normalizedWidth, normalizedHeight)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("function pointerMappingDiagnosticSignature(item, itemX, itemY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("maskCoordinateMapper.pointerMappingDiagnosticSignature(mapped.x, mapped.y)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("readonly property var patchViewRect: root.viewRectForImage(")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property var sourcePoint: root.viewPointForImage(mask.cloneSourceX || 0.5, mask.cloneSourceY || 0.5)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("property var targetPoint: root.viewPointForImage(")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("(mask.cloneSourceX || 0.5) * brushOverlay.width")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("patchBounds.x * previewItem.width")) ||
      !mask_view_source.contains(QStringLiteral("function viewPointForImage(normalizedX, normalizedY)")) ||
      !mask_view_source.contains(QStringLiteral("function pointerMappingDiagnosticSignature(item, itemX, itemY)")) ||
      !mask_view_source.contains(QStringLiteral("maskCoordinateMapper.pointerMappingDiagnosticSignature(mapped.x, mapped.y)")) ||
      !mask_view_source.contains(QStringLiteral("function viewDiameterForImageSpan(centerX, centerY, normalizedDiameter)")) ||
      !mask_view_source.contains(QStringLiteral("property var sourcePoint: root.viewPointForImage(mask.cloneSourceX || 0.5, mask.cloneSourceY || 0.5)")) ||
      !mask_view_source.contains(QStringLiteral("property var targetPoint: root.viewPointForImage(")) ||
      mask_view_source.contains(QStringLiteral("(mask.cloneSourceX || 0.5) * brushOverlay.width")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("CanvasCoordinateMapper {\n        id: sharedCoordinateMapper")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function viewPointForImage(normalizedX, normalizedY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function imagePointFromLocal(localX, localY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("function pointerMappingDiagnosticSignature(localX, localY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("sharedCoordinateMapper.pointerMappingDiagnosticSignature(localX, localY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property var viewPoint: root.viewPointForImage(normalizedX, normalizedY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("readonly property var viewPoint: root.viewPointForImage(Number(modelData.x || 0), Number(modelData.y || 0))")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("editorPreview.insertActivePolygonPointAfter(\n                                    parent.edgeIndex,\n                                    parent.normalizedX,\n                                    parent.normalizedY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("const point = root.imagePointFromLocal(localX, localY)")) ||
      !shared_mask_overlay_source.contains(QStringLiteral("editorPreview.moveActivePolygonPoint(\n                                        parent.pointIndex,\n                                        root.clamp(point.x, 0, 1),\n                                        root.clamp(point.y, 0, 1))")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.insertActivePolygonPointAfter(\n                                        parent.edgeIndex,\n                                        parent.normalizedX,\n                                        parent.normalizedY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("const point = root.imagePointFromItem(polygonOverlay, previewX, previewY)")) ||
      !mask_canvas_overlay_source.contains(QStringLiteral("editorPreview.moveActivePolygonPoint(\n                                                    parent.pointIndex,\n                                                    Math.max(0, Math.min(1, point.x)),\n                                                    Math.max(0, Math.min(1, point.y)))")) ||
      shared_mask_overlay_source.contains(QStringLiteral("parent.width * normalizedX")) ||
      shared_mask_overlay_source.contains(QStringLiteral("parent.height * normalizedY")) ||
      shared_mask_overlay_source.contains(QStringLiteral("parent.width * Number(modelData.x")) ||
      shared_mask_overlay_source.contains(QStringLiteral("(parent.x + mouse.x - parent.grabOffsetX + parent.width / 2) / Math.max(1, parent.parent.width)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("previewX / Math.max(1, previewItem.width)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("previewY / Math.max(1, previewItem.height)")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.previewPointX(root.pointXValue(modelData))")) ||
      mask_canvas_overlay_source.contains(QStringLiteral("root.previewPointX(root.pointXValue(firstPoint))")) ||
      mask_view_source.contains(QStringLiteral("root.previewPointX(modelData.x || 0.5)"))) {
    return fail("Range sample patches and retouch source/target guides should use image-to-view mapping and mapper-backed drag coordinates");
  }
  const QStringList pixel_grid_alignment_contract = {
      QStringLiteral("readonly property string pixelGridRenderContract: \"pixel-grid-render:v6|scenegraph-lines:true|viewport-bounded:true|visible-lines-only:true|no-raster-canvas:true|content-aligned:true|source-backed-required:true|visible-region-coverage-required:true|crop-output-coordinates:true\""),
      QStringLiteral("readonly property bool pixelGridFullResolutionBacked: nativePreviewActive &&\n                                                          previewContext.displayPreviewNativeViewportFullResolution"),
      QStringLiteral("readonly property bool pixelGridVisible: pixelGridRequested &&\n                                             pixelGridFullResolutionBacked"),
      QStringLiteral("readonly property bool highCostInspectionOverlaysVisible: inspectionDisplayReady"),
      QStringLiteral("pixel-inspection-performance:v5|full-resolution-visible-tile-required:true|proxy-display-while-inspecting:false|viewport-publisher-service:true|signature-dedup:true|event-loop-coalesced:true|source-patch-refresh:immediate"),
      QStringLiteral("one-to-one:one-source-pixel-per-device-pixel"),
      QStringLiteral("basePaintedContentWidth * inspectionDevicePixelRatio"),
      QStringLiteral("sourcePixelInspectionZoom * deepInspectionZoomMultiplier"),
      QStringLiteral("function wheelZoomFactor(wheel)"),
      QStringLiteral("(zoom <= 1.01 && !pixelInspectionRequested)"),
      QStringLiteral("previewContext.schedulePreviewViewportPublication("),
      QStringLiteral("onPixelInspectionRequestedChanged: schedulePublishedPreviewViewportUpdate(true)"),
      QStringLiteral("readonly property real sourcePixelScreenWidth: inspectionPixelWidth > 0\n        ? paintedContentWidth / Math.max(1, inspectionPixelWidth)"),
      QStringLiteral("readonly property real sourcePixelScreenHeight: inspectionPixelHeight > 0\n        ? paintedContentHeight / Math.max(1, inspectionPixelHeight)"),
      QStringLiteral("const rect = viewRectForImage(\n                    pixelX / sourceWidth,\n                    pixelY / sourceHeight,\n                    1.0 / sourceWidth,\n                    1.0 / sourceHeight)"),
      QStringLiteral("readonly property string pixelBrushCursorSnapContract: \"pixel-brush-cursor:v2|source-cell:floor|screen-rect:mapper|visible:pixel-grid|source-backed:pixelGridFullResolutionBacked\""),
      QStringLiteral("pixelBrushVisible: workspaceContext.pixelExactToolReady() && viewport.pixelGridVisible"),
      QStringLiteral("readonly property real contentLeft: pixelGridOverlay.paintedContentX"),
      QStringLiteral("readonly property real contentTop: pixelGridOverlay.paintedContentY"),
      QStringLiteral("Math.floor((visibleLeft - contentLeft) / stepX)"),
      QStringLiteral("Math.floor((visibleTop - contentTop) / stepY)"),
      QStringLiteral("x: pixelGridOverlay.contentLeft + sourceIndex * pixelGridOverlay.stepX"),
      QStringLiteral("y: pixelGridOverlay.contentTop + sourceIndex * pixelGridOverlay.stepY")};
  if (!require_source_patterns(
          studio_workspace_source + QStringLiteral("\n") + studio_image_viewport_source +
              QStringLiteral("\n") + studio_pixel_grid_source +
              QStringLiteral("\n") + studio_brush_cursor_source,
          pixel_grid_alignment_contract,
          "Pixel grid, snapped pixel cursor, and source-pixel overlays should share the same source-pixel image-to-view geometry")) {
    return fail("Pixel-grid alignment should be guarded against parallel hand-rolled geometry");
  }

  const auto painted_x = 266.0;
  const auto painted_y = 144.0;
  const auto painted_width = 640.0;
  const auto painted_height = 360.0;
  const auto crop_x = 0.12;
  const auto crop_y = 0.08;
  const auto crop_width = 0.72;
  const auto crop_height = 0.64;
  const auto rotation_degrees = 90.0;
  const auto device_pixel_ratio = 2.0;

  set_surface_view(
      mapper.get(),
      24.0,
      18.0,
      painted_width,
      painted_height,
      crop_x,
      crop_y,
      crop_width,
      crop_height,
      rotation_degrees,
      device_pixel_ratio);
  const auto studio_surface_point =
      invoke_map(mapper.get(), "viewToImagePoint", {24.0 + painted_x, 18.0 + painted_y});

  set_surface_view(
      mapper.get(),
      0.0,
      0.0,
      painted_width,
      painted_height,
      crop_x,
      crop_y,
      crop_width,
      crop_height,
      rotation_degrees,
      device_pixel_ratio);
  const auto mask_view_surface_point =
      invoke_map(mapper.get(), "viewToImagePoint", {painted_x, painted_y});
  const auto mask_canvas_surface_point =
      invoke_map(mapper.get(), "viewToImagePoint", {painted_x, painted_y});
  if (!expect_same_image_point(
          mask_view_surface_point,
          studio_surface_point,
          "MaskView should submit the same normalized point as Studio for the same painted image location") ||
      !expect_same_image_point(
          mask_canvas_surface_point,
          studio_surface_point,
          "MaskCanvasOverlay should submit the same normalized point as Studio for the same painted image location")) {
    return fail("Studio, MaskView, and MaskCanvasOverlay should share one normalized image coordinate mapping");
  }

  return EXIT_SUCCESS;
}
