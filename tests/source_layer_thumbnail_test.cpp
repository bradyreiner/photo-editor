#include "photo_render_utils.h"
#include "qimage_color_conversion.h"
#include "thumbnail_worker_renderer_factory.h"

#include <QCoreApplication>
#include <QFile>
#include <QEventLoop>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtConcurrent>
#include <iostream>

using namespace photo_editor::desktop;

namespace {
QImage thumbnail_image(const QVariantMap& entry) {
  const auto source = entry.value("thumbnailSource").toString();
  return QImage::fromData(QByteArray::fromBase64(source.section(',', 1).toLatin1()), "PNG");
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

  // Compare against the canonical neutral output, with this file's own metadata.
  EditRecipeState neutral;
  QImage decoded;
  photo_editor::pipeline::DecodedImageMetadata metadata;
  if (!decode_source_image(path, &decoded, 128, neutral, &metadata)) return false;
  if (path.endsWith(".jpg")) {
    // Independent ICC conversion oracle for neutral display-referred input.
    const auto icc_reference = scaled_layer_thumbnail_image(
        decoded.convertedToColorSpace(QColorSpace(QColorSpace::SRgb)))
        .convertToFormat(QImage::Format_RGBA8888);
    const auto pixels = actual.convertToFormat(QImage::Format_RGBA8888);
    int icc_delta = 0;
    if (icc_reference.size() != pixels.size()) return false;
    for (int y = 0; y < pixels.height(); ++y)
      for (int x = 0; x < pixels.width() * 4; ++x)
        icc_delta = std::max(icc_delta, std::abs(int(pixels.constScanLine(y)[x]) - int(icc_reference.constScanLine(y)[x])));
    std::cout << "Independent neutral JPEG ICC max RGB8 delta=" << icc_delta << '\n';
    if (icc_delta > 2) return false;
  }
  const auto reference = scaled_layer_thumbnail_image(metadata.is_raw ? render_output_image_with_backend(
      decoded, neutral, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, &metadata)
      : convert_tagged_qimage_color_space(decoded, QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32, QImageColorConversionPurpose::EvidenceMeasurement));
  const auto left = actual.convertToFormat(QImage::Format_RGBA8888);
  const auto right = reference.convertToFormat(QImage::Format_RGBA8888);
  if (left.isNull() || right.size() != left.size()) return false;
  int delta = 0;
  for (int y = 0; y < left.height(); ++y)
    for (int x = 0; x < left.width() * 4; ++x)
      delta = std::max(delta, std::abs(int(left.constScanLine(y)[x]) - int(right.constScanLine(y)[x])));
  std::cout << path.toStdString() << " canonical max RGB8 delta=" << delta
            << " worker=" << result.worker_thread.toStdString() << '\n';
  if (delta != 0) return false;

  // Base-image edits and working/output policy must not contaminate content thumbnails.
  request.state.exposure = 4;
  request.state.temperature = 9000;
  request.state.working_color_profile = QStringLiteral("Linear sRGB");
  request.state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
  result = run(request);
  if (!result.success || thumbnail_image(result.photo_layer_thumbnail_entries.first().toMap()) != actual)
    return false;
  request.cancellation->store(true);
  result = run(request);
  if (!result.cancelled || result.success || !result.photo_layer_thumbnail_entries.isEmpty()) return false;
  request.cancellation->store(false);
  // Missing worker support must fail closed, not publish a generic placeholder.
  result = compute_thumbnail_refresh_queue_result(request, {});
  return !result.success && result.photo_layer_thumbnail_failed && result.photo_layer_thumbnail_entries.isEmpty();
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return 1;
  for (const auto space : {QColorSpace::SRgb, QColorSpace::DisplayP3}) {
    QImage source(96, 80, QImage::Format_RGB32);
    source.setColorSpace(QColorSpace(space));
    for (int y = 0; y < source.height(); ++y)
      for (int x = 0; x < source.width(); ++x)
        source.setPixelColor(x, y, QColor(40 + x * 2, 20 + y * 2, 90));
    const auto path = root.filePath(QString::number(static_cast<int>(space)) + ".jpg");
    if (!source.save(path, "JPG") || !verify_source(path)) return 1;
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
