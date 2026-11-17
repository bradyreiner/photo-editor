#include "photo_canvas_coordinates.h"
#include "editor_preview_controller.h"
#include "photo_pixel_stroke.h"
#include "brush_rasterizer.h"
#include "edit_recipe_mask_schema.h"
#include "photo_render_utils.h"
#include "flattened_output_renderer.h"
#include "brush_input_schema.h"
#include "edit_recipe_v13_persistence.h"
#include "tablet_input_area.h"
#include "native_window_test_input.h"
#include <QMouseEvent>
#include <QKeyEvent>
#include <QFile>
#include <QEnterEvent>
#include <QBuffer>
#include <cstring>

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSettings>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJSValue>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <QDir>
#include <atomic>
#include <cmath>
#include <iostream>
#include <memory>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
struct EditorPreviewControllerTestAccess {
  static void configure(EditorPreviewController& controller, const QString& path, const EditState& state) {
    controller.asset_id_ = QStringLiteral("canvas-coordinate-proof");
    controller.file_path_ = path;
    controller.applyState(state);
  }
  static EditState state(const EditorPreviewController& controller) { return controller.currentState(); }
  static BrushLivePreviewFrame live_frame(const EditorPreviewController& controller) {
    return controller.active_brush_live_preview_frame_;
  }
  static QString paint_material(const EditorPreviewController& controller) {
    return controller.photo_paint_input_ ? controller.photo_paint_input_->stroke->texture_material : QString();
  }
  static bool paint_idle(const EditorPreviewController& controller) {
    return !controller.photo_paint_worker_running_ && controller.photo_paint_jobs_.empty();
  }
  static bool live_idle(const EditorPreviewController& controller) {
    return !controller.brush_live_preview_running_ && controller.pending_brush_live_preview_points_.isEmpty();
  }
  static bool brush_idle(const EditorPreviewController& controller) {
    return controller.pending_brush_commits_.isEmpty() && !controller.pending_brush_commit_scheduled_ &&
        !controller.brush_raster_commit_active_ &&
        !controller.mask_worker_coordinator_.brushRasterCommitWatcher().isRunning();
  }
};
}
namespace {
using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;
#define REQUIRE(condition, message) do { if (!(condition)) { std::cerr << message << '\n'; return 1; } } while (false)

RenderResult render(const QString& path, const EditState& state, int generation, bool draft,
                    int max_edge = 128, bool full_resolution = true) {
  RenderRequest request;
  request.source_asset_id = QStringLiteral("canvas-coordinate-proof");
  request.file_path = path;
  request.state = state;
  request.full_resolution = !draft && full_resolution;
  request.draft_interactive = draft;
  request.render_mode = draft ? QStringLiteral("live_preview") : QStringLiteral("max_preview");
  request.max_edge = max_edge;
  request.use_in_memory_preview = true;
  request.require_cpu_preview_image = true;
  request.require_cpu_display_image = true;
  request.require_canonical_render_plan = true;
  request.generation = generation;
  request.latest_generation = std::make_shared<std::atomic<int>>(generation);
  return render_preview_request_for_test(request);
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), static_cast<std::size_t>(a.width()) * 4) != 0) return false;
  }
  return true;
}

bool wait_paint(EditorPreviewController& controller) {
  QElapsedTimer timer; timer.start();
  while (!EditorPreviewControllerTestAccess::paint_idle(controller) && timer.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
  }
  return EditorPreviewControllerTestAccess::paint_idle(controller) && controller.photoPaintError().isEmpty();
}

int verify_native_source_aware_brush(QTemporaryDir& runtime) {
  constexpr int width = 2401, height = 65, boundary = 1801;
  QImage source(width, height, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
    source.setPixel(x, y, x < boundary ? qRgb(30, 34, 38) : qRgb(226, 231, 236));
  const auto path = runtime.filePath("native-guide-boundary.png");
  REQUIRE(source.save(path), "Save source-aware fixture beyond preview decode limits");
  EditState state;
  state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  QImage guide;
  REQUIRE(decode_brush_color_source_image(path, &guide, 0, state) && guide.size() == source.size(),
      "Prepare full native characterized source guide");
  for (const auto protect_only : {false, true}) {
    auto mask = default_mask_definition(QStringLiteral("brush"), 1);
    mask.insert("coordinateSpace", "source");
    mask.insert("brushAutoMask", !protect_only); mask.insert("brushProtectEdges", protect_only);
    mask.insert("brushAutoMaskTolerance", 0.05); mask.insert("brushSize", 6.0 / (height - 1));
    mask.insert("brushFlow", 1.0); mask.insert("brushHardness", 1.0);
    mask.insert("brushSpacing", 0.2); mask.insert("brushSmoothing", 0.0); mask.insert("feather", 0.0);
    auto brush_state = state; brush_state.mask_stack = {mask}; brush_state.active_mask_index = 0;
    auto controller = std::make_unique<EditorPreviewController>();
    EditorPreviewControllerTestAccess::configure(*controller, path, brush_state);
    const auto x = 1800.0 / (width - 1), y = 32.0 / (height - 1);
    controller->setPreviewViewport(1770.0 / width, 0, 64.0 / width, 1.0, 260, true);
    controller->beginActiveBrushSourceStroke();
    controller->addBrushSourcePointWithPressure(x, y, 1);
    QElapsedTimer live_settle; live_settle.start();
    while ((controller->activeMaskLiveBrushPreviewPointCount() < 1 ||
            !EditorPreviewControllerTestAccess::live_idle(*controller)) && live_settle.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    const auto live = EditorPreviewControllerTestAccess::live_frame(*controller);
    REQUIRE(live.raster.alpha.size() == width * height && live.source.size() == source.size() &&
        live.display_image.height() == height && live.display_image.width() <= 66 &&
        live.display_rect.width() < 0.03 && !live.display_image.isNull(),
        "Deep-zoom source-aware feedback retains native guide/alpha but projects only the visible native output ROI");
    const auto display_region = QRect(qRound(live.display_rect.x() * width), qRound(live.display_rect.y() * height),
        live.display_image.width(), live.display_image.height());
    const auto program = compile_geometry_stage_program(ColorAdjustmentState{}, width, height);
    const auto full_projection = project_mask_canvas_preview(live.raster.image, source.size(), program,
        source.size(), source.rect(), source.rect());
    REQUIRE(same_pixels(live.display_image, full_projection.copy(display_region)),
        "Native-pixel live ROI must match the same pixels of full projection at deep zoom");
    controller->endActiveBrushStroke();
    QElapsedTimer settle; settle.start();
    while (!EditorPreviewControllerTestAccess::brush_idle(*controller) && settle.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
    }
    REQUIRE(EditorPreviewControllerTestAccess::brush_idle(*controller), "Publish native-grid source-aware brush commit");
    const auto committed = EditorPreviewControllerTestAccess::state(*controller).mask_stack.at(0).toMap();
    REQUIRE(committed.value("segmentationWidth").toInt() == width && committed.value("segmentationHeight").toInt() == height,
        "Source-aware committed alpha must retain every original source pixel beyond preview cap");
    auto expected = QByteArray(width * height, '\0');
    const auto changed = rasterize_brush_point_into_alpha(&expected, width, height, &guide,
        x, y, 6.0 / (height - 1), 1, 1, 0, false, !protect_only, 0.05, x, y, protect_only,
        {}, nullptr, nullptr, nullptr, {.circular = true, .hardness = 1.0});
    REQUIRE(live.raster.alpha == expected,
        "Live native source-aware alpha must equal the same full-guide reference before committing");
    REQUIRE(!changed.isEmpty() && committed.value("segmentationData").toByteArray() == expected,
        "Committed source-aware coverage must equal a full-native guide raster at the exact one-pixel color boundary");
    std::cout << "canvas_native_source_brush_parity width=2401 boundary=1801 auto_mask=" << !protect_only
              << " protect_edges=" << protect_only << " native_alpha=1 exact_reference=1 live_roi=1 live_equals_commit=1\n";
  }
  return 0;
}

int verify_desktop_retouch_crop(QTemporaryDir& runtime) {
  constexpr int width = 201, height = 101;
  const QRect crop(100, 0, 81, height);
  QImage image(width, height, QImage::Format_ARGB32);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
    const auto texture = ((x / 2 + y / 2) % 2) * 28;
    image.setPixel(x, y, x >= 100 && y >= 15 && y <= 50
        ? qRgb(210 + texture, 22 + texture, 25 + texture)
        : qRgb(20 + x * 180 / 200, 30 + y * 150 / 100, 45 + texture));
  }
  const auto path = runtime.filePath("retouch-source.png");
  REQUIRE(image.save(path), "Save actual retouch source fixture");
  QImage patch(width, height, QImage::Format_ARGB32);
  patch.fill(QColor("#2571a9")); patch.setColorSpace(QColorSpace(QColorSpace::SRgb));
  QByteArray patch_png; QBuffer patch_buffer(&patch_png); patch_buffer.open(QIODevice::WriteOnly);
  REQUIRE(patch.save(&patch_buffer, "PNG"), "Encode actual desktop retouch patch");
  const auto mask_for = [&](const QString& mode, double clone_source_x) {
    auto mask = default_mask_definition(QStringLiteral("brush"), 1);
    mask.insert("id", "desktop-retouch-crop-proof");
    mask.insert("coordinateSpace", "source");
    mask.insert("toolRole", "retouch"); mask.insert("retouchMode", mode);
    mask.insert("committed", true); mask.insert("cloneSourceSet", true);
    mask.insert("cloneSourceX", clone_source_x); mask.insert("cloneSourceY", 0.3);
    mask.insert("cloneAligned", false); mask.insert("cloneOffsetX", clone_source_x - 0.8);
    mask.insert("cloneOffsetY", 0.0); mask.insert("brushSize", 0.08);
    mask.insert("brushHardness", 1.0); mask.insert("brushFlow", 1.0);
    mask.insert("brushAutoMask", false); mask.insert("brushProtectEdges", false);
    mask.insert("removeStrength", 100.0); mask.insert("feather", 0.0);
    BrushPreviewPoint first; first.x = 0.55; first.y = 0.3; first.size = 0.08; first.flow = 1; first.opacity = 1;
    auto second = first; second.x = 0.8;
    mask.insert("brushPoints", QVariantList{brush_point_variant(first), brush_point_variant(second)});
    mask.insert("brushStrokes", QVariantList{brush_stroke_variant(mask, {first}, 1), brush_stroke_variant(mask, {second}, 2)});
    if (mode == QStringLiteral("patch")) {
      mask.insert("inpaintPatchData", patch_png); mask.insert("inpaintPatchX", 0); mask.insert("inpaintPatchY", 0);
      mask.insert("inpaintPatchWidth", width); mask.insert("inpaintPatchHeight", height);
      mask.insert("inpaintPatchColorSpace", "sRGB"); mask.insert("inpaintBlendAmount", 1.0);
      mask.insert("inpaintColorMatch", 0.0); mask.insert("inpaintSeamFeather", 0.0);
    }
    return mask;
  };
  EditState neutral;
  neutral.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  const auto neutral_result = render(path, neutral, 500, false, 0, false);
  REQUIRE(neutral_result.success, "Render neutral full-source retouch reference");
  int generation = 501;
  for (const auto* mode_name : {"clone", "heal", "remove", "red-eye", "skin-cleanup", "frequency-smoothing", "patch"}) {
    const auto mode = QString::fromLatin1(mode_name);
    auto full = neutral; full.mask_stack = {mask_for(mode, 0.15)}; full.active_mask_index = 0;
    auto cropped = full;
    cropped.crop_rect_x = 100.0 / width; cropped.crop_rect_y = 0;
    cropped.crop_rect_width = 81.0 / width; cropped.crop_rect_height = 1;
    const auto full_preview = render(path, full, generation++, false, 0, false);
    const auto cropped_preview = render(path, cropped, generation++, false, 0, false);
    REQUIRE(full_preview.success && cropped_preview.success &&
        same_pixels(full_preview.preview_image.copy(crop), cropped_preview.preview_image),
        "Actual canonical desktop full-source/cropped preview pixels must agree for " << mode_name);
    REQUIRE(!same_pixels(neutral_result.preview_image.copy(crop), cropped_preview.preview_image),
        "Retouch crop proof must exercise a visible operation for " << mode_name);
    const auto full_export = render_flattened_output_file(QStringLiteral("retouch-full-proof"), path, full,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888);
    const auto crop_export = render_flattened_output_file(QStringLiteral("retouch-crop-proof"), path, cropped,
        QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888);
    REQUIRE(same_pixels(full_export.copy(crop), crop_export),
        "Actual canonical export crop must equal the same full-source output pixels for " << mode_name);
    if (mode == QStringLiteral("clone")) {
      auto moved = cropped; moved.mask_stack = {mask_for(mode, 0.05)};
      const auto changed = render(path, moved, generation++, false, 0, false);
      REQUIRE(changed.success && (changed.decoded_cache_hit || changed.linear_cache_hit) &&
          !same_pixels(changed.preview_image, cropped_preview.preview_image),
          "Changing off-crop clone origin on a decoded/linear cache hit must attach fresh full-source context");
      DesktopRecipeV13Context context;
      context.recipe_id = QStringLiteral("retouch-crop-roundtrip");
      context.source_id = QStringLiteral("retouch-crop-proof"); context.raw_source = false;
      context.decoder_provenance = QStringLiteral("raster");
      const auto unbound = serialize_desktop_recipe_as_canonical_v13(cropped, context);
      for (const auto& error : unbound.errors)
        std::cout << "canvas_retouch_unbound_serialization path=" << error.path
                  << " message=" << error.message << '\n';
      REQUIRE(!unbound.ok() && unbound.errors.size() == 1 &&
          unbound.errors.front().path == "masks[0].sources[0].payload_asset" &&
          unbound.errors.front().message ==
              "raster, vector, AI, depth, and retouch masks require immutable payload identity",
          "History-backed retouch serialization must require an immutable payload asset");
      const RecipeAssetStore assets(runtime.filePath("retouch-recipe-assets"));
      const auto saved = serialize_desktop_recipe_as_canonical_v13_with_assets(cropped, context, assets);
      for (const auto& error : saved.errors)
        std::cerr << "canvas_retouch_serialization path=" << error.path << " message=" << error.message << '\n';
      REQUIRE(saved.ok(), "Source-coordinate retouch must serialize canonically");
      const auto parsed = deserialize_edit_recipe_v13(saved.document.recipe_payload.toStdString());
      REQUIRE(parsed.has_value(), "Deserialize canonical source retouch recipe");
      REQUIRE(parsed->masks.size() == 1 && parsed->masks.front().sources.size() == 1 &&
          parsed->masks.front().sources.front().payload_asset.has_value() &&
          parsed->masks.front().sources.front().payload_asset->value.starts_with("sha256:"),
          "Canonical retouch recipe must bind its preserved history to immutable stored content");
      const auto reopened = project_canonical_v13_to_desktop_state_with_assets(*parsed, assets);
      REQUIRE(reopened.ok() && reopened.state->mask_stack.at(0).toMap().value("coordinateSpace").toString() == QStringLiteral("source"),
          "Canonical reopen preserves source-coordinate retouch provenance");
      const auto reopened_preview = render(path, *reopened.state, generation++, false, 0, false);
      REQUIRE(reopened_preview.success && same_pixels(reopened_preview.preview_image, cropped_preview.preview_image),
          "Canonical source retouch reopen preserves exact cropped output pixels");
      std::cout << "canvas_retouch_context_cache_roundtrip clone_offcrop=1 source_hit=1 changed_origin=1 reopened=1\n";
    }
    std::cout << "canvas_retouch_crop_pipeline_parity mode=" << mode_name << " preview=1 export=1 visible_effect=1 full_source=1\n";
  }
  return 0;
}

bool verify_renderer_sample_coordinates(const ColorAdjustmentState& state, const QSize& source_size,
                                        const QRect& crop) {
  const auto program = compile_geometry_stage_program(state, crop.width(), crop.height());
  std::vector<ColorPixel> ramp(static_cast<std::size_t>(crop.width()) * static_cast<std::size_t>(crop.height()));
  for (int y = 0; y < crop.height(); ++y) for (int x = 0; x < crop.width(); ++x) {
    ramp[static_cast<std::size_t>(y * crop.width() + x)] = {
        static_cast<float>(x) / static_cast<float>(crop.width() - 1),
        static_cast<float>(y) / static_cast<float>(crop.height() - 1), 0.0F, 1.0F};
  }
  if (!apply_compiled_geometry_stage_program(&ramp, program)) return false;
  for (const auto output_pixel : {QPoint(24, 12), QPoint(47, 27), QPoint(70, 16)}) {
    const auto output = QPointF((output_pixel.x() + 0.5) / crop.width(),
                                (output_pixel.y() + 0.5) / crop.height());
    const auto mapped = map_canvas_coordinate(program, source_size, crop, output, false,
                                               CanvasCoordinateSpace::SourceMask);
    const auto source_x = mapped.point.x() * (source_size.width() - 1) - crop.x();
    const auto source_y = mapped.point.y() * (source_size.height() - 1) - crop.y();
    const auto pixel = ramp[static_cast<std::size_t>(output_pixel.y() * crop.width() + output_pixel.x())];
    if (!mapped.valid || std::abs(source_x - pixel.red * (crop.width() - 1)) > 0.0001 ||
        std::abs(source_y - pixel.green * (crop.height() - 1)) > 0.0001) return false;
    const auto forward = map_canvas_coordinate(program, source_size, crop, mapped.point, true,
                                                CanvasCoordinateSpace::SourceMask);
    if (!forward.valid || std::hypot((forward.point.x() - output.x()) * crop.width(),
                                     (forward.point.y() - output.y()) * crop.height()) > 0.003) return false;
    // A real source-basis brush matte must hit the same rendered sample. The
    // one-pixel alpha raster is independent of the coordinate sampler itself.
    QByteArray alpha(source_size.width() * source_size.height(), '\0');
    const auto radius = 4.0 / std::min(source_size.width() - 1, source_size.height() - 1);
    const auto changed = rasterize_brush_point_into_alpha(&alpha, source_size.width(), source_size.height(), nullptr,
        mapped.point.x(), mapped.point.y(), radius, 1, 1, 0, false, false, 0.18, -1, -1,
        false, {}, nullptr, nullptr, nullptr, {.circular = true, .hardness = 1.0});
    if (changed.isEmpty()) return false;
    std::vector<ColorPixel> matte(ramp.size());
    for (int y = 0; y < crop.height(); ++y) for (int x = 0; x < crop.width(); ++x) {
      const auto coverage = static_cast<unsigned char>(alpha[(crop.y() + y) * source_size.width() + crop.x() + x]);
      matte[static_cast<std::size_t>(y * crop.width() + x)] = {0, 0, 0, coverage / 255.0F};
    }
    if (!apply_compiled_geometry_stage_program(&matte, program) ||
        matte[static_cast<std::size_t>(output_pixel.y() * crop.width() + output_pixel.x())].alpha < 0.99F) return false;
    QImage authored(source_size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < source_size.height(); ++y) {
      auto* row = reinterpret_cast<QRgb*>(authored.scanLine(y));
      for (int x = 0; x < source_size.width(); ++x) {
        const auto coverage = static_cast<unsigned char>(alpha[y * source_size.width() + x]);
        row[x] = qRgba(coverage, coverage, coverage, coverage);
      }
    }
    const auto projected = project_mask_canvas_preview(authored, crop.size(), program,
        source_size, crop, QRect(QPoint(0, 0), source_size));
    for (int y = 0; y < crop.height(); ++y) for (int x = 0; x < crop.width(); ++x) {
      const auto expected = matte[static_cast<std::size_t>(y * crop.width() + x)].alpha * 255;
      if (std::abs(qAlpha(projected.pixel(x, y)) - expected) > 1.01) return false;
    }
  }
  return true;
}
}

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  REQUIRE(runtime.isValid(), "Create isolated coordinate test runtime");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath("app").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", "1");
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/canvas-coordinate-proof");
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyCoordinateTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoCanvasCoordinates"));
  QGuiApplication app(argc, argv);
  qmlRegisterType<TabletInputArea>("PhotoEditor", 1, 0, "TabletInputArea");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, runtime.filePath("settings"));
  for (const auto& size : {QSize(0, 0), QSize(0, 20), QSize(20, 0), QSize(-1, 20)})
    REQUIRE(crop_rect_for_image(size, EditState{}).isEmpty(),
        "Unavailable or zero-axis source dimensions must fail closed before pixel-bound clamps");
  REQUIRE(crop_rect_for_image(QSize(1, 1), EditState{}) == QRect(0, 0, 1, 1) &&
      apply_crop(QImage(), EditState{}).isNull(),
      "Crop bounds preserve a valid single pixel and safely reject a null source image");
  const QSize source_size(128, 80);
  const QRect crop(16, 12, 96, 48);
  {
    const auto neutral_program = compile_geometry_stage_program(ColorAdjustmentState{}, crop.width(), crop.height());
    for (const auto point : {QPointF(-0.25, 0.4), QPointF(1.3, 1.15)}) {
      const auto mask = map_canvas_coordinate(neutral_program, source_size, crop, point, false,
          CanvasCoordinateSpace::SourceMask);
      const auto authoring = map_canvas_coordinate(neutral_program, source_size, crop, point, false,
          CanvasCoordinateSpace::SourceMask, true);
      const auto restored = map_canvas_coordinate(neutral_program, source_size, crop, authoring.point, true,
          CanvasCoordinateSpace::SourceMask, true);
      REQUIRE(!mask.valid && authoring.valid && restored.valid &&
          std::abs(restored.point.x() - point.x()) < 0.0001 &&
          std::abs(restored.point.y() - point.y()) < 0.0001,
          "Off-canvas geometry handles extrapolate and round-trip without changing bounded mask sampling");
    }
  }
  ColorAdjustmentState affine;
  affine.transform_rotation = 31;
  affine.transform_scale = 1.4;
  affine.transform_skew_x = 23;
  affine.transform_skew_y = -17;
  affine.perspective_vertical = 27;
  affine.perspective_horizontal = -19;
  REQUIRE(verify_renderer_sample_coordinates(affine, source_size, crop),
      "Non-square crop/rotation/scale/skew/perspective must hit actual rendered source samples");
  auto nonlinear = affine;
  nonlinear.lens_distortion = 11;
  nonlinear.liquify_strength = 19;
  nonlinear.liquify_radius = 0.8;
  REQUIRE(verify_renderer_sample_coordinates(nonlinear, source_size, crop),
      "Lens distortion and liquify must share the rendered coordinate sampler");

  for (const auto line_crop : {QRect(37, 12, 1, 48), QRect(16, 31, 96, 1), QRect(37, 31, 1, 1)}) {
    const auto line_program = compile_geometry_stage_program(affine, line_crop.width(), line_crop.height());
    const auto point = map_canvas_coordinate(line_program, source_size, line_crop, {0.5, 0.5}, false,
        CanvasCoordinateSpace::SourceMask);
    const auto restored = map_canvas_coordinate(line_program, source_size, line_crop, point.point, true,
        CanvasCoordinateSpace::SourceMask);
    REQUIRE(point.valid && restored.valid && std::abs(restored.point.x() - 0.5) < 1e-6 &&
        std::abs(restored.point.y() - 0.5) < 1e-6, "One-pixel crop axes retain exact sample centers");
  }
  const auto singleton_program = compile_geometry_stage_program(affine, 1, 1);
  const auto singleton = map_canvas_coordinate(singleton_program, QSize(1, 1), QRect(0, 0, 1, 1), {0.5, 0.5}, false,
      CanvasCoordinateSpace::SourceMask);
  REQUIRE(singleton.valid && singleton.point == QPointF(0.5, 0.5), "A one-pixel source uses its center on both mask axes");

  {
    auto mirrored = nonlinear; mirrored.content_aware_crop_fill = true;
    const auto program = compile_geometry_stage_program(mirrored, crop.width(), crop.height());
    QImage authored(source_size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < authored.height(); ++y) for (int x = 0; x < authored.width(); ++x) {
      const auto alpha = ((x / 7 + y / 5) % 3) * 127;
      authored.setPixel(x, y, qRgba(alpha, alpha, alpha, alpha));
    }
    const auto full = project_mask_canvas_preview(authored, crop.size(), program,
        source_size, crop, QRect(QPoint(), source_size));
    const QRect roi(31, 7, 41, 29);
    const auto projected_roi = project_mask_canvas_preview(authored, roi.size(), program,
        source_size, crop, QRect(QPoint(), source_size), roi, crop.size());
    REQUIRE(same_pixels(projected_roi, full.copy(roi)),
        "Output-space ROI projection must exactly preserve rotation/liquify/mirrored secondary coverage");
    std::cout << "canvas_live_roi_projection_parity rotation=1 distortion=1 liquify=1 mirror_fill=1 secondary_coverage=1 exact_pixels=1\n";
  }

  QImage source(source_size, QImage::Format_ARGB32);
  source.fill(QColor("#303030"));
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto path = runtime.filePath("source.png");
  REQUIRE(source.save(path), "Save source coordinate fixture");
  EditState state;
  state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  state.crop_rect_x = 16.0 / 128;
  state.crop_rect_y = 12.0 / 80;
  state.crop_rect_width = 96.0 / 128;
  state.crop_rect_height = 48.0 / 80;
  state.transform_rotation = affine.transform_rotation;
  state.transform_scale = affine.transform_scale;
  state.transform_skew_x = affine.transform_skew_x;
  state.transform_skew_y = affine.transform_skew_y;
  state.perspective_vertical = affine.perspective_vertical;
  state.perspective_horizontal = affine.perspective_horizontal;
  QVariantMap layer{{"id", "paint-proof"}, {"kind", "pixel"}, {"name", "Coordinate paint"},
      {"paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear))},
      {"pixelWidth", 128}, {"pixelHeight", 80}, {"canvasWidth", 128}, {"canvasHeight", 80},
      {"enabled", true}, {"opacity", 1.0}, {"scale", 1.0}, {"coordinateSpace", "source-area"},
      {"coordinateArea", QVariantMap{{"x", 16.0 / 128}, {"y", 12.0 / 80},
                                      {"width", 96.0 / 128}, {"height", 48.0 / 80}}}};
  state.layers = {layer};
  auto controller = std::make_unique<EditorPreviewController>();
  EditorPreviewControllerTestAccess::configure(*controller, path, state);
  const QPoint output_pixel(70, 16);
  const QPointF output((output_pixel.x() + 0.5) / crop.width(), (output_pixel.y() + 0.5) / crop.height());
  const auto mapped = controller->mapCanvasCoordinate(output.x(), output.y(), false, QStringLiteral("paint:paint-proof"));
  REQUIRE(mapped.value("valid").toBool(), "Paint pointer must map through current final geometry");

  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath("apps/desktop/qml/components/CanvasCoordinateMapper.qml")));
  std::unique_ptr<QObject> mapper(component.create());
  REQUIRE(mapper != nullptr, "Create shared QML coordinate mapper");
  mapper->setProperty("geometryMapper", QVariant::fromValue<QObject*>(controller.get()));
  mapper->setProperty("coordinateSpace", "paint:paint-proof");
  mapper->setProperty("contentX", 27.0);
  mapper->setProperty("contentY", -13.0);
  mapper->setProperty("contentWidth", 768.0);
  mapper->setProperty("contentHeight", 384.0);
  for (const auto dpr : {1.0, 2.0, 2.5}) {
    mapper->setProperty("devicePixelRatio", dpr);
    QVariant returned;
    REQUIRE(QMetaObject::invokeMethod(mapper.get(), "viewToImagePoint", Q_RETURN_ARG(QVariant, returned),
        Q_ARG(QVariant, 27.0 + output.x() * 768.0), Q_ARG(QVariant, -13.0 + output.y() * 384.0)),
        "Invoke pointer mapping at zoom/pan and fractional DPR");
    const auto point = returned.toMap();
    REQUIRE(point.value("valid").toBool() &&
        std::abs(point.value("x").toDouble() - mapped.value("x").toDouble()) < 1e-9 &&
        std::abs(point.value("y").toDouble() - mapped.value("y").toDouble()) < 1e-9,
        "QML pointer must preserve exact logical source coordinates at every DPR");
  }

  const auto source_point = controller->mapCanvasCoordinate(output.x(), output.y(), false, QStringLiteral("source"));
  REQUIRE(controller->applyActiveSelectionSourcePixel(source_point.value("x").toDouble(),
      source_point.value("y").toDouble(), QStringLiteral("replace")), "Select actual source pixel through inverse geometry");
  const auto selected_state = EditorPreviewControllerTestAccess::state(*controller);
  const auto selected_mask = selected_state.mask_stack.at(selected_state.active_mask_index).toMap();
  const auto selection = selected_mask.value("segmentationData").toByteArray();
  const auto selected_x = qRound(source_point.value("x").toDouble() * (source_size.width() - 1));
  const auto selected_y = qRound(source_point.value("y").toDouble() * (source_size.height() - 1));
  REQUIRE(selection.size() == source_size.width() * source_size.height() &&
      static_cast<unsigned char>(selection[selected_y * source_size.width() + selected_x]) == 255,
      "Pixel selector must mark the exact sampled source pixel, without a second crop");
  int selected_pixels = 0;
  for (const auto value : selection) if (value != 0) ++selected_pixels;
  REQUIRE(selected_pixels == 1, "Exact-pixel selection changes exactly one source pixel");
  EditorPreviewControllerTestAccess::configure(*controller, path, state);

  PhotoPixelStroke stroke;
  stroke.color = Qt::white;
  stroke.diameter = 10;
  stroke.hardness = 1;
  stroke.pressure_size = false;
  REQUIRE(stroke.begin(layer), "Begin production pixel paint stroke");
  const auto paint_point = QPointF(mapped.value("x").toDouble() * 128,
                                   mapped.value("y").toDouble() * 80);
  stroke.point(paint_point, 1);
  stroke.finish();
  layer.insert("paintTiles", stroke.commit());
  auto painted_state = state;
  painted_state.layers = {layer};
  for (const auto draft : {false, true}) {
    const auto neutral = render(path, state, draft ? 11 : 10, draft);
    const auto painted = render(path, painted_state, draft ? 13 : 12, draft);
    REQUIRE(neutral.success && painted.success && neutral.preview_image.size() == crop.size() &&
        painted.preview_image.size() == crop.size(), "Render canonical cropped paint preview/full output");
    REQUIRE(painted.preview_image.pixelColor(output_pixel).redF() >
        neutral.preview_image.pixelColor(output_pixel).redF() + 0.15,
        "Production paint must change the exact rendered pixel under the pointer");
    PhotoPixelStroke eraser;
    eraser.erase = true;
    eraser.diameter = 10;
    eraser.hardness = 1;
    eraser.pressure_size = false;
    REQUIRE(eraser.begin(layer), "Begin production pixel eraser stroke");
    eraser.point(paint_point, 1);
    eraser.finish();
    auto erased = layer;
    erased.insert("paintTiles", eraser.commit());
    auto erased_state = painted_state;
    erased_state.layers = {erased};
    const auto erased_render = render(path, erased_state, draft ? 15 : 14, draft);
    REQUIRE(erased_render.success && erased_render.preview_image.pixelColor(output_pixel).redF() + 0.15 <
        painted.preview_image.pixelColor(output_pixel).redF(),
        "Production eraser must remove the exact rendered pixel under the pointer");
    std::cout << "canvas_pointer_render_parity draft=" << draft << " crop=1 non_square=1 rotate=1 scale=1 skew=1 perspective=1 paint=1 erase=1\n";
  }
  auto empty_legacy = layer;
  empty_legacy.remove("coordinateArea"); empty_legacy.remove("coordinateSpace");
  empty_legacy.insert("paintTiles", empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgbLinear)));
  empty_legacy.insert("pixelWidth", 48); empty_legacy.insert("pixelHeight", 24);
  empty_legacy.insert("canvasWidth", 48); empty_legacy.insert("canvasHeight", 24);
  auto empty_state = state; empty_state.layers = {empty_legacy};
  EditorPreviewControllerTestAccess::configure(*controller, path, empty_state);
  const auto empty_before = EditorPreviewControllerTestAccess::state(*controller);
  REQUIRE(controller->beginPhotoPaintStroke(0, {{"color", "#ffffff"}, {"size", 10.0},
      {"hardness", 1.0}, {"pressureSize", false}}), "Begin empty legacy canvas native-grid promotion");
  const auto empty_point = controller->mapCanvasCoordinate(output.x(), output.y(), false, QStringLiteral("paint:paint-proof"));
  controller->addPhotoPaintPoint(empty_point.value("x").toDouble() * source_size.width(),
      empty_point.value("y").toDouble() * source_size.height(), 1);
  REQUIRE(controller->finishPhotoPaintStroke(true), "Release empty legacy canvas native-grid promotion");
  QElapsedTimer empty_settle; empty_settle.start();
  while (controller->photoPaintBusy() && empty_settle.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(5);
  }
  const auto promoted = EditorPreviewControllerTestAccess::state(*controller);
  REQUIRE(!controller->photoPaintBusy() && controller->photoPaintError().isEmpty(), "Commit promoted legacy canvas atomically");
  const auto promoted_layer = promoted.layers.at(0).toMap();
  REQUIRE(promoted_layer.value("pixelWidth").toInt() == source_size.width() &&
      promoted_layer.value("pixelHeight").toInt() == source_size.height() &&
      promoted_layer.value("canvasWidth").toInt() == source_size.width() &&
      promoted_layer.value("canvasHeight").toInt() == source_size.height() &&
      promoted_layer.value("coordinateArea").toMap().value("width").toDouble() == 1.0,
      "Empty legacy surface dimensions and full-source coordinate basis are promoted together");
  const auto promoted_render = render(path, promoted, 30, false);
  const auto empty_render = render(path, empty_before, 31, false);
  REQUIRE(promoted_render.success && empty_render.success &&
      promoted_render.preview_image.pixelColor(output_pixel).redF() > empty_render.preview_image.pixelColor(output_pixel).redF() + 0.15,
      "Promoted cropped legacy surface paints the exact rendered pointer pixel");
  EditorPreviewControllerTestAccess::configure(*controller, path, empty_state);
  const auto cancel_before = EditorPreviewControllerTestAccess::state(*controller);
  REQUIRE(controller->beginPhotoPaintStroke(0, {{"color", "#ffffff"}, {"size", 10.0}}), "Begin cancellable native surface promotion");
  controller->addPhotoPaintPoint(20, 20, 1); controller->finishPhotoPaintStroke(false);
  REQUIRE(EditorPreviewControllerTestAccess::state(*controller).layers == cancel_before.layers,
      "Cancelling native promotion leaves the original legacy target and dimensions unchanged");
  std::cout << "canvas_empty_legacy_promotion_parity cropped=1 native_dimensions=1 exact_pixel=1 cancel_unchanged=1\n";

  // A real QML first gesture must use the planned native authoring dimensions,
  // even while its bound layerData still describes the old empty 48x24 canvas.
  EditorPreviewControllerTestAccess::configure(*controller, path, empty_state);
  QQuickWindow paint_window; paint_window.resize(1200, 600); paint_window.show();
  QQuickItem paint_options(paint_window.contentItem());
  paint_options.setPosition({850, 0}); paint_options.setSize({320, 600});
  QQmlComponent paint_component(&engine, QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath("apps/desktop/qml/components/PhotoPaintOverlay.qml")));
  std::unique_ptr<QObject> paint_object(paint_component.createWithInitialProperties({
      {"controller", QVariant::fromValue<QObject*>(controller.get())}, {"active", true},
      {"controlsParent", QVariant::fromValue<QObject*>(&paint_options)},
      {"width", 840.0}, {"height", 500.0}, {"layerIndex", 0}, {"layerData", empty_legacy},
      {"imageX", 27.0}, {"imageY", 13.0}, {"imageWidth", 768.0}, {"imageHeight", 384.0},
      {"canvasWidth", 48}, {"canvasHeight", 24}, {"ink", "#ffffff"},
      {"brushSize", 10.0}, {"hardness", 1.0}, {"pressureSize", false}}));
  if (!paint_object) std::cerr << paint_component.errorString().toStdString() << '\n';
  REQUIRE(paint_object && qobject_cast<QQuickItem*>(paint_object.get()), "Instantiate actual first-stroke paint QML with a legacy empty canvas");
  qobject_cast<QQuickItem*>(paint_object.get())->setParentItem(paint_window.contentItem());
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  REQUIRE(paint_object->property("documentCanvasWidth").toDouble() == source_size.width() &&
      paint_object->property("documentCanvasHeight").toDouble() == source_size.height(),
      "QML point mapping uses planned native dimensions before layer metadata commits");
  const QPoint ui_pixel(70, 44);
  const QPointF ui_position(27 + (ui_pixel.x() + 0.5) / crop.width() * 768,
                            13 + (ui_pixel.y() + 0.5) / crop.height() * 384);
  QEnterEvent enter(ui_position, ui_position, QPointF(paint_window.mapToGlobal(ui_position.toPoint())));
  QCoreApplication::sendEvent(&paint_window, &enter);
  for (const auto press : {true, false}) {
    QMouseEvent event(press ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease, ui_position,
        QPointF(paint_window.mapToGlobal(ui_position.toPoint())), Qt::LeftButton,
        press ? Qt::LeftButton : Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&paint_window, &event);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
  REQUIRE(wait_paint(*controller), "Commit actual QML first stroke after empty legacy promotion");
  const auto ui_painted = EditorPreviewControllerTestAccess::state(*controller);
  const auto ui_render = render(path, ui_painted, 32, false);
  if (ui_painted.layers.at(0).toMap().value("pixelWidth").toInt() != source_size.width() ||
      !ui_render.success || ui_render.preview_image.pixelColor(ui_pixel).redF() <= empty_render.preview_image.pixelColor(ui_pixel).redF() + 0.15)
    std::cerr << "First QML stroke: editable=" << paint_object->property("editable").toBool()
        << " controlsHeight=" << paint_object->property("toolOptionsHeight").toDouble()
        << " pointer=" << ui_position.x() << ',' << ui_position.y()
        << " selection=" << controller->hasWorkingSelection()
        << " layerChanged=" << (ui_painted.layers != empty_before.layers)
        << " drawing=" << paint_object->property("drawing").toBool()
        << " status=" << controller->status().toStdString() << '\n';
  REQUIRE(ui_painted.layers.at(0).toMap().value("pixelWidth").toInt() == source_size.width() &&
      ui_render.success && ui_render.preview_image.pixelColor(ui_pixel).redF() >
          empty_render.preview_image.pixelColor(ui_pixel).redF() + 0.15,
      "Actual QML first stroke hits the exact cropped/transformed output pixel using native authoring dimensions");

  // Production Paint/Eraser overlay: native pointer events must use the active
  // selection even when the legacy optional clipping switch is false.
  EditState clipping_state;
  QImage clipping_pixels(128, 80, QImage::Format_ARGB32); clipping_pixels.fill(Qt::white);
  QByteArray clipping_png; QBuffer clipping_buffer(&clipping_png); clipping_buffer.open(QIODevice::WriteOnly);
  REQUIRE(clipping_pixels.save(&clipping_buffer, "PNG"), "Encode native clipping pixels");
  QVariantMap clipping_layer{{"id", "native-clipped-target"}, {"kind", "pixel"},
      {"pixelWidth", 128}, {"pixelHeight", 80}, {"canvasWidth", 128}, {"canvasHeight", 80},
      {"pixelData", clipping_png}, {"enabled", true}, {"opacity", 1.0}};
  auto clipping_sibling = clipping_layer; clipping_sibling.insert("id", "untouched-sibling");
  clipping_state.layers = {clipping_layer, clipping_sibling};
  EditorPreviewControllerTestAccess::configure(*controller, path, clipping_state);
  controller->addMask("rectangle"); controller->setActiveMaskFeather(0);
  controller->setActiveMaskCenterX(0.25); controller->setActiveMaskCenterY(0.5);
  controller->setActiveMaskRadiusX(0.25); controller->setActiveMaskRadiusY(0.5);
  const auto clipping_before = EditorPreviewControllerTestAccess::state(*controller);
  int clipping_index = -1;
  for (int i = 0; i < clipping_before.layers.size(); ++i)
    if (clipping_before.layers[i].toMap().value("id").toString() == "native-clipped-target") clipping_index = i;
  REQUIRE(clipping_index >= 0, "Find chosen native eraser layer");
  QQuickItem native_controls(paint_window.contentItem());
  native_controls.setY(430); native_controls.setWidth(840); native_controls.setHeight(70);
  paint_object->setProperty("controlsParent", QVariant::fromValue<QQuickItem*>(&native_controls));
  paint_object->setProperty("layerIndex", clipping_index);
  paint_object->setProperty("layerData", clipping_before.layers[clipping_index]);
  paint_object->setProperty("eraserTool", true);
  paint_object->setProperty("clipSelection", false);
  paint_object->setProperty("brushSize", 16.0);
  paint_object->setProperty("imageWidth", 768.0); paint_object->setProperty("imageHeight", 384.0);
  const auto send_clipped_gesture = [&] {
    const QPointF first(27 + 0.2 * 768, 13 + 0.7 * 384), last(27 + 0.85 * 768, 13 + 0.7 * 384);
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseButtonRelease}) {
      const auto point = type == QEvent::MouseButtonPress ? first : last;
      QMouseEvent event(type, point, QPointF(paint_window.mapToGlobal(point.toPoint())),
          type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(&paint_window, &event); QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
  };
  send_clipped_gesture(); REQUIRE(wait_paint(*controller), "Commit real selection-clipped eraser gesture");
  const auto native_erased = EditorPreviewControllerTestAccess::state(*controller);
  const auto native_erased_pixels = rasterize_photo_paint_layer(native_erased.layers[clipping_index].toMap());
  if (native_erased_pixels.pixelColor(35, 56).alpha() != 0 || native_erased_pixels.pixelColor(100, 56).alpha() != 255)
    std::cerr << "native clipped alpha=" << native_erased_pixels.pixelColor(35, 56).alpha() << "," << native_erased_pixels.pixelColor(100, 56).alpha()
      << " editable=" << paint_object->property("editable").toBool() << " selection=" << paint_object->property("selectionAvailable").toBool()
      << " size=" << paint_object->property("documentCanvasWidth").toDouble() << "," << paint_object->property("documentCanvasHeight").toDouble()
      << " has_tiles=" << native_erased.layers[clipping_index].toMap().contains("paintTiles") << '\n';
  REQUIRE(native_erased_pixels.pixelColor(35, 56).alpha() == 0 && native_erased_pixels.pixelColor(100, 56).alpha() == 255,
      "Production eraser UI automatically respects selected and unselected pixels");
  REQUIRE(native_erased.layers[clipping_index + 1] == clipping_before.layers[clipping_index + 1], "Native erase leaves sibling layer exact");

  QFile workspace_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  REQUIRE(workspace_source.open(QIODevice::ReadOnly), "Read actual selection shortcuts");
  const auto workspace_qml = QString::fromUtf8(workspace_source.readAll());
  QString shortcut_qml = QStringLiteral("import QtQuick\nItem { id: root; property var editorPreview; readonly property var editorPreviewController: editorPreview; property string page: 'edit'; property bool workspaceActive: true; property string pendingSelectionAction: ''; property string editTool: 'eraser'; function shortcutTextInputActive(){return false} function endMaskShapeEditTransaction(){} ");
  for (const auto* name : {"selectionShortcutContextActive", "queueSelectionAction", "runSelectionAction", "invertActiveMaskFromShortcut"}) {
    const auto start = workspace_qml.indexOf(QStringLiteral("    function %1(").arg(QString::fromLatin1(name)));
    const auto end = workspace_qml.indexOf(QStringLiteral("\n    }"), start);
    REQUIRE(start >= 0 && end > start, "Extract actual selection shortcut handler");
    shortcut_qml += workspace_qml.mid(start, end + 6 - start);
  }
  for (const auto* name : {"maskInvertShortcut", "selectionDeselectShortcut"}) {
    const auto shortcut_name = workspace_qml.indexOf(QStringLiteral("objectName: \"%1\"").arg(QString::fromLatin1(name)));
    const auto shortcut_start = workspace_qml.lastIndexOf(QStringLiteral("    Shortcut {"), shortcut_name);
    const auto shortcut_end = workspace_qml.indexOf(QStringLiteral("\n    }"), shortcut_name);
    REQUIRE(shortcut_name >= 0 && shortcut_start >= 0 && shortcut_end > shortcut_start, "Extract actual selection keyboard binding");
    shortcut_qml += workspace_qml.mid(shortcut_start, shortcut_end + 6 - shortcut_start);
  }
  shortcut_qml += QStringLiteral("\n}");
  QQmlComponent shortcut_component(&engine); shortcut_component.setData(shortcut_qml.toUtf8(), QUrl("inmemory:SelectionShortcut.qml"));
  QElapsedTimer shortcut_load_timer; shortcut_load_timer.start();
  while (shortcut_component.isLoading() && shortcut_load_timer.elapsed() < 3000)
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  std::unique_ptr<QObject> shortcut_object(shortcut_component.createWithInitialProperties({{"editorPreview", QVariant::fromValue<QObject*>(controller.get())}}));
  if (!shortcut_object) std::cerr << shortcut_component.errorString().toStdString();
  REQUIRE(shortcut_object, "Instantiate production invert shortcut and handlers");
  qobject_cast<QQuickItem*>(shortcut_object.get())->setParentItem(paint_window.contentItem());
  activate_native_test_window(&paint_window);
  paint_window.requestActivate(); QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
  if (QGuiApplication::platformName() == QStringLiteral("cocoa") && !paint_window.isActive()) {
    std::cerr << "Native keyboard qualification unavailable: test window cannot receive focus; unlock the macOS desktop to qualify shortcuts.\n";
    paint_object.reset();
    return 77;
  }
  const auto send_selection_key = [&](int key_code, Qt::KeyboardModifiers modifiers) {
    for (const auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
      QKeyEvent key(type, key_code, modifiers);
      QCoreApplication::sendEvent(&paint_window, &key); QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
  };
  send_selection_key(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
  REQUIRE(controller->maskStack().first().toMap().value("maskSetSelectionInvert").toBool() &&
      shortcut_object->property("editTool").toString() == "eraser", "Ctrl/Cmd+Shift+I inverts without changing Eraser");
#ifdef Q_OS_MACOS
  send_selection_key(Qt::Key_I, Qt::MetaModifier | Qt::ShiftModifier);
  REQUIRE(!controller->maskStack().first().toMap().value("maskSetSelectionInvert").toBool(), "Physical Control+Shift+I alias toggles inversion on macOS");
  send_selection_key(Qt::Key_I, Qt::ControlModifier | Qt::ShiftModifier);
#endif
  paint_object->setProperty("layerData", native_erased.layers[clipping_index]);
  paint_object->setProperty("eraserTool", false); paint_object->setProperty("ink", QColor("#1688ff"));
  send_clipped_gesture(); REQUIRE(wait_paint(*controller), "Commit real paint gesture in inverted selection");
  const auto native_painted = EditorPreviewControllerTestAccess::state(*controller);
  const auto native_painted_pixels = rasterize_photo_paint_layer(native_painted.layers[clipping_index].toMap());
  REQUIRE(native_painted_pixels.pixelColor(35, 56).alpha() == 0 && native_painted_pixels.pixelColor(100, 56).blue() > 240 &&
      native_painted_pixels.pixelColor(100, 56).red() < 80, "Native paint hits only the inverted selection on the chosen layer");
  REQUIRE(native_painted.layers[clipping_index + 1] == clipping_before.layers[clipping_index + 1], "Native paint preserves sibling layer exact");
  const auto retained_mask_count = controller->maskCount();
  send_selection_key(Qt::Key_D, Qt::ControlModifier);
  REQUIRE(!controller->hasActiveMask() && controller->maskCount() == retained_mask_count,
      "Ctrl/Cmd+D clears the working selection without deleting the saved mask");
  shortcut_object.reset();
  std::cout << "native_selection_paint_workflow production_shortcut=1 pointer_erase=1 inverted_pointer_paint=1 sibling_unchanged=1\n";

  paint_window.close(); paint_object.reset();
  std::cout << "canvas_empty_legacy_qml_first_stroke actual_events=1 planned_native_dimensions=1 exact_pixel=1\n";

  QVariantMap parent{{"id", "parent-proof"}, {"kind", "group"}, {"name", "Transformed parent"},
      {"canvasWidth", 128}, {"canvasHeight", 80}, {"x", 4}, {"y", -2},
      {"scale", 1.13}, {"rotation", 17.0}, {"enabled", true}, {"opacity", 1.0}};
  auto child = empty_legacy;
  child.insert("pixelWidth", 128); child.insert("pixelHeight", 80);
  child.insert("canvasWidth", 128); child.insert("canvasHeight", 80);
  child.insert("coordinateSpace", "source-area");
  child.insert("coordinateArea", QVariantMap{{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}});
  child.insert("parentId", "parent-proof"); child.insert("rotation", 12.0); child.insert("scale", 0.9);
  auto grouped = state; grouped.layers = {parent, child};
  EditorPreviewControllerTestAccess::configure(*controller, path, grouped);
  const auto grouped_before = EditorPreviewControllerTestAccess::state(*controller);
  const auto grouped_neutral = render(path, grouped_before, 40, false);
  const auto child_index = [&] {
    const auto layers = EditorPreviewControllerTestAccess::state(*controller).layers;
    for (int index = 0; index < layers.size(); ++index) if (layers.at(index).toMap().value("id") == child.value("id")) return index;
    return -1;
  };
  for (const auto erase : {false, true}) {
    REQUIRE(controller->beginPhotoPaintStroke(child_index(), {{"erase", erase}, {"color", "#ffffff"},
        {"size", 12.0}, {"hardness", 1.0}, {"pressureSize", false}}), "Begin paint/erase inside a transformed parent group");
    const auto point = controller->mapCanvasCoordinate(output.x(), output.y(), false, QStringLiteral("paint:paint-proof"));
    controller->addPhotoPaintPoint(point.value("x").toDouble() * 128, point.value("y").toDouble() * 80, 1);
    REQUIRE(controller->finishPhotoPaintStroke(true) && wait_paint(*controller), "Commit paint/erase in transformed parent group");
    const auto result = render(path, EditorPreviewControllerTestAccess::state(*controller), erase ? 42 : 41, false);
    REQUIRE(grouped_neutral.success && result.success, "Render cropped parent-group pointer proof");
    const auto delta = result.preview_image.pixelColor(output_pixel).redF() - grouped_neutral.preview_image.pixelColor(output_pixel).redF();
    REQUIRE(erase ? std::abs(delta) < 0.04 : delta > 0.15,
        "Paint and eraser must hit the exact rendered pointer pixel through child+parent+crop+global transforms");
  }
  std::cout << "canvas_parent_group_pointer_parity cropped=1 global_geometry=1 parent_transform=1 child_transform=1 paint=1 erase=1\n";

  auto base_state = state; base_state.layers.clear();
  EditorPreviewControllerTestAccess::configure(*controller, path, base_state);
  REQUIRE(controller->setBaseImageLocked(false), "Create actual editable Base Image for transformed eraser proof");
  auto moved_base = EditorPreviewControllerTestAccess::state(*controller);
  auto base = moved_base.layers.last().toMap();
  base.insert("x", 5); base.insert("y", -3); base.insert("scale", 1.1); base.insert("rotation", 11.0);
  moved_base.layers[moved_base.layers.size() - 1] = base;
  EditorPreviewControllerTestAccess::configure(*controller, path, moved_base);
  const auto base_before = render(path, EditorPreviewControllerTestAccess::state(*controller), 43, false);
  REQUIRE(controller->beginPhotoPaintStroke(-1, {{"erase", true}, {"size", 12.0}, {"hardness", 1.0},
      {"pressureSize", false}}), "Begin eraser against a moved/rotated Base Image");
  const auto base_point = controller->mapCanvasCoordinate(output.x(), output.y(), false,
      QStringLiteral("paint:") + base.value("id").toString());
  controller->addPhotoPaintPoint(base_point.value("x").toDouble() * 128, base_point.value("y").toDouble() * 80, 1);
  REQUIRE(controller->finishPhotoPaintStroke(true) && wait_paint(*controller), "Commit transformed Base Image eraser");
  const auto base_after = render(path, EditorPreviewControllerTestAccess::state(*controller), 44, false);
  REQUIRE(base_before.success && base_after.success &&
      base_after.preview_image.pixelColor(output_pixel).alphaF() + 0.4 < base_before.preview_image.pixelColor(output_pixel).alphaF(),
      "Moved/rotated Base Image eraser must remove the exact source pixel seen under the pointer");
  std::cout << "canvas_base_eraser_pointer_parity cropped=1 global_geometry=1 moved_rotated_base=1 exact_source_pixel=1\n";

  // Cursor support follows the material's actual affected domain, including
  // the light brush's circular Gaussian halo.
  auto material_layer = child; material_layer.remove("parentId");
  material_layer.insert("rotation", 0.0); material_layer.insert("scale", 1.0);
  auto material_state = state; material_state.layers = {material_layer};
  material_state.transform_rotation = 0; material_state.transform_scale = 1;
  material_state.transform_skew_x = 0; material_state.transform_skew_y = 0;
  material_state.perspective_vertical = 0; material_state.perspective_horizontal = 0;
  EditorPreviewControllerTestAccess::configure(*controller, path, material_state);
  const auto light_contours = controller->paintCursorContours(0.5, 0.5, 10, 1, 0.4, 128, 80,
      QStringLiteral("paint-proof"), {{"textureMaterial", "light_paint"}, {"lightFalloff", 1.0}});
  const auto light_outer = light_contours.value("outer").toList();
  const auto light_core = light_contours.value("inner").toList();
  REQUIRE(light_outer.size() == 65 && light_core.size() == 65 && light_contours.value("innerVisible").toBool(),
      "Light cursor exposes a support envelope and separate circular core");
  double left_support = 1e9, right_support = -1e9, top_support = 1e9, bottom_support = -1e9;
  for (const auto& value : light_outer) {
    const auto point = value.toMap();
    const auto x = crop.x() + point.value("x").toDouble() * crop.width();
    const auto y = crop.y() + point.value("y").toDouble() * crop.height();
    left_support = std::min(left_support, x); right_support = std::max(right_support, x);
    top_support = std::min(top_support, y); bottom_support = std::max(bottom_support, y);
  }
  PhotoPixelStroke light_stroke;
  light_stroke.texture_material = QStringLiteral("light_paint"); light_stroke.light_falloff = 1;
  light_stroke.color = Qt::white; light_stroke.diameter = 10; light_stroke.pressure_size = false;
  REQUIRE(light_stroke.begin(material_layer), "Begin actual light material support fixture");
  light_stroke.point({64, 36}, 1); light_stroke.finish(); (void)light_stroke.commit();
  const auto light_pixels = light_stroke.surface.raster();
  int min_x = 128, max_x = -1, min_y = 80, max_y = -1;
  for (int y = 0; y < light_pixels.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(light_pixels.constScanLine(y));
    for (int x = 0; x < light_pixels.width(); ++x) if (row[x].alpha() > 0) {
      min_x = std::min(min_x, x); max_x = std::max(max_x, x);
      min_y = std::min(min_y, y); max_y = std::max(max_y, y);
    }
  }
  REQUIRE(max_x - min_x > 20 && std::abs(left_support - min_x) < 0.003 &&
      std::abs(right_support - (max_x + 1)) < 0.003 && std::abs(top_support - min_y) < 0.003 &&
      std::abs(bottom_support - (max_y + 1)) < 0.003,
      "Projected light support envelope matches the actual circular halo, not only its nominal core");
  const auto core_edge = light_core.first().toMap();
  REQUIRE(std::abs(crop.x() + core_edge.value("x").toDouble() * crop.width() - 69.0) < 0.003,
      "Light core retains the actual nominal brush radius");
  const auto fragment = controller->paintCursorContours(0.5, 0.5, 10, 0.5, 0.4, 128, 80,
      QStringLiteral("paint-proof"), {{"textureMaterial", "photo_fragment"}, {"tipWidth", 10}, {"tipHeight", 4},
      {"directionDegrees", 0.0}, {"tipScaleVariation", 0.0}, {"tipRotationVariation", 0.0}});
  const auto fragment_outer = fragment.value("outer").toList();
  REQUIRE(fragment_outer.size() == 65 && !fragment.value("innerVisible").toBool(), "Fragment cursor exposes the actual rectangular stamp footprint");
  const auto fragment_right = fragment_outer.at(0).toMap(), fragment_bottom = fragment_outer.at(16).toMap();
  REQUIRE(std::abs(crop.x() + fragment_right.value("x").toDouble() * crop.width() - 66.5) < 0.003 &&
      std::abs(crop.y() + fragment_bottom.value("y").toDouble() * crop.height() - 37.0) < 0.003,
      "Fragment cursor preserves tip aspect and tablet pressure instead of drawing a circle");
  const auto varied = controller->paintCursorContours(0.5, 0.5, 10, 0.5, 0.4, 128, 80,
      QStringLiteral("paint-proof"), {{"textureMaterial", "photo_fragment"}, {"tipWidth", 10}, {"tipHeight", 4},
      {"directionDegrees", 23.0}, {"tipScaleVariation", 0.5}, {"tipRotationVariation", 30.0}});
  REQUIRE(varied.value("outer").toList().size() == 65 && varied.value("innerVisible").toBool(),
      "Rotated/scaled fragment variation exposes a conservative support envelope and nominal stamp");
  PhotoPixelStroke stamp; stamp.texture_material = QStringLiteral("photo_fragment");
  stamp.tip_image = QImage(10, 4, QImage::Format_ARGB32); stamp.tip_image.fill(Qt::white);
  stamp.tip_image.setColorSpace(QColorSpace(QColorSpace::SRgb)); stamp.diameter = 10;
  stamp.tip_scale_variation = 0.5; stamp.tip_rotation_variation = 30; stamp.pressure_size = true;
  REQUIRE(stamp.begin(material_layer), "Begin actual varied photo-fragment stamp");
  stamp.point({64, 36}, 0.5); stamp.finish(); (void)stamp.commit();
  const auto stamp_pixels = stamp.surface.raster();
  int stamped_pixels = 0;
  for (int y = 0; y < stamp_pixels.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(stamp_pixels.constScanLine(y));
    for (int x = 0; x < stamp_pixels.width(); ++x) if (row[x].alpha() > 0) {
      ++stamped_pixels;
      REQUIRE(std::hypot(x + 0.5 - 64, y + 0.5 - 36) <= 2.5 * 1.5 * std::hypot(1.0, 0.4) + 0.01,
          "Actual seeded fragment coverage stays inside the declared pressure/variation support envelope");
    }
  }
  REQUIRE(stamped_pixels > 0, "Fragment material support proof paints actual pixels");
  REQUIRE(controller->beginPhotoPaintStroke(0, {{"erase", true}, {"textureMaterial", "light_paint"}, {"lightFalloff", 1.0}}) &&
      EditorPreviewControllerTestAccess::paint_material(*controller) == QStringLiteral("solid"),
      "The actual eraser remains circular even after selecting a halo material");
  controller->finishPhotoPaintStroke(false); REQUIRE(wait_paint(*controller), "Settle cancelled eraser support fixture");
  std::cout << "canvas_material_cursor_support_parity light_halo=1 light_corners=1 light_core=1 fragment_aspect=1 pressure=1 variation=1 eraser_circle=1\n";

  // Extending a historical crop-basis mask must preserve every old byte while
  // authoring unrestricted coverage in the SAME set with one undoable stroke.
  auto legacy = default_mask_definition(QStringLiteral("brush"), 1);
  legacy.insert("coordinateSpace", "source-area");
  legacy.insert("coordinateArea", QVariantMap{{"x", 24.0 / 127}, {"y", 18.0 / 79},
      {"width", 63.0 / 127}, {"height", 35.0 / 79}});
  legacy.insert("maskSetId", "legacy-coordinate-set");
  legacy.insert("maskSetName", "Legacy selection");
  legacy.insert("brushAutoMask", false);
  legacy.insert("brushProtectEdges", false);
  legacy.insert("brushHardness", 1.0);
  legacy.insert("feather", 0.0);
  legacy.insert("brushSmoothing", 0.0);
  legacy.insert("segmentationWidth", 64);
  legacy.insert("segmentationHeight", 36);
  auto legacy_alpha = QByteArray(64 * 36, '\0');
  legacy_alpha[18 * 64 + 32] = static_cast<char>(255);
  legacy.insert("segmentationData", legacy_alpha);
  auto legacy_state = state;
  legacy_state.mask_stack = {legacy};
  legacy_state.active_mask_index = 0;
  EditorPreviewControllerTestAccess::configure(*controller, path, legacy_state);
  const auto before_extension = EditorPreviewControllerTestAccess::state(*controller);
  controller->beginActiveBrushSourceStroke(QStringLiteral("subtract"));
  const auto extending = EditorPreviewControllerTestAccess::state(*controller);
  REQUIRE(extending.mask_stack.size() == 2 && extending.mask_stack.at(0) == before_extension.mask_stack.at(0),
      "Legacy crop extension preserves its original source, alpha, and history byte-for-byte");
  const auto companion = extending.mask_stack.at(extending.active_mask_index).toMap();
  REQUIRE(companion.value("coordinateSpace").toString() == QStringLiteral("source") &&
      companion.value("maskSetId") == before_extension.mask_stack.at(0).toMap().value("maskSetId") &&
      companion.value("operation").toString() == QStringLiteral("subtract") &&
      extending.layers == before_extension.layers,
      "Extension is a full-source subtract component in the same set without another adjustment layer");
  controller->addBrushSourcePointWithTabletEraser(105.0 / 127, 40.0 / 79, 1, 0, 0, true);
  controller->endActiveBrushStroke();
  QElapsedTimer settle; settle.start();
  while (!EditorPreviewControllerTestAccess::brush_idle(*controller) && settle.elapsed() < 10000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  REQUIRE(EditorPreviewControllerTestAccess::brush_idle(*controller), "Commit same-set source extension");
  const auto extended = EditorPreviewControllerTestAccess::state(*controller);
  if (extended.mask_stack.size() == 2 && extended.mask_stack.at(0) != before_extension.mask_stack.at(0)) {
    const auto original = before_extension.mask_stack.at(0).toMap();
    const auto actual = extended.mask_stack.at(0).toMap();
    auto keys = original.keys();
    for (const auto& key : actual.keys()) if (!keys.contains(key)) keys.append(key);
    keys.sort();
    for (const auto& key : keys) if (original.value(key) != actual.value(key)) {
      std::cerr << "legacy source changed key=" << key.toStdString()
                << " before_present=" << original.contains(key) << " after_present=" << actual.contains(key)
                << " before_type=" << (original.value(key).typeName() ? original.value(key).typeName() : "<missing>")
                << " after_type=" << (actual.value(key).typeName() ? actual.value(key).typeName() : "<missing>");
      if (original.value(key).metaType().id() != QMetaType::QByteArray &&
          original.value(key).metaType().id() != QMetaType::QVariantList &&
          original.value(key).metaType().id() != QMetaType::QVariantMap) {
        std::cerr << " before=" << original.value(key).toString().toStdString()
                  << " after=" << actual.value(key).toString().toStdString();
      }
      std::cerr << '\n';
    }
  }
  REQUIRE(extended.mask_stack.size() == 2 && extended.mask_stack.at(0) == before_extension.mask_stack.at(0),
      "Committing extension leaves original source bytes unchanged");
  const auto extension_mask = extended.mask_stack.at(extended.active_mask_index).toMap();
  const auto extension_points = extension_mask.value("brushPoints").toList();
  if (extension_points.isEmpty() || extension_points.first().toMap().value("erase").toBool() ||
      std::abs(extension_points.first().toMap().value("x").toDouble() - 105.0 / 127) >= 1e-9 ||
      extension_mask.value("operation").toString() != QStringLiteral("subtract")) {
    std::cerr << "companion diagnostic points=" << extension_points.size()
              << " marker=" << extension_mask.value("brushCompositeSource").toBool()
              << " operation=" << extension_mask.value("operation").toString().toStdString()
              << " space=" << extension_mask.value("coordinateSpace").toString().toStdString();
    if (!extension_points.isEmpty()) std::cerr << " first_x=" << extension_points.first().toMap().value("x").toDouble()
        << " first_erase=" << extension_points.first().toMap().value("erase").toBool();
    std::cerr << '\n';
  }
  REQUIRE(!extension_points.isEmpty() && !extension_points.first().toMap().value("erase").toBool() &&
      std::abs(extension_points.first().toMap().value("x").toDouble() - 105.0 / 127) < 1e-9 &&
      extension_mask.value("operation").toString() == QStringLiteral("subtract"),
      "Tablet eraser outside legacy basis authors positive full-source coverage in a subtract component");
  REQUIRE(controller->canUndo(), "Source extension creates an undoable gesture");
  controller->undo();
  const auto undone = EditorPreviewControllerTestAccess::state(*controller);
  REQUIRE(undone.mask_stack == before_extension.mask_stack && undone.layers == before_extension.layers,
      "One undo removes the extension and restores the original set and adjustment stack");
  std::cout << "canvas_legacy_basis_extension_parity same_set=1 original_unchanged=1 outside_basis=1 tablet_eraser=1 one_undo=1\n";

  QQuickWindow cursor_window;
  cursor_window.resize(840, 500);
  cursor_window.show();
  QQmlComponent cursor_component(&engine, QUrl::fromLocalFile(QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath("apps/desktop/qml/components/ProjectedBrushCursor.qml")));
  for (int cursor_case = 0; cursor_case < 3; ++cursor_case) {
    auto mask = default_mask_definition(QStringLiteral("brush"), 1);
    mask.insert("coordinateSpace", cursor_case == 2 ? "source-area" : "source");
    if (cursor_case == 1) mask.insert("toolRole", "retouch");
    if (cursor_case == 2) mask.insert("coordinateArea", legacy.value("coordinateArea"));
    mask.insert("brushSize", 0.06);
    mask.insert("brushHardness", 0.6);
    mask.insert("feather", 0.0);
    auto cursor_state = state;
    cursor_state.mask_stack = {mask};
    cursor_state.active_mask_index = 0;
    EditorPreviewControllerTestAccess::configure(*controller, path, cursor_state);
    auto object = std::unique_ptr<QObject>(cursor_component.createWithInitialProperties({
        {"controller", QVariant::fromValue<QObject*>(controller.get())}, {"width", 840.0}, {"height", 500.0},
        {"pointerX", 27 + output.x() * 768}, {"pointerY", 13 + output.y() * 384},
        {"imageX", 27.0}, {"imageY", 13.0}, {"imageWidth", 768.0}, {"imageHeight", 384.0},
        {"brushSize", 0.06}, {"hardness", 0.6}}));
    REQUIRE(object && !cursor_component.isError(), "Create production typed-controller projected brush cursor");
    auto* cursor = qobject_cast<QQuickItem*>(object.get());
    REQUIRE(cursor != nullptr, "Projected cursor creates a real Qt Quick item");
    cursor->setParentItem(cursor_window.contentItem());
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    REQUIRE(cursor->property("available").toBool(), "Production cursor recognizes the controller contour API");
    auto points = cursor->property("outer").toList();
    if (points.isEmpty()) points = cursor->property("outer").value<QJSValue>().toVariant().toList();
    REQUIRE(points.size() == 65, "Production cursor exposes 65 projected physical ring samples");
    const auto bounds = cursor->property("bounds").toRectF();
    REQUIRE(bounds.width() > 0 && bounds.height() > 0 && bounds.width() <= cursor->width() &&
        bounds.height() <= cursor->height(), "Projected cursor raster stays bounded by the visible viewport");
    const auto program = compile_geometry_stage_program(affine, crop.width(), crop.height());
    const auto center = geometry_sample_coordinates(program,
        static_cast<float>(output_pixel.x()), static_cast<float>(output_pixel.y()));
    const auto radius_pixels = 0.06 * (cursor_case == 2 ? 35 : 79);
    for (int index = 0; index <= 64; ++index) {
      const auto point = points.at(index).toMap();
      REQUIRE(point.value("valid").toBool(), "Physical ring sample remains visible for the test fixture");
      const auto sample = geometry_sample_coordinates(program,
          static_cast<float>(point.value("x").toDouble() * crop.width() - 0.5),
          static_cast<float>(point.value("y").toDouble() * crop.height() - 0.5));
      const auto angle = index * 2.0 * std::acos(-1.0) / 64;
      REQUIRE(std::abs(sample.x - center.x - radius_pixels * std::cos(angle)) < 0.004 &&
          std::abs(sample.y - center.y - radius_pixels * std::sin(angle)) < 0.004,
          "Visible cursor ring must map back to the exact physical circle used by source/retouch/legacy brush rasterization");
    }
    std::cout << "canvas_projected_cursor_parity case=" << cursor_case << " actual_qml=1 points=65 bounded=1 physical_circle=1\n";
  }
  cursor_window.close();
  REQUIRE(verify_desktop_retouch_crop(runtime) == 0, "Production desktop retouch full-source crop proof");
  REQUIRE(verify_native_source_aware_brush(runtime) == 0, "Native source-aware brush pixel-grid proof");
  std::cout << "canvas_pointer_coordinate_parity source_pixel=1 mask_raster=1 live_projection=1 engine_sampler=1 qml_delegate=1 dpr=1,2,2.5\n";
  return 0;
}
