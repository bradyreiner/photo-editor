#include "app_bootstrap.h"
#include "observability.h"
#include "blank_canvas_document.h"
#include "photo_layer_clipboard.h"
#include "color_profile_asset_bridge.h"
#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_pipeline_bridge.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "preview_device_icc_options.h"
#include "photo_layer_compiler.h"
#include "photo_render_utils.h"
#include "photo_pixel_stroke.h"
#include "photo_text_style.h"
#include "render_plan_store.h"
#include "brush_rasterizer.h"
#include "brush_preview_compositor.h"
#include "photo_fragment_brush_preset_store.h"
#include "working_image.h"
#include "tablet_input_area.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"
#include "photo_editor/pipeline/color_space.h"
#include "photo_editor/pipeline/color_transform.h"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QJSValue>
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
#include <QCryptographicHash>
#include <QImageReader>
#include <QJsonDocument>
#include <QProcess>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <iostream>
#include <utility>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& c) {
    auto state=c.currentState();
    for(int i=0;i<state.layers.size();++i) state.layers[i]=photo_paint_target_identity(state.layers[i].toMap());
    return state;
  }
  static QVariantList layers(const EditorPreviewController& c) { return c.layer_session_.layers(); }
  static bool replace_layers_raw(EditorPreviewController& c, const QVariantList& layers) {
    return c.layer_session_.replaceLayers(layers);
  }
  static bool try_layer_stack_change(EditorPreviewController& c, const QVariantList& layers) {
    return c.applyPhotoLayerStackChange(layers, QStringLiteral("test_drop_base_source"), false,
        QStringLiteral("Remove Base Image"));
  }
  static bool paintRasterIdle(const EditorPreviewController& c) {
    return !c.photo_paint_worker_running_ && (c.photo_paint_jobs_.empty() || c.photo_paint_jobs_.front()->pending_points.empty());
  }
  static RenderRequest request(const EditorPreviewController& c) { return c.currentRenderRequest(false); }
  static void clear_working_caches() { EditorPreviewController::clearPreviewWorkingCachesForTesting(); }
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

static bool regional_matches_full(const RenderRequest& request, const QVariantList& base_layers,
                                  bool require_device_profile = false,
                                  const QString& partial_alpha_layer_id = {}) {
  auto region_request = request;
  region_request.use_in_memory_preview=true;
  if (require_device_profile) {
    if (!region_request.state.preview_icc_profile_asset.configured()) {
      std::cerr << "Device profile paint parity needs the loaded display ICC asset\n";
      return false;
    }
    region_request.allow_native_device_surface = true;
    region_request.allow_native_srgb_surface = false;
    region_request.require_cpu_preview_image = false;
    region_request.scopes_visible = false;
    region_request.canonical_render_plan.reset();
    region_request.canonical_scope_plan.reset();
  }
  auto base_request=region_request;
  base_request.state.layers=base_layers;
  base_request.recipe_cache_identity.clear();
  base_request.adjusted_recipe_cache_identity.clear();
  const auto base_render=render_preview_request_for_test(base_request);
  if (!base_render.success) {
    std::cerr << "Full paint base render failed: " << base_render.status.toStdString()
              << " backend=" << base_render.backend.toStdString()
              << " output_lut=" << base_render.output_lut.status.toStdString()
              << " fallback_reason=" << base_render.fallback_reason.toStdString()
              << " fallback=" << base_render.failure_detail.toStdString() << '\n';
    return false;
  }
  region_request.paint_preview_base=base_render.preview_image;
  if (region_request.paint_preview_base.isNull()) return false;
  if (require_device_profile &&
      region_request.paint_preview_base.text("looksy-preview-output-backend") !=
          QStringLiteral("canonical-metal-device8")) {
    std::cerr << "Device profile paint parity did not receive canonical device output: "
              << region_request.paint_preview_base.text("looksy-preview-output-backend").toStdString()
              << " result_backend=" << base_render.backend.toStdString()
              << " output_lut=" << base_render.output_lut.status.toStdString()
              << " fallback_reason=" << base_render.fallback_reason.toStdString()
              << " fallback=" << base_render.failure_detail.toStdString()
              << " native_composite=" << base_render.native_quality_composite
              << " rendered_hit=" << base_render.rendered_cache_hit
              << " adjusted_hit=" << base_render.adjusted_cache_hit
              << " full=" << base_request.full_resolution
              << " draft=" << base_request.draft_interactive
              << " edge=" << base_request.max_edge
              << " cpu_preview=" << base_request.require_cpu_preview_image
              << " cpu_display=" << base_request.require_cpu_display_image
              << " native_device=" << base_request.allow_native_device_surface
              << " renderer=" << base_request.state.photographic_rendering_model.toStdString()
              << " profile_asset="
              << region_request.state.preview_icc_profile_asset.asset_id.toStdString()
              << '\n';
    return false;
  }
  const auto regional=render_preview_request_for_test(region_request);
  if (!regional.success || regional.backend!="paint-region") return false;
  region_request.paint_preview_base={};
  region_request.recipe_cache_identity.clear();
  region_request.adjusted_recipe_cache_identity.clear();
  const auto full_draft=render_preview_request_for_test(region_request);
  if (!full_draft.success || full_draft.preview_image.size()!=regional.preview_image.size()) return false;
  if (require_device_profile && full_draft.preview_image.text("looksy-preview-output-backend") !=
          QStringLiteral("canonical-metal-device8")) {
    std::cerr << "Full ICC reference must retain canonical device output\n";
    return false;
  }
  if (!partial_alpha_layer_id.isEmpty()) {
    bool found_target = false;
    bool found_partial_alpha = false;
    for (const auto& value : region_request.state.layers) {
      const auto layer = value.toMap();
      if (layer.value(QStringLiteral("id")).toString() != partial_alpha_layer_id) continue;
      found_target = true;
      const auto pixels = rasterize_photo_paint_layer(layer, {}, nullptr);
      if (pixels.isNull()) return false;
      for (int y = 0; y < pixels.height() && !found_partial_alpha; ++y) {
        for (int x = 0; x < pixels.width(); ++x) {
          const auto alpha = pixels.pixelColor(x, y).alpha();
          if (alpha > 0 && alpha < 255) {
            found_partial_alpha = true;
            break;
          }
        }
      }
      break;
    }
    if (!found_target || !found_partial_alpha) {
      std::cerr << "Paint parity fixture lacks partial draft alpha for layer "
                << partial_alpha_layer_id.toStdString() << '\n';
      return false;
    }
  }
  int worst_channel_error=0, outside_error=0;
  for(int y=0;y<regional.preview_image.height();++y) for(int x=0;x<regional.preview_image.width();++x) {
    const auto a=regional.preview_image.pixelColor(x,y),b=full_draft.preview_image.pixelColor(x,y);
    const int delta=std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),
        std::abs(a.blue()-b.blue()),std::abs(a.alpha()-b.alpha())});
    if (delta>worst_channel_error) worst_channel_error=delta;
    if (!regional.texture_upload_rect.contains(x,y)) outside_error=std::max(outside_error,delta);
  }
  std::cout << "regional_max_channel_error=" << worst_channel_error
            << " outside_error=" << outside_error << '\n';
  if (require_device_profile) {
    std::cout << "native_device_icc_profile_sha256="
              << region_request.state.preview_icc_profile_asset.payload_sha256.toStdString()
              << " profile_source="
              << region_request.state.preview_icc_profile_asset.source_kind.toStdString()
              << " base_backend="
              << base_render.preview_image.text("looksy-preview-output-backend").toStdString()
              << " regional_backend=" << regional.backend.toStdString()
              << " full_backend="
              << full_draft.preview_image.text("looksy-preview-output-backend").toStdString() << '\n';
  }
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

static int verify_linked_cutout_clipboard_adjustments(QTemporaryDir& temp) {
  using Access = EditorPreviewControllerTestAccess;
  QImage source(257, 129, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, QColor(30 + x % 150, 40 + y % 120, 60 + (x + y) % 130));
    }
  }
  const auto source_path = temp.filePath("linked-cutout-clipboard-source.png");
  CHECK(source.save(source_path), "Save the linked cutout clipboard source");

  EditorPreviewController source_controller;
  source_controller.loadAsset("linked-cutout-clipboard", source_path, {}, {}, {}, {}, {});
  CHECK(source_controller.addAdjustmentLayer(QStringLiteral("Below cutout")) &&
      source_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 0.9) &&
      source_controller.addAdjustmentLayer(QStringLiteral("Above cutout")) &&
      source_controller.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), -0.45) &&
      wait_until([&] { return !source_controller.processing() && !source_controller.fullRenderRunning(); }),
      "Create distinct adjustment grades below and above the linked cutout");
  auto linked_state = Access::state(source_controller);
  const auto below_layer = linked_state.layers.at(1).toMap();
  QVariantMap linked_cutout{
      {QStringLiteral("id"), QStringLiteral("linked-cutout-clipboard-layer")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), QStringLiteral("Linked Cutout")},
      {QStringLiteral("copyBaseImage"), true},
      {QStringLiteral("copyBaseImageThroughAdjustments"), true},
      {QStringLiteral("sourcePath"), source_path},
      {QStringLiteral("pixelWidth"), source.width()},
      {QStringLiteral("pixelHeight"), source.height()},
      {QStringLiteral("canvasWidth"), source.width()},
      {QStringLiteral("canvasHeight"), source.height()},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
  };
  linked_state.layers.insert(1, linked_cutout);
  Access::apply(source_controller, linked_state);
  CHECK(wait_until([&] { return !source_controller.processing() && !source_controller.fullRenderRunning(); }),
      "Settle the source recipe with one adjustment on each side of the linked cutout");

  auto lower_only_state = linked_state;
  lower_only_state.layers = QVariantList{below_layer};
  QString render_error;
  const auto lower_only_pixels = render_editable_base_pixels(
      source_path, lower_only_state, &render_error, true);
  const auto lower_only_composite = render_output_image_with_backend(source, lower_only_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  const auto source_composite = render_output_image_with_backend(source, linked_state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  CHECK(!lower_only_pixels.isNull() && !lower_only_composite.isNull() && !source_composite.isNull() &&
      render_error.isEmpty(), "Render lower-only and complete linked-cutout references");
  const auto display_pixels = [](const QImage& image) {
    return image.convertedToColorSpace(QColorSpace(QColorSpace::SRgb))
        .convertToFormat(QImage::Format_ARGB32);
  };
  const auto pixels_match = [](const QImage& left, const QImage& right) {
    if (left.size() != right.size()) return false;
    for (int y = 0; y < left.height(); ++y) {
      for (int x = 0; x < left.width(); ++x) {
        const auto a = left.pixelColor(x, y);
        const auto b = right.pixelColor(x, y);
        if (std::abs(a.red() - b.red()) > 1 || std::abs(a.green() - b.green()) > 1 ||
            std::abs(a.blue() - b.blue()) > 1 || std::abs(a.alpha() - b.alpha()) > 1) {
          return false;
        }
      }
    }
    return true;
  };
  const auto pixel_difference_summary = [](const QImage& actual, const QImage& expected) {
    if (actual.size() != expected.size()) {
      return QStringLiteral("size=%1x%2 expected_size=%3x%4")
          .arg(actual.width()).arg(actual.height()).arg(expected.width()).arg(expected.height());
    }
    auto max_error = 0;
    auto differing_pixels = 0;
    QPoint max_error_position;
    QColor actual_at_max;
    QColor expected_at_max;
    for (int y = 0; y < actual.height(); ++y) {
      for (int x = 0; x < actual.width(); ++x) {
        const auto actual_color = actual.pixelColor(x, y);
        const auto expected_color = expected.pixelColor(x, y);
        const auto error = std::max({
            std::abs(actual_color.red() - expected_color.red()),
            std::abs(actual_color.green() - expected_color.green()),
            std::abs(actual_color.blue() - expected_color.blue()),
            std::abs(actual_color.alpha() - expected_color.alpha())});
        if (error > 1) ++differing_pixels;
        if (error > max_error) {
          max_error = error;
          max_error_position = QPoint(x, y);
          actual_at_max = actual_color;
          expected_at_max = expected_color;
        }
      }
    }
    return QStringLiteral("size=%1x%2 differing_pixels_over_1_code=%3 max_error=%4 at=%5,%6 actual_rgba=%7,%8,%9,%10 expected_rgba=%11,%12,%13,%14")
        .arg(actual.width()).arg(actual.height()).arg(differing_pixels).arg(max_error)
        .arg(max_error_position.x()).arg(max_error_position.y())
        .arg(actual_at_max.red()).arg(actual_at_max.green()).arg(actual_at_max.blue()).arg(actual_at_max.alpha())
        .arg(expected_at_max.red()).arg(expected_at_max.green()).arg(expected_at_max.blue()).arg(expected_at_max.alpha());
  };
  const auto expected_copied_pixels = display_pixels(lower_only_pixels);
  CHECK(!pixels_match(lower_only_composite, source_composite),
      "The below-only final composite must differ from the source composite containing the above grade");

  CHECK(source_controller.copyPhotoLayers({1}) && wait_until([&] {
        return !source_controller.photoLayerClipboardBusy();
      }), "Copy a linked cutout whose source has grades both below and above it");
  const auto linked_payload = decode_photo_layer_clipboard(current_photo_layer_clipboard());
  CHECK(linked_payload && linked_payload->state.layers.size() == 1,
      "The clipboard contains just the selected linked cutout");
  const auto copied_layer = linked_payload->state.layers.front().toMap();
  CHECK(!copied_layer.value(QStringLiteral("copyBaseImage")).toBool() &&
      !copied_layer.contains(QStringLiteral("copyBaseImageThroughAdjustments")) &&
      !copied_layer.value(QStringLiteral("paintTiles")).toMap().isEmpty(),
      "Clipboard materialization turns the linked source into independent pixels");
  const auto copied_pixels = display_pixels(rasterize_photo_paint_layer(copied_layer));
  CHECK(pixels_match(copied_pixels, expected_copied_pixels),
      "Clipboard tiles contain the lower-layer grade in working color without baking the upper grade");

  QImage neutral_destination(source.size(), QImage::Format_ARGB32);
  neutral_destination.fill(QColor("#47515b"));
  neutral_destination.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto destination_path = temp.filePath("linked-cutout-clipboard-destination.png");
  CHECK(neutral_destination.save(destination_path), "Save the neutral clipboard destination");
  EditorPreviewController destination;
  destination.loadAsset("linked-cutout-clipboard-destination", destination_path, {}, {}, {}, {}, {});
  CHECK(destination.pastePhotoLayers(0, true).size() == 1,
      "Paste the materialized cutout into a neutral destination");
  const auto pasted_composite = render_output_image_with_backend(
      neutral_destination, Access::state(destination), QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  if (!pixels_match(pasted_composite, lower_only_composite) ||
      pixels_match(pasted_composite, source_composite)) {
    const auto pasted_layers = Access::state(destination).layers;
    const auto pasted_layer = pasted_layers.isEmpty() ? QVariantMap{} : pasted_layers.front().toMap();
    std::cerr << "linked cutout paste pixel mismatch: "
              << pixel_difference_summary(pasted_composite, lower_only_composite).toStdString()
              << " differs_from_full_composite=" << !pixels_match(pasted_composite, source_composite)
              << " destination_layer_count=" << pasted_layers.size()
              << " top_kind=" << pasted_layer.value(QStringLiteral("kind")).toString().toStdString()
              << " top_enabled=" << pasted_layer.value(QStringLiteral("enabled"), true).toBool()
              << " top_opacity=" << pasted_layer.value(QStringLiteral("opacity"), 1.0).toDouble()
              << " top_position=" << pasted_layer.value(QStringLiteral("x")).toDouble() << ','
              << pasted_layer.value(QStringLiteral("y")).toDouble()
              << " tiles_empty=" << pasted_layer.value(QStringLiteral("paintTiles")).toMap().isEmpty()
              << '\n';
    return 1;
  }

  linked_state = Access::state(source_controller);
  QVariantMap legacy_cutout{
      {QStringLiteral("id"), QStringLiteral("markerless-legacy-cutout")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), QStringLiteral("Legacy Cutout")},
      {QStringLiteral("copyBaseImage"), true},
      {QStringLiteral("sourcePath"), source_path},
      {QStringLiteral("pixelWidth"), source.width()},
      {QStringLiteral("pixelHeight"), source.height()},
      {QStringLiteral("canvasWidth"), source.width()},
      {QStringLiteral("canvasHeight"), source.height()},
  };
  linked_state.layers.prepend(legacy_cutout);
  Access::apply(source_controller, linked_state);
  CHECK(wait_until([&] { return !source_controller.processing() && !source_controller.fullRenderRunning(); }) &&
      source_controller.copyPhotoLayers({0}) && wait_until([&] {
        return !source_controller.photoLayerClipboardBusy();
      }), "Copy a markerless legacy cutout with surrounding adjustment layers");
  const auto legacy_payload = decode_photo_layer_clipboard(current_photo_layer_clipboard());
  CHECK(legacy_payload && legacy_payload->state.layers.size() == 1,
      "The markerless legacy copy has one clipboard layer");
  const auto legacy_pixels = display_pixels(
      rasterize_photo_paint_layer(legacy_payload->state.layers.front().toMap()));
  const auto base_only_pixels = display_pixels(render_editable_base_pixels(
      source_path, EditRecipeState{}, &render_error, false));
  CHECK(!base_only_pixels.isNull() && pixels_match(legacy_pixels, base_only_pixels) &&
      !pixels_match(legacy_pixels, expected_copied_pixels),
      "Markerless clipboard layers keep their original base-only materialization behavior");
  std::cout << "Linked cutout clipboard: lower grade baked once, upper grade excluded, legacy copy unchanged\n";
  return 0;
}

static int verify_base_erase_and_clipboard(QTemporaryDir& temp, bool require_device_profile = false) {
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
  CHECK(controller.setBaseImageLocked(false) && controller.photoLayerCount() == 1 &&
      controller.photoLayers().size() == 1 && controller.photoLayers().front().toMap().value("baseImageEdit").toBool() &&
      controller.photoLayers().front().toMap().value("copyBaseImage").toBool(),
      "Unlocking exposes the original as one movable source row carrying its base eraser");
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

  EditorPreviewController base_order_controller;
  base_order_controller.loadAsset("base-order", path, {}, {}, {}, {}, {});
  CHECK(base_order_controller.setBaseImageLocked(false) &&
      base_order_controller.addPhotoShape("rectangle", 280, 120, 360, 220, 640, 400, "#4879ff", "#ffffff", 0),
      "Create an unlocked source row with a visible layer above it");
  const auto base_index = [&] {
    const auto layers = Access::state(base_order_controller).layers;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index)
      if (layers.at(index).toMap().value("baseImageEdit").toBool()) return index;
    return -1;
  };
  const QString base_removal_status = QStringLiteral("Hide the Base Image to remove it from the composition.");
  EditorPreviewController ancestor_delete_controller;
  ancestor_delete_controller.loadAsset("base-ancestor-delete", path, {}, {}, {}, {}, {});
  CHECK(ancestor_delete_controller.setBaseImageLocked(false), "Create the source node for ancestor deletion proof");
  auto nested_base_layers = Access::layers(ancestor_delete_controller);
  auto nested_base = nested_base_layers.front().toMap();
  nested_base.insert(QStringLiteral("parentId"), QStringLiteral("ancestor-group"));
  nested_base.insert(QStringLiteral("groupId"), QStringLiteral("ancestor-group"));
  nested_base_layers[0] = nested_base;
  nested_base_layers.prepend(QVariantMap{{QStringLiteral("id"), QStringLiteral("ancestor-group")},
      {QStringLiteral("kind"), QStringLiteral("group")}, {QStringLiteral("name"), QStringLiteral("Ancestor")},
      {QStringLiteral("locked"), false}});
  CHECK(Access::replace_layers_raw(ancestor_delete_controller, nested_base_layers) &&
      !ancestor_delete_controller.deletePhotoLayer(0) && ancestor_delete_controller.photoLayerCount() == 2 &&
      ancestor_delete_controller.status() == base_removal_status &&
      Access::layers(ancestor_delete_controller).at(1).toMap().value("baseImageEdit").toBool(),
      "Deleting an ancestor row cannot discard a nested Base Image source");
  CHECK(!base_order_controller.groupPhotoLayers({0,1}, QStringLiteral("Grouped source")) &&
      base_order_controller.photoLayerCount() == 2 && base_index() == 1 &&
      base_order_controller.status() == QStringLiteral(
          "The Base Image source stays outside groups so its stack position remains explicit."),
      "Grouping cannot absorb the Base Image source into an ancestor row");
  const auto base_under_shape = render(Access::state(base_order_controller));
  auto base_only_state = Access::state(base_order_controller);
  base_only_state.layers.clear();
  const auto base_only_image = render(base_only_state);
  CHECK(base_order_controller.photoLayerCount() == 2 && base_index() == 1 &&
      base_under_shape.pixelColor(320, 170).blue() > base_under_shape.pixelColor(320, 170).red(),
      "The source starts below the shape at the bottom of the layer stack");
  const auto order_before_ids = [&] {
    QStringList ids;
    for (const auto& layer : Access::state(base_order_controller).layers)
      ids.push_back(layer.toMap().value("id").toString());
    return ids;
  }();
  CHECK(base_order_controller.movePhotoLayer(base_index(), -1), "Move the unlocked Base Image above the shape");
  const auto base_over_shape = render(Access::state(base_order_controller));
  const auto order_after_state = Access::state(base_order_controller);
  QStringList order_after_ids;
  for (const auto& layer : order_after_state.layers) order_after_ids.push_back(layer.toMap().value("id").toString());
  CHECK(order_after_ids != order_before_ids && order_after_state.layers.front().toMap().value("baseImageEdit").toBool() &&
      base_over_shape.pixelColor(320, 170) == base_only_image.pixelColor(320, 170),
      "Moving the source row places the original above lower layers without a duplicate underlay");
  CHECK(!base_order_controller.deletePhotoLayer(base_index()) && base_index() == 0 &&
      base_order_controller.status() == base_removal_status &&
      render(Access::state(base_order_controller)) == base_over_shape,
      "Deleting an unlocked Base Image reports that it must be hidden and leaves its moved source intact");
  auto ancestor_removal_candidate = Access::layers(base_order_controller);
  ancestor_removal_candidate.removeAt(base_index());
  CHECK(!Access::try_layer_stack_change(base_order_controller, ancestor_removal_candidate) && base_index() == 0 &&
      base_order_controller.status() == base_removal_status &&
      render(Access::state(base_order_controller)) == base_over_shape,
      "A stack or ancestor removal cannot silently restore the implicit bottom source");
  base_order_controller.undo();
  CHECK(render(Access::state(base_order_controller)) == base_under_shape,
      "Undo restores the original source and shape order");
  base_order_controller.redo();
  const auto moved_json = serialize_edit_recipe_json(Access::state(base_order_controller));
  CHECK(render(Access::state(base_order_controller)) == base_over_shape &&
      validate_edit_recipe_json(moved_json).valid,
      "Redo restores the reordered source layer and its valid recipe");
  EditorPreviewController reopened_base_order;
  reopened_base_order.loadAsset("base-order-reopened", path, {}, {}, {}, {}, moved_json);
  CHECK(wait_until([&] { return !reopened_base_order.processing(); }) &&
      render(Access::state(reopened_base_order)) == base_over_shape,
      "Reopening preserves the Base Image row's stack position and composite");
  const auto base_order_export_path = temp.filePath("base-order-export.png");
  CHECK(reopened_base_order.exportFlattenedPhoto(base_order_export_path) &&
      wait_until([&] { return !reopened_base_order.photoExportBusy(); }),
      "Export the composition with the reordered Base Image source");
  const QImage reordered_export(base_order_export_path);
  const auto base_only_export_state = [&] {
    auto state = Access::state(reopened_base_order);
    state.layers.clear();
    return state;
  }();
  EditorPreviewController base_only_export_reference;
  base_only_export_reference.loadAsset("base-order-export-reference", path, {}, {}, {}, {}, {});
  Access::apply(base_only_export_reference, base_only_export_state);
  CHECK(wait_until([&] { return !base_only_export_reference.processing(); }),
      "Settle a source-only export reference with the same output settings");
  const auto base_only_export_path = temp.filePath("base-order-source-only-export.png");
  CHECK(base_only_export_reference.exportFlattenedPhoto(base_only_export_path) &&
      wait_until([&] { return !base_only_export_reference.photoExportBusy(); }),
      "Export the same source and output settings without layers");
  const QImage base_only_export(base_only_export_path);
  if (reordered_export.size() == base_only_export.size() && !reordered_export.isNull()) {
    int export_vs_inline_error = 0;
    int export_vs_base_only_error = 0;
    for (int y = 0; y < reordered_export.height(); ++y) {
      for (int x = 0; x < reordered_export.width(); ++x) {
        const auto actual = reordered_export.pixelColor(x, y);
        const auto inline_reference = base_only_image.pixelColor(x, y);
        const auto source_export_reference = base_only_export.pixelColor(x, y);
        export_vs_inline_error = std::max(export_vs_inline_error, std::max({
            std::abs(actual.red() - inline_reference.red()),
            std::abs(actual.green() - inline_reference.green()),
            std::abs(actual.blue() - inline_reference.blue()),
            std::abs(actual.alpha() - inline_reference.alpha())}));
        export_vs_base_only_error = std::max(export_vs_base_only_error, std::max({
            std::abs(actual.red() - source_export_reference.red()),
            std::abs(actual.green() - source_export_reference.green()),
            std::abs(actual.blue() - source_export_reference.blue()),
            std::abs(actual.alpha() - source_export_reference.alpha())}));
      }
    }
    std::cout << "base_order_export_inline_max_error=" << export_vs_inline_error
              << " base_order_export_source_only_max_error=" << export_vs_base_only_error
              << " export_center=" << reordered_export.pixelColor(320, 170).name(QColor::HexArgb).toStdString()
              << " inline_center=" << base_only_image.pixelColor(320, 170).name(QColor::HexArgb).toStdString()
              << '\n';
    CHECK(export_vs_base_only_error == 0,
        "Flattened export matches an independent source-only export with identical output settings");
  } else {
    CHECK(false, "Reordered and source-only exports have matching non-null dimensions");
  }
  const auto reopened_base_index = [&] {
    const auto layers = Access::state(reopened_base_order).layers;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index)
      if (layers.at(index).toMap().value("baseImageEdit").toBool()) return index;
    return -1;
  };
  CHECK(reopened_base_index() >= 0 && reopened_base_order.setPhotoLayerEnabled(reopened_base_index(), false) &&
      render(Access::state(reopened_base_order)).pixelColor(20, 20).alpha() == 0 &&
      render(Access::state(reopened_base_order)).pixelColor(320, 170).alpha() == 255,
      "Hiding the Base Image removes its source while retaining lower composition layers");
  CHECK(reopened_base_order.setPhotoLayerEnabled(reopened_base_index(), true) &&
      render(Access::state(reopened_base_order)) == base_over_shape,
      "Showing the Base Image restores its source at the same stack position");
  CHECK((!reopened_base_order.baseImageLocked() || reopened_base_order.setBaseImageLocked(false)) &&
      reopened_base_order.beginPhotoPaintStroke(-1, eraser),
      "A reloaded, reordered Base Image can start a sparse erase stroke");
  reopened_base_order.addPhotoPaintPoint(60, 60);
  CHECK(reopened_base_order.finishPhotoPaintStroke() &&
      wait_until([&] { return !reopened_base_order.photoPaintBusy(); }) &&
      reopened_base_order.photoPaintError().isEmpty(),
      "Finish the sparse erase stroke on the reordered source row");
  const auto sparse_erased_state = Access::state(reopened_base_order);
  const auto sparse_erased_output = render(sparse_erased_state);
  QVariantMap sparse_base_row;
  for (const auto& value : sparse_erased_state.layers) {
    const auto layer = value.toMap();
    if (layer.value("baseImageEdit").toBool()) sparse_base_row = layer;
  }
  const auto sparse_tiles = sparse_base_row.value("paintTiles").toMap().value("tiles").toMap();
  CHECK(sparse_base_row.value("copyBaseImage").toBool() && sparse_tiles.size() == 1 && sparse_tiles.contains("0,0") &&
      sparse_erased_output.pixelColor(60, 60).alpha() == 0 &&
      sparse_erased_output.pixelColor(20, 20) == base_over_shape.pixelColor(20, 20),
      "Reordered base erasure changes only local alpha and keeps its full-image source represented by sparse coverage");
  const auto sparse_erased_json = serialize_edit_recipe_json(sparse_erased_state);
  CHECK(validate_edit_recipe_json(sparse_erased_json).valid &&
      render(parse_edit_recipe_json(sparse_erased_json)) == sparse_erased_output,
      "Sparse eraser coverage survives recipe validation and reopening");

  // Keep this color-managed regional parity fixture to the same supported
  // source-plus-pixel-layer stack used by the paint benchmark. Shape layers
  // deliberately take the full-render fallback and cannot qualify device8.
  EditorPreviewController base_parity_controller;
  base_parity_controller.loadAsset("base-erase-device-parity", path, {}, {}, {}, {}, {});
  CHECK(wait_until([&] { return !base_parity_controller.processing() &&
      !base_parity_controller.fullRenderRunning(); }) &&
      base_parity_controller.addPaintLayer(),
      "Create a source plus transparent paint-layer parity document");
  CHECK(wait_until([&] { return !base_parity_controller.processing() &&
      !base_parity_controller.fullRenderRunning(); }) &&
      base_parity_controller.setBaseImageLocked(false),
      "Unlock the base source for the device-profile erase fixture");
  const auto parity_base_index = [&] {
    const auto layers = Access::state(base_parity_controller).layers;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index)
      if (layers.at(index).toMap().value(QStringLiteral("baseImageEdit")).toBool()) return index;
    return -1;
  };
  CHECK(parity_base_index() >= 0 &&
      (parity_base_index() == 0 || base_parity_controller.movePhotoLayer(
          parity_base_index(), -parity_base_index())) &&
      wait_until([&] { return !base_parity_controller.processing() &&
          !base_parity_controller.fullRenderRunning(); }),
      "Place the Base Image source at its explicit stack position");
  const auto base_parity_before = Access::state(base_parity_controller);
  const QString parity_base_id = base_parity_before.layers.at(parity_base_index())
      .toMap().value(QStringLiteral("id")).toString();
  auto partial_eraser = eraser;
  partial_eraser.insert(QStringLiteral("opacity"), 0.45);
  CHECK(base_parity_controller.beginPhotoPaintStroke(-1, partial_eraser),
      "Start a partial-opacity Base Image erase stroke");
  base_parity_controller.addPhotoPaintPoint(220, 300);
  CHECK(wait_until([&] {
    if (!Access::paintRasterIdle(base_parity_controller)) return false;
    const auto draft = Access::request(base_parity_controller).state.layers;
    for (const auto& value : draft)
      if (value.toMap().value(QStringLiteral("id")).toString() == parity_base_id)
        return value.toMap().contains(QStringLiteral("paintDraftTiles"));
    return false;
  }), "Partial base-erasure draft settles on its source row");
  CHECK(regional_matches_full(Access::request(base_parity_controller), base_parity_before.layers,
          require_device_profile, parity_base_id),
      "Partial-alpha base erasing preserves regional/full preview parity");
  base_parity_controller.finishPhotoPaintStroke(false);
  CHECK(wait_until([&] { return !base_parity_controller.photoPaintBusy(); }) &&
      Access::state(base_parity_controller) == base_parity_before &&
      base_parity_controller.photoPaintError().isEmpty(),
      "Cancel the temporary Base Image parity stroke and settle without changing the saved recipe");
  auto* malformed = new QMimeData;
  malformed->setData(photo_layer_clipboard_mime, "{invalid}");
  QGuiApplication::clipboard()->setMimeData(malformed);
  const auto before_invalid = Access::state(destination);
  CHECK(destination.pastePhotoLayers().isEmpty() && Access::state(destination) == before_invalid,
      "Malformed clipboard data cannot mutate the document");
  if (verify_linked_cutout_clipboard_adjustments(temp) != 0) return 1;
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

static int verify_text_transform_icc_parity(QTemporaryDir& temp) {
  (void)temp;
  if (!photo_editor::pipeline::metal::shared_metal_pipeline_backend().available()) {
    std::cout << "SKIP: system Metal backend is unavailable for the canonical suffix fallback proof\n";
    return 77;
  }
  const auto source_path = QDir(QStringLiteral(PHOTO_PAINT_TEST_ROOT)).filePath(
      QStringLiteral("marketing/media/stock/landscape-mountain.jpg"));
  const QImageReader source_reader(source_path);
  const auto source_size = source_reader.size();
  if (!source_size.isValid() || source_size.isEmpty()) {
    std::cerr << "Could not load the fixed photo-text ICC source fixture\n";
    return 1;
  }

  EditorPreviewController controller;
  controller.loadAsset(
      QStringLiteral("text-transform-icc-parity"), source_path,
      {}, {}, {}, {}, {});
  if (!wait_until([&] {
        return controller.sourceImageWidth() == source_size.width() &&
               controller.sourceImageHeight() == source_size.height() &&
               !controller.processing();
      })) {
    std::cerr << "The photo source did not settle before the ICC parity proof\n";
    return 1;
  }

  auto request = EditorPreviewControllerTestAccess::request(controller);
  auto state = request.state;
  if (!state.preview_icc_profile_asset.configured() ||
      state.preview_icc_profile_asset.asset_id.trimmed().isEmpty()) {
    std::cout << "SKIP: no active display ICC profile is configured\n";
    return 77;
  }

  auto style = photo_editor::desktop::PhotoTextStyle{};
  style.family = QStringLiteral("Georgia");
  style.size = 48.0;
  style.weight = 700;
  style.italic = true;
  style.underline = true;
  style.color = QColor(QStringLiteral("#ffffffff"));
  style.shadow_color = QColor(QStringLiteral("#a0000000"));
  style.runs = QVector<QVariantMap>{
      QVariantMap{
          {QStringLiteral("start"), 0},
          {QStringLiteral("length"), 4},
          {QStringLiteral("family"), QStringLiteral("Georgia")},
          {QStringLiteral("color"), QStringLiteral("#ffff347a")},
          {QStringLiteral("strokeColor"), QStringLiteral("#ff102030")},
          {QStringLiteral("strokeWidth"), 2.5},
      },
  };
  const auto typography = photo_editor::desktop::encode_photo_text_style(style);
  if (typography.value(QStringLiteral("version")).toInt() != 10) {
    std::cerr << "The ICC parity fixture must use persisted typography version 10\n";
    return 1;
  }
  const auto layer_id = QStringLiteral("text-transform-icc-v10");
  auto text_layer = QVariantMap{
      {QStringLiteral("id"), layer_id},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), QStringLiteral("CITY LIGHT")},
      {QStringLiteral("text"), QStringLiteral("CITY LIGHT\nAn evening edition")},
      {QStringLiteral("pixelWidth"), 460},
      {QStringLiteral("pixelHeight"), 180},
      {QStringLiteral("canvasWidth"), source_size.width()},
      {QStringLiteral("canvasHeight"), source_size.height()},
      {QStringLiteral("x"), 60},
      {QStringLiteral("y"), 60},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("rotation"), 0.0},
      {QStringLiteral("textTypography"), typography.toVariantMap()},
  };
  state.layers = QVariantList{text_layer};
  state.preview_output_mode = QStringLiteral("Monitor");
  state.preview_color_space = QStringLiteral("sRGB");
  const auto device_profile = photo_editor::pipeline::AssetIdentifier{
      state.preview_icc_profile_asset.asset_id.toStdString()};
  const auto admission = RenderPlanStore::shared().admit(
      request.source_asset_id,
      source_path,
      state,
      source_size,
      photo_editor::pipeline::RenderPurpose::InteractivePreview,
      photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay,
      device_profile);
  if (!admission.admitted || !admission.plan.has_value()) {
    std::cerr << "The actual v10 styled ICC fixture was not admitted: "
              << admission.error.toStdString() << '\n';
    return 1;
  }
  auto base_state = state;
  auto disabled_text_layer = text_layer;
  disabled_text_layer.insert(QStringLiteral("enabled"), false);
  base_state.layers = QVariantList{disabled_text_layer};
  const auto base_admission = RenderPlanStore::shared().admit(
      request.source_asset_id,
      source_path,
      base_state,
      source_size,
      photo_editor::pipeline::RenderPurpose::InteractivePreview,
      photo_editor::pipeline::DisplayRenderingTarget::ActiveDisplay,
      device_profile);
  if (!base_admission.admitted || !base_admission.plan.has_value()) {
    std::cerr << "The unstyled reference fixture was not admitted: "
              << base_admission.error.toStdString() << '\n';
    return 1;
  }

  request.state = state;
  request.render_reason = QStringLiteral("photo_layer_transform");
  request.interaction_mode = QStringLiteral("draft_interactive");
  request.render_mode = QStringLiteral("live_preview");
  request.dirty_domain = QStringLiteral("layers_composite");
  request.dirty_stages = QStringList{
      QStringLiteral("layers"), QStringLiteral("detail"),
      QStringLiteral("geometry"), QStringLiteral("photographic_output"),
      QStringLiteral("device_transform"), QStringLiteral("publish")};
  request.full_invalidation = false;
  request.draft_interactive = true;
  request.reuse_layer_transform_prefix = true;
  request.allow_native_device_surface = true;
  request.allow_native_srgb_surface = false;
  request.require_cpu_preview_image = false;
  request.require_canonical_render_plan = true;
  request.use_in_memory_preview = true;
  request.max_edge = 1024;
  request.active_layer_id = layer_id;
  request.edit_transaction_generation = 1;
  request.layer_source_content_revision = 1;
  request.generation = 1;
  request.latest_generation = std::make_shared<std::atomic<int>>(1);
  request.canonical_render_plan = admission.plan;
  request.canonical_scope_plan.reset();

  auto unstyled_request = request;
  unstyled_request.state = base_state;
  unstyled_request.active_layer_id.clear();
  unstyled_request.edit_transaction_generation = 0;
  unstyled_request.layer_source_content_revision = 0;
  unstyled_request.allow_parallel_cpu_device_transform = false;
  unstyled_request.canonical_render_plan = base_admission.plan;
  unstyled_request.recipe_cache_identity = QStringLiteral("text-transform-icc-base");
  unstyled_request.adjusted_recipe_cache_identity =
      QStringLiteral("text-transform-icc-base:adjusted");
  unstyled_request.latest_generation = std::make_shared<std::atomic<int>>(1);

  EditorPreviewControllerTestAccess::clear_working_caches();
  auto unstyled_result = render_preview_request_for_test(unstyled_request);
  if (!unstyled_result.success || unstyled_result.preview_image.isNull()) {
    std::cerr << "The unstyled ICC comparison render failed: status="
              << unstyled_result.status.toStdString() << " failure="
              << unstyled_result.failure_stage.toStdString() << '/'
              << unstyled_result.failure_detail.toStdString() << '\n';
    return 1;
  }
  const auto unstyled_pixels = unstyled_result.preview_image.convertToFormat(
      QImage::Format_RGBA8888);

  struct Sample final {
    RenderResult result;
    qint64 wall_ms = 0;
  };
  const auto run = [&](bool allow_parallel, int sample_index) {
    EditorPreviewControllerTestAccess::clear_working_caches();
    auto sample_request = request;
    sample_request.allow_parallel_cpu_device_transform = allow_parallel;
    sample_request.recipe_cache_identity =
        QStringLiteral("text-transform-icc-parity:%1:%2")
            .arg(sample_index)
            .arg(allow_parallel ? QStringLiteral("parallel")
                                : QStringLiteral("serial"));
    sample_request.adjusted_recipe_cache_identity =
        sample_request.recipe_cache_identity + QStringLiteral(":adjusted");
    sample_request.canonical_render_plan->plan_cache_key +=
        QStringLiteral(":text-icc-%1-%2")
            .arg(sample_index)
            .arg(allow_parallel ? QStringLiteral("parallel")
                                : QStringLiteral("serial"))
            .toStdString();
    sample_request.latest_generation = std::make_shared<std::atomic<int>>(1);
    QElapsedTimer timer;
    timer.start();
    auto result = render_preview_request_for_test(sample_request);
    return Sample{std::move(result), timer.elapsed()};
  };

  auto serial_wall = QVector<qint64>{};
  auto parallel_wall = QVector<qint64>{};
  auto serial_output = QVector<qint64>{};
  auto parallel_output = QVector<qint64>{};
  auto serial_photo = QVector<qint64>{};
  auto parallel_photo = QVector<qint64>{};
  auto serial_device = QVector<qint64>{};
  auto parallel_device = QVector<qint64>{};
  auto compared_pixels = qint64{0};
  auto styled_text_pixels = qint64{0};
  for (auto pair_index = 0; pair_index < 5; ++pair_index) {
    const auto parallel_first = pair_index % 2 != 0;
    const auto first = run(parallel_first, pair_index * 2);
    const auto second = run(!parallel_first, pair_index * 2 + 1);
    const auto& serial_sample = parallel_first ? second : first;
    const auto& parallel_sample = parallel_first ? first : second;
    const auto& serial_result = serial_sample.result;
    const auto& parallel_result = parallel_sample.result;
    if (!serial_result.success || !parallel_result.success ||
        serial_result.preview_image.isNull() || parallel_result.preview_image.isNull()) {
      std::cerr << "Text ICC serial/parallel CPU render failed: serial(status="
                << serial_result.status.toStdString() << ", failure="
                << serial_result.failure_stage.toStdString() << '/'
                << serial_result.failure_detail.toStdString() << "), parallel(status="
                << parallel_result.status.toStdString() << ", failure="
                << parallel_result.failure_stage.toStdString() << '/'
                << parallel_result.failure_detail.toStdString() << ")\n";
      return 1;
    }
    if (serial_result.output_width != 1024 || serial_result.output_height != 670 ||
        parallel_result.output_width != 1024 || parallel_result.output_height != 670 ||
        serial_result.preview_image.size() != parallel_result.preview_image.size() ||
        serial_result.preview_image.format() != parallel_result.preview_image.format() ||
        serial_result.preview_image.convertToFormat(QImage::Format_RGBA8888) !=
            parallel_result.preview_image.convertToFormat(QImage::Format_RGBA8888)) {
      std::cerr << "Text ICC parallel output must match serial RGBA exactly at 1024x670\n";
      return 1;
    }
    const auto styled_pixels = serial_result.preview_image.convertToFormat(
        QImage::Format_RGBA8888);
    if (styled_pixels.size() != unstyled_pixels.size()) {
      std::cerr << "Styled and unstyled fixture outputs have different dimensions\n";
      return 1;
    }
    auto sample_changed_pixels = qint64{0};
    for (auto y = 0; y < styled_pixels.height(); ++y) {
      const auto* styled_row = styled_pixels.constScanLine(y);
      const auto* unstyled_row = unstyled_pixels.constScanLine(y);
      for (auto x = 0; x < styled_pixels.width(); ++x) {
        if (std::memcmp(styled_row + x * 4, unstyled_row + x * 4, 4) != 0)
          ++sample_changed_pixels;
      }
    }
    if (sample_changed_pixels <= 500 ||
        sample_changed_pixels >=
            static_cast<qint64>(styled_pixels.width()) *
                styled_pixels.height() / 2) {
      std::cerr << "The styled v10 text must visibly change a bounded set of output pixels, changed="
                << sample_changed_pixels << '\n';
      return 1;
    }
    const auto serial_device_backend =
        serial_result.render_stage_backends.value(QStringLiteral("device_transform")).toString();
    const auto parallel_device_backend =
        parallel_result.render_stage_backends.value(QStringLiteral("device_transform")).toString();
    if (!serial_device_backend.contains(QStringLiteral("cpu-reference-fused-fallback")) ||
        serial_device_backend.contains(QStringLiteral("parallel-icc")) ||
        !parallel_device_backend.contains(QStringLiteral("parallel-icc"))) {
      std::cerr << "Text ICC proof did not qualify the CPU fused serial/parallel DeviceTransform routes: serial="
                << serial_device_backend.toStdString() << " parallel="
                << parallel_device_backend.toStdString() << '\n';
      return 1;
    }
    if (serial_result.output_lut.kernel_dispatch_count != 0 ||
        parallel_result.output_lut.kernel_dispatch_count != 0 ||
        serial_result.texture_upload_surface != nullptr ||
        parallel_result.texture_upload_surface != nullptr) {
      std::cerr << "The ICC parity proof must use CPU fallback output with no native surface or Metal dispatch\n";
      return 1;
    }

    serial_wall.push_back(serial_sample.wall_ms);
    parallel_wall.push_back(parallel_sample.wall_ms);
    serial_output.push_back(serial_result.output_stage_elapsed_ms);
    parallel_output.push_back(parallel_result.output_stage_elapsed_ms);
    serial_photo.push_back(serial_result.photographic_output_stage_elapsed_ms);
    parallel_photo.push_back(parallel_result.photographic_output_stage_elapsed_ms);
    serial_device.push_back(serial_result.render_stage_elapsed_ms
                                .value(QStringLiteral("device_transform")).toLongLong());
    parallel_device.push_back(parallel_result.render_stage_elapsed_ms
                                  .value(QStringLiteral("device_transform")).toLongLong());
    styled_text_pixels = sample_changed_pixels;
    compared_pixels += static_cast<qint64>(
        serial_result.preview_image.width()) * serial_result.preview_image.height();
  }
  std::sort(serial_wall.begin(), serial_wall.end());
  std::sort(parallel_wall.begin(), parallel_wall.end());
  std::sort(serial_output.begin(), serial_output.end());
  std::sort(parallel_output.begin(), parallel_output.end());
  std::sort(serial_photo.begin(), serial_photo.end());
  std::sort(parallel_photo.begin(), parallel_photo.end());
  std::sort(serial_device.begin(), serial_device.end());
  std::sort(parallel_device.begin(), parallel_device.end());
  const auto serial_median = serial_wall.at(serial_wall.size() / 2);
  const auto parallel_median = parallel_wall.at(parallel_wall.size() / 2);
  const auto wall_improvement = serial_median > 0
      ? 100.0 * static_cast<double>(serial_median - parallel_median) /
            static_cast<double>(serial_median)
      : 0.0;
  QFile source_file(source_path);
  if (!source_file.open(QIODevice::ReadOnly)) return 1;
  const auto source_sha = QCryptographicHash::hash(
      source_file.readAll(), QCryptographicHash::Sha256).toHex();
  const auto style_sha = QCryptographicHash::hash(
      QJsonDocument(typography).toJson(QJsonDocument::Compact),
      QCryptographicHash::Sha256).toHex();
  std::cout << "Text transform ICC parity: source=" << source_path.toStdString()
            << " source_sha256=" << source_sha.constData()
            << " style_v10_sha256=" << style_sha.constData()
            << " output=1024x670 model=" << state.photographic_rendering_model.toStdString()
            << " exposure=" << state.exposure
            << " working_profile=" << state.working_color_profile.toStdString()
            << " device_profile=" << state.preview_icc_profile_asset.asset_id.toStdString()
            << " pairs=5 compared_pixels=" << compared_pixels
            << " styled_text_changed_pixels=" << styled_text_pixels
            << " serial_wall_median_ms=" << serial_median
            << " parallel_wall_median_ms=" << parallel_median
            << " wall_improvement_percent=" << wall_improvement
            << " serial_output_median_ms=" << serial_output.at(serial_output.size() / 2)
            << " parallel_output_median_ms=" << parallel_output.at(parallel_output.size() / 2)
            << " serial_photo_output_median_ms=" << serial_photo.at(serial_photo.size() / 2)
            << " parallel_photo_output_median_ms=" << parallel_photo.at(parallel_photo.size() / 2)
            << " serial_device_transform_median_ms=" << serial_device.at(serial_device.size() / 2)
            << " parallel_device_transform_median_ms=" << parallel_device.at(parallel_device.size() / 2)
            << '\n';
  return 0;
}

int main(int argc, char** argv) {
  qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  const bool require_device_profile = app.arguments().contains("--require-native-device-icc");
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
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR"))
    observability::initialize();
  if (app.arguments().contains("--shape-fills")) return verify_shape_fills(temp);
  if (app.arguments().contains("--blank-canvas")) return verify_blank_canvas(temp);
  if (app.arguments().contains("--board-owned-persistence")) return verify_board_owned_persistence(temp);
  if (app.arguments().contains("--text-transform-icc-parity"))
    return verify_text_transform_icc_parity(temp);
  if (app.arguments().contains("--linked-cutout-clipboard-only"))
    return verify_linked_cutout_clipboard_adjustments(temp);
  if (app.arguments().contains("--document-workflow"))
    return verify_base_erase_and_clipboard(temp, require_device_profile);
  const QVariantMap empty{{"pixelWidth",640},{"pixelHeight",400},
      {"paintTiles",empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
  const auto raster = [](const PhotoPixelStroke& value) { return value.surface.raster({},nullptr,value.draft_tiles()); };
  const auto verify_paint_edge_parity = [&]() -> QString {
    const QColor color(230, 110, 50, 177);
    constexpr double flow = 0.63;
    constexpr double opacity = 0.72;
    for (const auto canvas : {QSize(640, 400), QSize(97, 71)}) {
      const auto min_extent = static_cast<double>(std::min(canvas.width() - 1, canvas.height() - 1));
      const QPointF center(canvas.width() * 0.5 + 0.25, canvas.height() * 0.5 - 0.25);
      const auto large_diameter = std::min(220.0, static_cast<double>(canvas.height()) * 0.7);
      for (const auto diameter : {3.0, 24.0, large_diameter}) {
        const auto requested_radius_pixels = diameter * 0.5;
        const auto selection_requested_radius = requested_radius_pixels / min_extent;
        // The selection rasterizer clamps normalized brush size to 0.01.
        // Keep that existing API behavior in the oracle for tiny brushes.
        const auto selection_radius_pixels = std::max(0.01, selection_requested_radius) * min_extent;
        const bool selection_radius_matches_paint =
            std::abs(selection_radius_pixels - requested_radius_pixels) <= 1.0e-9;
        for (const auto hardness : {0.0, 0.45, 0.8, 1.0}) {
          QVariantMap layer{{"id", QStringLiteral("edge-parity")}, {"kind", "pixel"},
              {"name", QStringLiteral("Edge parity")}, {"pixelWidth", canvas.width()},
              {"pixelHeight", canvas.height()}, {"canvasWidth", canvas.width()},
              {"canvasHeight", canvas.height()}, {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0},
              {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
          PhotoPixelStroke stroke;
          stroke.color = color;
          stroke.diameter = diameter;
          stroke.hardness = hardness;
          stroke.flow = flow;
          stroke.opacity = opacity;
          stroke.pressure_size = false;
          if (!stroke.begin(layer)) return QStringLiteral("PhotoPixelStroke initialization failed.");
          stroke.point(center, 1.0);
          stroke.finish();
          const auto draft = stroke.surface.raster({}, nullptr, stroke.draft_tiles());
          if (draft.isNull()) return QStringLiteral("PhotoPixelStroke draft raster was null.");

          QVariantMap selection{{"kind", "brush"}, {"segmentationWidth", canvas.width()},
              {"segmentationHeight", canvas.height()},
              {"segmentationData", QByteArray(canvas.width() * canvas.height(), '\0')}};
          const auto selection_center_x = (center.x() - 0.5) / (canvas.width() - 1);
          const auto selection_center_y = (center.y() - 0.5) / (canvas.height() - 1);
          (void)rasterize_brush_point_into_mask(&selection, nullptr, selection_center_x,
              selection_center_y, selection_requested_radius, flow,
              opacity * color.alphaF(), 0.0, false, false, 0.18, -1.0, -1.0, false,
              {}, nullptr, nullptr, nullptr, {.circular = true, .hardness = hardness});
          const auto selection_alpha = selection.value(QStringLiteral("segmentationData")).toByteArray();
          bool observed_selection_size_clamp = false;

          layer.insert(QStringLiteral("paintTiles"), stroke.commit());
          EditRecipeState state;
          state.layers = {layer};
          const auto serialized = serialize_edit_recipe_json(state);
          const auto decoded = parse_edit_recipe_json(serialized);
          if (!validate_edit_recipe_json(serialized).valid || decoded.layers.size() != 1)
            return QStringLiteral("Committed edge fixture did not survive recipe round-trip.");
          const auto restored = rasterize_photo_paint_layer(decoded.layers.front().toMap());
          if (restored.isNull() || restored != draft)
            return QStringLiteral("Draft and committed/export raster pixels differ.");

          for (int y = 0; y < canvas.height(); ++y) {
            const auto* actual_row = reinterpret_cast<const QRgbaFloat32*>(draft.constScanLine(y));
            for (int x = 0; x < canvas.width(); ++x) {
              const auto distance = std::hypot(x + 0.5 - center.x(), y + 0.5 - center.y());
              const auto paint_edge = photo_editor::pipeline::circular_brush_coverage_pixels(
                  distance, requested_radius_pixels, hardness);
              const auto expected_paint_weight = static_cast<float>(flow * paint_edge);
              const auto expected_paint_alpha = static_cast<float>(static_cast<double>(expected_paint_weight) *
                  opacity * color.alphaF());
              const auto selection_edge = photo_editor::pipeline::circular_brush_coverage_pixels(
                  distance, selection_radius_pixels, hardness);
              const auto expected_selection_alpha = flow * opacity * color.alphaF() * selection_edge;
              const auto actual_alpha = actual_row[x].alpha();
              const auto mask_alpha = static_cast<unsigned char>(selection_alpha.at(y * canvas.width() + x));
              const auto expected_selection_mask = qRound(expected_selection_alpha * 255.0);
              const auto paint_selection_difference = std::abs(
                  qRound(expected_paint_alpha * 255.0F) - expected_selection_mask);
              if (!selection_radius_matches_paint && paint_selection_difference > 1)
                observed_selection_size_clamp = true;
              const bool paint_matches_requested_geometry =
                  std::abs(actual_alpha - expected_paint_alpha) <= 1.0e-6F;
              const bool selection_matches_clamped_geometry =
                  std::abs(static_cast<int>(mask_alpha) - expected_selection_mask) <= 1;
              const bool tools_match_when_radius_matches = !selection_radius_matches_paint ||
                  std::abs(qRound(actual_alpha * 255.0F) - static_cast<int>(mask_alpha)) <= 1;
              if (!paint_matches_requested_geometry || !selection_matches_clamped_geometry ||
                  !tools_match_when_radius_matches) {
                return QStringLiteral(
                    "Edge parity failed canvas=%1x%2 diameter=%3 hardness=%4 pixel=(%5,%6) "
                    "paint=%7 expectedRequested=%8 selection8=%9 expectedClamped=%10 "
                    "paintRadius=%11 selectionRadius=%12")
                    .arg(canvas.width()).arg(canvas.height()).arg(diameter).arg(hardness)
                    .arg(x).arg(y).arg(actual_alpha, 0, 'g', 9).arg(expected_paint_alpha, 0, 'g', 9)
                    .arg(mask_alpha).arg(expected_selection_mask).arg(requested_radius_pixels)
                    .arg(selection_radius_pixels);
              }
            }
          }
          if (!selection_radius_matches_paint && !observed_selection_size_clamp)
            return QStringLiteral("Tiny selection-brush fixture did not exercise its existing size clamp.");
        }
      }
    }
    const QSize canvas(256, 256);
    QVariantMap seeded_layer{{"id", QStringLiteral("saturated-coverage-parity")}, {"kind", "pixel"},
        {"name", QStringLiteral("Saturated coverage parity")}, {"pixelWidth", canvas.width()},
        {"pixelHeight", canvas.height()}, {"canvasWidth", canvas.width()}, {"canvasHeight", canvas.height()},
        {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0},
        {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))}};
    PhotoPaintSurface seeded_surface;
    QString seed_error;
    if (!seeded_surface.load(seeded_layer, &seed_error))
      return QStringLiteral("Saturated coverage baseline setup failed: ") + seed_error;
    for (int ty = 0; ty * kPhotoPaintTileEdge < canvas.height(); ++ty) {
      for (int tx = 0; tx * kPhotoPaintTileEdge < canvas.width(); ++tx) {
        auto tile = seeded_surface.tile(tx, ty, &seed_error);
        if (tile.isNull()) return QStringLiteral("Saturated coverage tile setup failed: ") + seed_error;
        const auto rect = seeded_surface.tile_rect(tx, ty);
        for (int y = 0; y < tile.height(); ++y) {
          auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
          for (int x = 0; x < tile.width(); ++x) {
            const int gx = rect.x() + x, gy = rect.y() + y;
            const float alpha = 0.15F + 0.85F * static_cast<float>((gx * 13 + gy * 17) % 97) / 96.0F;
            row[x] = QRgbaFloat32{0.68F * alpha, 0.24F * alpha, 0.91F * alpha, alpha};
          }
        }
        seeded_surface.store(tx, ty, tile);
      }
    }
    seeded_layer.insert(QStringLiteral("paintTiles"), seeded_surface.snapshot());
    QImage white_selection(canvas, QImage::Format_Grayscale8);
    white_selection.fill(255);
    QImage changed_selection(canvas, QImage::Format_Grayscale8);
    for (int y = 0; y < canvas.height(); ++y) {
      auto* row = changed_selection.scanLine(y);
      for (int x = 0; x < canvas.width(); ++x)
        row[x] = static_cast<uchar>(((x / 24 + y / 32) % 3) == 0 ? 0 : ((x / 24) % 2 == 0 ? 128 : 255));
    }
    const auto run_saturated = [&](bool erase, double hardness, bool white_mask,
                                   const QString& mutation, bool change_size_flow) {
      PhotoPixelStroke stroke;
      stroke.color = erase ? QColor(255, 255, 255, 255) : QColor(225, 92, 37, 207);
      stroke.diameter = 80;
      stroke.hardness = hardness;
      stroke.opacity = 0.67;
      stroke.flow = 1.0;
      stroke.spacing = 0.16;
      stroke.erase = erase;
      stroke.pressure_size = false;
      if (white_mask) stroke.selection = white_selection;
      if (!stroke.begin(seeded_layer)) return QPair<QImage, QVariantMap>{};
      stroke.point({88, 128}, 1.0);
      if (change_size_flow) {
        stroke.diameter = 112;
        stroke.hardness = 0.42;
        stroke.flow = 0.83;
        stroke.pressure_size = true;
        stroke.pressure_flow = true;
      }
      if (mutation == QStringLiteral("opacity")) stroke.opacity = 0.31;
      else if (mutation == QStringLiteral("color_alpha")) stroke.color.setAlpha(89);
      else if (mutation == QStringLiteral("erase")) stroke.erase = !stroke.erase;
      else if (mutation == QStringLiteral("material")) {
        stroke.texture_material = QStringLiteral("graphite");
        stroke.texture_strength = 0.64;
      } else if (mutation == QStringLiteral("selection")) stroke.selection = changed_selection;
      stroke.point({143, 128}, 0.72);
      stroke.point({168, 144}, 1.0);
      stroke.finish();
      auto draft = stroke.surface.raster({}, nullptr, stroke.draft_tiles());
      auto committed = stroke.commit();
      return qMakePair(std::move(draft), std::move(committed));
    };
    for (const bool erase : {false, true}) {
      for (const double hardness : {0.55, 1.0}) {
        const auto optimized = run_saturated(erase, hardness, false, {}, false);
        const auto white_control = run_saturated(erase, hardness, true, {}, false);
        if (optimized.first.isNull() || white_control.first.isNull() ||
            optimized.first != white_control.first || optimized.second != white_control.second)
          return QStringLiteral("Absorbing-coverage paint/eraser output differs from all-white selection control.");
      }
    }
    for (const auto& mutation : {QStringLiteral("opacity"), QStringLiteral("color_alpha"),
             QStringLiteral("erase"), QStringLiteral("material"), QStringLiteral("selection")}) {
      const auto optimized = run_saturated(false, 1.0, false, mutation, false);
      const auto white_control = run_saturated(false, 1.0, true, mutation, false);
      if (optimized.first.isNull() || white_control.first.isNull() ||
          optimized.first != white_control.first || optimized.second != white_control.second)
        return QStringLiteral("Midstroke %1 change did not fall back to all-white-selection parity.").arg(mutation);
    }
    const auto changing_pressure = run_saturated(false, 1.0, false, {}, true);
    const auto changing_pressure_control = run_saturated(false, 1.0, true, {}, true);
    if (changing_pressure.first.isNull() || changing_pressure_control.first.isNull() ||
        changing_pressure.first != changing_pressure_control.first ||
        changing_pressure.second != changing_pressure_control.second)
      return QStringLiteral("Diameter/hardness/flow/pressure changes altered saturated output.");
    return {};
  };
  if (app.arguments().contains(QStringLiteral("--paint-edge-parity"))) {
    const auto diagnostic = verify_paint_edge_parity();
    CHECK(diagnostic.isEmpty(), diagnostic.toStdString());
    return 0;
  }
  auto cpu_brush_compositor = BrushPreviewCompositor::createCpu();
  QImage brush_overlay;
  CHECK(cpu_brush_compositor && cpu_brush_compositor->ensureTarget(&brush_overlay, QSize(201, 101)),
      "Allocate a premultiplied CPU brush overlay");
  const auto verify_preview_edge = [&](const QColor& color, QString* diagnostic) {
    cpu_brush_compositor->clear(&brush_overlay);
    const BrushPreviewStyle style{.color = color, .feather = 0.0, .hardness = 1.0,
        .opacity = 0.4, .circular = true};
    const QVector<BrushPreviewPoint> hard_dab{
        BrushPreviewPoint{.x = 0.5, .y = 0.5, .size = 0.2, .flow = 1.0, .opacity = 1.0}};
    for (int dab = 0; dab < 4; ++dab)
      cpu_brush_compositor->compositePoints(&brush_overlay, hard_dab, style);
    const auto pixel = [&](int x, int y) {
      return reinterpret_cast<const QRgb*>(brush_overlay.constScanLine(y))[x];
    };
    const auto center = pixel(100, 50);
    const auto horizontal_edge = pixel(120, 50);
    const auto vertical_edge = pixel(100, 70);
    const auto outside = pixel(121, 50);
    const auto expected_center_alpha = qRound((1.0 - std::pow(1.0 - 0.4, 4)) * 255.0);
    const auto expected_edge_alpha = qRound((1.0 - std::pow(1.0 - 0.2, 4)) * 255.0);
    const auto premultiplied_matches = [&](QRgb sample) {
      const auto alpha = qAlpha(sample);
      if (alpha <= 0 || qRed(sample) > alpha || qGreen(sample) > alpha || qBlue(sample) > alpha)
        return false;
      const auto channel_error = [alpha](int premultiplied, int straight) {
        return std::abs(premultiplied - straight * alpha / 255.0);
      };
      // A one-code-value tolerance is the quantization limit for this
      // 8-bit premultiplied target. At alpha 151, unassociating a one-code
      // channel difference can appear as a two-code RGB difference.
      return channel_error(qRed(sample), color.red()) <= 1.0 &&
          channel_error(qGreen(sample), color.green()) <= 1.0 &&
          channel_error(qBlue(sample), color.blue()) <= 1.0;
    };
    const auto pixel_text = [](QRgb value) {
      const auto alpha = qAlpha(value);
      const auto straight = [alpha](int channel) {
        return alpha > 0 ? qRound(channel * 255.0 / alpha) : 0;
      };
      return QStringLiteral("premult=(%1,%2,%3,%4), straight=(%5,%6,%7)")
          .arg(qRed(value)).arg(qGreen(value)).arg(qBlue(value)).arg(alpha)
          .arg(straight(qRed(value))).arg(straight(qGreen(value))).arg(straight(qBlue(value)));
    };
    const bool center_alpha_matches = std::abs(qAlpha(center) - expected_center_alpha) <= 1;
    const bool edge_alpha_matches = std::abs(qAlpha(horizontal_edge) - expected_edge_alpha) <= 1;
    const bool shape_matches = horizontal_edge == vertical_edge;
    const bool outside_is_clear = qAlpha(outside) == 0;
    const bool center_color_matches = premultiplied_matches(center);
    const bool edge_color_matches = premultiplied_matches(horizontal_edge);
    if (diagnostic != nullptr && !(center_alpha_matches && edge_alpha_matches && shape_matches &&
            outside_is_clear && center_color_matches && edge_color_matches)) {
      *diagnostic = QStringLiteral(
          "color=%1 center{%2} expectedAlpha=%3 edge{%4} expectedAlpha=%5 vertical{%6} outside{%7}; "
          "checks alpha=%8/%9 shape=%10 clear=%11 premultColor=%12/%13")
          .arg(color.name(QColor::HexRgb)).arg(pixel_text(center)).arg(expected_center_alpha)
          .arg(pixel_text(horizontal_edge)).arg(expected_edge_alpha).arg(pixel_text(vertical_edge))
          .arg(pixel_text(outside)).arg(center_alpha_matches).arg(edge_alpha_matches)
          .arg(shape_matches).arg(outside_is_clear).arg(center_color_matches).arg(edge_color_matches);
    }
    return center_alpha_matches &&
        edge_alpha_matches && shape_matches && outside_is_clear &&
        center_color_matches && edge_color_matches;
  };
  QString pale_preview_diagnostic;
  QString dark_preview_diagnostic;
  const auto pale_preview_ok = verify_preview_edge(QColor("#f4f7fa"), &pale_preview_diagnostic);
  const auto dark_preview_ok = verify_preview_edge(QColor("#202830"), &dark_preview_diagnostic);
  CHECK(pale_preview_ok && dark_preview_ok,
      "Repeated circular preview dabs preserve premultiplied alpha, color, pixel-space shape and a one-pixel hard edge: pale[" <<
          pale_preview_diagnostic.toStdString() << "] dark[" << dark_preview_diagnostic.toStdString() << "]");
  PhotoPixelStroke hard_pixel_brush;
  hard_pixel_brush.color = QColor("#f4f7fa");
  hard_pixel_brush.diameter = 40;
  hard_pixel_brush.hardness = 1.0;
  hard_pixel_brush.pressure_size = false;
  CHECK(hard_pixel_brush.begin(empty), "Hard pixel-brush initialization");
  hard_pixel_brush.point({50.5, 50.5}, 1.0);
  hard_pixel_brush.finish();
  const auto hard_pixel_image = raster(hard_pixel_brush);
  // Pixel centers at radii 19, 20, and 21 pin the band around the tip edge.
  CHECK(qAlpha(hard_pixel_image.pixel(69, 50)) == 255 &&
      qAlpha(hard_pixel_image.pixel(70, 50)) >= 126 && qAlpha(hard_pixel_image.pixel(70, 50)) <= 129 &&
      qAlpha(hard_pixel_image.pixel(71, 50)) == 0,
      "Hard paint and eraser brush edges use one raster pixel of antialiasing at hardness 1");
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
  {
    // Reduced previews must consider live regions before native escalation.
    auto reduced = draft_request;
    reduced.max_edge = 320;
    reduced.use_in_memory_preview = true;
    reduced.recipe_cache_identity.clear();
    reduced.adjusted_recipe_cache_identity.clear();
    reduced.canonical_render_plan.reset();
    reduced.canonical_scope_plan.reset();
    auto base_request = reduced;
    base_request.state.layers = blank.layers;
    base_request.paint_preview_base = {};
    const auto base = render_preview_request_for_test(base_request);
    CHECK(base.success && base.preview_image.size() == QSize(320, 200), "Reduced paint display base");
    reduced.paint_preview_base = base.preview_image;
    const auto live = render_preview_request_for_test(reduced);
    CHECK(live.success && live.backend == QStringLiteral("paint-region") &&
        live.partial_texture_upload_candidate && live.texture_upload_pixels < 320 * 200 &&
        live.preview_image.pixelColor(0, 0) == base.preview_image.pixelColor(0, 0),
        "Live paint on a reduced preview must update its region before native escalation");
    reduced.paint_preview_base = {};
    const auto settled = render_preview_request_for_test(reduced);
    CHECK(settled.success && settled.backend != QStringLiteral("paint-region") && settled.native_quality_composite,
        "Paint without a transient base must retain full native-quality compositing");
  }
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
  const auto erase_draft_request = Access::request(controller);
  CHECK(regional_matches_full(erase_draft_request, before_erase.layers, require_device_profile,
          erase_draft_request.state.layers[0].toMap().value(QStringLiteral("id")).toString()),
        "Partial-alpha paint-layer erasing preserves regional/full preview parity");
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
    Item { id: paintOptions; x: 800; y: 0; width: 480; height: 720 }
    PhotoPaintOverlay {
        objectName: "paintOverlay"; anchors.fill: parent
        controlsParent: paintOptions
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
    const auto hover_ready = wait_until([&] {
      return hover_cursor->isVisible() &&
          QLineF(paintOverlay->property("cursorPoint").toPointF(), QPointF(600, 420)).length() < 1;
    });
    if (!hover_ready) std::cerr << "paint hover diagnostic cursorVisible=" << hover_cursor->isVisible()
        << " overlayVisible=" << paintOverlay->property("cursorVisible").toBool()
        << " inside=" << paintOverlay->property("cursorInsideImage").toBool()
        << " point=" << paintOverlay->property("cursorPoint").toPointF().x() << ","
        << paintOverlay->property("cursorPoint").toPointF().y()
        << " cursorSize=" << hover_cursor->width() << "x" << hover_cursor->height() << '\n';
    CHECK(hover_ready, "A mouse move with no buttons must show and position the brush outline before any stroke");
    const auto hover_frame = window.grabWindow();
    CHECK(!before_hover_frame.isNull() && !hover_frame.isNull(), "Hover readback captures the real canvas");
    const double hover_dpr = static_cast<double>(hover_frame.width()) / window.width();
    auto contour_value = hover_cursor->property("contours");
    auto contour_map = contour_value.toMap();
    if (contour_map.isEmpty()) contour_map = contour_value.value<QJSValue>().toVariant().toMap();
    const auto contour = contour_map.value(QStringLiteral("outer")).toList();
    CHECK(contour.size() == 65, "Hover uses the production projected physical brush contour");
    const auto outline_point = contour.first().toMap();
    CHECK(outline_point.value(QStringLiteral("valid")).toBool(), "Hover contour sample is visible");
    const int outline_x = qRound((paintOverlay->property("imageX").toDouble() +
        outline_point.value(QStringLiteral("x")).toDouble() * paintOverlay->property("imageWidth").toDouble()) * hover_dpr);
    const int outline_y = qRound((paintOverlay->property("imageY").toDouble() +
        outline_point.value(QStringLiteral("y")).toDouble() * paintOverlay->property("imageHeight").toDouble()) * hover_dpr);
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
  // The preset round-trip below requires partially covered edge pixels.
  // A pixel-aligned rectangle with zero feather has only binary coverage.
  controller.setActiveMaskFeather(0.02);
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
