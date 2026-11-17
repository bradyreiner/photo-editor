#include "app_bootstrap.h"
#include "canonical_native_preview_suffix.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_provider.h"
#include "editor_preview_texture_item.h"
#include "editor_preview_texture.h"
#include "photo_fragment_brush_preset_store.h"
#include "photo_layer_adjustment_recipe.h"
#include "photo_paint_surface.h"
#include "observability.h"
#if defined(__APPLE__)
#include "native_window_test_input.h"
#endif
#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QThread>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QCryptographicHash>
#include <QBuffer>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sys/resource.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
using namespace photo_editor::desktop;
namespace photo_editor::desktop {
#if defined(PHOTO_PAINT_HAS_NATIVE_TEXTURE)
std::unique_ptr<EditorPreviewTexture> create_metal_editor_preview_texture();
#endif
struct EditorPreviewControllerTestAccess {
  inline static double last_photo_layer_snapshot_ms = 0.0;
  inline static QJsonArray photo_paint_worker_samples;
  static bool enableNativeTexture(EditorPreviewController& c) {
#if defined(PHOTO_PAINT_HAS_NATIVE_TEXTURE)
    c.display_preview_texture_ = create_metal_editor_preview_texture();
    return c.display_preview_texture_ && c.display_preview_texture_->available();
#else
    (void)c;
    return false;
#endif
  }
  static EditState state(const EditorPreviewController& c) { return c.currentState(); }
  static void replaceState(EditorPreviewController& c, const EditState& state) {
    c.applyState(state);
    c.clearHistoryForAsset(c.asset_id_);
  }
  static void restorePaintTiles(EditorPreviewController& c, const QVariantMap& tiles) {
    auto state = c.currentState();
    if (state.layers.isEmpty()) return;
    auto layer = state.layers[0].toMap();
    layer.insert(QStringLiteral("paintTiles"), tiles);
    state.layers[0] = layer;
    c.applyState(state);
    c.clearHistoryForAsset(c.asset_id_);
  }
  static bool settled(const EditorPreviewController& c) {
    return !c.photo_paint_worker_running_ && !c.photo_paint_preview_pending_ &&
        !c.processing() && !c.fullRenderRunning();
  }
  static bool workerRunning(const EditorPreviewController& c) {
    return c.photo_paint_worker_running_;
  }
  static qint64 historySnapshotBytes(const EditorPreviewController& c) {
    return QJsonDocument(QJsonArray::fromVariantList(c.editHistorySnapshots()))
        .toJson(QJsonDocument::Compact).size();
  }
  static QJsonObject timings(const EditorPreviewController& c) {
    const auto r=c.interactive_render_watcher_.result();
    return {{"backend",r.backend},{"uploaded_pixels",r.texture_upload_pixels},{"render_ms",r.total_elapsed_ms},{"decode_ms",r.decode_elapsed_ms},
      {"edit_ms",r.edit_elapsed_ms},{"encode_ms",r.encode_elapsed_ms},
      {"pipeline_ms",r.adjustment_pipeline_elapsed_ms},{"queue_ms",r.queue_wait_ms},
      {"output_ms",r.output_stage_elapsed_ms},{"max_edge",r.max_edge},
      {"output_width",r.output_width},{"output_height",r.output_height},
      {"paint_region_fallback_reason",r.paint_region_fallback_reason}};
  }
  static int paintTileCount(const QVariantMap& payload) {
    return static_cast<int>(payload.value(QStringLiteral("tiles")).toMap().size());
  }
  static int draftTileCount(const QVariantMap& draft_tiles) {
    // Draft coverage is stored as a direct tile-key map; committed paintTiles
    // wraps that same map in its versioned payload envelope.
    return static_cast<int>(draft_tiles.size());
  }
  static double cropAspectRatio(QString preset) {
    preset = preset.trimmed().toLower();
    preset.replace(QLatin1Char(':'), QLatin1Char('x'));
    preset.remove(QLatin1Char(' '));
    if (preset == QStringLiteral("square") || preset == QStringLiteral("1x1")) return 1.0;
    if (preset == QStringLiteral("a3") || preset == QStringLiteral("a4") ||
        preset == QStringLiteral("a5")) return 210.0 / 297.0;
    if (preset == QStringLiteral("a3landscape") || preset == QStringLiteral("a4landscape") ||
        preset == QStringLiteral("a5landscape")) return 297.0 / 210.0;
    const auto parts = preset.split(QLatin1Char('x'), Qt::SkipEmptyParts);
    if (parts.size() != 2) return 0.0;
    auto width_label = parts.at(0);
    auto height_label = parts.at(1);
    for (const auto& unit : {QStringLiteral("inches"), QStringLiteral("inch"), QStringLiteral("in"),
                             QStringLiteral("cm"), QStringLiteral("mm")}) {
      width_label.remove(unit);
      height_label.remove(unit);
    }
    bool width_ok = false;
    bool height_ok = false;
    const auto width = width_label.toDouble(&width_ok);
    const auto height = height_label.toDouble(&height_ok);
    return width_ok && height_ok && width > 0.0 && height > 0.0 ? width / height : 0.0;
  }
  static QSize expectedPreviewSize(QSize source_size, const RenderRequest& request) {
    if (!source_size.isValid()) return {};
    if (request.max_edge > 0 && std::max(source_size.width(), source_size.height()) > request.max_edge)
      source_size = source_size.scaled(request.max_edge, request.max_edge, Qt::KeepAspectRatio);
    if (request.bypass_crop) return source_size;

    const auto& state = request.state;
    QRect crop;
    if (state.crop_rect_width > 0.0 && state.crop_rect_height > 0.0) {
      const auto width = qBound(0.0, state.crop_rect_width, 1.0);
      const auto height = qBound(0.0, state.crop_rect_height, 1.0);
      const auto x = std::min(qBound(0.0, state.crop_rect_x, 1.0), std::max(0.0, 1.0 - width));
      const auto y = std::min(qBound(0.0, state.crop_rect_y, 1.0), std::max(0.0, 1.0 - height));
      const auto px = qBound(0, qRound(x * source_size.width()), source_size.width() - 1);
      const auto py = qBound(0, qRound(y * source_size.height()), source_size.height() - 1);
      crop = QRect(px, py,
          qBound(1, qRound(width * source_size.width()), source_size.width() - px),
          qBound(1, qRound(height * source_size.height()), source_size.height() - py));
    } else {
      auto crop_width = source_size.width();
      auto crop_height = source_size.height();
      auto ratio = cropAspectRatio(state.crop_preset);
      if (ratio <= 0.0 && state.crop_preset.trimmed().compare(QStringLiteral("Original"), Qt::CaseInsensitive) == 0)
        ratio = double(source_size.width()) / double(source_size.height());
      if (ratio > 0.0) {
        const auto source_ratio = double(source_size.width()) / double(source_size.height());
        if (source_ratio > ratio) crop_width = qRound(double(source_size.height()) * ratio);
        else crop_height = qRound(double(source_size.width()) / ratio);
      }
      crop_width = qMax(1, qRound(double(crop_width) / state.crop_zoom));
      crop_height = qMax(1, qRound(double(crop_height) / state.crop_zoom));
      const auto x = qMax(0, (source_size.width() - crop_width) / 2);
      const auto y = qMax(0, (source_size.height() - crop_height) / 2);
      const auto nx = double(x) / double(source_size.width());
      const auto ny = double(y) / double(source_size.height());
      const auto nw = double(crop_width) / double(source_size.width());
      const auto nh = double(crop_height) / double(source_size.height());
      const auto px = qBound(0, qRound(nx * source_size.width()), source_size.width() - 1);
      const auto py = qBound(0, qRound(ny * source_size.height()), source_size.height() - 1);
      crop = QRect(px, py,
          qBound(1, qRound(nw * source_size.width()), source_size.width() - px),
          qBound(1, qRound(nh * source_size.height()), source_size.height() - py));
    }
    return crop.size();
  }
  static QJsonObject imageAdmissionInfo(const QImage& image) {
    const auto color_space = image.colorSpace();
    return {{"is_null", image.isNull()}, {"width", image.width()}, {"height", image.height()},
        {"format", static_cast<int>(image.format())}, {"is_premultiplied", image.format() == QImage::Format_ARGB32_Premultiplied ||
            image.format() == QImage::Format_RGBA8888_Premultiplied || image.format() == QImage::Format_RGBA64_Premultiplied ||
            image.format() == QImage::Format_RGBA16FPx4_Premultiplied},
        {"backend_tag", image.text(QStringLiteral("looksy-preview-output-backend"))},
        {"color_space_valid", color_space.isValid()}, {"color_space_description", color_space.description()}};
  }
  static QJsonObject paintRegionAdmissionSnapshot(EditorPreviewController& c, QSize source_size) {
    const auto request = c.currentRenderRequest(false);
    std::shared_ptr<PhotoPaintSession> job;
    if (!c.photo_paint_jobs_.empty()) job = c.photo_paint_jobs_.front();
    else if (c.photo_paint_running_job_) job = c.photo_paint_running_job_;
    else job = c.photo_paint_input_;
    const auto expected_size = expectedPreviewSize(source_size, request);
    const auto job_base = job ? job->preview_base : QImage{};

    QJsonArray layers;
    QJsonArray regional_layer_failures;
    QRect region;
    int pixel_layers = 0;
    int paint_tile_layers = 0;
    int draft_tile_layers = 0;
    int draft_tiles_total = 0;
    for (const auto& value : request.state.layers) {
      const auto layer = value.toMap();
      const auto kind = layer.value(QStringLiteral("kind")).toString();
      const auto paint_tiles = layer.value(QStringLiteral("paintTiles")).toMap();
      const auto draft_tiles = layer.value(QStringLiteral("paintDraftTiles")).toMap();
      const auto draft_count = draftTileCount(draft_tiles);
      const auto adjustment_recipe = photo_layer_adjustment_recipe(layer);
      QJsonArray failures;
      if (kind != QStringLiteral("pixel")) failures.append(QStringLiteral("kind_not_pixel"));
      if (!layer.contains(QStringLiteral("paintTiles"))) failures.append(QStringLiteral("missing_paintTiles"));
      if (layer.value(QStringLiteral("x")).toInt() != 0 || layer.value(QStringLiteral("y")).toInt() != 0)
        failures.append(QStringLiteral("offset_transform"));
      if (layer.value(QStringLiteral("scale"), 1.0).toDouble() != 1.0 ||
          layer.value(QStringLiteral("rotation")).toDouble() != 0.0) failures.append(QStringLiteral("scale_or_rotation"));
      if (layer.value(QStringLiteral("selectionScoped")).toBool()) failures.append(QStringLiteral("selection_scoped"));
      if (layer.value(QStringLiteral("clippingMask")).toBool()) failures.append(QStringLiteral("clipping_mask"));
      if (!layer.value(QStringLiteral("maskData")).toByteArray().isEmpty()) failures.append(QStringLiteral("mask_data"));
      if (!layer.value(QStringLiteral("effects")).toList().isEmpty()) failures.append(QStringLiteral("effects"));
      if (!adjustment_recipe.isEmpty()) failures.append(QStringLiteral("adjustment_recipe"));
      if (!failures.isEmpty()) regional_layer_failures.append(QJsonObject{{"layer_id", layer.value(QStringLiteral("id")).toString()},
          {"failures", failures}});
      if (kind == QStringLiteral("pixel")) ++pixel_layers;
      if (layer.contains(QStringLiteral("paintTiles"))) ++paint_tile_layers;
      if (draft_count > 0) ++draft_tile_layers;
      draft_tiles_total += draft_count;
      if (layer.value(QStringLiteral("pixelWidth")).toInt() > 0 && layer.value(QStringLiteral("pixelHeight")).toInt() > 0) {
        const auto sx = double(request.paint_preview_base.width()) / layer.value(QStringLiteral("pixelWidth")).toInt();
        const auto sy = double(request.paint_preview_base.height()) / layer.value(QStringLiteral("pixelHeight")).toInt();
        for (auto it = draft_tiles.cbegin(); it != draft_tiles.cend(); ++it) {
          const auto parts = it.key().split(QLatin1Char(','));
          if (parts.size() != 2) continue;
          bool x_ok = false;
          bool y_ok = false;
          const auto tile_x = parts.at(0).toInt(&x_ok);
          const auto tile_y = parts.at(1).toInt(&y_ok);
          if (!x_ok || !y_ok) continue;
          region |= QRectF(tile_x * kPhotoPaintTileEdge * sx, tile_y * kPhotoPaintTileEdge * sy,
              kPhotoPaintTileEdge * sx, kPhotoPaintTileEdge * sy).adjusted(-2, -2, 2, 2).toAlignedRect();
        }
      }
      layers.append(QJsonObject{{"id", layer.value(QStringLiteral("id")).toString()},
          {"kind", kind}, {"base_image_edit", layer.value(QStringLiteral("baseImageEdit")).toBool()},
          {"copy_base_image", layer.value(QStringLiteral("copyBaseImage")).toBool()},
          {"has_paintTiles", layer.contains(QStringLiteral("paintTiles"))},
          {"paint_tile_count", paintTileCount(paint_tiles)}, {"draft_tile_count", draft_count},
          {"pixel_width", layer.value(QStringLiteral("pixelWidth")).toInt()},
          {"pixel_height", layer.value(QStringLiteral("pixelHeight")).toInt()},
          {"x", layer.value(QStringLiteral("x")).toInt()}, {"y", layer.value(QStringLiteral("y")).toInt()},
          {"scale", layer.value(QStringLiteral("scale"), 1.0).toDouble()},
          {"rotation", layer.value(QStringLiteral("rotation")).toDouble()},
          {"selection_scoped", layer.value(QStringLiteral("selectionScoped")).toBool()},
          {"clipping_mask", layer.value(QStringLiteral("clippingMask")).toBool()},
          {"mask_bytes", layer.value(QStringLiteral("maskData")).toByteArray().size()},
          {"effect_count", layer.value(QStringLiteral("effects")).toList().size()},
          {"adjustment_recipe_keys", QJsonArray::fromStringList(adjustment_recipe.keys())},
          {"regional_gate_failures", failures}});
    }
    region &= QRect(QPoint(), request.paint_preview_base.size());

    const auto backend = request.paint_preview_base.text(QStringLiteral("looksy-preview-output-backend"));
    const bool native_backend = backend == QStringLiteral("canonical-metal-srgb8") ||
        backend == QStringLiteral("canonical-metal-device8");
    const auto suffix = QFileInfo(request.file_path).suffix().toLower();
    const bool raw_candidate = QStringList{QStringLiteral("3fr"), QStringLiteral("ari"), QStringLiteral("arw"),
        QStringLiteral("cr2"), QStringLiteral("cr3"), QStringLiteral("dng"), QStringLiteral("nef"),
        QStringLiteral("nrw"), QStringLiteral("orf"), QStringLiteral("pef"), QStringLiteral("raf"),
        QStringLiteral("rw2"), QStringLiteral("srw")}.contains(suffix);
    const bool native_suffix_backend_available = canonical_native_suffix_backend_available();
    auto neutral_state = request.state;
    neutral_state.layers.clear();
    neutral_state.exposure = 0.0;
    neutral_state.temperature = 0.0;
    neutral_state.tint = 0.0;

    QJsonArray request_rejections;
    if (request.paint_preview_base.isNull()) request_rejections.append(QStringLiteral("paint_preview_base_null"));
    if (!request.use_in_memory_preview) request_rejections.append(QStringLiteral("preview_not_in_memory"));
    if (request.full_resolution) request_rejections.append(QStringLiteral("full_resolution"));
    if (request.compare_mode) request_rejections.append(QStringLiteral("compare_mode"));
    if (request.overlay_only) request_rejections.append(QStringLiteral("overlay_only"));
    if (raw_candidate) request_rejections.append(QStringLiteral("raw_source_candidate"));
    if (request.state.soft_proof_enabled) request_rejections.append(QStringLiteral("soft_proof_enabled"));
    if (!request.state.mask_stack.isEmpty()) request_rejections.append(QStringLiteral("mask_stack_nonempty"));
    if (request.state.sharpening != 0.0 || request.state.luminance_nr != 0.0 || request.state.color_nr != 0.0 ||
        request.state.perspective_vertical != 0.0 || request.state.perspective_horizontal != 0.0 ||
        request.state.transform_rotation != 0.0 || request.state.transform_scale != 1.0 ||
        request.state.transform_skew_x != 0.0 || request.state.transform_skew_y != 0.0 ||
        request.state.liquify_strength != 0.0 || request.state.content_aware_crop_fill)
      request_rejections.append(QStringLiteral("non_neutral_geometry_or_detail"));
    if (!expected_size.isValid()) request_rejections.append(QStringLiteral("expected_size_invalid"));
    if (!request.paint_preview_base.isNull() && expected_size != request.paint_preview_base.size())
      request_rejections.append(QStringLiteral("preview_size_mismatch"));
    if (native_backend && request.require_cpu_preview_image)
      request_rejections.append(QStringLiteral("native_backend_requires_cpu_preview"));
    if (backend == QStringLiteral("canonical-metal-srgb8") && !request.allow_native_srgb_surface)
      request_rejections.append(QStringLiteral("native_srgb_surface_unavailable"));
    if (backend == QStringLiteral("canonical-metal-device8") && !request.allow_native_device_surface)
      request_rejections.append(QStringLiteral("native_device_surface_unavailable"));
    if (native_backend && !request.require_cpu_preview_image && !native_suffix_backend_available)
      request_rejections.append(QStringLiteral("native_suffix_backend_unavailable"));
    if (!regional_layer_failures.isEmpty()) request_rejections.append(QStringLiteral("layer_guard_failed"));
    if (region.isEmpty()) request_rejections.append(QStringLiteral("draft_region_empty"));
    const auto total_pixels = qint64(request.paint_preview_base.width()) * request.paint_preview_base.height();
    const auto region_pixels = qint64(region.width()) * region.height();
    if (total_pixels > 0 && region_pixels > total_pixels / 2) request_rejections.append(QStringLiteral("draft_region_over_half"));

    const auto output_base = imageAdmissionInfo(request.paint_preview_base);
    const auto queued_base = imageAdmissionInfo(job_base);
    bool captured_state_matches = false;
    if (job) {
      auto current = c.currentState();
      auto captured = job->selection_state;
      for (auto* state : {&current, &captured})
        for (int i = 0; i < state->layers.size(); ++i)
          state->layers[i] = photo_paint_target_identity(state->layers[i].toMap());
      captured_state_matches = current == captured;
    }
    const auto& state = request.state;
    const QJsonObject global_inputs{{"exposure", state.exposure}, {"temperature", state.temperature},
        {"tint", state.tint}, {"sharpening", state.sharpening}, {"luminance_nr", state.luminance_nr},
        {"color_nr", state.color_nr}, {"perspective_vertical", state.perspective_vertical},
        {"perspective_horizontal", state.perspective_horizontal}, {"transform_rotation", state.transform_rotation},
        {"transform_scale", state.transform_scale}, {"transform_skew_x", state.transform_skew_x},
        {"transform_skew_y", state.transform_skew_y}, {"liquify_strength", state.liquify_strength},
        {"content_aware_crop_fill", state.content_aware_crop_fill}, {"crop_preset", state.crop_preset},
        {"crop_zoom", state.crop_zoom}, {"crop_rect_x", state.crop_rect_x}, {"crop_rect_y", state.crop_rect_y},
        {"crop_rect_width", state.crop_rect_width}, {"crop_rect_height", state.crop_rect_height}};
    return {{"job_found", bool(job)}, {"job_asset_id", job ? job->asset_id : QString{}},
        {"job_layer_id", job ? job->layer_id : QString{}}, {"job_is_base_image_erase", job && job->base_image_erase},
        {"job_has_draft_tiles", job && !job->draft_tiles.isEmpty()},
        {"job_draft_tile_count", job ? draftTileCount(job->draft_tiles) : 0},
        {"job_preview_base", queued_base}, {"request_preview_base", output_base},
        {"captured_state_matches_current", captured_state_matches},
        {"request", QJsonObject{{"max_edge", request.max_edge}, {"use_in_memory_preview", request.use_in_memory_preview},
            {"full_resolution", request.full_resolution}, {"compare_mode", request.compare_mode},
            {"overlay_only", request.overlay_only}, {"bypass_crop", request.bypass_crop},
            {"soft_proof_enabled", request.state.soft_proof_enabled}, {"mask_stack_count", request.state.mask_stack.size()},
            {"source_suffix", suffix}, {"raw_source_candidate", raw_candidate},
            {"neutral_state_equals_edit_state_default_after_layer_and_allowed_controls_removed", neutral_state == EditState{}},
            {"compare_adjustment_group_count", request.compare_adjustment_groups.size()},
            {"draft_interactive", request.draft_interactive}, {"require_cpu_preview_image", request.require_cpu_preview_image},
            {"require_cpu_display_image", request.require_cpu_display_image},
            {"allow_native_srgb_surface", request.allow_native_srgb_surface},
            {"allow_native_device_surface", request.allow_native_device_surface},
            {"source_size", QJsonArray{source_size.width(), source_size.height()}},
            {"paint_preview_base_size", QSize(request.paint_preview_base.size()).isValid()
                ? QJsonArray{request.paint_preview_base.width(), request.paint_preview_base.height()} : QJsonArray{}},
            {"expected_size", expected_size.isValid() ? QJsonArray{expected_size.width(), expected_size.height()} : QJsonArray{}},
            {"size_matches", request.paint_preview_base.size() == expected_size}, {"backend", backend},
            {"native_backend", native_backend}, {"native_suffix_backend_available", native_suffix_backend_available},
            {"global_gate_inputs", global_inputs}}},
        {"layer_count", request.state.layers.size()}, {"pixel_layer_count", pixel_layers},
        {"paint_tile_layer_count", paint_tile_layers}, {"draft_tile_layer_count", draft_tile_layers},
        {"draft_tile_count", draft_tiles_total}, {"layers", layers},
        {"regional_layer_failures", regional_layer_failures},
        {"draft_region", QJsonArray{region.x(), region.y(), region.width(), region.height()}},
        {"draft_region_pixels", region_pixels}, {"base_pixels", total_pixels},
        {"draft_region_fraction", total_pixels > 0 ? double(region_pixels) / double(total_pixels) : 0.0},
        {"request_rejections", request_rejections}};
  }
  static void collectPhotoPaintWorkerTimings(EditorPreviewController& c) {
    QObject::disconnect(&c.photo_paint_watcher_, nullptr, &c, nullptr);
    QObject::connect(&c.photo_paint_watcher_, &QFutureWatcher<PhotoPaintWorkResult>::finished,
        &c, [&c] {
          const auto r = c.photo_paint_watcher_.result();
          photo_paint_worker_samples.append(QJsonObject{
              {"initialize_ms", r.initialize_ms}, {"raster_ms", r.raster_ms},
              {"finalize_ms", r.finalize_ms}, {"commit_ms", r.commit_ms}});
        });
    QObject::connect(&c.photo_paint_watcher_, &QFutureWatcher<PhotoPaintWorkResult>::finished,
        &c, &EditorPreviewController::completePhotoPaintWork);
  }
  static void resetPhotoPaintWorkerTimings() { photo_paint_worker_samples = {}; }
  static QJsonObject workerTimings() {
    QJsonObject totals{{"task_count", photo_paint_worker_samples.size()}};
    for (const auto* stage : {"initialize_ms", "raster_ms", "finalize_ms", "commit_ms"}) {
      double total = 0.0;
      double maximum = 0.0;
      for (const auto& sample : photo_paint_worker_samples) {
        const auto elapsed = sample.toObject().value(QLatin1String(stage)).toDouble();
        total += elapsed;
        maximum = std::max(maximum, elapsed);
      }
      auto maximum_name = QString::fromLatin1(stage);
      maximum_name.chop(3);
      maximum_name += QStringLiteral("_max_ms");
      totals.insert(QLatin1String(stage), total);
      totals.insert(maximum_name, maximum);
    }
    totals.insert(QStringLiteral("samples"), photo_paint_worker_samples);
    return totals;
  }
  static QJsonObject layerStackTimings(const EditorPreviewController& c) {
    return QJsonObject::fromVariantMap(c.last_photo_layer_stack_performance_);
  }
  static QString firstLayerThumbnailSource(EditorPreviewController& c) {
    QElapsedTimer timer; timer.start();
    const auto rows = c.photoLayers();
    last_photo_layer_snapshot_ms = timer.nsecsElapsed() / 1e6;
    return rows.isEmpty() ? QString{} : rows.constFirst().toMap().value("thumbnailSource").toString();
  }
  static bool layerThumbnailsSettled(EditorPreviewController& c) {
    const auto rows = c.photoLayers();
    return !rows.isEmpty() && !c.thumbnail_cache_session_.photoLayerDirty() &&
        !c.thumbnail_refresh_coordinator_.pending() &&
        !c.thumbnail_refresh_coordinator_.workerRunning();
  }
  static QJsonObject thumbnailStatus(EditorPreviewController& c) {
    const auto rows = c.photoLayers();
    const auto row = rows.isEmpty() ? QVariantMap{} : rows.constFirst().toMap();
    return {{"dirty", c.thumbnail_cache_session_.photoLayerDirty()},
        {"pending", c.thumbnail_refresh_coordinator_.pending()},
        {"worker", c.thumbnail_refresh_coordinator_.workerRunning()},
        {"source", row.value("thumbnailCacheSource").toString()},
        {"queuePending", row.value("thumbnailQueuePending").toBool()},
        {"failure", row.value("thumbnailFailureStage").toString()}};
  }
};
}
static qint64 residentRssBytes() {
#if defined(__APPLE__)
  mach_task_basic_info_data_t info{};
  mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
  if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
          reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
    return static_cast<qint64>(info.resident_size);
  }
#endif
  return -1;
}
static bool spin(const std::function<bool()>& done, int timeout = 45000) {
  QElapsedTimer t; t.start();
  do { QCoreApplication::processEvents(QEventLoop::AllEvents,5); if(done()) return true; QThread::msleep(1); } while(t.elapsed()<timeout);
  return false;
}
static int committedPaintTileCount(const EditorPreviewController& controller) {
  int count = 0;
  const auto state = EditorPreviewControllerTestAccess::state(controller);
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    count += layer.value("paintTiles").toMap().value("tiles").toMap().size();
  }
  return count;
}
static qint64 directoryBytes(const QString& path) {
  qint64 total = 0;
  QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
      QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}
static double percentile95(QVector<double> values) {
  if (values.isEmpty()) return 0.0;
  std::sort(values.begin(), values.end());
  return values[static_cast<qsizetype>(std::ceil(values.size() * 0.95)) - 1];
}
static qsizetype differingPixels(const QImage& first, const QImage& second) {
  if (first.size() != second.size() || first.isNull() || second.isNull()) return -1;
  const auto a = first.convertToFormat(QImage::Format_ARGB32);
  const auto b = second.convertToFormat(QImage::Format_ARGB32);
  qsizetype count = 0;
  for (int y = 0; y < a.height(); ++y) {
    const auto* a_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const auto* b_row = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) count += a_row[x] != b_row[x];
  }
  return count;
}
static QString imageDifferenceSummary(const QImage& first, const QImage& second) {
  if (first.size() != second.size() || first.isNull() || second.isNull()) return QStringLiteral("size-or-null-mismatch");
  const auto a = first.convertToFormat(QImage::Format_ARGB32);
  const auto b = second.convertToFormat(QImage::Format_ARGB32);
  qsizetype exact = 0, over_one = 0, over_four = 0, over_sixteen = 0;
  int left = a.width(), top = a.height(), right = -1, bottom = -1, max_delta = 0;
  for (int y = 0; y < a.height(); ++y) {
    const auto* a_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const auto* b_row = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const auto p = a_row[x], q = b_row[x];
      const auto delta = std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
          std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
      if (delta == 0) continue;
      ++exact;
      if (delta > 1) ++over_one;
      if (delta > 4) ++over_four;
      if (delta > 16) ++over_sixteen;
      max_delta = std::max(max_delta, delta);
      left = std::min(left, x); top = std::min(top, y);
      right = std::max(right, x); bottom = std::max(bottom, y);
    }
  }
  return QStringLiteral("changed=%1,gt1=%2,gt4=%3,gt16=%4,max=%5,bounds=%6,%7-%8,%9")
      .arg(exact).arg(over_one).arg(over_four).arg(over_sixteen).arg(max_delta)
      .arg(left).arg(top).arg(right).arg(bottom);
}
static bool sameImageWithinOneLevel(const QImage& first, const QImage& second) {
  if (first.size() != second.size() || first.isNull() || second.isNull()) return false;
  const auto a = first.convertToFormat(QImage::Format_ARGB32);
  const auto b = second.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < a.height(); ++y) {
    const auto* a_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const auto* b_row = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const auto p = a_row[x], q = b_row[x];
      if (std::abs(qRed(p) - qRed(q)) > 1 || std::abs(qGreen(p) - qGreen(q)) > 1 ||
          std::abs(qBlue(p) - qBlue(q)) > 1 || std::abs(qAlpha(p) - qAlpha(q)) > 1) return false;
    }
  }
  return true;
}
static bool exactlySameImage(const QImage& first, const QImage& second) {
  return first == second && first.colorSpace() == second.colorSpace();
}
static bool verifyLayerTipCache() {
  QImage source(12, 8, QImage::Format_ARGB32_Premultiplied);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(Qt::transparent);
  source.setPixelColor(3, 2, QColor(220, 70, 30, 90));
  source.setPixelColor(5, 4, QColor(30, 180, 230, 210));
  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "PNG")) return false;
  QVariantMap layer{{QStringLiteral("id"), QStringLiteral("fragment-tip")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), source.width()},
      {QStringLiteral("pixelHeight"), source.height()},
      {QStringLiteral("pixelData"), encoded},
      {QStringLiteral("paintTiles"), empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgb))}};
  PhotoFragmentLayerTipCache cache;
  const auto expected = PhotoFragmentBrushPresetStore::cropToVisiblePixels(
      rasterize_photo_paint_layer(layer));
  const auto first = cache.prepare(QStringLiteral("asset-a"), layer);
  if (first.isNull() || first.size() != QSize(3, 3) ||
      !exactlySameImage(first, expected) ||
      cache.retainedByteCount() > PhotoFragmentLayerTipCache::kMaxRetainedBytes) return false;
  const auto first_storage = first.constBits();
  const auto hit = cache.prepare(QStringLiteral("asset-a"), layer);
  if (hit.constBits() != first_storage) return false;

  auto changed_source = source;
  changed_source.setPixelColor(8, 3, QColor(70, 210, 90, 180));
  QByteArray changed_encoded;
  QBuffer changed_buffer(&changed_encoded);
  if (!changed_buffer.open(QIODevice::WriteOnly) || !changed_source.save(&changed_buffer, "PNG")) return false;
  auto changed_layer = layer;
  changed_layer.insert(QStringLiteral("pixelData"), changed_encoded);
  const auto changed = cache.prepare(QStringLiteral("asset-a"), changed_layer);
  if (changed.isNull() || changed.size() != QSize(6, 3) ||
      changed.constBits() == first_storage) return false;

  // Restoring the exact immutable source after an edit (the undo/redo shape)
  // must recompute it safely, then reuse that restored image on the next call.
  const auto restored = cache.prepare(QStringLiteral("asset-a"), layer);
  if (restored.isNull() || restored.size() != QSize(3, 3) ||
      restored.constBits() == changed.constBits()) return false;
  const auto restored_hit = cache.prepare(QStringLiteral("asset-a"), layer);
  if (restored_hit.constBits() != restored.constBits()) return false;

  const auto other_asset = cache.prepare(QStringLiteral("asset-b"), layer);
  return !other_asset.isNull() && other_asset.constBits() != restored.constBits() &&
      cache.cachedKey().value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-b") &&
      exactlySameImage(other_asset, expected);
}

static int runLayerTipPreparationBenchmark() {
  const int edge = qBound(64, qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_LAYER_TIP_EDGE") > 0
      ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_LAYER_TIP_EDGE") : 1024, 4096);
  QImage source(edge, edge, QImage::Format_ARGB32_Premultiplied);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  source.fill(Qt::transparent);
  {
    QPainter painter(&source);
    painter.fillRect(QRect(edge / 4, edge / 4, edge / 2, edge / 2), QColor(80, 150, 220, 190));
  }
  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly) || !source.save(&buffer, "PNG")) return 31;
  QVariantMap layer{{QStringLiteral("id"), QStringLiteral("fragment-tip-benchmark")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("pixelWidth"), edge}, {QStringLiteral("pixelHeight"), edge},
      {QStringLiteral("pixelData"), encoded},
      {QStringLiteral("paintTiles"), empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgb))}};
  PhotoFragmentLayerTipCache cache;
  QElapsedTimer timer;
  timer.start();
  const auto cold = cache.prepare(QStringLiteral("benchmark-asset"), layer);
  const auto cold_ms = timer.nsecsElapsed() / 1e6;
  if (cold.isNull()) return 32;
  const auto storage = cold.constBits();
  timer.restart();
  const auto warm = cache.prepare(QStringLiteral("benchmark-asset"), layer);
  const auto warm_ms = timer.nsecsElapsed() / 1e6;
  const QJsonObject result{{"scenario", QStringLiteral("photo_fragment_layer_tip_preparation")},
      {"input_width", edge}, {"input_height", edge},
      {"tip_width", cold.width()}, {"tip_height", cold.height()},
      {"cold_prepare_ms", cold_ms}, {"warm_prepare_ms", warm_ms},
      {"warm_cache_hit", warm.constBits() == storage},
      {"retained_bytes", static_cast<qint64>(cache.retainedByteCount())},
      {"cache_cap_bytes", static_cast<qint64>(PhotoFragmentLayerTipCache::kMaxRetainedBytes)}};
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << "\n";
  return 0;
}
int main(int argc,char** argv) {
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE","1");
  QGuiApplication app(argc,argv);
  if (qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_LAYER_TIP_PREPARATION"))
    return runLayerTipPreparationBenchmark();
  if (qEnvironmentVariableIsSet("PHOTO_PAINT_VERIFY_LAYER_TIP_CACHE") && !verifyLayerTipCache()) {
    std::cerr << "Photo Fragment layer-tip cache correctness check failed.\n";
    return 30;
  }
  if (qEnvironmentVariableIsSet("PHOTO_PAINT_VERIFY_LAYER_TIP_CACHE")) return 0;
  const bool brush_size_override_set = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_BRUSH_SIZE");
  const int brush_size = brush_size_override_set
      ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_BRUSH_SIZE")
      : 96;
  if (brush_size < 1 || brush_size > 4096) {
    std::cerr << "PHOTO_PAINT_BENCH_BRUSH_SIZE must be an integer from 1 through 4096.\n";
    return 43;
  }
  QTemporaryDir dir; qputenv("PHOTO_EDITOR_APP_DATA_DIR",dir.path().toUtf8());
  AppBootstrap::ensureRuntimeDirectories();
  observability::initialize();
  const int width = qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_WIDTH") > 0 ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_WIDTH") : 6000;
  QImage source(width,width*2/3,QImage::Format_RGB32); source.fill(QColor("#304050"));
  const auto photo_path=qEnvironmentVariable("PHOTO_PAINT_BENCH_SOURCE");
  if(!photo_path.isEmpty()) {
    QImage photo(photo_path); if(photo.isNull()) return 14;
    photo=photo.scaled(source.size(),Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    source=photo.copy((photo.width()-width)/2,(photo.height()-width*2/3)/2,width,width*2/3);
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const bool layer_tip_bench = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_LAYER_TIP");
  const bool fragment_bench = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_PHOTO_FRAGMENT");
  constexpr auto fragment_preset_name = "Performance Photo Fragment 16MP";
  const int fragment_tip_edge = fragment_bench
      ? qBound(32, qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_FRAGMENT_EDGE") > 0
          ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_FRAGMENT_EDGE") : 4000, 4000)
      : 0;
  const int layer_tip_edge = layer_tip_bench
      ? qBound(64, qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_LAYER_TIP_EDGE") > 0
          ? qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_LAYER_TIP_EDGE") : 1024, 4096)
      : 0;
  const bool cold_fragment_cache = fragment_bench &&
      qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_COLD_FRAGMENT_CACHE");
  if (fragment_bench) {
    const auto scaled_tip_source = source.scaled(fragment_tip_edge, fragment_tip_edge,
        Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const auto fragment_tip = scaled_tip_source.copy(
        (scaled_tip_source.width() - fragment_tip_edge) / 2,
        (scaled_tip_source.height() - fragment_tip_edge) / 2,
        fragment_tip_edge, fragment_tip_edge);
    QString preset_error;
    if (!PhotoFragmentBrushPresetStore::save(QString::fromLatin1(fragment_preset_name),
            fragment_tip, 0, 0, true, &preset_error)) {
      std::cerr << "Could not create the performance Photo Fragment: " << preset_error.toStdString() << "\n";
      return 20;
    }
    if (cold_fragment_cache) PhotoFragmentBrushPresetStore::clearDecodedImageCache();
  }
  const auto path=dir.filePath("large.png"); if(!source.save(path)) return 1;
  source = {};
  QQmlEngine engine;
  EditorPreviewController c; c.setPreviewTargetEdge(1024);
  const bool native_texture = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_NATIVE_TEXTURE");
  const bool base_eraser_bench = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_BASE_ERASER");
  const bool layer_eraser_bench = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_LAYER_ERASER");
  const bool eraser_bench = base_eraser_bench || layer_eraser_bench;
  if ((base_eraser_bench && layer_eraser_bench) ||
      (eraser_bench && (fragment_bench || layer_tip_bench))) {
    std::cerr << "Choose one paint operation; eraser runs cannot combine with Photo Fragment benchmarks.\n";
    return 35;
  }
  if (native_texture && !EditorPreviewControllerTestAccess::enableNativeTexture(c)) {
    std::cerr << "Native preview Metal texture could not be created.\n";
    return 17;
  }
  const bool memory_preview=!qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_FILE_PREVIEW");
  if(memory_preview) {
    auto* provider=new EditorPreviewImageProvider;
    engine.addImageProvider("editor-preview",provider); c.setImageProvider(provider);
  }
  c.loadAsset("paint-benchmark",path,{},{},{},{},{});
  if(!spin([&]{return !c.displayPreviewSource().isEmpty() && !c.processing() && !c.fullRenderRunning();})) return 2;
  engine.rootContext()->setContextProperty("paintController",&c);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nItem { width: 900; height: 600; Image { objectName: \"image\"; anchors.fill: parent; source: paintController.displayPreviewSource; cache: false } }",QUrl());
  std::unique_ptr<QObject> root(component.create()); if(!root) return 3;
  QQuickWindow window; window.resize(900,600); qobject_cast<QQuickItem*>(root.get())->setParentItem(window.contentItem());
  if (native_texture) {
    if (!c.displayPreviewNativeAvailable()) {
      std::cerr << "Native preview texture is unavailable before window exposure (handle="
          << c.displayPreviewNativeHandle() << ", size="
          << c.displayPreviewNativeSize().width() << "x"
          << c.displayPreviewNativeSize().height() << ", platform="
          << QGuiApplication::platformName().toStdString() << ").\n";
      return 17;
    }
    root->findChild<QObject*>("image")->setProperty("visible", false);
    auto* item = new EditorPreviewTextureItem(window.contentItem());
    item->setSize(QSizeF(900,600));
    item->setController(&c);
  }
  const bool skip_native_activation = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_SKIP_NATIVE_ACTIVATION");
  window.show();
  if (!skip_native_activation) window.requestActivate();
  if(!spin([&]{return window.isExposed();})) return 4;
#if defined(__APPLE__)
  QVariantMap native_window_sample_before;
  if (native_texture) {
    if (!skip_native_activation) activate_native_test_window(&window);
    native_window_sample_before = native_window_foreground_sample(&window);
  }
#endif
  QElapsedTimer timer; timer.start(); if(!c.addPaintLayer()) return 5;
  const auto create_ms=timer.nsecsElapsed()/1e6;
  if(!spin([&]{return !c.processing() && !c.fullRenderRunning();})) return 6;
  if (base_eraser_bench) {
    if (!c.setBaseImageLocked(false)) return 36;
    auto base_index = -1;
    const auto layers = EditorPreviewControllerTestAccess::state(c).layers;
    for (int index = 0; index < static_cast<int>(layers.size()); ++index)
      if (layers.at(index).toMap().value(QStringLiteral("baseImageEdit")).toBool()) base_index = index;
    if (base_index < 0 || (base_index > 0 && !c.movePhotoLayer(base_index, -base_index))) return 37;
    if (!spin([&] { return !c.processing() && !c.fullRenderRunning(); })) return 38;
  }
  const auto baseline_state = EditorPreviewControllerTestAccess::state(c);
  const auto baseline_tiles = baseline_state.layers.isEmpty() ? QVariantMap{} :
      baseline_state.layers[0].toMap().value(QStringLiteral("paintTiles")).toMap();
  if (baseline_tiles.isEmpty()) return 24;
  QString layer_tip_id;
  if (layer_tip_bench) {
    const int edge = layer_tip_edge;
    QImage tip(edge, edge, QImage::Format_ARGB32_Premultiplied);
    tip.setColorSpace(QColorSpace(QColorSpace::SRgb));
    tip.fill(Qt::transparent);
    {
      QPainter painter(&tip);
      painter.fillRect(QRect(edge / 4, edge / 4, edge / 2, edge / 2), QColor(80, 150, 220, 190));
    }
    QByteArray tip_png;
    QBuffer tip_buffer(&tip_png);
    if (!tip_buffer.open(QIODevice::WriteOnly) || !tip.save(&tip_buffer, "PNG")) return 29;
    auto tip_state = EditorPreviewControllerTestAccess::state(c);
    if (tip_state.layers.isEmpty()) return 28;
    auto tip_layer = tip_state.layers.first().toMap();
    layer_tip_id = QStringLiteral("performance-fragment-layer-tip");
    tip_layer.insert(QStringLiteral("id"), layer_tip_id);
    tip_layer.insert(QStringLiteral("name"), QStringLiteral("Performance Fragment Tip"));
    tip_layer.insert(QStringLiteral("pixelWidth"), edge);
    tip_layer.insert(QStringLiteral("pixelHeight"), edge);
    tip_layer.insert(QStringLiteral("pixelData"), tip_png);
    tip_layer.insert(QStringLiteral("paintTiles"), empty_photo_paint_tiles(QColorSpace(QColorSpace::SRgb)));
    tip_layer.remove(QStringLiteral("paintDraftTiles"));
    tip_state.layers.append(tip_layer);
    EditorPreviewControllerTestAccess::replaceState(c, tip_state);
    if (!spin([&]{return !c.processing() && !c.fullRenderRunning();})) return 27;
  }
  const int requested_trials = qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_TRIALS");
  const bool cancel_stress = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_CANCEL_STRESS");
  if (cancel_stress && eraser_bench) {
    std::cerr << "Cancellation stress currently measures paint strokes only.\n";
    return 39;
  }
  const int trial_count = cancel_stress
      ? qBound(1, requested_trials > 0 ? requested_trials : 100, 600)
      : requested_trials > 0 ? qBound(1, requested_trials, 600) : base_eraser_bench ? 1 : 3;
  const int cancel_warmup_cycles = cancel_stress
      ? (qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_CANCEL_WARMUP_CYCLES")
          ? qBound(0, qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_CANCEL_WARMUP_CYCLES"), 200) : 50)
      : 0;
  const int total_trial_count = trial_count + cancel_warmup_cycles;
  if (cancel_stress && !native_texture) {
    std::cerr << "Cancellation stress requires the native preview texture.\n";
    return 25;
  }
  const bool spread_rows = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_SPREAD_ROWS");
  const bool repeat_identical = qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_REPEAT_IDENTICAL") &&
      !base_eraser_bench;
  const bool measure_layer_thumbnails = spread_rows && !fragment_bench;
  QJsonArray rows;
  int frames=0; QObject::connect(&window,&QQuickWindow::frameSwapped,&app,[&]{++frames;});
  EditorPreviewControllerTestAccess::collectPhotoPaintWorkerTimings(c);
  const auto resident_rss_before_trials = residentRssBytes();
  const auto baseline_recipe = serialize_edit_recipe_json(baseline_state);
  const auto baseline_history_count = c.editHistoryEntries().size();
  const auto baseline_history_snapshot_bytes = EditorPreviewControllerTestAccess::historySnapshotBytes(c);
  const auto baseline_can_undo = c.canUndo();
  const auto baseline_can_redo = c.canRedo();
  const auto baseline_frame = cancel_stress ? window.grabWindow() : QImage{};
  if (cancel_stress && baseline_frame.isNull()) return 26;
  QVector<double> cancel_visible_samples;
  QVector<double> cancel_texture_revision_samples;
  QVector<double> cancel_restored_capture_samples;
  QVector<qint64> cancel_rss_samples;
  int restore_screenshot_samples = 0;
  qint64 cancel_warm_rss = cancel_warmup_cycles == 0 ? resident_rss_before_trials : 0;
  qint64 cancel_peak_after_warm_rss = 0;
  for(int trial=0;trial<total_trial_count;++trial) {
    const int grid_columns = qCeil(std::sqrt(static_cast<double>(trial_count)));
    const int grid_rows = (trial_count + grid_columns - 1) / grid_columns;
    const int grid_column = trial % grid_columns;
    const int grid_row = trial / grid_columns;
    const int row_y = repeat_identical
        ? qRound((width * 2.0 / 3.0 - 1.0) * 0.5)
        : spread_rows && fragment_bench
        ? qRound((width * 2.0 / 3.0 - 1.0) *
            (0.08 + 0.84 * (grid_row + 0.5) / grid_rows))
        : spread_rows
            ? qRound((width * 2.0 / 3.0 - 1.0) * (0.12 + 0.74 * (trial + 0.5) / trial_count))
            : qRound(width * 0.2 + trial * 150.0);
    const int spread_x = repeat_identical
        ? qRound((width - 1.0) * 0.5)
        : spread_rows && fragment_bench
        ? qRound((width - 1.0) * (0.08 + 0.84 * (grid_column + 0.5) / grid_columns))
        : qRound(width * 0.25);
    if (cancel_stress) {
      QVariantMap cancel_settings{{"color", "#ff5020"},
          {"size", qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_CANCEL_BRUSH_SIZE") > 0
              ? qBound(32, qEnvironmentVariableIntValue("PHOTO_PAINT_BENCH_CANCEL_BRUSH_SIZE"), 4096) : 512},
          {"pressureSize", false}};
      if (!c.beginPhotoPaintStroke(0, cancel_settings)) return 27;
      const int mark_x = qRound(width * 0.5);
      const int mark_y = qRound(width * 2.0 / 3.0 * 0.5);
      const auto before_mark_revision = c.displayPreviewNativeRevision();
      const int before_mark_frame = frames;
      c.addPhotoPaintPoint(mark_x, mark_y);
      if (!spin([&] { return c.displayPreviewNativeRevision() != before_mark_revision && frames > before_mark_frame; }, 5000)) {
        std::cerr << "Cancellation stress stroke did not become visible at cycle " << trial << "\n";
        return 28;
      }
      // Read back every canceled frame: requesting a frame alone can be
      // coalesced when the test scene has no other dirty QQuickItems.
      const bool sample_visible_pixels = true;
      QImage painted_frame;
      if (sample_visible_pixels) {
        painted_frame = window.grabWindow();
        if (painted_frame.isNull() || painted_frame == baseline_frame) {
          std::cerr << "Cancellation stress failed to establish a visible mark at cycle " << trial << "\n";
          return 29;
        }
      }
      for (int point = 1; point <= 128; ++point) {
        const auto t = static_cast<double>(point) / 128.0;
        c.addPhotoPaintPoint(width * (0.1 + 0.8 * t), mark_y + ((point % 2) ? 4 : -4));
      }
      if (!spin([&] { return EditorPreviewControllerTestAccess::workerRunning(c); }, 2000)) return 30;
      QElapsedTimer cancellation_timer;
      cancellation_timer.start();
      const auto cancel_native_revision_before = c.displayPreviewNativeRevision();
      c.cancelPhotoPaint();
      const auto cancel_call_ms = cancellation_timer.nsecsElapsed() / 1e6;
      window.requestUpdate();
      bool restored_frame_visible = false;
      bool cancel_frame_swapped = false;
      double cancel_to_first_frame_ms = -1.0;
      double cancel_to_texture_revision_ms = -1.0;
      QImage canceled_frame;
      const auto visual_deadline_ms = sample_visible_pixels ? 5000 : 1000;
      // Measure the first restored native frame independently of synchronous
      // screenshot readback. grabWindow() can block on Metal readback, so it
      // must not be part of the cancellation-response clock.
      while (cancellation_timer.elapsed() < visual_deadline_ms &&
          (!cancel_frame_swapped || cancel_to_texture_revision_ms < 0.0)) {
        const auto previous_frame = frames;
        window.requestUpdate();
        if (!spin([&] { return frames > previous_frame; }, 250)) continue;
        cancel_frame_swapped = true;
        if (cancel_to_first_frame_ms < 0.0)
          cancel_to_first_frame_ms = cancellation_timer.nsecsElapsed() / 1e6;
        if (cancel_to_texture_revision_ms < 0.0 &&
            c.displayPreviewNativeRevision() != cancel_native_revision_before)
          cancel_to_texture_revision_ms = cancellation_timer.nsecsElapsed() / 1e6;
      }
      const auto cancel_response_ms = cancellation_timer.nsecsElapsed() / 1e6;
      QElapsedTimer restored_capture_timer;
      restored_capture_timer.start();
      while (sample_visible_pixels && restored_capture_timer.elapsed() < visual_deadline_ms &&
          !restored_frame_visible) {
        if (cancel_frame_swapped) {
          canceled_frame = window.grabWindow();
          restored_frame_visible = sameImageWithinOneLevel(baseline_frame, canceled_frame);
        }
        if (restored_frame_visible) break;
        const auto previous_frame = frames;
        window.requestUpdate();
        if (!spin([&] { return frames > previous_frame; }, 250)) continue;
        cancel_frame_swapped = true;
      }
      const auto cancel_restored_capture_ms = restored_capture_timer.nsecsElapsed() / 1e6;
      if (!spin([&] { return !c.photoPaintBusy() && EditorPreviewControllerTestAccess::settled(c); }, 30000)) {
        std::cerr << "Canceled paint worker failed to settle at cycle " << trial << "\n";
        return 31;
      }
      const auto cycle_recipe = serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(c));
      const bool document_unchanged = cycle_recipe == baseline_recipe;
      const bool history_unchanged = c.editHistoryEntries().size() == baseline_history_count &&
          EditorPreviewControllerTestAccess::historySnapshotBytes(c) == baseline_history_snapshot_bytes &&
          c.canUndo() == baseline_can_undo && c.canRedo() == baseline_can_redo;
      const bool restore_check_ok = cancel_to_texture_revision_ms >= 0.0 &&
          (!sample_visible_pixels || restored_frame_visible);
      if (!document_unchanged || !history_unchanged || !cancel_frame_swapped || !restore_check_ok) {
        const auto diagnostics_prefix = qEnvironmentVariable("PHOTO_PAINT_CANCEL_DIAGNOSTICS_PREFIX");
        if (!diagnostics_prefix.isEmpty()) {
          const QFileInfo prefix_info(diagnostics_prefix);
          QDir().mkpath(prefix_info.absolutePath());
          baseline_frame.save(diagnostics_prefix + "-baseline.png");
          painted_frame.save(diagnostics_prefix + "-painted.png");
          canceled_frame.save(diagnostics_prefix + "-canceled.png");
        }
        std::cerr << "Cancellation gate failed at cycle " << trial
            << " (document_unchanged=" << document_unchanged
            << ", history_unchanged=" << history_unchanged
            << ", restored_frame_visible=" << restored_frame_visible
            << ", changed_pixels=" << differingPixels(baseline_frame, canceled_frame)
            << ", diff=" << imageDifferenceSummary(baseline_frame, canceled_frame).toStdString()
            << ", first_frame_ms=" << cancel_to_first_frame_ms
            << ", response_ms=" << cancel_response_ms
            << ", capture_ms=" << cancel_restored_capture_ms << ")\n";
        return 32;
      }
      const auto cycle_rss = residentRssBytes();
      const bool warmup_cycle = trial < cancel_warmup_cycles;
      if (cancel_warmup_cycles > 0 && trial + 1 == cancel_warmup_cycles) cancel_warm_rss = cycle_rss;
      if (!warmup_cycle) {
        cancel_rss_samples.push_back(cycle_rss);
        cancel_visible_samples.push_back(cancel_to_texture_revision_ms);
        cancel_texture_revision_samples.push_back(cancel_to_texture_revision_ms);
        cancel_restored_capture_samples.push_back(cancel_restored_capture_ms);
        if (sample_visible_pixels) ++restore_screenshot_samples;
        cancel_peak_after_warm_rss = std::max(cancel_peak_after_warm_rss, cycle_rss);
      }
      rows.append(QJsonObject{{"cycle", trial + 1},
          {"phase", warmup_cycle ? QStringLiteral("warmup") : QStringLiteral("measured")},
          {"restore_screenshot_sampled", sample_visible_pixels},
          {"restore_screenshot_matched", !sample_visible_pixels || restored_frame_visible},
          {"cancel_call_ms", cancel_call_ms},
          {"cancel_to_first_frame_ms", cancel_to_first_frame_ms},
          {"cancel_to_native_texture_revision_ms", cancel_to_texture_revision_ms},
          {"cancel_to_first_visible_ms", cancel_response_ms},
          {"cancel_to_restored_capture_ms", cancel_restored_capture_ms},
          {"cancel_frame_swapped", cancel_frame_swapped},
          {"restored_frame_visible", restored_frame_visible},
          {"document_unchanged", document_unchanged}, {"history_unchanged", history_unchanged},
          {"worker_settled", true}, {"resident_rss_bytes", cycle_rss}});
      continue;
    }
    double eraser_seed_ms = 0.0;
    if (layer_eraser_bench) {
      QElapsedTimer seed_timer;
      seed_timer.start();
      const QVariantMap seed_settings{{"color", "#ff5020"}, {"size", brush_size}, {"pressureSize", false}};
      if (!c.beginPhotoPaintStroke(0, seed_settings)) return 40;
      c.addPhotoPaintPoint(width * 0.25, row_y);
      c.addPhotoPaintPoint(width * 0.65, row_y);
      if (!c.finishPhotoPaintStroke() || !spin([&] {
            return !c.photoPaintBusy() && EditorPreviewControllerTestAccess::settled(c);
          })) return 41;
      eraser_seed_ms = seed_timer.nsecsElapsed() / 1e6;
    }
    EditorPreviewControllerTestAccess::resetPhotoPaintWorkerTimings();
    const int tiles_before = committedPaintTileCount(c);
    const auto thumbnail_before = measure_layer_thumbnails
        ? EditorPreviewControllerTestAccess::firstLayerThumbnailSource(c) : QString{};
    const auto photo_layer_snapshot_before_ms = measure_layer_thumbnails
        ? EditorPreviewControllerTestAccess::last_photo_layer_snapshot_ms : 0.0;
    const auto before=c.displayPreviewSource();
    const auto before_frame=frames;
    const auto before_native_revision=c.displayPreviewNativeRevision();
    const bool verify_visible_pixels = native_texture && !fragment_bench && !layer_tip_bench &&
        qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_VISIBLE_PIXELS");
    const auto baseline_frame = verify_visible_pixels ? window.grabWindow() : QImage{};
    const auto midpoint_pixel = [&](const QImage& image) {
      return image.pixelColor(qBound(0, qRound(image.width() * 0.45), image.width() - 1),
          qBound(0, qRound(image.height() * row_y / (width * 2.0 / 3.0)), image.height() - 1));
    };
    if (verify_visible_pixels && baseline_frame.isNull()) return 33;
    const auto baseline_midpoint = verify_visible_pixels ? midpoint_pixel(baseline_frame) : QColor{};
    timer.restart();
    const bool use_fragment = layer_tip_bench || (fragment_bench && trial % 2 == 1);
    QVariantMap stroke_settings{{"color",trial%2?"#20e0a0":"#ff5020"},
        {"size",brush_size},{"pressureSize",false}};
    if (eraser_bench) stroke_settings.insert(QStringLiteral("erase"), true);
    if (use_fragment) {
      stroke_settings.insert("textureMaterial", QStringLiteral("photo_fragment"));
      if (layer_tip_bench) stroke_settings.insert("tipLayerId", layer_tip_id);
      else stroke_settings.insert("tipPresetName", QString::fromLatin1(fragment_preset_name));
      stroke_settings.insert("spacing", 0.15);
    }
    const bool cold_fragment_first_use = cold_fragment_cache && use_fragment && trial == 1;
    if(!c.beginPhotoPaintStroke(base_eraser_bench ? -1 : 0,stroke_settings)) return 7;
    const auto begin_ms=timer.nsecsElapsed()/1e6;
    QElapsedTimer input; input.start();
    c.addPhotoPaintPoint(spread_rows && fragment_bench ? spread_x : width * 0.25, row_y);
    c.addPhotoPaintPoint(spread_rows && fragment_bench ? spread_x + 1 : width * 0.65, row_y);
    const auto input_ms=input.nsecsElapsed()/1e6;
    if(!spin([&]{return (native_texture ? c.displayPreviewNativeRevision()!=before_native_revision :
        c.displayPreviewSource()!=before && root->findChild<QObject*>("image")->property("status").toInt()==1) && frames>before_frame;})) {
      std::cerr << "Paint preview did not present trial " << trial << " (native_revision="
          << c.displayPreviewNativeRevision() << ", previous_revision=" << before_native_revision
          << ", frame_delta=" << (frames - before_frame) << ", busy="
          << c.property("photoPaintBusy").toBool() << ")\n";
      return 8;
    }
    if (!fragment_bench) {
      const auto published_frame=frames; window.requestUpdate();
      if(!spin([&]{return frames>published_frame;})) return 9;
    }
    const auto presented_ms=timer.nsecsElapsed()/1e6;
    double visible_stroke_ms = -1.0;
    if (verify_visible_pixels) {
      if (!spin([&] {
            const auto frame = window.grabWindow();
            if (frame.isNull()) return false;
            const auto current = midpoint_pixel(frame);
            return std::abs(current.red() - baseline_midpoint.red()) +
                   std::abs(current.green() - baseline_midpoint.green()) +
                   std::abs(current.blue() - baseline_midpoint.blue()) > 32;
          })) {
        std::cerr << "The native window did not display fresh paint pixels at the stroke midpoint.\n";
        return 33;
      }
      visible_stroke_ms = timer.nsecsElapsed()/1e6;
    }
    const auto stages=EditorPreviewControllerTestAccess::timings(c);
    const auto paint_region_admission = (eraser_bench ||
        qEnvironmentVariableIsSet("PHOTO_PAINT_BENCH_REGION_ADMISSION"))
        ? EditorPreviewControllerTestAccess::paintRegionAdmissionSnapshot(c, QSize(width, width * 2 / 3))
        : QJsonObject{};
    if(!spin([&]{return EditorPreviewControllerTestAccess::settled(c);})) return 15;
    const auto complete_frame=frames; window.requestUpdate();
    bool completion_frame_swapped = false;
    bool completion_capture_valid = false;
    if (spread_rows) {
      completion_frame_swapped = spin([&]{return frames>complete_frame;});
      if (fragment_bench && !completion_frame_swapped) {
        completion_capture_valid = false;
      } else if (fragment_bench) {
        completion_capture_valid = true;
      } else {
        if (!completion_frame_swapped) return 16;
        const auto completion_capture = window.grabWindow();
        completion_capture_valid = !completion_capture.isNull();
        if (!completion_capture_valid) return 16;
      }
    } else {
      completion_frame_swapped = spin([&]{return frames>complete_frame;});
      if (!completion_frame_swapped) return 16;
      completion_capture_valid = true;
    }
    const auto complete_presented_ms=timer.nsecsElapsed()/1e6;
    input.restart(); if(!c.finishPhotoPaintStroke()) return 10;
    const auto release_ms=input.nsecsElapsed()/1e6;
    if(!spin([&]{return !c.property("photoPaintBusy").toBool();})) return 11;
    const auto commit_ms=input.nsecsElapsed()/1e6;
    double thumbnail_refresh_ms = 0.0;
    double photo_layer_snapshot_after_ms = 0.0;
    auto thumbnail_after = thumbnail_before;
    if (measure_layer_thumbnails) {
      QElapsedTimer thumbnail_timer; thumbnail_timer.start();
      if(!spin([&]{return EditorPreviewControllerTestAccess::layerThumbnailsSettled(c);})) {
        std::cerr << "Layer thumbnail refresh did not publish: "
            << QJsonDocument(EditorPreviewControllerTestAccess::thumbnailStatus(c)).toJson().toStdString()
            << "\n";
        return 19;
      }
      thumbnail_refresh_ms=thumbnail_timer.nsecsElapsed()/1e6;
      thumbnail_after = EditorPreviewControllerTestAccess::firstLayerThumbnailSource(c);
      photo_layer_snapshot_after_ms = EditorPreviewControllerTestAccess::last_photo_layer_snapshot_ms;
    }
    const auto recipe=serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(c));
    bool base_source_copy_preserved = false;
    int base_coverage_tiles = 0;
    if (base_eraser_bench) {
      for (const auto& value : EditorPreviewControllerTestAccess::state(c).layers) {
        const auto layer = value.toMap();
        if (!layer.value(QStringLiteral("baseImageEdit")).toBool()) continue;
        base_source_copy_preserved = layer.value(QStringLiteral("copyBaseImage")).toBool();
        const auto paint_tiles = layer.value(QStringLiteral("paintTiles")).toMap();
        base_coverage_tiles = static_cast<int>(paint_tiles.value(QStringLiteral("tiles")).toMap().size());
        break;
      }
      const auto columns = (width + kPhotoPaintTileEdge - 1) / kPhotoPaintTileEdge;
      const auto rows_count = (width * 2 / 3 + kPhotoPaintTileEdge - 1) / kPhotoPaintTileEdge;
      if (!base_source_copy_preserved || base_coverage_tiles <= 0 ||
          base_coverage_tiles >= columns * rows_count) return 42;
    }
    auto row = QJsonObject{{"operation", base_eraser_bench ? QStringLiteral("base_eraser") :
            layer_eraser_bench ? QStringLiteral("paint_layer_eraser") : QStringLiteral("paint")},
        {"brush_size_px", brush_size},
        {"eraser_seed_ms", eraser_seed_ms},
        {"base_source_copy_preserved", base_source_copy_preserved},
        {"base_coverage_tiles", base_coverage_tiles},
        {"material", use_fragment ? QStringLiteral("photo_fragment") : QStringLiteral("solid")},
        {"tip_width", use_fragment ? (layer_tip_bench ? layer_tip_edge : fragment_tip_edge) : 0},
        {"tip_height", use_fragment ? (layer_tip_bench ? layer_tip_edge : fragment_tip_edge) : 0},
        {"stroke_complete_presented_ms",complete_presented_ms},{"completion_frame_swapped",completion_frame_swapped},
        {"completion_capture_valid",completion_capture_valid},{"stages",stages},
        {"paint_region_admission", paint_region_admission},
        {"worker_stages",EditorPreviewControllerTestAccess::workerTimings()},
        {"layer_stack_stages",EditorPreviewControllerTestAccess::layerStackTimings(c)},
        {"begin_ms",begin_ms},{"input_batch_ms",input_ms},{"first_presented_ms",presented_ms},
        {"visible_midstroke_ms",visible_stroke_ms},{"visible_pixels_verified",verify_visible_pixels},
        {"cold_fragment_first_use",cold_fragment_first_use},
        {"layer_tip_source", layer_tip_bench},
        {"release_ms",release_ms},{"commit_complete_ms",commit_ms},{"committed_tiles_before",tiles_before},
        {"committed_tiles_after",committedPaintTileCount(c)},{"recipe_bytes",recipe.toUtf8().size()},
        {"resident_rss_bytes",residentRssBytes()}};
    if (measure_layer_thumbnails) {
      row.insert("thumbnail_refresh_ms", thumbnail_refresh_ms);
      row.insert("thumbnail_updated", thumbnail_after != thumbnail_before);
      row.insert("photo_layer_snapshot_before_ms", photo_layer_snapshot_before_ms);
      row.insert("photo_layer_snapshot_after_ms", photo_layer_snapshot_after_ms);
    }
    rows.append(row);
    if(!spin([&]{return !c.processing() && !c.fullRenderRunning();})) return 12;
    if (fragment_bench && trial + 1 < trial_count) {
      EditorPreviewControllerTestAccess::restorePaintTiles(c, baseline_tiles);
      if (!spin([&]{return !c.processing() && !c.fullRenderRunning() &&
          !c.property("photoPaintBusy").toBool() && committedPaintTileCount(c) == 0;})) return 22;
      const auto undo_frame = frames;
      window.requestUpdate();
      if (!spin([&]{return frames > undo_frame;}, 3000)) return 23;
    }
  }
  struct rusage usage{}; getrusage(RUSAGE_SELF,&usage);
  const auto* screen = QGuiApplication::primaryScreen();
  const auto resident_rss_after_trials = residentRssBytes();
  const auto history_snapshot_bytes_after_trials = EditorPreviewControllerTestAccess::historySnapshotBytes(c);
  double export_ms = -1.0;
  QString layer_tip_export_sha256;
  QSize layer_tip_export_size;
  if (cancel_stress || layer_tip_bench) {
    QElapsedTimer export_timer;
    export_timer.start();
    const auto export_path = dir.filePath(layer_tip_bench ? "layer-tip-export.png" : "cancel-stress-export.png");
    if (!c.exportFlattenedPhoto(export_path) ||
        !spin([&] { return !c.photoExportBusy(); }, 120000) || !c.photoExportError().isEmpty()) {
      std::cerr << "Cancellation stress baseline export failed: " << c.photoExportError().toStdString() << "\n";
      return 33;
    }
    export_ms = export_timer.nsecsElapsed() / 1e6;
    if (layer_tip_bench) {
      QImage decoded(export_path);
      if (decoded.isNull()) return 34;
      decoded = decoded.convertToFormat(QImage::Format_RGBA64);
      layer_tip_export_size = decoded.size();
      layer_tip_export_sha256 = QString::fromLatin1(QCryptographicHash::hash(
          QByteArray(reinterpret_cast<const char*>(decoded.constBits()), decoded.sizeInBytes()),
          QCryptographicHash::Sha256).toHex());
    }
  }
  const auto scratch_bytes = directoryBytes(dir.path());
  const auto rss_warm_growth = cancel_warm_rss > 0
      ? cancel_peak_after_warm_rss - cancel_warm_rss : 0;
  QJsonObject result{{"native_texture_item",native_texture},{"in_memory_preview",memory_preview},
      {"brush_size_px",brush_size},{"brush_size_override_set",brush_size_override_set},
      {"photographic_fixture",!photo_path.isEmpty()},{"spread_rows",spread_rows},
      {"base_eraser_bench",base_eraser_bench},{"layer_eraser_bench",layer_eraser_bench},
      {"repeat_identical_strokes",repeat_identical},
      {"photo_fragment_bench",fragment_bench || layer_tip_bench},{"photo_fragment_layer_tip_bench",layer_tip_bench},{"photo_fragment_tip_width",fragment_bench ? fragment_tip_edge : layer_tip_edge},
      {"photo_fragment_tip_height",fragment_bench ? fragment_tip_edge : layer_tip_edge},
      {"cold_fragment_cache_requested",cold_fragment_cache},
      {"os",QSysInfo::prettyProductName()},{"architecture",QSysInfo::currentCpuArchitecture()},
      {"qt_version",QString::fromLatin1(qVersion())},{"platform",QGuiApplication::platformName()},
      {"screen_name",screen ? screen->name() : QString{}},
      {"screen_device_pixel_ratio",screen ? screen->devicePixelRatio() : 0.0},
      {"screen_geometry_width",screen ? screen->geometry().width() : 0},
      {"screen_geometry_height",screen ? screen->geometry().height() : 0},
      {"requested_trial_count",trial_count},{"document_width",width},{"document_height",width*2/3},
      {"create_layer_ms",create_ms},{"peak_rss_bytes",double(usage.ru_maxrss)},
      {"resident_rss_before_trials_bytes",resident_rss_before_trials},
      {"resident_rss_after_trials_bytes",resident_rss_after_trials},{"trials",rows}};
#if defined(__APPLE__)
  if (native_texture) {
    bool visible_pixels_verified = !rows.isEmpty();
    for (const auto& trial : rows)
      visible_pixels_verified = visible_pixels_verified &&
          trial.toObject().value(QStringLiteral("visible_pixels_verified")).toBool();
    const auto native_window_sample_after = native_window_foreground_sample(&window);
    result.insert(QStringLiteral("native_window_before"), QJsonObject::fromVariantMap(native_window_sample_before));
    result.insert(QStringLiteral("native_window_after"), QJsonObject::fromVariantMap(
        native_window_sample_after));
    result.insert(QStringLiteral("native_window_qualified"),
        native_window_sample_after.value(QStringLiteral("qualified")).toBool());
    result.insert(QStringLiteral("visible_pixels_and_window_qualified"),
        visible_pixels_verified && native_window_sample_after.value(QStringLiteral("qualified")).toBool());
  }
#endif
  if (layer_tip_bench) {
    result.insert("layer_tip_export_ms", export_ms);
    result.insert("layer_tip_export_width", layer_tip_export_size.width());
    result.insert("layer_tip_export_height", layer_tip_export_size.height());
    result.insert("layer_tip_export_rgba64_sha256", layer_tip_export_sha256);
  }
  if (cancel_stress) {
    result.insert("cancel_stress", true);
    result.insert("stress_document_megapixels", (width * (width * 2 / 3)) / 1'000'000.0);
    result.insert("cancel_visible_p95_ms", percentile95(cancel_visible_samples));
    result.insert("cancel_visible_max_ms", cancel_visible_samples.isEmpty() ? 0.0 :
        *std::max_element(cancel_visible_samples.cbegin(), cancel_visible_samples.cend()));
    result.insert("cancel_texture_revision_p95_ms", percentile95(cancel_texture_revision_samples));
    result.insert("cancel_texture_revision_max_ms", cancel_texture_revision_samples.isEmpty() ? 0.0 :
        *std::max_element(cancel_texture_revision_samples.cbegin(), cancel_texture_revision_samples.cend()));
    result.insert("cancel_restored_capture_p95_ms", percentile95(cancel_restored_capture_samples));
    result.insert("cancel_restored_capture_max_ms", cancel_restored_capture_samples.isEmpty() ? 0.0 :
        *std::max_element(cancel_restored_capture_samples.cbegin(), cancel_restored_capture_samples.cend()));
    result.insert("settled_rss_warmup_cycles", cancel_warmup_cycles);
    result.insert("measured_cycles", cancel_rss_samples.size());
    result.insert("total_cycles", total_trial_count);
    result.insert("restore_screenshot_samples", restore_screenshot_samples);
    result.insert("resident_rss_before_measured_cycles_bytes", cancel_warm_rss);
    result.insert("rss_peak_over_warm_baseline_bytes", rss_warm_growth);
    result.insert("rss_end_delta_from_warm_baseline_bytes", resident_rss_after_trials - cancel_warm_rss);
    const auto settled_count = cancel_rss_samples.size();
    const auto first_half_count = (settled_count + 1) / 2;
    qint64 first_half_total = 0, second_half_total = 0;
    for (qsizetype i = 0; i < settled_count; ++i) {
      if (i < first_half_count) first_half_total += cancel_rss_samples[i];
      else second_half_total += cancel_rss_samples[i];
    }
    result.insert("settled_rss_first_half_mean_bytes", first_half_count > 0
        ? first_half_total / first_half_count : 0);
    const auto second_half_count = settled_count - first_half_count;
    result.insert("settled_rss_second_half_mean_bytes", second_half_count > 0
        ? second_half_total / second_half_count : 0);
    result.insert("settled_rss_second_half_mean_delta_bytes", second_half_count > 0 && first_half_count > 0
        ? second_half_total / second_half_count - first_half_total / first_half_count : 0);
    const auto final_window_count = std::min<qsizetype>(50, settled_count);
    qint64 final_window_total = 0;
    qint64 final_window_peak = 0;
    for (qsizetype i = settled_count - final_window_count; i < settled_count; ++i) {
      final_window_total += cancel_rss_samples[i];
      final_window_peak = std::max(final_window_peak, cancel_rss_samples[i]);
    }
    result.insert("retained_rss_final_window_sample_count", final_window_count);
    result.insert("retained_rss_final_window_mean_delta_bytes", final_window_count > 0
        ? final_window_total / final_window_count - cancel_warm_rss : 0);
    result.insert("retained_rss_final_window_peak_delta_bytes", final_window_count > 0
        ? final_window_peak - cancel_warm_rss : 0);
    result.insert("document_unchanged", serialize_edit_recipe_json(EditorPreviewControllerTestAccess::state(c)) == baseline_recipe);
    result.insert("history_unchanged", c.editHistoryEntries().size() == baseline_history_count &&
        history_snapshot_bytes_after_trials == baseline_history_snapshot_bytes &&
        c.canUndo() == baseline_can_undo && c.canRedo() == baseline_can_redo);
    result.insert("history_entry_count", baseline_history_count);
    result.insert("history_snapshot_serialized_bytes", history_snapshot_bytes_after_trials);
    result.insert("baseline_export_ms", export_ms);
    result.insert("scratch_tree_bytes_after_export", scratch_bytes);
  }
  std::cout<<QJsonDocument(result).toJson().toStdString();
  const auto evidence=qEnvironmentVariable("PHOTO_PAINT_PERF_OUTPUT");
  if(!evidence.isEmpty()) { QFile file(evidence); if(!file.open(QIODevice::WriteOnly)) return 13; file.write(QJsonDocument(result).toJson()); window.grabWindow().save(evidence+".png"); }
  return 0;
}
