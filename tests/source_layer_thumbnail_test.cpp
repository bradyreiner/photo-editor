#include "photo_render_utils.h"
#include "qimage_color_conversion.h"
#include "thumbnail_worker_renderer_factory.h"
#include "photo_paint_surface.h"
#include "photo_layer_adjustment_recipe.h"
#include "render_plan_store.h"
#include "working_image.h"

#include <QCoreApplication>
#include <QFile>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace photo_editor::desktop;

namespace {
QImage thumbnail_image(const QVariantMap& entry);
bool same_pixels(const QImage& a, const QImage& b, const char* label, int tolerance = 1) {
  if (a.isNull() || a.size() != b.size()) return false;
  int maximum = 0;
  for (int y = 0; y < a.height(); ++y)
    for (int x = 0; x < a.width(); ++x) {
      const auto left = a.pixelColor(x, y), right = b.pixelColor(x, y);
      maximum = std::max(maximum, std::max({std::abs(left.red() - right.red()),
          std::abs(left.green() - right.green()), std::abs(left.blue() - right.blue()),
          std::abs(left.alpha() - right.alpha())}));
    }
  std::cout << label << " max RGBA8 delta=" << maximum << '\n';
  return maximum <= tolerance;
}

QVariantMap paint_fixture(const QString& profile) {
  QVariantMap layer{{"id", "float-paint"}, {"kind", "pixel"}, {"enabled", true},
      {"pixelWidth", 512}, {"pixelHeight", 512}, {"canvasWidth", 512}, {"canvasHeight", 512},
      {"opacity", 1.0}, {"scale", 1.0},
      {"paintTiles", empty_photo_paint_tiles(working_color_space(profile))}};
  PhotoPaintSurface surface;
  if (!surface.load(layer)) return {};
  for (int ty = 0; ty * kPhotoPaintTileEdge < 512; ++ty)
    for (int tx = 0; tx * kPhotoPaintTileEdge < 512; ++tx) {
    auto tile = surface.tile(tx, ty);
    for (int y = 0; y < tile.height(); ++y) {
      auto* row = reinterpret_cast<QRgbaFloat32*>(tile.scanLine(y));
      for (int x = 0; x < tile.width(); ++x) {
        const int gx = tx * kPhotoPaintTileEdge + x, gy = ty * kPhotoPaintTileEdge + y;
        // Block boundaries align at both 128px backing and 64px display size:
        // the cross-resolution oracle exercises filtering without depending
        // on the non-commutativity of tone mapping and edge resampling.
        const float alpha = gy >= 384 ? 0.0F : gy >= 256 ? 0.20F : 0.65F;
        const float red = gx < 256 ? 2.4F : 0.08F;
        const float green = gx < 256 ? -0.025F : 0.7F;
        const float blue = gx < 256 ? 0.16F : 1.7F;
        row[x] = QRgbaFloat32{red * alpha, green * alpha, blue * alpha, alpha};
      }
    }
    surface.store(tx, ty, tile);
  }
  layer.insert("paintTiles", surface.snapshot());
  return layer;
}

QImage isolated_paint_composite(const QVariantMap& layer,
                               const EditRecipeState& document, const QSize& size) {
  EditRecipeState state;
  state.working_color_profile = document.working_color_profile;
  state.photographic_rendering_model = document.photographic_rendering_model;
  state.layers = {layer};
  QImage transparent(size, QImage::Format_RGBA32FPx4);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  transparent.fill(Qt::transparent);
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("paint-thumbnail-reference:%1").arg(transparent.cacheKey()), {}, state,
      size, photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  if (state.photographic_rendering_model == "aces2-ocio-v1" && !admission.admitted) return {};
  return render_output_image_with_backend(transparent, state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly, nullptr, nullptr, {},
      photo_editor::pipeline::RemovalQuality::Final, RenderOutputPolicy::DisplayReferred,
      admission.admitted ? &*admission.plan : nullptr);
}

bool verify_float_paint() {
  const auto renderers = make_thumbnail_worker_renderers({});
  for (const auto* profile : {"Linear Display P3", "Linear ProPhoto RGB"}) {
    auto layer = paint_fixture(QString::fromLatin1(profile));
    if (layer.isEmpty() || !rendered_photo_layer_thumbnail_source(layer).isEmpty()) return false;
    ThumbnailRefreshQueueRequest request;
    request.job.asset_id = "paint-thumbnail-proof";
    request.state.working_color_profile = QString::fromLatin1(profile);
    const auto entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
        layer, request, renderers.source_layer_thumbnail);
    const auto actual = thumbnail_image(entry);
    if (actual.colorSpace() != QColorSpace(QColorSpace::SRgb) ||
        !entry.value("thumbnailSharedRenderPath").toBool() ||
        entry.value("thumbnailRenderSource").toString() != "paint-photographic-output") return false;
    const auto backing_reference = scaled_layer_thumbnail_image(
        isolated_paint_composite(layer, request.state, QSize(128, 128)));
    const auto full_reference = scaled_layer_thumbnail_image(
        isolated_paint_composite(layer, request.state, QSize(512, 512)));
    if (!same_pixels(actual, backing_reference, "Paint backing composition") ||
        !same_pixels(actual, full_reference, "Paint full-resolution composition", 2)) return false;
    if (actual.pixelColor(16, 56).alpha() != 0 ||
        std::abs(actual.pixelColor(16, 40).alpha() - 51) > 1 ||
        std::abs(actual.pixelColor(16, 16).alpha() - 166) > 1) return false;
    const auto icc_only = scaled_layer_thumbnail_image(convert_tagged_qimage_color_space(
        rasterize_photo_paint_layer(layer, QSize(128, 128)), QColorSpace(QColorSpace::SRgb),
        QImage::Format_ARGB32, QImageColorConversionPurpose::EvidenceMeasurement));
    if (actual == icc_only) return false;  // Exercise photographic output, not just ICC tagging.

    request.state.exposure = 3.0;
    request.state.temperature = 9000;
    request.state.crop_rect_x = 0.25;
    request.state.crop_rect_width = 0.5;
    request.state.crop_rect_height = 1.0;
    auto changed = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
    if (!same_pixels(actual, thumbnail_image(changed), "Paint content ignores document grade", 0) ||
        changed.value("thumbnailCacheKey") != entry.value("thumbnailCacheKey")) return false;

    request.state.photographic_rendering_model = "legacy-oklch-v1";
    changed = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
    if (changed.isEmpty() || changed.value("thumbnailCacheKey") == entry.value("thumbnailCacheKey") ||
        thumbnail_image(changed) == actual || !same_pixels(thumbnail_image(changed),
          scaled_layer_thumbnail_image(isolated_paint_composite(layer, request.state, QSize(128, 128))),
          "Paint document rendering policy")) return false;
    const auto prior_key = changed.value("thumbnailCacheKey");
    request.state.working_color_profile = "Linear sRGB";
    changed = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
    if (changed.isEmpty() || changed.value("thumbnailCacheKey") == prior_key) return false;

    request.state = {};
    layer.insert("hasNonNeutralAdjustments", true);
    layer.insert("exposure", 0.75);
    changed = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
    if (changed.isEmpty() || !same_pixels(thumbnail_image(changed),
          scaled_layer_thumbnail_image(isolated_paint_composite(layer, request.state, QSize(128, 128))),
          "Paint content Colors")) return false;
    layer.remove("hasNonNeutralAdjustments");
    const auto raw_colors = rendered_non_adjustment_photo_layer_thumbnail_entry(
        layer, request, renderers.source_layer_thumbnail);
    if (!same_pixels(thumbnail_image(raw_colors), thumbnail_image(changed),
          "Persisted paint Colors without UI annotation", 0)) return false;
  }
  return true;
}

QImage thumbnail_image(const QVariantMap& entry) {
  const auto source = entry.value("thumbnailSource").toString();
  return QImage::fromData(QByteArray::fromBase64(source.section(',', 1).toLatin1()), "PNG");
}

bool verify_persisted_content_colors(const QString& path) {
  QImage decoded;
  if (!decode_source_image(path, &decoded, 0)) return false;
  const auto renderers = make_thumbnail_worker_renderers({});
  ThumbnailRefreshQueueRequest request;
  request.job.asset_id = "raw-layer-colors-proof";
  const QVariantList identity_curve{QVariantMap{{"x", 0.0}, {"y", 0.0}},
      QVariantMap{{"x", 0.5}, {"y", 0.5}}, QVariantMap{{"x", 1.0}, {"y", 1.0}}};
  const QVariantMap neutral_recipe{{"exposure", 0.0}, {"levelsGamma", 0.0},
      {"levelsWhitePoint", 0.0}, {"sharpenRadius", 1.0},
      {"transformScale", 1.0}, {"liquifyCenterX", 0.5}, {"liquifyRadius", 0.25},
      {"colorizeHue", 0.3}, {"masterCurvePoints", identity_curve},
      {"adjustments", QVariantMap{{"colorMixer", QVariantMap{{"model", "OKLab"}, {"channel", "Reds"}}},
          {"colorGrading", QVariantMap{{"amount", 100.0},
              {"shadows", QVariantMap{{"hue", 0.6}, {"saturation", 0.0}}}}}}}};
  for (const auto* kind : {"source", "pixel"}) {
    QVariantMap layer{{"id", "raw-content"}, {"kind", QLatin1String(kind)}, {"enabled", true},
        {"sourcePath", path}};
    if (QLatin1String(kind) == QLatin1String("pixel"))
      layer.insert("pixelData", QByteArray::fromBase64(encoded_layer_thumbnail_source(decoded)
          .section(',', 1).toLatin1()));
    const auto base = rendered_non_adjustment_photo_layer_thumbnail_entry(
        layer, request, renderers.source_layer_thumbnail);
    if (base.isEmpty()) return false;
    auto neutral = layer;
    for (auto it = neutral_recipe.cbegin(); it != neutral_recipe.cend(); ++it)
      neutral.insert(it.key(), it.value());
    neutral.insert("hasNonNeutralAdjustments", true);  // Stale UI annotation must not change pixels.
    const auto neutral_entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
        neutral, request, renderers.source_layer_thumbnail);
    if (photo_layer_has_non_neutral_adjustments(neutral) ||
        !same_pixels(thumbnail_image(base), thumbnail_image(neutral_entry),
          "Neutral persisted defaults preserve ICC-only content", 0) ||
        neutral_entry.value("thumbnailRenderSource") == QStringLiteral("layer-content-colors")) return false;
    const QVariantList changed_curve{QVariantMap{{"x", 0.0}, {"y", 0.0}},
        QVariantMap{{"x", 0.5}, {"y", 0.7}}, QVariantMap{{"x", 1.0}, {"y", 1.0}}};
    const QVariantList recipes{QVariantMap{{"exposure", 0.75}},
        QVariantMap{{"adjustments", QVariantMap{{"masterCurvePoints", changed_curve}}}}};
    for (const auto& recipe : recipes) {
      auto raw = layer;
      const auto colors = recipe.toMap();
      for (auto it = colors.cbegin(); it != colors.cend(); ++it) raw.insert(it.key(), it.value());
      raw.insert("hasNonNeutralAdjustments", false);  // Persisted recipe is authoritative.
      const auto actual = rendered_non_adjustment_photo_layer_thumbnail_entry(
          raw, request, renderers.source_layer_thumbnail);
      raw.insert("hasNonNeutralAdjustments", true);
      const auto annotated = rendered_non_adjustment_photo_layer_thumbnail_entry(
          raw, request, renderers.source_layer_thumbnail);
      auto adjustment = photo_layer_adjustment_recipe(raw);
      adjustment.insert("kind", "adjustment");
      adjustment.insert("id", "independent-content-colors");
      EditRecipeState reference_state;
      reference_state.layers = {adjustment};
      const auto reference = scaled_layer_thumbnail_image(render_output_image_with_backend(
          decoded, reference_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
          RenderBackendPreference::CpuOnly));
      if (actual.isEmpty() || actual.value("thumbnailRenderSource") != QStringLiteral("layer-content-colors") ||
          !same_pixels(thumbnail_image(actual), thumbnail_image(annotated), "Raw/annotated local Colors", 0) ||
          !same_pixels(thumbnail_image(actual), reference, "Persisted local Colors full oracle", 0) ||
          thumbnail_image(actual) == thumbnail_image(base)) return false;
      const auto prior_key = actual.value("thumbnailCacheKey");
      request.state.photographic_rendering_model = "legacy-oklch-v1";
      const auto policy_changed = rendered_non_adjustment_photo_layer_thumbnail_entry(
          raw, request, renderers.source_layer_thumbnail);
      request.state = {};
      if (policy_changed.isEmpty() || policy_changed.value("thumbnailCacheKey") == prior_key) return false;
    }
    QVariantList table;
    for (int blue = 0; blue < 2; ++blue)
      for (int green = 0; green < 2; ++green)
        for (int red = 0; red < 2; ++red)
          table.append(QVariant{QVariantList{red * 0.55, green * 0.8, blue * 1.0}});
    const QVariantMap lut{{"enabled", true}, {"strength", 75.0}, {"size", 2}, {"table", table}};
    auto flat = layer;
    flat.insert("lutEnabled", true);
    flat.insert("lutStrength", 75.0);
    flat.insert("lutSize", 2);
    flat.insert("lutTable", table);
    auto structured = layer;
    structured.insert("adjustments", QVariantMap{{"lut", lut}});
    const auto flat_entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
        flat, request, renderers.source_layer_thumbnail);
    const auto structured_entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
        structured, request, renderers.source_layer_thumbnail);
    const auto output_for = [&decoded](const QVariantMap& input) {
      EditRecipeState state;
      auto adjustment = photo_layer_adjustment_recipe(input);
      adjustment.insert("kind", "adjustment");
      adjustment.insert("id", "lut-content-full-reference");
      state.layers = {adjustment};
      return render_output_image_with_backend(decoded, state, QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
    };
    if (!same_pixels(thumbnail_image(flat_entry), thumbnail_image(structured_entry),
          "Legacy flat/structured LUT thumbnail", 0) ||
        !same_pixels(output_for(flat), output_for(structured), "Legacy flat/structured LUT full output", 0) ||
        thumbnail_image(flat_entry) == thumbnail_image(base)) return false;
    for (const auto& explicit_lut : QVariantList{QVariantMap{}, QVariantMap{{"enabled", false},
             {"strength", 75.0}, {"size", 2}, {"table", table}}}) {
      auto cleared = flat;
      cleared.insert("adjustments", QVariantMap{{"lut", explicit_lut}});
      const auto cleared_entry = rendered_non_adjustment_photo_layer_thumbnail_entry(
          cleared, request, renderers.source_layer_thumbnail);
      auto neutral_output = layer;
      if (photo_layer_has_non_neutral_adjustments(cleared) ||
          !same_pixels(thumbnail_image(cleared_entry), thumbnail_image(base),
              "Explicit structured LUT clear/disable wins over legacy", 0) ||
          !same_pixels(output_for(cleared), output_for(neutral_output),
              "Structured LUT clear/disable full output", 0)) return false;
    }
  }
  return true;
}

bool verify_source(const QString& path) {
  auto request = make_thumbnail_refresh_queue_request(ThumbnailRefreshQueueRequestFacts{
      .asset_id = QStringLiteral("thumbnail-proof"),
      .file_path = QStringLiteral("/deliberately-unrelated-base.dng"),
      .photo_layers = {QVariantMap{{"id", "source"}, {"kind", "source"}, {"sourcePath", path}}},
      .generation = 1,
      .photo_layer_thumbnail_dirty = true,
      .cancellation = std::make_shared<std::atomic_bool>(false)});
  const auto layer = request.photo_layers.first().toMap();
  if (!rendered_photo_layer_thumbnail_source(layer).isEmpty()) return false;
  const auto renderers = make_thumbnail_worker_renderers({});
  const auto run = [&renderers](const ThumbnailRefreshQueueRequest& snapshot) {
    QEventLoop loop;
    QFutureWatcher<ThumbnailRefreshQueueResult> watcher;
    QObject::connect(&watcher, &QFutureWatcher<ThumbnailRefreshQueueResult>::finished,
        &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run([snapshot, renderers] {
      return compute_thumbnail_refresh_queue_result(snapshot, renderers);
    }));
    loop.exec();
    return watcher.result();
  };
  auto result = run(request);
  if (!result.success || result.photo_layer_thumbnail_entries.size() != 1) return false;
  const auto ui_thread = QStringLiteral("0x%1").arg(
      reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
  if (result.worker_thread.isEmpty() || result.worker_thread == ui_thread) return false;
  const auto entry = result.photo_layer_thumbnail_entries.first().toMap();
  const auto actual = thumbnail_image(entry);
  if (actual.size() != QSize(64, 64) || actual.colorSpace() != QColorSpace(QColorSpace::SRgb) ||
      !entry.value("thumbnailReady").toBool() || !entry.value("thumbnailColorManaged").toBool() ||
      entry.value("thumbnailPreviewRenderMode").toString() != "source_neutral_srgb") return false;
  return true;
}

ThumbnailRefreshQueueResult run_thumbnail_request_worker(
    ThumbnailRefreshQueueRequest snapshot,
    ThumbnailRefreshQueueRenderers renderers) {
  return compute_thumbnail_refresh_queue_result(snapshot, renderers);
}

ThumbnailRefreshQueueResult run_thumbnail_request(
    const ThumbnailRefreshQueueRequest& snapshot,
    const ThumbnailRefreshQueueRenderers& renderers) {
  QEventLoop loop;
  QFutureWatcher<ThumbnailRefreshQueueResult> watcher;
  QObject::connect(
      &watcher,
      &QFutureWatcher<ThumbnailRefreshQueueResult>::finished,
      &loop,
      &QEventLoop::quit);
  watcher.setFuture(QtConcurrent::run(run_thumbnail_request_worker, snapshot, renderers));
  loop.exec();
  return watcher.result();
}

int maximum_rgba8_delta(const QImage& candidate, const QImage& reference) {
  const auto left = candidate.convertToFormat(QImage::Format_RGBA8888);
  const auto right = reference.convertToFormat(QImage::Format_RGBA8888);
  if (left.isNull() || right.size() != left.size()) return -1;
  auto delta = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width() * 4; ++x) {
      delta = std::max(
          delta,
          std::abs(
              int(left.constScanLine(y)[x]) -
              int(right.constScanLine(y)[x])));
    }
  }
  return delta;
}

bool matches_canonical_thumbnail(
    const QString& path,
    const QImage& actual) {
  EditRecipeState neutral;
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, 128, neutral, &metadata)) return false;
  if (path.endsWith(".jpg")) {
    // Independent ICC conversion oracle for neutral display-referred input.
    const auto icc_reference = scaled_layer_thumbnail_image(
        decoded.convertedToColorSpace(QColorSpace(QColorSpace::SRgb)));
    const auto icc_delta = maximum_rgba8_delta(actual, icc_reference);
    std::cout << "Independent neutral JPEG ICC max RGB8 delta=" << icc_delta << '\n';
    if (icc_delta < 0 || icc_delta > 2) return false;
  }
  const auto reference = scaled_layer_thumbnail_image(
      metadata.is_raw
          ? render_output_image_with_backend(
                decoded,
                neutral,
                QColorSpace(QColorSpace::SRgb),
                QImage::Format_ARGB32,
                RenderBackendPreference::CpuOnly,
                &metadata)
          : convert_tagged_qimage_color_space(
                decoded,
                QColorSpace(QColorSpace::SRgb),
                QImage::Format_ARGB32,
                QImageColorConversionPurpose::EvidenceMeasurement));
  const auto delta = maximum_rgba8_delta(actual, reference);
  std::cout << path.toStdString() << " canonical max RGB8 delta=" << delta << '\n';
  return delta == 0;
}

bool verify_thumbnail_request_controls(
    ThumbnailRefreshQueueRequest request,
    const ThumbnailRefreshQueueRenderers& renderers,
    const QImage& actual) {
  // Base-image edits and working/output policy must not contaminate content thumbnails.
  request.state.exposure = 4;
  request.state.temperature = 9000;
  request.state.working_color_profile = QStringLiteral("Linear sRGB");
  request.state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
  auto result = run_thumbnail_request(request, renderers);
  if (!result.success ||
      thumbnail_image(result.photo_layer_thumbnail_entries.first().toMap()) != actual) {
    return false;
  }
  request.cancellation->store(true);
  result = run_thumbnail_request(request, renderers);
  if (!result.cancelled || result.success ||
      !result.photo_layer_thumbnail_entries.isEmpty()) {
    return false;
  }
  request.cancellation->store(false);
  // Missing worker support must fail closed, not publish a generic placeholder.
  result = compute_thumbnail_refresh_queue_result(request, {});
  return !result.success && result.photo_layer_thumbnail_failed &&
         result.photo_layer_thumbnail_entries.isEmpty();
}

ThumbnailRefreshQueueRequest make_source_thumbnail_request(const QString& path) {
  ThumbnailRefreshQueueRequestFacts facts;
  facts.asset_id = QStringLiteral("thumbnail-proof");
  facts.file_path = QStringLiteral("/deliberately-unrelated-base.dng");
  QVariantMap layer;
  layer.insert(QStringLiteral("id"), QStringLiteral("source"));
  layer.insert(QStringLiteral("kind"), QStringLiteral("source"));
  layer.insert(QStringLiteral("sourcePath"), path);
  facts.photo_layers.push_back(layer);
  facts.generation = 1;
  facts.photo_layer_thumbnail_dirty = true;
  facts.cancellation = std::make_shared<std::atomic_bool>(false);
  return make_thumbnail_refresh_queue_request(std::move(facts));
}

QString current_thread_label() {
  return QStringLiteral("0x%1").arg(
      reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
}

bool has_expected_thumbnail_result(
    const ThumbnailRefreshQueueResult& result,
    const QString& ui_thread,
    QImage* actual) {
  if (!result.success || result.photo_layer_thumbnail_entries.size() != 1 ||
      result.worker_thread.isEmpty() || result.worker_thread == ui_thread) {
    return false;
  }
  const auto entry = result.photo_layer_thumbnail_entries.first().toMap();
  *actual = thumbnail_image(entry);
  return actual->size() == QSize(64, 64) &&
         actual->colorSpace() == QColorSpace(QColorSpace::SRgb) &&
         entry.value("thumbnailReady").toBool() &&
         entry.value("thumbnailColorManaged").toBool() &&
         entry.value("thumbnailPreviewRenderMode").toString() == "source_neutral_srgb";
}

bool verify_source(const QString& path) {
  auto request = make_source_thumbnail_request(path);
  const auto layer = request.photo_layers.first().toMap();
  if (!rendered_photo_layer_thumbnail_source(layer).isEmpty()) return false;
  const auto renderers = make_thumbnail_worker_renderers({});
  const auto result = run_thumbnail_request(request, renderers);
  QImage actual;
  if (!has_expected_thumbnail_result(result, current_thread_label(), &actual)) return false;
  std::cout << "worker=" << result.worker_thread.toStdString() << '\n';
  return matches_canonical_thumbnail(path, actual) &&
         verify_thumbnail_request_controls(request, renderers, actual);
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  if (!verify_float_paint()) return 1;
  for (const auto space : {QColorSpace::SRgb, QColorSpace::DisplayP3}) {
    QImage source(96, 80, QImage::Format_RGB32);
    source.setColorSpace(QColorSpace(space));
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x)
        source.setPixelColor(x, y, QColor(40 + x * 2, 20 + y * 2, 90));
    const auto path = root.filePath(QString::number(static_cast<int>(space)) + ".jpg");
    if (!source.save(path, "JPG") || !verify_source(path) || !verify_persisted_content_colors(path)) return 1;
  }
  const auto path = root.filePath("changing.png");
  QImage source(64, 64, QImage::Format_RGB32);
  source.fill(Qt::red);
  if (!source.save(path)) return 1;
  QVariantMap layer{{"id", "changing"}, {"kind", "source"}, {"sourcePath", path}};
  const auto old_hash = non_adjustment_photo_layer_thumbnail_recipe_hash(layer);
  const auto renderers = make_thumbnail_worker_renderers({});
  ThumbnailRefreshQueueRequest request;
  auto before = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
  source.fill(Qt::blue);
  QSaveFile replacement(path);
  if (!replacement.open(QIODevice::WriteOnly) || !source.save(&replacement, "PNG") || !replacement.commit()) return 1;
  auto after = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail);
  if (old_hash == non_adjustment_photo_layer_thumbnail_recipe_hash(layer) ||
      before.value("thumbnailCacheKey") == after.value("thumbnailCacheKey") ||
      thumbnail_image(before) == thumbnail_image(after)) return 1;
  // A file changed during rendering cannot be published under the old revision.
  const auto raced = rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request,
      [&](const QVariantMap& input, const ThumbnailRefreshQueueRequest& snapshot, QVariantMap* metadata) {
        auto payload = renderers.source_layer_thumbnail(input, snapshot, metadata);
        QFile::remove(path);
        return payload;
      });
  if (!raced.isEmpty() || !rendered_non_adjustment_photo_layer_thumbnail_entry(
          layer, request, renderers.source_layer_thumbnail).isEmpty()) return 1;
  QSaveFile corrupt(path);
  if (!corrupt.open(QIODevice::WriteOnly) || corrupt.write("not an image") < 0 || !corrupt.commit()) return 1;
  if (!rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail).isEmpty()) return 1;
  layer.insert("pixelData", QByteArray::fromBase64(
      encoded_layer_thumbnail_source(source).section(',', 1).toLatin1()));
  if (rendered_non_adjustment_photo_layer_thumbnail_entry(layer, request, renderers.source_layer_thumbnail).isEmpty()) return 1;
  if (argc > 1 && !verify_source(QString::fromLocal8Bit(argv[1]))) return 1;
  std::cout << "Source thumbnail JPEG/ICC, cancellation, revision and failure checks passed.\n";
  return 0;
}
