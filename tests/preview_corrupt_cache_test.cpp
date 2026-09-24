#include "editor_preview_controller.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QUrl>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

namespace {
QByteArray file_digest(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file)) return {};
  return hash.result();
}
// Damage PNG's compressed IDAT data, retaining IHDR and image dimensions.
// Header inspection succeeds; pixel decoding must fail. This is a
// generated private-cache fixture, never a mutation of the source photograph.
QByteArray damaged_png_data(QByteArray png) {
  if (!png.startsWith(QByteArray::fromHex("89504e470d0a1a0a"))) return {};
  qsizetype offset = 8;
  while (offset + 12 <= png.size()) {
    quint32 length = 0;
    for (int byte = 0; byte < 4; ++byte)
      length = (length << 8) | static_cast<unsigned char>(png[offset + byte]);
    if (length > static_cast<quint64>(png.size() - offset - 12)) return {};
    if (png.sliced(offset + 4, 4) == "IDAT" && length > 0) {
      png[offset + 8] = static_cast<char>(static_cast<unsigned char>(png[offset + 8]) ^ 0xff);
      return png;
    }
    offset += static_cast<qsizetype>(length) + 12;
  }
  return {};
}
bool break_cache(const QString& path, const QString& runtime, const QSize& expected) {
  if (!path.startsWith(runtime + QLatin1Char('/'))) return false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const auto corrupted = damaged_png_data(file.readAll());
  file.close();
  if (corrupted.isEmpty() || !file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      file.write(corrupted) != corrupted.size()) return false;
  file.close();
  QImageReader reader(path);
  return reader.canRead() && reader.size() == expected && reader.read().isNull();
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.size() != 1 && args.size() != 3) return 2;
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  const auto source = args.size() == 3 ? args[1] : runtime.filePath("source.png");
  if (args.size() == 1) {
    QImage fixture(256, 192, QImage::Format_ARGB32);
    fixture.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
    for (int y = 0; y < fixture.height(); ++y) for (int x = 0; x < fixture.width(); ++x)
      fixture.setPixelColor(x, y, QColor(x % 220, y % 220, (x + y) % 220));
    if (!fixture.save(source)) return 2;
  }
  const auto external_source = runtime.filePath(QStringLiteral("external-reference.") + QFileInfo(source).suffix());
  if (!QFile::copy(source, external_source)) return 2;
  using namespace photo_editor::desktop;
  QVariantList cases;
  bool all_passed = true;
  for (const auto& model : {QStringLiteral("legacy-oklch-v1"), QStringLiteral("aces2-ocio-v1")}) {
    for (int mode = 0; mode < 4; ++mode) {
      RenderRequest request;
      request.source_asset_id = QStringLiteral("corrupt-cache-fixture");
      request.require_canonical_render_plan = model == QStringLiteral("aces2-ocio-v1");
      request.file_path = source;
      request.full_resolution = true;
      request.require_cpu_preview_image = true;
      request.tile_size = 512;
      request.render_mode = mode == 1 ? QString() : QStringLiteral("max_preview");
      request.state.photographic_rendering_model = model;
      request.state.exposure = 0.25 + 0.03 * mode;
      request.compare_mode = mode >= 2;
      request.has_comparison_reference_state = mode == 2;
      request.comparison_reference_state = request.state;
      request.comparison_reference_state.exposure = 0.1;
      if (mode == 3) request.reference_file_path = external_source;
      const auto seed = render_preview_request_for_test(request);
      const auto path = QUrl(request.compare_mode ? seed.reference_preview_source : seed.preview_source).toLocalFile();
      const QImage expected(path);
      if (!seed.success || expected.isNull() || !break_cache(path, runtime.path(), expected.size())) {
        std::cerr << "Unable to construct header-valid, decode-invalid cache fixture model="
                  << model.toStdString() << " mode=" << mode << " seedSuccess=" << seed.success
                  << " status=" << seed.status.toStdString() << " expectedNull=" << expected.isNull()
                  << " fallback=" << seed.fallback_reason.toStdString()
                  << " failure=" << seed.failure_detail.toStdString()
                  << " path=" << path.toStdString() << '\n';
        return 2;
      }
      const auto corrupted_digest = file_digest(path);
      auto canceled_request = request;
      canceled_request.generation = 1;
      canceled_request.latest_generation = std::make_shared<std::atomic<int>>(2);
      const auto canceled = render_preview_request_for_test(canceled_request);
      const bool cancellation_passed = canceled.cancelled && !canceled.success &&
          !corrupted_digest.isEmpty() && file_digest(path) == corrupted_digest;
      const auto repaired = render_preview_request_for_test(request);
      const auto repair_path = QUrl(request.compare_mode ? repaired.reference_preview_source : repaired.preview_source).toLocalFile();
      const QImage repaired_image(repair_path);
      bool passed = cancellation_passed && repaired.success && !repaired.disk_cache_hit && repair_path == path &&
          !repaired_image.isNull() && repaired_image == expected && repaired_image.colorSpace() == expected.colorSpace();
      const auto warm = render_preview_request_for_test(request);
      passed = passed && warm.success && (request.compare_mode || warm.disk_cache_hit);
      if (mode == 0) passed = passed && warm.preview_image == expected.convertToFormat(QImage::Format_ARGB32);
      // A fully decodable but wrong-size reference cache must also be repaired.
      const auto small = expected.scaled(8, 8);
      if (!small.save(path, "png")) return 2;
      const auto resized_repair = render_preview_request_for_test(request);
      const bool dimensions_passed = resized_repair.success && QImage(path) == expected;
      passed = passed && dimensions_passed;
      cases.push_back(QVariantMap{{"model", model}, {"mode", mode}, {"passed", passed},
          {"repairReportedHit", repaired.disk_cache_hit}, {"repairReportedSuccess", repaired.success},
          {"width", expected.width()}, {"height", expected.height()},
          {"cancellationPassed", cancellation_passed}, {"dimensionsPassed", dimensions_passed},
          {"repairedPixelsEqual", repaired_image == expected}});
      all_passed = all_passed && passed;
    }
  }
  const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"cases", cases}, {"passed", all_passed}}).toJson();
  if (args.size() == 3) {
    QFile receipt(args[2]);
    if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly) || receipt.write(bytes) != bytes.size()) return 2;
  } else std::cout << bytes.toStdString();
  return all_passed ? 0 : 1;
}
