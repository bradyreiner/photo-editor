#include "app_bootstrap.h"
#include "blank_canvas_document.h"
#include "photo_layer_clipboard.h"
#include "desktop_pipeline_state_compiler.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "photo_layer_compiler.h"
#include "photo_render_utils.h"
#include "photo_pixel_stroke.h"
#include "photo_fragment_brush_preset_store.h"
#include "working_image.h"
#include "tablet_input_area.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QCursor>
#include <QScopeGuard>
#include <QLineF>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <limits>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& c) {
    auto state=c.currentState();
    for(int i=0;i<state.layers.size();++i) state.layers[i]=photo_paint_target_identity(state.layers[i].toMap());
    return state;
  }
  static bool paintRasterIdle(const EditorPreviewController& c) {
    return !c.photo_paint_worker_running_ && (c.photo_paint_jobs_.empty() || c.photo_paint_jobs_.front()->pending_points.empty());
  }
  static RenderRequest request(const EditorPreviewController& c) { return c.currentRenderRequest(false); }
  static void apply(EditorPreviewController& c, const EditState& s) { c.applyState(s); }
};
}
using namespace photo_editor::desktop;
#define CHECK(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

static bool wait_until(const std::function<bool()>& predicate) {
  QElapsedTimer timer; timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(5);
  } while (timer.elapsed() < 15000);
  return false;
}

static bool regional_matches_full(const RenderRequest& request, const QVariantList& base_layers) {
  auto region_request = request;
  region_request.use_in_memory_preview=true;
  auto base_request=region_request;
  base_request.state.layers=base_layers;
  base_request.recipe_cache_identity.clear();
  base_request.adjusted_recipe_cache_identity.clear();
  const auto base_render=render_preview_request_for_test(base_request);
  region_request.paint_preview_base=base_render.preview_image;
  if (region_request.paint_preview_base.isNull()) return false;
  const auto regional=render_preview_request_for_test(region_request);
  if (!regional.success || regional.backend!="paint-region") return false;
  region_request.paint_preview_base={};
  region_request.recipe_cache_identity.clear();
  region_request.adjusted_recipe_cache_identity.clear();
  const auto full_draft=render_preview_request_for_test(region_request);
  if (!full_draft.success || full_draft.preview_image.size()!=regional.preview_image.size()) return false;
  int worst_channel_error=0, outside_error=0;
  for(int y=0;y<regional.preview_image.height();++y) for(int x=0;x<regional.preview_image.width();++x) {
    const auto a=regional.preview_image.pixelColor(x,y),b=full_draft.preview_image.pixelColor(x,y);
    const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});
    if (delta>worst_channel_error) worst_channel_error=delta;
    if (!regional.texture_upload_rect.contains(x,y)) outside_error=std::max(outside_error,delta);
  }
  std::cout << "regional_max_channel_error=" << worst_channel_error
            << " outside_error=" << outside_error << '\n';
  return worst_channel_error <= 1;
}

static int verify_shape_fills(QTemporaryDir& temp) {
  using Access = EditorPreviewControllerTestAccess;
  QImage source(640, 400, QImage::Format_ARGB32);
  source.fill(QColor("#304052"));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temp.filePath("shape-source.png");
  CHECK(source.save(source_path), "Save the isolated shape source");
  EditorPreviewController controller;
  controller.loadAsset("shape-fill-proof", source_path, {}, {}, {}, {}, {});
  CHECK(controller.addPhotoShape("rectangle", 80, 80, 560, 320, 640, 400, "#ff0000", "#ffffff", 4),
      "Create an editable rectangle");
  const auto image = [&] { return QImage::fromData(controller.photoLayers().at(0).toMap().value("pixelData").toByteArray(), "PNG"); };
  const auto definition = [&] { return controller.photoLayers().at(0).toMap().value("shapeDefinition").toMap(); };
  const auto solid = image();
  CHECK(!solid.isNull() && solid.pixelColor(100, 100).red() == 255 && solid.pixelColor(0, 0).alpha() == 0,
      "Solid fill has transparent pixels outside the shape");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"fillMode", "linear"}, {"gradientStartColor", "#ff0000"},
      {"gradientEndColor", "#0000ff"}, {"gradientAngle", 0}}), "Apply a horizontal gradient");
  const auto horizontal = image();
  CHECK(horizontal.pixelColor(12, 120).red() > 235 && horizontal.pixelColor(472, 120).blue() > 235,
      "The gradient spans the shape bounds");
  CHECK(horizontal.pixelColor(0, 0).alpha() == 0 && horizontal.pixelColor(4, 120) == solid.pixelColor(4, 120),
      "Gradient filling keeps transparency and stroke appearance");
  controller.beginCoalescedEdit();
  CHECK(controller.setPhotoLayerShapeFill(0, {{"gradientAngle", 45}}) &&
        controller.setPhotoLayerShapeFill(0, {{"gradientAngle", 90}}), "Edit gradient angle");
  controller.endCoalescedEdit();
  const auto vertical = image();
  CHECK(vertical.pixelColor(240, 12).red() > 235 && vertical.pixelColor(240, 232).blue() > 235,
      "Gradient angle changes actual image pixels");
  controller.undo();
  CHECK(image() == horizontal, "One undo restores the gradient before the angle gesture");
  controller.redo();
  CHECK(image() == vertical, "Redo restores the final angle");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"gradientStartColor", "#00ff0000"}}), "Transparent gradient stop");
  CHECK(image().pixelColor(240, 12).alpha() < 15 && image().pixelColor(240, 232).alpha() > 235,
      "Gradient stop transparency is rendered");
  CHECK(controller.addPhotoShape("ellipse", 100, 70, 500, 330, 640, 400, "#ffffff", "", 0), "Create an ellipse");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"fillMode", "radial"}, {"gradientStartColor", "#ff0000"},
      {"gradientEndColor", "#0000ff"}, {"gradientRadius", 0.5}}), "Apply a radial gradient");
  const auto radial = image();
  CHECK(radial.pixelColor(202, 132).red() > 245 && radial.pixelColor(292, 132).blue() > 215 &&
      radial.pixelColor(8, 8).alpha() == 0, "Radial gradient remains clipped to the ellipse");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"fillMode", "texture"}, {"textureType", "grain"},
      {"textureStrength", 0.8}, {"textureSeed", 7}}), "Apply a seeded grain texture");
  const auto grain = image();
  CHECK(grain != radial && grain.pixelColor(8, 8).alpha() == 0 &&
        grain.pixelColor(180, 130) != grain.pixelColor(181, 130), "Texture changes interior pixels and keeps the shape boundary");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"textureSeed", 8}}) && image() != grain,
      "Texture variation changes the seeded pixels");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"textureSeed", 7}}) && image() == grain,
      "Texture generation is deterministic");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"textureType", "hatch"}}), "Apply a hatch texture");
  const auto hatch = image();
  CHECK(hatch.pixelColor(120, 130) == hatch.pixelColor(132, 130) &&
        hatch.pixelColor(180, 120) == hatch.pixelColor(180, 132), "Hatch texture repeats without tile seams");
  CHECK(controller.setPhotoLayerShapeFill(0, {{"textureStrength", 0}}), "Disable texture strength");
  const auto no_texture = image();
  CHECK(controller.setPhotoLayerShapeFill(0, {{"fillMode", "solid"}}) && image() == no_texture,
      "Zero texture strength is exactly the solid fill");
  QImage texture(16, 16, QImage::Format_ARGB32);
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x)
    texture.setPixelColor(x, y, x < 8 ? QColor("#ffb43c") : QColor("#376bc4"));
  const auto texture_path = temp.filePath("shape-texture.png");
  CHECK(texture.save(texture_path) && controller.setPhotoLayerShapeTexture(0, QUrl::fromLocalFile(texture_path).toString()),
      "Import a tiled image texture using the file dialog URL");
  CHECK(!definition().value("textureData").toString().isEmpty() &&
      definition().value("textureName").toString() == "shape-texture.png", "The image texture is embedded in the shape");
  const auto imported = image();
  CHECK(imported.pixelColor(130, 130) == imported.pixelColor(146, 130) &&
        imported.pixelColor(130, 130) != imported.pixelColor(138, 130), "Imported pixels tile without upscaling a small texture");
  CHECK(QFile::remove(texture_path) && controller.setPhotoLayerShapeFill(0, {{"textureScale", 2}}),
      "Editing an image texture does not require the original file");
  const auto saved = Access::state(controller);
  const auto json = serialize_edit_recipe_json(saved);
  CHECK(validate_edit_recipe_json(json).valid, "Gradient and image-texture recipes validate");
  const auto reopened = parse_edit_recipe_json(json);
  CHECK(reopened.layers.at(0).toMap().value("shapeDefinition") == saved.layers.at(0).toMap().value("shapeDefinition"),
      "Shape fill controls survive recipe round trip");
  EditorPreviewController restored;
  restored.loadAsset("shape-fill-restored", source_path, {}, {}, {}, {}, json);
  CHECK(restored.photoLayers().at(0).toMap().value("shapeDefinition") == definition(),
      "A new controller restores the editable texture definition");
  const auto output = render_output_image_with_backend(source, saved, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  CHECK(!output.isNull() && output == render_output_image_with_backend(source, reopened, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly), "Rendered output survives saving and reopening exactly");
  const auto output_path = temp.filePath("shape-fill-export.png");
  CHECK(controller.exportFlattenedPhoto(output_path) && wait_until([&] { return !controller.photoExportBusy(); }) &&
        controller.photoExportError().isEmpty(), "Export the actual shape document");
  const auto exported = QImage(output_path);
  CHECK(!exported.isNull() && exported.size() == output.size(), "The shape export is readable at the document size");
  int export_error = 0;
  for (int y = 0; y < output.height(); ++y) for (int x = 0; x < output.width(); ++x) {
    const auto a = output.pixelColor(x, y), b = exported.pixelColor(x, y);
    export_error = std::max(export_error, std::max({std::abs(a.red() - b.red()),
        std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue())}));
  }
  CHECK(export_error <= 1, "PNG export agrees with the canonical CPU render within one code value");
  const auto before_invalid = definition();
  CHECK(!controller.setPhotoLayerShapeFill(0, {{"textureScale", 0}}) &&
      !controller.setPhotoLayerShapeFill(0, {{"gradientAngle", std::numeric_limits<double>::quiet_NaN()}}) &&
      !controller.setPhotoLayerShapeFill(0, {{"textureData", "not-a-texture"}}) && definition() == before_invalid,
      "Invalid fill requests leave the shape unchanged");
  CHECK(controller.setPhotoLayerLocked(0, true) && !controller.setPhotoLayerShapeFill(0, {{"textureStrength", 0.1}}),
      "Locked shapes cannot be changed");
  const auto evidence = qEnvironmentVariable("PHOTO_SHAPE_FILL_EVIDENCE");
  if (!evidence.isEmpty()) {
    QDir().mkpath(evidence);
    horizontal.save(evidence + "/linear.png"); vertical.save(evidence + "/linear-vertical.png");
    radial.save(evidence + "/radial.png"); grain.save(evidence + "/grain.png");
    hatch.save(evidence + "/hatch.png");
    imported.save(evidence + "/image-texture.png"); output.save(evidence + "/composite.png");
    QFile receipt(evidence + "/recipe.json");
    CHECK(receipt.open(QIODevice::WriteOnly) && receipt.write(json.toUtf8()) > 0, "Save shape evidence");
  }
  std::cout << "Shape fills: gradient pixels, alpha, clipped textures, independent image data, undo, reopen and export passed.\n";
  return 0;
}

static int verify_blank_canvas(QTemporaryDir& temp) {
  const auto settings = QVariantMap{{"name", "Poster / 1"}, {"width", 640}, {"height", 400}, {"background", "transparent"}};
  const auto created = create_blank_canvas_source(temp.filePath("documents"), settings);
  const auto path = created.value("path").toString();
  CHECK(!path.isEmpty(), "Create an owned blank canvas PNG");
  const QImage source(path);
  CHECK(source.size() == QSize(640, 400) && source.pixelColor(40, 40).alpha() == 0 &&
      source.colorSpace() == QColorSpace(QColorSpace::SRgb) && !source.text("LooksyCanvasId").isEmpty(),
      "Canvas dimensions, transparent background, identity and sRGB survive reading the file");
  const auto second = create_blank_canvas_source(temp.filePath("documents"), settings);
  CHECK(second.value("path") != created.value("path") &&
      QImage(second.value("path").toString()).text("LooksyCanvasId") != source.text("LooksyCanvasId"),
      "Two otherwise identical canvases remain distinct documents");
  for (const auto& invalid : {QVariantMap{{"width", 8193}}, QVariantMap{{"width", 8192}, {"height", 8192}},
      QVariantMap{{"width", 0}}, QVariantMap{{"width", 12.5}}, QVariantMap{{"background", "color"}, {"color", "invalid"}}}) {
    CHECK(!create_blank_canvas_source(temp.filePath("documents"), invalid).value("error").toString().isEmpty(),
        "Reject invalid or oversized document settings before importing");
  }
  EditorPreviewController controller;
  controller.loadAsset("blank-canvas", path, {}, {}, {}, {}, {});
  CHECK(controller.addPhotoShape("rectangle", 80, 80, 180, 180, 640, 400, "#4879ff", "#ffffff", 0) &&
      controller.addParagraphTextLayer("Editable canvas", 200, 100, 360, 100, 640, 400),
      "A blank canvas uses the ordinary editable shape and text tools");
  const auto state = EditorPreviewControllerTestAccess::state(controller);
  const auto json = serialize_edit_recipe_json(state);
  CHECK(validate_edit_recipe_json(json).valid, "Canvas composition validates for persistence");
  const auto reopened = parse_edit_recipe_json(json);
  const auto render = [&](const auto& recipe) {
    return render_output_image_with_backend(source, recipe, QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  };
  const auto output = render(state);
  CHECK(!output.isNull() && output.pixelColor(20, 20).alpha() == 0 && output.pixelColor(120, 120).alpha() == 255,
      "The canonical canvas render keeps transparency outside graphics");
  CHECK(output == render(reopened), "Saving and reopening preserves rendered canvas pixels exactly");
  EditorPreviewController restored;
  restored.loadAsset("blank-canvas", path, {}, {}, {}, {}, json);
  CHECK(restored.photoLayerCount() == 2 && restored.photoLayers().at(0).toMap().value("kind") == "text" &&
      !restored.photoLayers().at(1).toMap().value("shapeDefinition").toMap().isEmpty(),
      "Text and shape definitions stay editable after reopening");
  const auto export_path = temp.filePath("canvas-export.png");
  CHECK(controller.exportFlattenedPhoto(export_path) && wait_until([&] { return !controller.photoExportBusy(); }) &&
      controller.photoExportError().isEmpty(), "Export a composed canvas through the real export command");
  CHECK(QImage(export_path).pixelColor(20, 20).alpha() == 0, "PNG export retains blank-canvas transparency");
  std::cout << "Blank canvas creation, composition, reopen and alpha export passed\n";
  return 0;
}

static int verify_base_erase_and_clipboard(QTemporaryDir& temp) {
  using Access = EditorPreviewControllerTestAccess;
  QImage source(640, 400, QImage::Format_ARGB32);
  source.fill(QColor("#ac7954"));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto path = temp.filePath("base-source.png");
  CHECK(source.save(path), "Save the base-image fixture");
  EditorPreviewController controller;
  controller.loadAsset("base-edit", path, {}, {}, {}, {}, {});
  const QVariantMap eraser{{"erase", true}, {"size", 40}, {"hardness", 1}, {"opacity", 1}, {"flow", 1}, {"pressureSize", false}};
  CHECK(controller.baseImageLocked() && !controller.beginPhotoPaintStroke(-1, eraser), "The original base starts locked");
  CHECK(controller.setBaseImageLocked(false) && controller.photoLayerCount() == 0 && controller.photoLayers().isEmpty(),
      "Unlocking the base does not create a second visible photo layer");
  CHECK(controller.addPhotoShape("rectangle", 280, 120, 360, 220, 640, 400, "#4879ff", "#ffffff", 0), "Add graphics above the base");
  const auto before_erasing = Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(-1, eraser), "The dedicated Eraser can target the unlocked base");
  controller.addPhotoPaintPoint(100, 120);
  CHECK(controller.finishPhotoPaintStroke() && wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Commit base-image erasure");
  CHECK(controller.beginPhotoPaintStroke(-1, eraser), "Start erasing beneath an upper shape");
  controller.addPhotoPaintPoint(320, 170);
  CHECK(controller.finishPhotoPaintStroke() && wait_until([&] { return !controller.photoPaintBusy(); }), "Finish erasing under an upper shape");
  const auto erased = Access::state(controller);
  const auto render = [&](const auto& state) { return render_output_image_with_backend(source, state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly); };
  const auto output = render(erased);
  std::cout << "base_erase alpha=" << output.pixelColor(100,120).alpha()
            << " outside=" << output.pixelColor(20,20).alpha()
            << " shape=" << output.pixelColor(320,170).name(QColor::HexArgb).toStdString() << '\n';
  CHECK(output.pixelColor(100,120).alpha() == 0 && output.pixelColor(20,20).alpha() == 255 &&
      output.pixelColor(320,170) == render(before_erasing).pixelColor(320,170),
      "Erase only the base, preserving pixels outside the brush and graphics above it");
  CHECK(QImage(path) == source, "Base erasure never changes the source file");
  controller.undo();
  CHECK(render(Access::state(controller)).pixelColor(100,120).alpha() == 0, "One undo restores only the last base stroke");
  controller.undo();
  CHECK(render(Access::state(controller)).pixelColor(100,120).alpha() == 255, "Undo restores original base pixels");
  controller.redo(); controller.redo();
  CHECK(render(Access::state(controller)) == output, "Redo reproduces base erasure exactly");
  const auto reopened = parse_edit_recipe_json(serialize_edit_recipe_json(erased));
  CHECK(validate_edit_recipe_json(serialize_edit_recipe_json(erased)).valid && render(reopened) == output,
      "Base coverage survives canonical recipe validation and reopening");
  CHECK(controller.setBaseImageLocked(true) && !controller.beginPhotoPaintStroke(-1, eraser), "Relocking prevents further base erasure");
  CHECK(controller.setBaseImageLocked(false), "Unlock for selection erasure");
  controller.addMask("rectangle");
  controller.setActiveMaskFeather(0);
  controller.setActiveMaskCenterX(0.75); controller.setActiveMaskCenterY(0.5);
  controller.setActiveMaskRadiusX(0.25); controller.setActiveMaskRadiusY(0.5);
  CHECK(controller.erasePixelsFromSelection(-1) && wait_until([&] { return !controller.photoPaintBusy(); }), "Erase a selection on the unlocked base");
  CHECK(render(Access::state(controller)).pixelColor(500,200).alpha() == 0 &&
      render(Access::state(controller)).pixelColor(50,200).alpha() == 255, "Base selection erasure respects its boundary");
  controller.undo();
  CHECK(controller.addParagraphTextLayer("Copy this exactly", 200, 30, 360, 100, 640, 400) &&
      controller.linkActiveSelectionToPhotoLayer(0), "Create editable text with a linked selection");
  CHECK(controller.groupPhotoLayers({0,1}, "Composition"), "Group text and shape for clipboard transfer");
  const auto copied_state = Access::state(controller);
  CHECK(controller.copyPhotoLayers({0}) && wait_until([&] { return !controller.photoLayerClipboardBusy(); }) &&
      controller.hasCopiedPhotoLayers(), "Copy a group and its complete dependencies asynchronously");
  const auto payload = decode_photo_layer_clipboard(current_photo_layer_clipboard());
  CHECK(payload && payload->state.layers.size() == 3 && payload->state.mask_stack.size() == copied_state.mask_stack.size(),
      "The layer clipboard includes group children and linked selections");
  QImage destination_source(640,400,QImage::Format_ARGB32);
  destination_source.fill(QColor("#334055"));
  destination_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto destination_path = temp.filePath("destination.png");
  CHECK(destination_source.save(destination_path), "Save destination document");
  EditorPreviewController destination;
  destination.loadAsset("paste-document", destination_path, {}, {}, {}, {}, {});
  const auto pasted_ids = destination.pastePhotoLayers(0, true);
  CHECK(pasted_ids.size() == 1 && destination.photoLayerCount() == 3, "Paste the whole group above the active layer");
  const auto pasted_state = Access::state(destination);
  CHECK(pasted_state.layers.at(0).toMap().value("id") != copied_state.layers.at(0).toMap().value("id") &&
      pasted_state.layers.at(1).toMap().value("parentId") == pasted_state.layers.at(0).toMap().value("id") &&
      pasted_state.mask_stack.at(0).toMap().value("maskSetId") != copied_state.mask_stack.at(0).toMap().value("maskSetId"),
      "Paste remaps layer, group and selection identities rather than sharing source IDs");
  CHECK(pasted_state.layers.at(1).toMap().value("text") == "Copy this exactly" &&
      !pasted_state.layers.at(2).toMap().value("shapeDefinition").toMap().isEmpty(), "Pasted text and shapes remain editable");
  CHECK(destination.pastePhotoLayers(0).size() == 1 && destination.photoLayerCount() == 6 &&
      destination.photoLayers().at(0).toMap().value("x").toInt() == 0, "Repeated paste creates fresh IDs and preserves same-canvas placement");
  destination.undo();
  CHECK(Access::state(destination).layers == pasted_state.layers && Access::state(destination).mask_stack == pasted_state.mask_stack,
      "One undo removes the entire pasted group and its mask dependencies");
  destination.redo();
  CHECK(destination.photoLayerCount() == 6, "Redo restores the entire pasted group");
  const auto clipboard_before = current_photo_layer_clipboard();
  CHECK(controller.setPhotoLayerShapeFill(2, {{"fillColor", "#ff0000"}}) && current_photo_layer_clipboard() == clipboard_before,
      "Editing a source layer after Copy cannot change the clipboard snapshot");
  CHECK(controller.copyPhotoLayers({-1}) && wait_until([&] { return !controller.photoLayerClipboardBusy(); }), "Copy base pixels as an independent layer");
  const auto base_payload = decode_photo_layer_clipboard(current_photo_layer_clipboard());
  CHECK(base_payload && !base_payload->state.layers.at(0).toMap().value("copyBaseImage").toBool(),
      "The clipboard materializes base pixels instead of following the destination image");
  const auto copied_base = rasterize_photo_paint_layer(base_payload->state.layers.at(0).toMap()).convertedToColorSpace(QColorSpace(QColorSpace::SRgb));
  CHECK(copied_base.pixelColor(100,120).alpha() == 0 && copied_base.pixelColor(20,20).red() > 130,
      "Copied base pixels preserve source color and prior erasure");
  const auto moved_mask = rebase_photo_clipboard_mask({{"kind", "rectangle"}, {"centerX", 0.75},
      {"centerY", 0.5}, {"radiusX", 0.25}, {"radiusY", 0.5}}, QSize(640,400), QSize(800,500), 80, 50);
  CHECK(std::abs(moved_mask.value("centerX").toDouble() - 0.7) < 1e-9 &&
      std::abs(moved_mask.value("centerY").toDouble() - 0.5) < 1e-9 &&
      std::abs(moved_mask.value("radiusX").toDouble() - 0.2) < 1e-9,
      "Linked selection geometry retains its pixel footprint when pasting to a different canvas");
  QByteArray matte(8, char(0)); matte[1] = char(255); matte[2] = char(255); matte[5] = char(255); matte[6] = char(255);
  const auto moved_raster = rebase_photo_clipboard_mask({{"kind", "subject"}, {"segmentationData", matte},
      {"segmentationWidth", 4}, {"segmentationHeight", 2}}, QSize(8,4), QSize(12,8), 2, 2);
  const auto moved_alpha = moved_raster.value("segmentationData").toByteArray();
  CHECK(moved_raster.value("segmentationWidth").toInt() == 6 && moved_raster.value("segmentationHeight").toInt() == 4 &&
      moved_alpha.size() == 24 && static_cast<uchar>(moved_alpha[8]) == 255 && moved_alpha[0] == 0,
      "Copied raster selections move with the source-canvas footprint instead of stretching to the destination");
  auto* malformed = new QMimeData;
  malformed->setData(photo_layer_clipboard_mime, "{invalid}");
  QGuiApplication::clipboard()->setMimeData(malformed);
  const auto before_invalid = Access::state(destination);
  CHECK(destination.pastePhotoLayers().isEmpty() && Access::state(destination) == before_invalid,
      "Malformed clipboard data cannot mutate the document");
  std::cout << "Base eraser and layer clipboard, groups, selections, immutable pixels and atomic undo passed\n";
  return 0;
}

static int verify_board_owned_persistence(QTemporaryDir& temp) {
  QImage source(240, 160, QImage::Format_ARGB32);
  source.fill(Qt::white);
  const auto path = temp.filePath("board-owned-source.png");
  CHECK(source.save(path), "Write board-owned source fixture");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  const auto restore = qScopeGuard([] { qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1"); });
  EditorPreviewController controller;
  int document_commits = 0;
  QObject::connect(&controller, &EditorPreviewController::documentEditCommitted, [&] { ++document_commits; });
  CHECK(controller.catalogRecipePersistenceEnabled(), "Normal photo persistence is the default");
  controller.setCatalogRecipePersistenceEnabled(false);
  controller.loadAsset("board-session|slot:0:primary|edit:1", path, {}, {}, {}, {}, {});
  CHECK(controller.addPhotoShape("rectangle", 20, 20, 80, 60, 240, 160, "#334477", "#ffffff", 0) &&
      document_commits > 0 && !controller.recipeSavePending(),
      "Board edits publish accepted state without queuing a synthetic catalog asset");
  const auto accepted = document_commits;
  controller.setCatalogRecipePersistenceEnabled(true);
  controller.loadAsset({}, {}, {}, {}, {}, {}, {});
  CHECK(!controller.recipeSavePending() && document_commits > accepted,
      "Returning to normal persistence preserves ownership of the outgoing board asset");
  std::cout << "Board-owned persistence: accepted edits and source handoff never queue a synthetic catalog asset\n";
  return 0;
}

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  if (argc == 5 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--verify-photo-fragment-preset")) {
    const auto image = PhotoFragmentBrushPresetStore::imageFromPreset(
        PhotoFragmentBrushPresetStore::load(QString::fromLocal8Bit(argv[2])));
    if (image.isNull() || image.width() != QString::fromLocal8Bit(argv[3]).toInt() ||
        image.height() != QString::fromLocal8Bit(argv[4]).toInt()) return 2;
    QByteArray png;
    QBuffer buffer(&png);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return 3;
    std::cout << QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex().constData() << '\n';
    return 0;
  }
  QTemporaryDir temp;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  CHECK(AppBootstrap::ensureRuntimeDirectories(), "Runtime directories");
  if (app.arguments().contains("--shape-fills")) return verify_shape_fills(temp);
  if (app.arguments().contains("--blank-canvas")) return verify_blank_canvas(temp);
  if (app.arguments().contains("--board-owned-persistence")) return verify_board_owned_persistence(temp);
  if (app.arguments().contains("--document-workflow")) return verify_base_erase_and_clipboard(temp);
  const QVariantMap empty{{"pixelWidth",640},{"pixelHeight",400},
      {"paintTiles",empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  const auto raster = [](const PhotoPixelStroke& value) { return value.surface.raster({},nullptr,value.draft_tiles()); };
  PhotoPixelStroke stroke;
  stroke.color = QColor("#ff4000"); stroke.diameter = 40; stroke.opacity = 0.5;
  stroke.pressure_size = false;
  CHECK(stroke.begin(empty), "Stroke initialization");
  stroke.point({80,100},1); stroke.point({400,100},1); stroke.point({80,100},1); stroke.finish();
  const auto stroke_image = raster(stroke);
  CHECK(qAlpha(stroke_image.pixel(200,100)) == 128, "Overlapping dabs must respect stroke opacity");
  CHECK(qAlpha(stroke_image.pixel(200,121)) == 0, "Circular brush must remain local on non-square images");
  PhotoPixelStroke segmented;
  segmented.color=stroke.color; segmented.diameter=40; segmented.opacity=0.5; segmented.pressure_size=false;
  CHECK(segmented.begin(empty), "Segmented initialization"); segmented.point({80,100},1);
  for(int x=81;x<=400;++x) segmented.point({double(x),100},1);
  for(int x=399;x>=80;--x) segmented.point({double(x),100},1);
  segmented.finish();
  CHECK(raster(segmented)==stroke_image, "Pointer event density must not change the stroke");
  QImage fragment_tip(5, 5, QImage::Format_ARGB32_Premultiplied);
  fragment_tip.setColorSpace(QColorSpace(QColorSpace::SRgb));
  fragment_tip.fill(QColor(220, 70, 30, 210));
  fragment_tip.setPixelColor(2, 2, QColor(0, 0, 0, 0));
  fragment_tip.setPixelColor(0, 0, QColor(220, 70, 30, 75));
  PhotoPixelStroke photo_fragments;
  photo_fragments.diameter=50; photo_fragments.pressure_size=false; photo_fragments.spacing=1.0;
  photo_fragments.texture_material="photo_fragment"; photo_fragments.texture_seed=0x5eed;
  photo_fragments.tip_image=fragment_tip; photo_fragments.tip_rotation_variation=37;
  photo_fragments.tip_scale_variation=0.35;
  CHECK(photo_fragments.begin(empty), "Photo-fragment stamp initialization");
  fragment_tip.fill(Qt::transparent);  // The active brush owns a frozen tip snapshot.
  photo_fragments.point({100,100},1); photo_fragments.point({180,100},1); photo_fragments.finish();
  const auto photo_fragment_image=raster(photo_fragments);
  bool has_soft_fragment_alpha = false;
  for (int y=70; y<130; ++y) for (int x=70; x<130; ++x) {
    const auto alpha = qAlpha(photo_fragment_image.pixel(x,y));
    has_soft_fragment_alpha = has_soft_fragment_alpha || (alpha > 0 && alpha < 210);
  }
  CHECK(qAlpha(photo_fragment_image.pixel(100,100))<64 &&
      qAlpha(photo_fragment_image.pixel(90,100))>0 && has_soft_fragment_alpha,
      "Photo-fragment stamps retain transparent holes and soft source alpha after source edits");
  QImage opaque_fragment_tip(16, 8, QImage::Format_RGBA8888);
  opaque_fragment_tip.setColorSpace(QColorSpace(QColorSpace::SRgb));
  opaque_fragment_tip.fill(QColor(230, 210, 180, 255));
  PhotoPixelStroke rotated_opaque_fragment;
  rotated_opaque_fragment.diameter = 48;
  rotated_opaque_fragment.pressure_size = false;
  rotated_opaque_fragment.spacing = 1.0;
  rotated_opaque_fragment.texture_material = "photo_fragment";
  rotated_opaque_fragment.texture_seed = 0x5eed;
  rotated_opaque_fragment.tip_image = opaque_fragment_tip;
  rotated_opaque_fragment.tip_rotation_variation = 45;
  CHECK(rotated_opaque_fragment.begin(empty), "Opaque rotated Photo Fragment initialization");
  rotated_opaque_fragment.point({260, 120}, 1);
  rotated_opaque_fragment.finish();
  const auto rotated_opaque_fragment_image = raster(rotated_opaque_fragment);
  auto has_interpolated_fragment_edge = false;
  for (int y = 80; y < 160; ++y) for (int x = 220; x < 300; ++x) {
    const auto alpha = qAlpha(rotated_opaque_fragment_image.pixel(x, y));
    has_interpolated_fragment_edge = has_interpolated_fragment_edge || (alpha > 0 && alpha < 255);
  }
  CHECK(has_interpolated_fragment_edge,
      "Rotated opaque photo fragments use bilinear coverage instead of jagged nearest-neighbor edges");
  QImage aliased_fragment_tip(128, 16, QImage::Format_RGBA8888);
  aliased_fragment_tip.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < aliased_fragment_tip.height(); ++y) {
    for (int x = 0; x < aliased_fragment_tip.width(); ++x) {
      const auto stripe = x % 2 == 0 ? 0 : 255;
      aliased_fragment_tip.setPixelColor(x, y, QColor(stripe, stripe, stripe, 255));
    }
  }
  PhotoPixelStroke downscaled_fragment;
  downscaled_fragment.diameter = 32;
  downscaled_fragment.pressure_size = false;
  downscaled_fragment.spacing = 1.0;
  downscaled_fragment.texture_material = "photo_fragment";
  downscaled_fragment.tip_image = aliased_fragment_tip;
  CHECK(downscaled_fragment.begin(empty), "Downscaled Photo Fragment initialization");
  downscaled_fragment.point({320, 120}, 1);
  downscaled_fragment.finish();
  const auto downscaled_fragment_image = raster(downscaled_fragment);
  float fragment_red_min = std::numeric_limits<float>::infinity();
  float fragment_red_max = -std::numeric_limits<float>::infinity();
  for (int x = 312; x < 328; ++x) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(downscaled_fragment_image.constScanLine(120));
    fragment_red_min = std::min(fragment_red_min, row[x].red());
    fragment_red_max = std::max(fragment_red_max, row[x].red());
  }
  CHECK(fragment_red_min > 0.4F && fragment_red_max < 0.6F &&
      fragment_red_max - fragment_red_min < 0.08F,
      "Photo-fragment downsampling filters high-frequency source detail instead of aliasing it into repeated dabs");
  PhotoPixelStroke repeated_photo_fragments;
  repeated_photo_fragments.diameter=50; repeated_photo_fragments.pressure_size=false;
  repeated_photo_fragments.spacing=1.0; repeated_photo_fragments.texture_material="photo_fragment";
  repeated_photo_fragments.texture_seed=0x5eed; repeated_photo_fragments.tip_image=photo_fragments.tip_image;
  repeated_photo_fragments.tip_rotation_variation=37; repeated_photo_fragments.tip_scale_variation=0.35;
  CHECK(repeated_photo_fragments.begin(empty), "Repeatable photo-fragment initialization");
  repeated_photo_fragments.point({100,100},1);
  for (int x=101; x<=180; ++x) repeated_photo_fragments.point({static_cast<double>(x),100},1);
  repeated_photo_fragments.finish();
  CHECK(raster(repeated_photo_fragments)==photo_fragment_image,
      "Seeded photo-fragment scale and rotation variation is deterministic across input sampling density");
  PhotoPixelStroke solid_material;
  solid_material.color=QColor("#20242a"); solid_material.diameter=44; solid_material.pressure_size=false;
  CHECK(solid_material.begin(empty), "Solid material initialization");
  solid_material.point({64,210},0.9); solid_material.point({540,210},0.9); solid_material.finish();
  const auto solid_material_image=raster(solid_material);
  PhotoPixelStroke dry_ink;
  dry_ink.color=solid_material.color; dry_ink.diameter=solid_material.diameter; dry_ink.pressure_size=false;
  dry_ink.texture_material="dry_ink"; dry_ink.texture_strength=0.9; dry_ink.texture_scale=6;
  dry_ink.texture_angle=24; dry_ink.texture_seed=0x5eed;
  CHECK(dry_ink.begin(empty), "Dry ink initialization");
  dry_ink.point({64,210},0.9); dry_ink.point({540,210},0.9); dry_ink.finish();
  const auto dry_ink_image=raster(dry_ink);
  PhotoPixelStroke repeated_dry_ink;
  repeated_dry_ink.color=solid_material.color; repeated_dry_ink.diameter=solid_material.diameter;
  repeated_dry_ink.pressure_size=false; repeated_dry_ink.texture_material="dry_ink";
  repeated_dry_ink.texture_strength=0.9; repeated_dry_ink.texture_scale=6;
  repeated_dry_ink.texture_angle=24; repeated_dry_ink.texture_seed=0x5eed;
  CHECK(repeated_dry_ink.begin(empty), "Repeatable dry ink initialization");
  repeated_dry_ink.point({64,210},0.9);
  for (int x=65; x<=540; ++x) repeated_dry_ink.point({static_cast<double>(x),210},0.9);
  repeated_dry_ink.finish();
  CHECK(raster(repeated_dry_ink)==dry_ink_image,
      "Document-anchored material grain is deterministic and independent of pointer sample density");
  PhotoPixelStroke graphite;
  graphite.color=solid_material.color; graphite.diameter=solid_material.diameter; graphite.pressure_size=false;
  graphite.texture_material="graphite"; graphite.texture_strength=0.9; graphite.texture_scale=6;
  graphite.texture_angle=24; graphite.texture_seed=0x5eed;
  CHECK(graphite.begin(empty), "Graphite initialization");
  graphite.point({64,210},0.9); graphite.point({540,210},0.9); graphite.finish();
  const auto graphite_image=raster(graphite);
  CHECK(dry_ink_image!=solid_material_image && graphite_image!=solid_material_image &&
      graphite_image!=dry_ink_image, "Dry ink and graphite produce distinct textured coverage");
  PhotoPixelStroke light_paint;
  light_paint.color=QColor("#ff8040"); light_paint.diameter=40; light_paint.pressure_size=false;
  light_paint.texture_material="light_paint"; light_paint.light_falloff=0.8;
  light_paint.light_exposure=3.0; light_paint.opacity=0.5;
  CHECK(light_paint.begin(empty), "Light paint initialization");
  light_paint.point({160,300},1); light_paint.finish();
  const auto light_image=raster(light_paint);
  const auto* light_center=reinterpret_cast<const QRgbaFloat32*>(light_image.constScanLine(300));
  const auto* light_tail=reinterpret_cast<const QRgbaFloat32*>(light_image.constScanLine(300));
  CHECK(std::isfinite(light_center[160].red()) && light_center[160].red()>1.0F &&
      light_center[160].alpha()>0.0F && light_center[205].alpha()>0.0F &&
      light_tail[215].alpha()==0.0F,
      "Light paint writes finite HDR core pixels with a soft halo and bounded support");
  for (int y=0; y<light_image.height(); ++y) {
    const auto* row=reinterpret_cast<const QRgbaFloat32*>(light_image.constScanLine(y));
    for (int x=0; x<light_image.width(); ++x)
      CHECK(std::isfinite(row[x].red()) && std::isfinite(row[x].green()) &&
          std::isfinite(row[x].blue()) && std::isfinite(row[x].alpha()),
          "Light-paint storage must remain finite across the entire tile surface");
  }
  PhotoPixelStroke tapered_light;
  tapered_light.color=QColor("#ff8040"); tapered_light.diameter=36;
  tapered_light.pressure_size=false; tapered_light.texture_material="light_paint";
  tapered_light.light_falloff=0.4; tapered_light.light_exposure=2; tapered_light.light_taper=0.6;
  CHECK(tapered_light.begin(empty), "Tapered light initialization");
  tapered_light.point({100,200},1); tapered_light.point({400,200},1); tapered_light.finish();
  const auto tapered_image=raster(tapered_light);
  const auto* tapered_row=reinterpret_cast<const QRgbaFloat32*>(tapered_image.constScanLine(200));
  CHECK(tapered_row[250].alpha()>0.8F && tapered_row[100].alpha()<0.25F &&
      tapered_row[400].alpha()<0.25F,
      "Finished light strokes taper at both endpoints while retaining a luminous center");
  PhotoPixelStroke sampled_tapered_light;
  sampled_tapered_light.color=tapered_light.color; sampled_tapered_light.diameter=tapered_light.diameter;
  sampled_tapered_light.pressure_size=false; sampled_tapered_light.texture_material="light_paint";
  sampled_tapered_light.light_falloff=tapered_light.light_falloff;
  sampled_tapered_light.light_exposure=tapered_light.light_exposure;
  sampled_tapered_light.light_taper=tapered_light.light_taper;
  CHECK(sampled_tapered_light.begin(empty), "Sampled tapered light initialization");
  sampled_tapered_light.point({100,200},1);
  for (int x=101; x<=400; ++x) sampled_tapered_light.point({static_cast<double>(x),200},1);
  sampled_tapered_light.finish();
  const auto sampled_tapered_image=raster(sampled_tapered_light);
  double taper_sample_error=0;
  for (int y=0; y<tapered_image.height(); ++y) {
    const auto* expected=reinterpret_cast<const QRgbaFloat32*>(tapered_image.constScanLine(y));
    const auto* actual=reinterpret_cast<const QRgbaFloat32*>(sampled_tapered_image.constScanLine(y));
    for (int x=0; x<tapered_image.width(); ++x) {
      taper_sample_error=std::max(taper_sample_error, std::abs(double(expected[x].red()-actual[x].red())));
      taper_sample_error=std::max(taper_sample_error, std::abs(double(expected[x].green()-actual[x].green())));
      taper_sample_error=std::max(taper_sample_error, std::abs(double(expected[x].blue()-actual[x].blue())));
      taper_sample_error=std::max(taper_sample_error, std::abs(double(expected[x].alpha()-actual[x].alpha())));
    }
  }
  CHECK(taper_sample_error<=1.0/255.0,
      "Stroke endpoint taper stays within one SDR code step across pointer sample densities");
  PhotoPixelStroke accumulated_light;
  accumulated_light.color=light_paint.color; accumulated_light.diameter=light_paint.diameter;
  accumulated_light.pressure_size=false; accumulated_light.texture_material="light_paint";
  accumulated_light.light_falloff=light_paint.light_falloff;
  accumulated_light.opacity=light_paint.opacity; accumulated_light.flow=0.25;
  CHECK(accumulated_light.begin(empty), "Accumulating light initialization");
  accumulated_light.point({160,300},1); accumulated_light.point({220,300},1);
  accumulated_light.point({160,300},1); accumulated_light.finish();
  const auto accumulated_image=raster(accumulated_light);
  PhotoPixelStroke non_accumulating_light;
  non_accumulating_light.color=light_paint.color; non_accumulating_light.diameter=light_paint.diameter;
  non_accumulating_light.pressure_size=false; non_accumulating_light.texture_material="light_paint";
  non_accumulating_light.light_falloff=light_paint.light_falloff;
  non_accumulating_light.opacity=light_paint.opacity; non_accumulating_light.flow=0.25;
  non_accumulating_light.light_accumulation=false;
  CHECK(non_accumulating_light.begin(empty), "Non-accumulating light initialization");
  non_accumulating_light.point({160,300},1); non_accumulating_light.point({220,300},1);
  non_accumulating_light.point({160,300},1); non_accumulating_light.finish();
  const auto non_accumulating_image=raster(non_accumulating_light);
  const auto* accumulated_row=reinterpret_cast<const QRgbaFloat32*>(accumulated_image.constScanLine(300));
  const auto* stable_row=reinterpret_cast<const QRgbaFloat32*>(non_accumulating_image.constScanLine(300));
  CHECK(accumulated_row[160].alpha()>stable_row[160].alpha(),
      "Light accumulation increases overlap energy while the restrained mode caps repeated coverage");
  PhotoPixelStroke disabled_texture;
  disabled_texture.color=solid_material.color; disabled_texture.diameter=solid_material.diameter;
  disabled_texture.pressure_size=false; disabled_texture.texture_material="dry_ink";
  disabled_texture.texture_strength=0; disabled_texture.texture_seed=0x5eed;
  CHECK(disabled_texture.begin(empty), "Zero texture initialization");
  disabled_texture.point({64,210},0.9); disabled_texture.point({540,210},0.9); disabled_texture.finish();
  CHECK(raster(disabled_texture)==solid_material_image,
      "Zero texture strength is exactly the solid brush result");
  auto colored=empty; colored.insert("paintTiles",stroke.commit());
  PhotoPixelStroke eraser;
  eraser.erase=true; eraser.opacity=0.5; eraser.diameter=40; eraser.pressure_size=false;
  CHECK(eraser.begin(colored), "Eraser initialization"); eraser.point({200,100},1);
  const auto erased_image=raster(eraser);
  CHECK(qAlpha(erased_image.pixel(200,100))==64 && erased_image.pixel(90,100)==stroke_image.pixel(90,100),
      "Eraser must preserve pixels beyond its footprint");
  const auto earlier_tiles=colored.value("paintTiles").toMap().value("tiles").toMap();
  const auto later_tiles=eraser.commit().value("tiles").toMap();
  const auto untouched=PhotoPaintSurface::key(0,0);
  CHECK(earlier_tiles.value(untouched).toString().constData()==later_tiles.value(untouched).toString().constData(),
      "Undo states must share unchanged compressed tile storage");
  PhotoPixelStroke clipped; clipped.diameter=80; clipped.pressure_size=false;
  clipped.selection=QImage(640,400,QImage::Format_Grayscale8); clipped.selection.fill(0);
  for(int y=0;y<400;++y) for(int x=0;x<320;++x) clipped.selection.scanLine(y)[x]=128;
  CHECK(clipped.begin(empty), "Clipped initialization"); clipped.point({320,200},1);
  const auto clipped_image=raster(clipped);
  CHECK(qAlpha(clipped_image.pixel(310,200))==128 && qAlpha(clipped_image.pixel(321,200))==0,
      "Soft selections must clip paint without crossing boundaries");
  PhotoPixelStroke pressure; pressure.diameter=40;
  CHECK(pressure.begin(empty), "Pressure initialization"); pressure.point({50,50},0.25); pressure.point({150,50},1);
  const auto pressure_image=raster(pressure);
  CHECK(qAlpha(pressure_image.pixel(50,58))==0 && qAlpha(pressure_image.pixel(140,58))>0, "Pressure radius");
  QVariantMap transform{{"pixelWidth",640},{"pixelHeight",400},{"x",20},{"y",30},{"scale",0.5},{"rotation",90}};
  PhotoPixelStroke transformed; transformed.diameter=10; transformed.pressure_size=false;
  transformed.layer_to_document=photo_pixel_layer_transform(transform);
  CHECK(transformed.begin(empty), "Transform initialization");
  transformed.point(transformed.layer_to_document.map(QPointF(100,80)),1);
  CHECK(qAlpha(raster(transformed).pixel(100,80))>0, "Inverse layer transform");
  auto wide=empty;
  wide.insert("paintTiles",empty_photo_paint_tiles(working_color_space("Linear Display P3")));
  PhotoPaintSurface high_precision; CHECK(high_precision.load(wide), "Wide-gamut surface");
  auto hdr_tile=high_precision.tile(0,0);
  auto* hdr=reinterpret_cast<QRgbaFloat32*>(hdr_tile.bits());
  const QRgbaFloat32 hdr_value{1.678901F,-0.012345F,0.1234567F,0.876543F};
  for(int i=0;i<hdr_tile.width()*hdr_tile.height();++i) hdr[i]=hdr_value;
  high_precision.store(0,0,hdr_tile); wide.insert("paintTiles",high_precision.snapshot());
  const auto hdr_reopened=rasterize_photo_paint_layer(wide);
  CHECK(*reinterpret_cast<const QRgbaFloat32*>(hdr_reopened.constBits())==hdr_value,
      "Float tiles must preserve negative, HDR and sub-16-bit values exactly");
  PhotoPixelStroke hdr_eraser; hdr_eraser.erase=true; hdr_eraser.diameter=20; hdr_eraser.opacity=0.123456;
  hdr_eraser.pressure_size=false; CHECK(hdr_eraser.begin(wide), "HDR erase"); hdr_eraser.point({50,50},1);
  const auto hdr_result=raster(hdr_eraser);
  const auto result_pixel=reinterpret_cast<const QRgbaFloat32*>(hdr_result.constScanLine(50))[50];
  CHECK(std::abs(result_pixel.red()-hdr_value.red()*0.876544F)<1e-6F && result_pixel.green()<0,
      "Erasing must preserve floating precision and extended gamut");
  const auto wide_durable=hdr_eraser.commit();
  wide.insert("paintTiles",wide_durable);
  EditRecipeState hdr_state; hdr_state.layers={wide};
  const auto hdr_json=serialize_edit_recipe_json(hdr_state);
  CHECK(validate_edit_recipe_json(hdr_json).valid, "Version 16 paint recipe validates");
  CHECK(!migrate_edit_recipe_json(hdr_json,15).errors.isEmpty(), "Older recipe versions must not silently discard paint");
  const auto hdr_parsed=parse_edit_recipe_json(hdr_json);
  CHECK(rasterize_photo_paint_layer(hdr_parsed.layers[0].toMap())==hdr_result, "Float tile recipe round-trip exactness");
  auto corrupt=wide; auto payload=wide.value("paintTiles").toMap(); auto bad_tiles=payload.value("tiles").toMap();
  bad_tiles.insert("0,0","invalid"); payload.insert("tiles",bad_tiles); corrupt.insert("paintTiles",payload);
  QString corruption_error;
  CHECK(rasterize_photo_paint_layer(corrupt,{},&corruption_error).isNull() && !corruption_error.isEmpty(), "Corrupt tiles must fail explicitly");
  auto large=empty; large.insert("pixelWidth",16000); large.insert("pixelHeight",12000);
  PhotoPixelStroke sparse; CHECK(sparse.begin(large), "192 MP sparse canvas initialization"); sparse.diameter=32; sparse.point({1000,1000},1);
  CHECK(sparse.changed_tile_count()<=4 && sparse.commit().value("tiles").toMap().size()<=4,
      "A small stroke on a 192 MP canvas must allocate only touched tiles");

  QImage source(640,400,QImage::Format_ARGB32); source.fill(QColor("#304052"));
  const auto photo_path = qEnvironmentVariable("PHOTO_PAINT_TEST_SOURCE");
  if (!photo_path.isEmpty()) {
    QImage photo(photo_path);
    CHECK(!photo.isNull(), "Photographic proof fixture loads");
    source = photo.scaled(source.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto source_path = temp.filePath("source.png"); CHECK(source.save(source_path), "Source PNG");
  QQmlEngine engine;
  auto* provider = new EditorPreviewImageProvider;
  engine.addImageProvider("editor-preview", provider);
  EditorPreviewController controller;
  controller.setImageProvider(provider);
  controller.loadAsset("paint-proof", source_path, {}, {}, {}, {}, {});
  CHECK(wait_until([&] { return !controller.displayPreviewSource().isEmpty(); }), "Source preview");
  CHECK(controller.addPaintLayer(), "Transparent layer creation");
  using Access = EditorPreviewControllerTestAccess;
  const auto blank = Access::state(controller);
  const QVariantMap settings{{"color","#ff7040"},{"size",40},{"opacity",0.65},{"pressureSize",false}};
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Controller begins stroke");
  controller.addPhotoPaintPoint(100,100); controller.addPhotoPaintPoint(400,100);
  CHECK(Access::state(controller).layers == blank.layers, "Draft must not mutate persisted state");
  CHECK(wait_until([&] { return Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Worker publishes draft tiles");
  CHECK(wait_until([&] { return Access::paintRasterIdle(controller); }), "All pending stroke samples reach the draft");
  const auto draft_request = Access::request(controller);
  const auto draft_layers = compile_pipeline_photo_layers(draft_request.state.layers, {},
      LocalMaskRasterPolicy::UseStoredRaster, QSize(640,400), {}, {}, DesktopRenderPurpose::Preview);
  CHECK(draft_layers.size() == 1 && draft_layers[0].pixels[100 * 640 + 200].alpha > 0,
      "Draft image must reach actual layer compilation before pointer release");
  CHECK(!draft_request.state.layers[0].toMap().value("paintDraftTiles").toMap().isEmpty(), "Draft renderer must see live pixels");
  CHECK(regional_matches_full(draft_request, blank.layers),
        "Regional paint must agree with the complete color-managed preview");
  CHECK(controller.finishPhotoPaintStroke(), "Stroke commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto painted = Access::state(controller);
  const auto painted_image = rasterize_photo_paint_layer(painted.layers[0].toMap());
  CHECK(qAlpha(painted_image.pixel(200,100)) > 0, "Committed color pixels");
  controller.undo(); CHECK(Access::state(controller).layers == blank.layers, "One undo restores entire stroke");
  controller.redo(); CHECK(Access::state(controller).layers == painted.layers, "Redo restores pixels");
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Cancel begins"); controller.addPhotoPaintPoint(100,200);
  controller.finishPhotoPaintStroke(false);
  CHECK(Access::state(controller).layers == painted.layers, "Cancel preserves durable image");
  auto clipped_settings = settings; clipped_settings.insert("clipSelection", true);
  CHECK(!controller.beginPhotoPaintStroke(0,clipped_settings), "Missing selection must never paint unclipped");
  controller.addMask("rectangle");
  controller.setActiveMaskFeather(0);
  controller.setActiveMaskCenterX(0.25); controller.setActiveMaskCenterY(0.5);
  controller.setActiveMaskRadiusX(0.25); controller.setActiveMaskRadiusY(0.5);
  int paint_index = -1;
  const auto selection_state = Access::state(controller);
  for (int i = 0; i < selection_state.layers.size(); ++i)
    if (selection_state.layers[i].toMap().value("id") == painted.layers[0].toMap().value("id")) paint_index = i;
  CHECK(controller.beginPhotoPaintStroke(paint_index,clipped_settings),
      "Actual canonical selection clipping: " << controller.status().toStdString());
  controller.addPhotoPaintPoint(100,200); controller.addPhotoPaintPoint(500,200);
  CHECK(controller.finishPhotoPaintStroke(), "Selection-clipped stroke commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto selection_pixels = rasterize_photo_paint_layer(Access::state(controller).layers[paint_index].toMap());
  CHECK(qAlpha(selection_pixels.pixel(150,200)) > 0 && qAlpha(selection_pixels.pixel(400,200)) == 0,
      "Controller stroke must honor the real active selection boundary");
  CHECK(controller.copyPixelsFromSelection(), "Copy selected photo pixels");
  const auto cutout_before = Access::state(controller);
  auto cutout_erase = settings;
  cutout_erase.insert("erase", true); cutout_erase.insert("opacity", 1.0);
  cutout_erase.insert("clipSelection", true);
  CHECK(controller.beginPhotoPaintStroke(0, cutout_erase), "Erase copied photo pixels");
  controller.addPhotoPaintPoint(150, 200);
  CHECK(controller.finishPhotoPaintStroke(), "Commit cutout eraser");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Cutout materialization: " << controller.photoPaintError().toStdString());
  const auto cutout_after = Access::state(controller);
  CHECK(!cutout_after.layers[0].toMap().value("copyBaseImage").toBool(), "Edited cutout contains real pixels");
  const auto cutout_pixels = rasterize_photo_paint_layer(cutout_after.layers[0].toMap());
  CHECK(!cutout_pixels.isNull() && qAlpha(cutout_pixels.pixel(150, 200)) == 0 &&
      qAlpha(cutout_pixels.pixel(50, 200)) == 255, "Cutout eraser removes only its footprint");
  const auto before_render = render_output_image_with_backend(source, cutout_before, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  const auto after_render = render_output_image_with_backend(source, cutout_after, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  CHECK(!before_render.isNull() && !after_render.isNull(), "Cutout renders before and after editing");
  for (int y = 0; y < source.height(); ++y) for (int x = 0; x < source.width(); ++x) {
    if (std::abs(x - 150) < 25 && std::abs(y - 200) < 25) continue;
    const auto a = before_render.pixelColor(x, y), b = after_render.pixelColor(x, y);
    CHECK(std::abs(a.red()-b.red()) <= 1 && std::abs(a.green()-b.green()) <= 1 && std::abs(a.blue()-b.blue()) <= 1,
        "Materializing a cutout must preserve rendered color outside the erase footprint");
  }
  controller.undo(); CHECK(Access::state(controller).layers == cutout_before.layers, "Undo cutout materialization and erase together");
  controller.redo(); CHECK(Access::state(controller).layers == cutout_after.layers, "Redo cutout pixels");
  const auto erase_command = controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "erase_selection"}, {"layerId", cutout_after.layers[0].toMap().value("id")},
      {"values", QVariantMap{{"maskId", controller.maskStack().at(controller.activeMaskIndex()).toMap().value("maskSetId")}}}});
  CHECK(erase_command.value("ok").toBool() && erase_command.value("paintBusy").toBool(),
      "Agent selection erase queues the same pixel operation");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Selection erase completes");
  const auto selection_erased = rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap());
  CHECK(qAlpha(selection_erased.pixel(50, 200)) == 0 && qAlpha(selection_erased.pixel(500, 200)) == 255,
      "Selection erase respects selection and preserves pixels outside");
  controller.undo(); CHECK(Access::state(controller).layers == cutout_after.layers, "Undo whole selection erase");
  const auto before_agent_paint=Access::state(controller);
  const auto paint_target=before_agent_paint.layers[0].toMap().value("id").toString();
  const auto agent_paint=controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "paint"}, {"layerId", paint_target},
      {"values", QVariantMap{{"points", QVariantList{QVariantMap{{"x", 280.0}, {"y", 250.0}, {"pressure", 0.7}},
          QVariantMap{{"x", 340.0}, {"y", 250.0}, {"pressure", 1.0}}}}, {"color", "#ffcf76"},
          {"size", 36.0}, {"textureMaterial", "graphite"}, {"textureStrength", 0.8},
          {"textureScale", 5.0}, {"textureAngle", 18.0}, {"textureSeed", 9231}}}});
  CHECK(agent_paint.value("ok").toBool() && agent_paint.value("paintBusy").toBool(),
      "Agent composition paint dispatches a typed graphite stroke to the selected pixel layer");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Agent graphite stroke completes through the user paint pipeline");
  CHECK(Access::state(controller).layers != before_agent_paint.layers,
      "Agent paint commits pixels to the requested target");
  controller.undo(); CHECK(Access::state(controller).layers == before_agent_paint.layers,
      "Agent paint remains one undoable operation");
  const auto agent_light_paint=controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "paint"}, {"layerId", paint_target},
      {"values", QVariantMap{{"points", QVariantList{QVariantMap{{"x", 180.0}, {"y", 300.0}, {"pressure", 0.8}},
          QVariantMap{{"x", 500.0}, {"y", 300.0}, {"pressure", 1.0}}}}, {"color", "#ffcf76"},
          {"size", 36.0}, {"textureMaterial", "light_paint"}, {"lightFalloff", 0.9},
          {"lightExposure", 3.0}, {"lightTaper", 0.6}, {"lightAccumulation", true}}}});
  CHECK(agent_light_paint.value("ok").toBool() && agent_light_paint.value("paintBusy").toBool(),
      "Agent composition accepts typed light-paint exposure, falloff, and accumulation controls");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Agent HDR light stroke completes through the shared paint worker");
  const auto agent_light_layer=Access::state(controller).layers[0].toMap();
  const auto agent_light_state=Access::state(controller);
  const auto agent_light_pixels=rasterize_photo_paint_layer(agent_light_layer);
  const auto* agent_light_row=reinterpret_cast<const QRgbaFloat32*>(agent_light_pixels.constScanLine(300));
  CHECK(agent_light_row[280].red()>1.0F && agent_light_row[280].alpha()>0,
      "Agent light paint stores extended-range radiance in editable pixel tiles");
  const auto linear_light_preview=render_output_image_with_backend(source, agent_light_state,
      working_color_space("Linear ProPhoto RGB"), QImage::Format_RGBA32FPx4_Premultiplied,
      RenderBackendPreference::CpuOnly, nullptr, nullptr, {},
      photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::SceneLinearWorking);
  const auto* composed_light_row=linear_light_preview.isNull() ? nullptr :
      reinterpret_cast<const QRgbaFloat32*>(linear_light_preview.constScanLine(300));
  CHECK(!linear_light_preview.isNull() &&
      std::isfinite(composed_light_row[280].red()) && composed_light_row[280].red()>1.0F,
      "Scene-linear CPU composition preserves finite HDR light-paint radiance");
  const auto display_light_preview=render_output_image_with_backend(source, agent_light_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64, RenderBackendPreference::CpuOnly);
  const auto light_export_path=temp.filePath("light-paint-export.png");
  CHECK(!display_light_preview.isNull() && controller.exportFlattenedPhoto(light_export_path),
      "Render preview and start a lossless light-paint export");
  const auto light_export=QImage(light_export_path);
  CHECK(!light_export.isNull() && light_export.size()==display_light_preview.size(),
      "Lossless light-paint export has the preview dimensions");
  int light_export_max_error=0;
  for (int y=0; y<light_export.height(); ++y) for (int x=0; x<light_export.width(); ++x) {
    const auto expected=display_light_preview.pixelColor(x,y);
    const auto actual=light_export.pixelColor(x,y);
    light_export_max_error=std::max(light_export_max_error,std::max({
        std::abs(expected.red()-actual.red()),std::abs(expected.green()-actual.green()),
        std::abs(expected.blue()-actual.blue()),std::abs(expected.alpha()-actual.alpha())}));
  }
  CHECK(light_export_max_error<=1,
      "CPU preview and lossless PNG export agree within one 8-bit code value");
  controller.undo(); CHECK(Access::state(controller).layers == before_agent_paint.layers,
      "Agent light painting is one undoable operation");
  CHECK(!controller.photoCompositionCommand({{"assetId", "paint-proof"}, {"operation", "paint"},
      {"layerId", paint_target}, {"values", QVariantMap{{"points", QVariantList{QVariantMap{{"x", 10.0}, {"y", 10.0}}}},
          {"textureMaterial", "light_paint"}, {"lightExposure", 9.0}}}}).value("ok").toBool(),
      "Agent rejects light-paint exposure outside its documented range");
  Access::apply(controller, painted);
  auto locked = painted; auto layer = locked.layers[0].toMap(); layer.insert("locked",true); locked.layers[0] = layer;
  Access::apply(controller, locked);
  CHECK(!controller.beginPhotoPaintStroke(0,settings), "Locked pixels must reject strokes");
  Access::apply(controller, painted);
  auto erased_settings = settings; erased_settings.insert("erase",true); erased_settings.insert("opacity",0.5);
  const auto before_erase = Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,erased_settings), "Controller erase"); controller.addPhotoPaintPoint(200,100);
  CHECK(wait_until([&] { return Access::paintRasterIdle(controller) &&
        Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Eraser draft settles");
  CHECK(regional_matches_full(Access::request(controller), before_erase.layers),
        "Regional erasing must agree with the complete color-managed preview");
  CHECK(controller.finishPhotoPaintStroke(), "Erase commit");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Async paint commit completes");
  const auto erased = Access::state(controller);
  CHECK(qAlpha(rasterize_photo_paint_layer(erased.layers[0].toMap()).pixel(200,100)) <
        qAlpha(painted_image.pixel(200,100)), "Controller erases actual pixels");
  const auto serialized = serialize_edit_recipe_json(erased);
  const auto reopened = parse_edit_recipe_json(serialized);
  CHECK(reopened.layers[0].toMap().value("paintTiles") == erased.layers[0].toMap().value("paintTiles"), "Reopen preserves immutable tile bytes");
  QFile saved_recipe(temp.filePath("paint-recipe.json"));
  CHECK(saved_recipe.open(QIODevice::WriteOnly) && saved_recipe.write(serialized.toUtf8()) > 0, "Write recipe to disk");
  saved_recipe.close();
  CHECK(saved_recipe.open(QIODevice::ReadOnly), "Read saved recipe");
  auto reopened_controller = std::make_unique<EditorPreviewController>();
  reopened_controller->loadAsset("paint-reopened", source_path, {}, {}, {}, {}, QString::fromUtf8(saved_recipe.readAll()));
  CHECK(Access::state(*reopened_controller).layers[0].toMap().value("paintTiles") == erased.layers[0].toMap().value("paintTiles"),
      "A new controller must restore painted pixels from the saved document");
  reopened_controller.reset();
  const auto output = render_output_image_with_backend(source, erased, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  const auto restored = render_output_image_with_backend(source, reopened, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  CHECK(!output.isNull() && output == restored, "Output must exactly survive save and reopen");
  const auto compiled = compile_pipeline_photo_layers(erased.layers, {}, LocalMaskRasterPolicy::UseStoredRaster,
      QSize(320,200), {}, {}, DesktopRenderPurpose::Preview);
  CHECK(compiled.size() == 1 && compiled[0].pixel_width == 320 && compiled[0].pixel_height == 200,
      "Pixel layers must scale with preview resolution");
  CHECK(compiled[0].pixels[50 * 320 + 150].alpha > 0, "Half-size preview stroke must stay aligned");
  const auto export_path = temp.filePath("paint-export.png");
  CHECK(controller.exportFlattenedPhoto(export_path), "Start actual PNG export");
  CHECK(wait_until([&] { return !controller.photoExportBusy(); }) && controller.photoExportError().isEmpty(), "PNG export completes");
  CHECK(!QImage(export_path).isNull(), "Exported file is readable");
  const auto layer_export_path = temp.filePath("paint-layer-export.png");
  CHECK(controller.startPhotoLayerExport(0,layer_export_path), "Start asynchronous float paint layer export");
  CHECK(wait_until([&] { return !controller.photoExportBusy(); }) && controller.photoExportError().isEmpty(),
      "Paint layer export completes");
  const QImage layer_export(layer_export_path);
  CHECK(layer_export.depth()==64 && layer_export.colorSpace().isValid(),
      "Float paint PNG export must retain 16-bit channels and its color profile");
  CHECK(std::abs(layer_export.pixelColor(300,100).alphaF()-
      rasterize_photo_paint_layer(erased.layers[0].toMap()).pixelColor(300,100).alphaF())<0.0001,
      "PNG export must retain paint alpha beyond 8-bit precision");

  const auto before_queue=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "First queued stroke"); controller.addPhotoPaintPoint(100,250); controller.finishPhotoPaintStroke();
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Second queued stroke"); controller.addPhotoPaintPoint(500,250); controller.finishPhotoPaintStroke();
  CHECK(!controller.exportFlattenedPhoto(temp.filePath("pending.png")), "Export must not omit pending strokes");
  QString close_error; CHECK(!controller.prepareRecipeClose(&close_error), "Close must wait for pending paint");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(), "Queued strokes finish: " << controller.photoPaintError().toStdString());
  auto queued_image=rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap());
  CHECK(qAlpha(queued_image.pixel(100,250))>0 && qAlpha(queued_image.pixel(500,250))>0, "Queued strokes must rebase without losing either result");
  controller.undo(); queued_image=rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap());
  CHECK(qAlpha(queued_image.pixel(100,250))>0 && qAlpha(queued_image.pixel(500,250))==0, "Undo removes only the last queued stroke");
  controller.undo(); CHECK(Access::state(controller).layers==before_queue.layers, "Second undo restores prior tile map");
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Pending cancel"); controller.addPhotoPaintPoint(300,350); controller.finishPhotoPaintStroke(); controller.undo();
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && Access::state(controller).layers==before_queue.layers, "Undo can cancel an uncommitted worker stroke");

  qmlRegisterType<TabletInputArea>("PhotoEditor",1,0,"TabletInputArea");
  engine.rootContext()->setContextProperty("paintController", &controller);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
Item {
    width: 1280; height: 720
    property int paintLayerRequests: 0
    Rectangle { anchors.fill: parent; color: "#181c21" }
    Image { x: 40; y: 30; width: 720; height: 450; source: paintController.displayPreviewSource; cache: false }
    PhotoPaintOverlay {
        objectName: "paintOverlay"; anchors.fill: parent
        active: true; controller: paintController; documentId: "paint-proof"
        layerIndex: 0; layerData: ({kind: "pixel", name: "Paint Layer"})
        imageX: 40; imageY: 30; imageWidth: 720; imageHeight: 450
        canvasWidth: 640; canvasHeight: 400
        ink: "#60d8bd"; brushSize: 32; pressureSize: false
        onNewLayerRequested: parent.paintLayerRequests += 1
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_PAINT_TEST_ROOT "/tests/paint-harness.qml")));
  std::unique_ptr<QObject> root(component.create());
  CHECK(root, component.errorString().toStdString());
  QQuickWindow window; window.resize(1280,720);
  qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  window.show(); window.requestActivate();
  CHECK(wait_until([&] { return window.isExposed(); }), "Native paint window");
  auto* paintOverlay = root->findChild<QObject*>("paintOverlay");
  auto* paintAdvancedButton = root->findChild<QObject*>("photoPaintAdvancedButton");
  auto* paintAdvancedControls = root->findChild<QObject*>("photoPaintAdvancedControls");
  auto* textureMaterial = root->findChild<QObject*>("photoPaintTextureMaterial");
  auto* paintStatus = root->findChild<QObject*>("photoPaintStatusLabel");
  auto* newPaintLayerButton = root->findChild<QObject*>("photoPaintNewLayerButton");
  CHECK(paintOverlay && paintAdvancedButton && paintAdvancedControls, "Compact brush HUD controls instantiate");
  CHECK(paintStatus && paintStatus->property("text").toString() == QStringLiteral("Paint on Paint Layer") &&
      newPaintLayerButton && QMetaObject::invokeMethod(newPaintLayerButton, "clicked") &&
      root->property("paintLayerRequests").toInt() == 1,
      "Paint overlay names the selected layer and exposes a distinct new-layer request");
  paintOverlay->setProperty("brushSize", 44.0);
  paintOverlay->setProperty("hardness", 0.42);
  paintOverlay->setProperty("brushOpacity", 0.72);
  paintOverlay->setProperty("flow", 0.58);
  paintOverlay->setProperty("brushSpacing", 0.24);
  paintOverlay->setProperty("pressureSize", false);
  paintOverlay->setProperty("pressureFlow", true);
  paintOverlay->setProperty("advancedControlsExpanded", true);
  paintOverlay->setProperty("erase", true);
  paintOverlay->setProperty("eraserTool", true);
  CHECK(paintOverlay->property("erase").toBool() &&
      paintOverlay->property("brushSize").toDouble() == 48.0 &&
      paintOverlay->property("brushSpacing").toDouble() == 0.15 &&
      paintOverlay->property("pressureFlow").toBool() == false &&
      paintOverlay->property("advancedControlsExpanded").toBool() == false &&
      paintStatus->property("text").toString() == QStringLiteral("Erase on Paint Layer"),
      "Dedicated eraser shows its target and loads independent retained settings");
  paintOverlay->setProperty("brushSize", 62.0);
  paintOverlay->setProperty("brushSpacing", 0.31);
  paintOverlay->setProperty("pressureFlow", true);
  paintOverlay->setProperty("eraserTool", false);
  CHECK(paintOverlay->property("brushSize").toDouble() == 44.0 &&
      paintOverlay->property("hardness").toDouble() == 0.42 &&
      paintOverlay->property("brushOpacity").toDouble() == 0.72 &&
      paintOverlay->property("flow").toDouble() == 0.58 &&
      paintOverlay->property("brushSpacing").toDouble() == 0.24 &&
      paintOverlay->property("pressureFlow").toBool() &&
      paintOverlay->property("advancedControlsExpanded").toBool() &&
      paintOverlay->property("erase").toBool(),
      "Switching back restores paint brush settings and its Paint/Erase mode");
  paintOverlay->setProperty("eraserTool", true);
  CHECK(paintOverlay->property("brushSize").toDouble() == 62.0 &&
      paintOverlay->property("brushSpacing").toDouble() == 0.31 &&
      paintOverlay->property("pressureFlow").toBool(),
      "Dedicated eraser retains its own changes after switching back and forth");
  paintOverlay->setProperty("eraserTool", false);
  paintOverlay->setProperty("erase", false);
  paintOverlay->setProperty("advancedControlsExpanded", false);
  CHECK(!paintAdvancedControls->property("visible").toBool(), "Brush details start collapsed");
  CHECK(QMetaObject::invokeMethod(paintAdvancedButton, "clicked"), "Expand brush details");
  QCoreApplication::processEvents();
  CHECK(paintAdvancedControls->property("visible").toBool(), "Brush details expand on demand");
  CHECK(textureMaterial && textureMaterial->property("count").toInt() == 5,
      "Paint UI offers solid, drypoint, graphite, afterglow, and photo-fragment materials");
  textureMaterial->setProperty("currentIndex", 1);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 1)),
      "Dry ink material is selectable from the native control");
  paintOverlay->setProperty("textureStrength", 0.8);
  paintOverlay->setProperty("textureScale", 6.0);
  paintOverlay->setProperty("textureAngle", 24.0);
  paintOverlay->setProperty("textureSeed", 24301.0);
  CHECK(paintOverlay->property("textureMaterial").toString() == QStringLiteral("dry_ink"),
      "Native material selection configures the active paint overlay");
  auto* textureStrength=root->findChild<QObject*>("photoPaintTextureStrength");
  auto* textureScale=root->findChild<QObject*>("photoPaintTextureScale");
  auto* textureAngle=root->findChild<QObject*>("photoPaintTextureAngle");
  auto* textureSeed=root->findChild<QObject*>("photoPaintTextureSeed");
  auto* brushSpacing=root->findChild<QObject*>("photoPaintSpacing");
  CHECK(textureStrength && textureScale && textureAngle && textureSeed &&
      brushSpacing &&
      textureStrength->property("visible").toBool() && textureScale->property("visible").toBool() &&
      textureAngle->property("visible").toBool() && textureSeed->property("visible").toBool() &&
      brushSpacing->property("visible").toBool(),
      "Native brush panel exposes grain, document scale, angle, and deterministic seed controls");
  textureMaterial->setProperty("currentIndex", 3);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 3)),
      "Light paint material is selectable from the native control");
  auto* light_controls=root->findChild<QObject*>("photoPaintLightControls");
  auto* light_falloff=root->findChild<QObject*>("photoPaintLightFalloff");
  auto* light_taper=root->findChild<QObject*>("photoPaintLightTaper");
  auto* light_exposure=root->findChild<QObject*>("photoPaintLightExposure");
  auto* light_accumulation=root->findChild<QObject*>("photoPaintLightAccumulation");
  CHECK(paintOverlay->property("textureMaterial").toString() == QStringLiteral("light_paint") &&
      light_controls && light_controls->property("visible").toBool() && light_falloff &&
      light_exposure && light_taper && light_accumulation,
      "Selecting light paint reveals halo, taper, exposure, and accumulation controls");
  textureMaterial->setProperty("currentIndex", 4);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 4)),
      "Photo fragments are selectable from the native brush control");
  auto* fragment_controls=root->findChild<QObject*>("photoPaintFragmentControls");
  auto* fragment_source=root->findChild<QObject*>("photoPaintFragmentSource");
  auto* fragment_rotation=root->findChild<QObject*>("photoPaintFragmentRotation");
  auto* fragment_scale=root->findChild<QObject*>("photoPaintFragmentScale");
  CHECK(paintOverlay->property("textureMaterial").toString() == QStringLiteral("photo_fragment") &&
      fragment_controls && fragment_controls->property("visible").toBool() &&
      fragment_source && fragment_rotation && fragment_scale &&
      fragment_source->property("count").toInt() > 1,
      "Photo-fragment controls select a pixel-layer tip and expose seeded rotation and scale variation");
  textureMaterial->setProperty("currentIndex", 1);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 1)),
      "Restore dry ink after checking the light-paint controls");
  auto mouse = [&](QEvent::Type type, QPointF p, Qt::MouseButton button, Qt::MouseButtons buttons) {
    QMouseEvent event(type,p,window.mapToGlobal(p.toPoint()),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(&window,&event); QCoreApplication::processEvents();
  };
  CHECK(wait_until([&] { return !controller.processing() && !controller.fullRenderRunning(); }), "Settle before native input");
  // Hover must work with no button held; drawing tests cannot catch a tablet
  // event surface swallowing mouse hover before it reaches the paint MouseArea.
  {
    if (qEnvironmentVariableIsSet("PHOTO_PAINT_TEST_LEGACY_HOVER")) {
      for (auto* tablet_area : root->findChildren<TabletInputArea*>())
        tablet_area->setAcceptHoverEvents(true);
    }
    const bool native_hover = QGuiApplication::platformName() == QStringLiteral("cocoa");
    const auto original_pointer = QCursor::pos();
    const auto restore_pointer = qScopeGuard([&] { if (native_hover) QCursor::setPos(original_pointer); });
    const auto hover_mouse = [&](const QPointF& position) {
      // Cocoa refreshes hover from the system pointer during frame readback.
      // Keep it aligned with the event so screenshots cannot reset our hover.
      if (native_hover) QCursor::setPos(window.mapToGlobal(position.toPoint()));
      mouse(QEvent::MouseMove, position, Qt::NoButton, Qt::NoButton);
    };
    paintOverlay->setProperty("advancedControlsExpanded", false);
    auto* hover_cursor = root->findChild<QQuickItem*>("photoPaintCursor");
    CHECK(hover_cursor, "Paint exposes its brush outline");
    const auto before_hover = Access::state(controller);
    QEnterEvent hover_enter(QPointF(810, 420), QPointF(810, 420),
        QPointF(window.mapToGlobal(QPoint(810, 420))));
    QCoreApplication::sendEvent(&window, &hover_enter);
    hover_mouse({810, 420});
    const auto before_hover_frame = window.grabWindow();
    hover_mouse({600, 420});
    const auto hover_proof_dir = qEnvironmentVariable("PHOTO_PAINT_HOVER_PROOF_DIR");
    if (!hover_proof_dir.isEmpty()) {
      QDir().mkpath(hover_proof_dir);
      window.grabWindow().save(QDir(hover_proof_dir).filePath("brush-hover.png"));
    }
    CHECK(wait_until([&] {
      return hover_cursor->isVisible() &&
          QLineF(paintOverlay->property("cursorPoint").toPointF(), QPointF(600, 420)).length() < 1;
    }), "A mouse move with no buttons must show and position the brush outline before any stroke");
    const auto hover_frame = window.grabWindow();
    CHECK(!before_hover_frame.isNull() && !hover_frame.isNull(), "Hover readback captures the real canvas");
    const double hover_dpr = static_cast<double>(hover_frame.width()) / window.width();
    const int outline_x = qRound((600 + hover_cursor->width() / 2 - 1) * hover_dpr);
    const int outline_y = qRound(420 * hover_dpr);
    int outline_delta = 0;
    for (int y = outline_y - 2; y <= outline_y + 2; ++y) {
      for (int x = outline_x - 2; x <= outline_x + 2; ++x) {
        const auto before = before_hover_frame.pixelColor(x, y);
        const auto after = hover_frame.pixelColor(x, y);
        outline_delta = std::max(outline_delta, std::abs(before.red() - after.red()) +
            std::abs(before.green() - after.green()) + std::abs(before.blue() - after.blue()));
      }
    }
    CHECK(outline_delta > 64, "The hovering brush outline must actually appear in the displayed pixels");
    hover_mouse({480, 420});
    CHECK(QLineF(paintOverlay->property("cursorPoint").toPointF(), QPointF(480, 420)).length() < 1,
        "The outline follows mouse hover without pressing");
    hover_mouse({810, 420});
    CHECK(!hover_cursor->isVisible(), "The brush outline hides outside the image");
    CHECK(!paintOverlay->property("drawing").toBool() && Access::state(controller).layers == before_hover.layers,
        "Hover must never start a stroke or alter the document");
  }
  textureMaterial->setProperty("currentIndex", 3);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 3)),
      "Select light paint for a native canvas stroke");
  paintOverlay->setProperty("lightExposure", 3.0);
  paintOverlay->setProperty("lightFalloff", 0.8);
  paintOverlay->setProperty("lightTaper", 0.5);
  const auto preview_before_drag = controller.displayPreviewSource();
  mouse(QEvent::MouseButtonPress,{180,270},Qt::LeftButton,Qt::LeftButton);
  mouse(QEvent::MouseMove,{550,310},Qt::NoButton,Qt::LeftButton);
  CHECK(root->findChild<QObject*>("paintOverlay")->property("drawing").toBool(), "Native canvas must own stroke");
  CHECK(wait_until([&] { return Access::request(controller).state.layers[0].toMap().contains("paintDraftTiles"); }), "Native drag reaches worker and renderer");
  CHECK(wait_until([&] { return controller.displayPreviewSource() != preview_before_drag && !controller.processing(); }),
      "Live pointer movement must publish a visible preview before release");
  mouse(QEvent::MouseButtonRelease,{550,310},Qt::LeftButton,Qt::NoButton);
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && Access::state(controller).layers != erased.layers, "Native pointer release commits pixels");
  const auto after_native = Access::state(controller);
  const auto native_light_pixels=rasterize_photo_paint_layer(after_native.layers[0].toMap());
  const auto* native_light_row=reinterpret_cast<const QRgbaFloat32*>(native_light_pixels.constScanLine(213));
  CHECK(std::isfinite(native_light_row[288].green()) && native_light_row[288].green()>1.0F,
      "Native canvas stroke retains HDR light-paint energy in its committed pixel layer");
  mouse(QEvent::MouseButtonPress,{200,350},Qt::LeftButton,Qt::LeftButton);
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(&window,&escape);
  mouse(QEvent::MouseButtonRelease,{200,350},Qt::LeftButton,Qt::NoButton);
  CHECK(Access::state(controller).layers == after_native.layers, "Escape discards live native stroke");
  textureMaterial->setProperty("currentIndex", 4);
  CHECK(QMetaObject::invokeMethod(textureMaterial, "activated", Q_ARG(int, 4)),
      "Show photo-fragment controls in the retained native evidence");
  const auto evidence = qEnvironmentVariable("PHOTO_PAINT_EVIDENCE_DIR");
  if (!evidence.isEmpty()) {
    QDir{}.mkpath(evidence);
    wait_until([&] { return !controller.processing() && !controller.fullRenderRunning(); });
    QElapsedTimer settle; settle.start();
    while (settle.elapsed() < 300) { QCoreApplication::processEvents(); QThread::msleep(5); }
    CHECK(window.grabWindow().save(evidence + "/native-paint-controls.png"), "Native screenshot");
    CHECK(output.save(evidence + "/paint-erase-output.png"), "Output evidence");
  }
  const auto before_navigation=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Navigation stroke begins");
  controller.addPhotoPaintPoint(580,370); controller.finishPhotoPaintStroke();
  controller.loadAsset("next-photo",source_path,{},{},{},{},{});
  CHECK(wait_until([&]{return controller.assetId()=="next-photo" && !controller.photoPaintBusy();}),
      "Navigation waits for a released stroke");
  CHECK(Access::state(controller).layers.isEmpty(), "A pending stroke cannot land on the next photo");
  controller.loadAsset("paint-proof",source_path,{},{},{},{},{});
  CHECK(Access::state(controller).layers!=before_navigation.layers &&
      qAlpha(rasterize_photo_paint_layer(Access::state(controller).layers[0].toMap()).pixel(580,370))>0,
      "Returning to the original photo must retain the stroke released before navigation");
  const auto before_cancel=Access::state(controller);
  CHECK(controller.beginPhotoPaintStroke(0,settings), "Cancel pending work begins");
  controller.addPhotoPaintPoint(580,20); controller.finishPhotoPaintStroke(); controller.cancelPhotoPaint();
  CHECK(!controller.photoPaintBusy() && Access::state(controller).layers==before_cancel.layers,
      "Explicit cancellation must discard queued work without altering saved pixels");
  controller.addMask("rectangle");
  controller.setActiveMaskFeather(0);
  controller.setActiveMaskCenterX(0.25); controller.setActiveMaskCenterY(0.5);
  controller.setActiveMaskRadiusX(0.25); controller.setActiveMaskRadiusY(0.5);
  const auto before_independent_copy = Access::state(controller);
  CHECK(controller.copyPixelsFromSelection(-1, true), "Queue independent selection cutout");
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Independent selection cutout materializes asynchronously: " << controller.photoPaintError().toStdString());
  const auto independent_state = Access::state(controller);
  const auto independent_layer = independent_state.layers[0].toMap();
  CHECK(!independent_layer.value("copyBaseImage").toBool() && !independent_layer.value("selectionScoped").toBool(),
      "Independent cutout must no longer depend on the developed base or live selection");
  const auto independent_pixels = rasterize_photo_paint_layer(independent_layer);
  CHECK(!independent_pixels.isNull() && qAlpha(independent_pixels.pixel(50, 200)) == 255 &&
      qAlpha(independent_pixels.pixel(500, 200)) == 0,
      "Independent cutout bakes selection alpha into its own pixels");
  auto changed_base = independent_state;
  changed_base.exposure += 1.0;
  Access::apply(controller, changed_base);
  CHECK(Access::state(controller).layers[0].toMap().value("paintTiles") == independent_layer.value("paintTiles"),
      "Later base-photo development cannot alter the independent snapshot");
  Access::apply(controller, independent_state);
  controller.undo();
  CHECK(Access::state(controller).layers == before_independent_copy.layers,
      "Creating an independent cutout remains one undoable operation");
  controller.redo();
  CHECK(Access::state(controller).layers[0].toMap().value("paintTiles") == independent_layer.value("paintTiles"),
      "Redo restores the completed independent cutout snapshot");
  CHECK(controller.addPaintLayer(), "Create a destination layer for photo-fragment painting");
  const auto fragment_target_state = Access::state(controller);
  const auto fragment_target_id = fragment_target_state.layers[0].toMap().value("id").toString();
  auto* fragment_preset_name = root->findChild<QObject*>("photoPaintFragmentPresetName");
  auto* fragment_preset_save = root->findChild<QObject*>("photoPaintFragmentPresetSave");
  CHECK(fragment_preset_name && fragment_preset_save,
      "Native Photo Fragment controls include standalone preset capture");
  paintOverlay->setProperty("tipLayerId", independent_layer.value("id").toString());
  fragment_preset_name->setProperty("text", QStringLiteral("Rim Echo"));
  CHECK(QMetaObject::invokeMethod(fragment_preset_save, "clicked"),
      "Save the selection-derived layer as a reusable native preset");
  CHECK(controller.photoFragmentBrushPresetNames().contains(QStringLiteral("Rim Echo")) &&
      paintOverlay->property("tipPresetName").toString() == QStringLiteral("Rim Echo"),
      "Native preset capture removes future dependence on the source layer");
  const auto original_tip = PhotoFragmentBrushPresetStore::cropToVisiblePixels(independent_pixels)
      .convertToFormat(QImage::Format_ARGB32);
  const auto saved_tip = PhotoFragmentBrushPresetStore::imageFromPreset(
      PhotoFragmentBrushPresetStore::load(QStringLiteral("Rim Echo"))).convertToFormat(QImage::Format_ARGB32);
  CHECK(!saved_tip.isNull() && saved_tip.size() == original_tip.size(),
      "Saved Photo Fragment preset retains the cropped alpha geometry");
  QImage cached_tip;
  const auto cached_tip_metadata = PhotoFragmentBrushPresetStore::loadForBrush(
      QStringLiteral("Rim Echo"), &cached_tip);
  CHECK(cached_tip.convertToFormat(QImage::Format_ARGB32) == saved_tip &&
      !cached_tip_metadata.contains(QStringLiteral("imagePngBase64")),
      "Brush preset cache returns the exact saved PNG pixels and only brush metadata");
  QImage cache_probe(7, 5, QImage::Format_ARGB32_Premultiplied);
  cache_probe.fill(QColor(40, 90, 130, 180));
  CHECK(PhotoFragmentBrushPresetStore::save(QStringLiteral("Cache Probe"), cache_probe,
      12, 0.2, false), "Create a cache-invalidation probe preset");
  QImage cache_probe_loaded;
  CHECK(PhotoFragmentBrushPresetStore::loadForBrush(QStringLiteral("Cache Probe"),
      &cache_probe_loaded).value(QStringLiteral("rotationVariation")).toDouble() == 12 &&
      cache_probe_loaded.convertToFormat(QImage::Format_ARGB32) ==
          cache_probe.convertToFormat(QImage::Format_ARGB32),
      "Saved preset populates an exact decoded-image cache entry");
  QImage replacement_tip(9, 4, QImage::Format_ARGB32_Premultiplied);
  replacement_tip.fill(QColor(180, 55, 20, 90));
  CHECK(PhotoFragmentBrushPresetStore::save(QStringLiteral("Cache Probe"), replacement_tip,
      64, 0.4, true), "Overwrite the cache-invalidation probe preset");
  const auto replaced_metadata = PhotoFragmentBrushPresetStore::loadForBrush(
      QStringLiteral("Cache Probe"), &cache_probe_loaded);
  CHECK(replaced_metadata.value(QStringLiteral("rotationVariation")).toDouble() == 64 &&
      cache_probe_loaded.convertToFormat(QImage::Format_ARGB32) ==
          replacement_tip.convertToFormat(QImage::Format_ARGB32),
      "Preset overwrite replaces both cached pixels and editable metadata");
  CHECK(PhotoFragmentBrushPresetStore::remove(QStringLiteral("Cache Probe")),
      "Remove the decoded-image cache probe preset");
  bool saved_soft_edge = false;
  for (int y = 0; y < original_tip.height(); ++y) for (int x = 0; x < original_tip.width(); ++x) {
    const auto original_alpha = qAlpha(original_tip.pixel(x, y));
    const auto saved_alpha = qAlpha(saved_tip.pixel(x, y));
    if (original_alpha > 0 && original_alpha < 255) saved_soft_edge = true;
    CHECK(original_alpha == saved_alpha, "Preset PNG save preserves captured alpha exactly");
  }
  CHECK(saved_soft_edge, "Preset file preserves partially covered source-edge pixels");
  QByteArray expected_preset_png;
  QBuffer expected_preset_buffer(&expected_preset_png);
  CHECK(expected_preset_buffer.open(QIODevice::WriteOnly) && saved_tip.save(&expected_preset_buffer, "PNG"),
      "Encode the saved fragment for clean-process persistence verification");
  const auto expected_preset_hash = QCryptographicHash::hash(
      expected_preset_png, QCryptographicHash::Sha256).toHex();
  QProcess persistence_check;
  persistence_check.setProgram(QCoreApplication::applicationFilePath());
  persistence_check.setArguments({QStringLiteral("--verify-photo-fragment-preset"), QStringLiteral("Rim Echo"),
      QString::number(saved_tip.width()), QString::number(saved_tip.height())});
  persistence_check.start();
  CHECK(persistence_check.waitForStarted(3000) && persistence_check.waitForFinished(15000),
      "Launch a clean process to reopen the Photo Fragment preset from app data");
  const auto child_hash = persistence_check.readAllStandardOutput().trimmed();
  CHECK(persistence_check.exitStatus() == QProcess::NormalExit && persistence_check.exitCode() == 0 &&
      child_hash == expected_preset_hash,
      "A clean process reloads the exact standalone Photo Fragment PNG from app data");
  const auto agent_saved_preset = controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "save_photo_fragment_preset"},
      {"values", QVariantMap{{"name", "Rim Echo Agent"},
          {"tipLayerId", independent_layer.value("id").toString()},
          {"rotationVariation", 22.0}, {"scaleVariation", 0.2}, {"overwrite", false}}}});
  CHECK(agent_saved_preset.value("ok").toBool() &&
      agent_saved_preset.value("photoFragmentBrushPresets").toStringList().contains(QStringLiteral("Rim Echo Agent")),
      "Agent can save a named fragment preset from an independent cutout layer");
  CHECK(controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "delete_photo_fragment_preset"},
      {"values", QVariantMap{{"name", "Rim Echo Agent"}}}}).value("ok").toBool(),
      "Agent can delete a named Photo Fragment preset");
  const auto exported_tip_path = temp.filePath("Rim-Echo.looksybrush");
  CHECK(controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "export_photo_fragment_preset"},
      {"values", QVariantMap{{"name", "Rim Echo"}, {"path", exported_tip_path}}}}).value("ok").toBool(),
      "Agent can export a portable Photo Fragment brush-tip asset");
  QTemporaryDir transferred_presets;
  CHECK(transferred_presets.isValid(), "Preset transfer destination exists");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", transferred_presets.path().toUtf8());
  CHECK(controller.photoCompositionCommand({{"assetId", "paint-proof"},
      {"operation", "import_photo_fragment_preset"},
      {"values", QVariantMap{{"path", exported_tip_path}, {"overwrite", false}}}}).value("ok").toBool() &&
      controller.photoFragmentBrushPresetNames().contains(QStringLiteral("Rim Echo")),
      "Agent imports the exported preset into a separate application data directory");
  const auto transferred_tip = PhotoFragmentBrushPresetStore::imageFromPreset(
      PhotoFragmentBrushPresetStore::load(QStringLiteral("Rim Echo"))).convertToFormat(QImage::Format_ARGB32);
  CHECK(transferred_tip == saved_tip,
      "Preset import preserves every captured alpha/color pixel after transfer");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", temp.path().toUtf8());
  auto state_without_fragment_source = fragment_target_state;
  for (auto layer_index = state_without_fragment_source.layers.size() - 1; layer_index >= 0; --layer_index) {
    if (state_without_fragment_source.layers.at(layer_index).toMap().value("id").toString() ==
        independent_layer.value("id").toString()) state_without_fragment_source.layers.removeAt(layer_index);
  }
  Access::apply(controller, state_without_fragment_source);
  const auto before_fragment_paint = Access::state(controller);
  const auto fragment_command = controller.photoCompositionCommand({
      {"assetId", "paint-proof"}, {"operation", "paint"}, {"layerId", fragment_target_id},
      {"values", QVariantMap{{"points", QVariantList{QVariantMap{{"x", 120.0}, {"y", 120.0}},
          QVariantMap{{"x", 480.0}, {"y", 120.0}}}}, {"textureMaterial", "photo_fragment"},
          {"tipPresetName", QStringLiteral("Rim Echo")}, {"size", 54.0},
          {"spacing", 0.85}, {"textureSeed", 9031}, {"tipRotationVariation", 48.0},
          {"tipScaleVariation", 0.35}}}});
  CHECK(fragment_command.value("ok").toBool(),
      "Agent can stamp a copied selection as a seeded photo fragment: " << fragment_command.value("error").toString().toStdString());
  CHECK(wait_until([&] { return !controller.photoPaintBusy(); }) && controller.photoPaintError().isEmpty(),
      "Agent photo-fragment stroke settles: " << controller.photoPaintError().toStdString());
  const auto fragment_painted_state = Access::state(controller);
  const auto fragment_painted_layer = fragment_painted_state.layers[0].toMap();
  const auto fragment_painted = rasterize_photo_paint_layer(fragment_painted_layer);
  CHECK(!fragment_painted.isNull() && qAlpha(fragment_painted.pixel(120,120)) > 0 &&
      fragment_painted_layer.value("paintTiles") != before_fragment_paint.layers[0].toMap().value("paintTiles"),
      "Photo-fragment marks are committed into ordinary editable pixel tiles");
  const auto fragment_recipe = parse_edit_recipe_json(serialize_edit_recipe_json(fragment_painted_state));
  CHECK(fragment_recipe.layers[0].toMap().value("paintTiles") == fragment_painted_layer.value("paintTiles"),
      "Stamped fragments survive a recipe save and reopen");
  const auto fragment_evidence = qEnvironmentVariable("PHOTO_PAINT_EVIDENCE_DIR");
  if (!fragment_evidence.isEmpty()) {
    wait_until([&] { return !controller.processing() && !controller.fullRenderRunning(); });
    CHECK(QMetaObject::invokeMethod(paintOverlay, "refreshFragmentLayers"),
        "Refresh available pixel layers after the cutout is created");
    paintOverlay->setProperty("tipLayerId", QString{});
    paintOverlay->setProperty("tipPresetName", QStringLiteral("Rim Echo"));
    CHECK(window.grabWindow().save(fragment_evidence + "/native-paint-controls.png"),
        "Capture native controls with a portable Photo Fragment preset selected");
    CHECK(fragment_painted.save(fragment_evidence + "/photo-fragment-layer.png"),
        "Capture the stamped photo-fragment layer");
    QFile::remove(fragment_evidence + "/Rim-Echo.looksybrush");
    CHECK(QFile::copy(exported_tip_path, fragment_evidence + "/Rim-Echo.looksybrush"),
        "Retain the exported transferable Photo Fragment preset as evidence");
  }
  controller.undo();
  CHECK(Access::state(controller).layers == before_fragment_paint.layers,
      "Photo-fragment painting is one undoable operation");
  std::cout << "Paint/erase, selection alpha, pressure, transforms, undo, recipe reopen, export and native pointer checks passed\n";
  return 0;
}
