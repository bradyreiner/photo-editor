#include "editor_preview_controller.h"
#include "color_mask_coverage_cache.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
struct EditorPreviewControllerTestAccess {
  static void clear() { EditorPreviewController::clearPreviewWorkingCachesForTesting(); }
};
}

// Exercises the actual preview request entry point, not the agent/export proof
// renderer. No catalog or model is opened. Real image fixtures are caller-owned.
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  auto args = app.arguments();
  const bool local_benchmark = args.removeOne(QStringLiteral("--benchmark-local"));
  const bool uncached_coverage = args.removeOne(QStringLiteral("--uncached-coverage"));
  QTemporaryDir synthetic;
  const bool aces2_self_test = args.size() == 2 && args[1] == "--self-test-aces2";
  if (args.size() == 1 || aces2_self_test) {
    // Portable raster coverage. Real RAW proof still requires an explicit file.
    if (!synthetic.isValid()) return 1;
    QImage fixture(96, 64, QImage::Format_ARGB32);
    fixture.setColorSpace(QColorSpace(QColorSpace::SRgb));
    for (int y = 0; y < fixture.height(); ++y) for (int x = 0; x < fixture.width(); ++x)
      fixture.setPixelColor(x, y, QColor(20 + x * 2, 20 + y * 3, 30 + (x + y) % 180));
    const auto source = synthetic.filePath("source.png");
    if (!fixture.save(source)) return 1;
    args = {args.first(), source, synthetic.filePath("proof")};
    if (aces2_self_test) args.append("aces2");
  }
  if (args.size() < 3 || args.size() > 5) {
    std::cerr << "usage: legacy_preview_locality_driver SOURCE NEW_OUTPUT_DIRECTORY [legacy|aces2] [MAX_EDGE_OR_ZERO_FOR_FULL] [--benchmark-local] [--uncached-coverage]\n";
    return 1;
  }
  const auto renderer = args.size() >= 4 ? args[3] : QStringLiteral("legacy");
  if (renderer != "legacy" && renderer != "aces2") return 1;
  bool valid_edge = true;
  const auto edge = args.size() == 5 ? args[4].toInt(&valid_edge) : 1600;
  if (!valid_edge || edge < 0 || edge > 8192) return 1;
  const auto rendering_model = renderer == "aces2" ? QStringLiteral("aces2-ocio-v1") : QStringLiteral("legacy-oklch-v1");
  QDir output(args[2]);
  if (!QFileInfo(args[1]).isFile() || !QFileInfo(args[2]).isAbsolute() ||
      !output.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty() || !output.mkpath(".")) return 1;
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  using namespace photo_editor::desktop;
  RenderRequest request;
  request.file_path = QFileInfo(args[1]).absoluteFilePath();
  request.source_asset_id = "legacy-preview-locality";
  request.max_edge = edge;
  request.full_resolution = edge == 0;
  request.use_in_memory_preview = true;
  request.require_cpu_preview_image = true;
  request.state.photographic_rendering_model = rendering_model;
  if (uncached_coverage) photo_editor::pipeline::reset_mask_coverage_cache_for_testing(false);
  if (local_benchmark) {
    const auto neutral = render_preview_request_for_test(request);
    if (!neutral.success || neutral.preview_image.isNull()) return 1;
    QByteArray alpha(256, '\0');
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 8; ++x) alpha[y * 16 + x] = char(0xff);
    QVariantList samples;
    QSet<QByteArray> output_hashes;
    QImage last;
    for (int frame = 0; frame < 24; ++frame) {
      const auto temperature = 12.0 + double(frame) * 0.25;
      request.state.mask_stack = {QVariantMap{{"id", "local-temperature"}, {"kind", "background"},
          {"enabled", true}, {"operation", "add"}, {"temperature", temperature}, {"opacity", 1.0},
          {"density", 1.0}, {"feather", 0.0}, {"segmentationWidth", 16}, {"segmentationHeight", 16},
          {"segmentationData", alpha}}};
      const auto cache_before = photo_editor::pipeline::mask_coverage_cache_stats();
      const auto result = render_preview_request_for_test(request);
      const auto cache_after = photo_editor::pipeline::mask_coverage_cache_stats();
      if (!result.success || result.preview_image.size() != neutral.preview_image.size() ||
          result.preview_image == neutral.preview_image) return 1;
      // Checks and hashes are outside the measured render call. RAW decoding,
      // mandatory input characterization and all normal output stages remain.
      for (int y = 0; y < result.preview_image.height(); ++y)
        for (int x = result.preview_image.width() * 3 / 4; x < result.preview_image.width(); ++x)
          if (result.preview_image.pixelColor(x, y) != neutral.preview_image.pixelColor(x, y)) return 1;
      const auto rgba = result.preview_image.convertToFormat(QImage::Format_RGBA8888);
      const auto digest = QCryptographicHash::hash(
          QByteArrayView(reinterpret_cast<const char*>(rgba.constBits()), rgba.sizeInBytes()),
          QCryptographicHash::Sha256).toHex();
      if (output_hashes.contains(digest)) return 1;
      output_hashes.insert(digest);
      const QVariantMap sample{{"frame", frame}, {"temperature", temperature},
          {"coverageHits", QVariant::fromValue(cache_after.hits - cache_before.hits)},
          {"coverageMisses", QVariant::fromValue(cache_after.misses - cache_before.misses)},
          {"coverageRetainedBytes", QVariant::fromValue(cache_after.retained_bytes)},
          {"renderMs", result.total_elapsed_ms}, {"pipelineMs", result.adjustment_pipeline_elapsed_ms},
          {"outputMs", result.adjustment_output_elapsed_ms}, {"decodeMs", result.decode_elapsed_ms},
          {"decodedCacheHit", result.decoded_cache_hit}, {"backend", result.backend},
          {"fallbackReason", result.fallback_reason}, {"rgbaSha256", QString::fromLatin1(digest)},
          {"protectedChangedPixels", 0}};
      samples.append(sample);
      std::cout << QJsonDocument::fromVariant(sample).toJson(QJsonDocument::Compact).constData() << '\n' << std::flush;
      last = result.preview_image;
    }
    QFile final_image(output.filePath("local-temperature.png"));
    if (!final_image.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !last.save(&final_image, "PNG")) return 1;
    const QVariantMap receipt{{"scope", "real-preview-repeated-local-temperature"},
        {"coverageEnabled", !uncached_coverage},
        {"renderingModel", rendering_model}, {"fullResolution", request.full_resolution},
        {"width", last.width()}, {"height", last.height()}, {"samples", samples}, {"passed", true}};
    QFile file(output.filePath("receipt.json"));
    const auto bytes = QJsonDocument::fromVariant(receipt).toJson();
    return file.open(QIODevice::WriteOnly | QIODevice::NewOnly) && file.write(bytes) == bytes.size() ? 0 : 1;
  }
  const auto external_source = runtime.filePath("external-source." + QFileInfo(request.file_path).suffix());
  if (!QFile::copy(request.file_path, external_source)) return 1;
  QVariantList stages;
  QImage before;
  int maximum_error = 0;
  qint64 changed_pixels = 0;
  const QStringList names{"before", "after", "compare-reference", "snapshot-reference",
      "external-reference", "snapshot-different-decode", "warm-snapshot-reference"};
  for (int pass = 0; pass < names.size(); ++pass) {
    if (pass != 6) EditorPreviewControllerTestAccess::clear();
    if (pass == 1)
      request.state.mask_stack = {QVariantMap{{"id", "transparent-preview-mask"}, {"kind", "background"},
          {"enabled", true}, {"operation", "add"}, {"temperature", 12.0}, {"opacity", 1.0},
          {"density", 1.0}, {"feather", 0.0}, {"segmentationWidth", 16}, {"segmentationHeight", 16},
          {"segmentationData", QByteArray(256, '\0')}}};
    if (pass >= 2) {
      request.compare_mode = true;
      request.state.exposure = 1.0;
    }
    if (pass == 3) {
      request.has_comparison_reference_state = true;
      request.comparison_reference_state = EditState{};
      request.comparison_reference_state.photographic_rendering_model = rendering_model;
    }
    if (pass == 4) {
      request.has_comparison_reference_state = false;
      request.reference_file_path = external_source;
      request.state.raw_input_transform_enabled = false;
      request.state.raw_white_balance_override = true;
      request.state.raw_white_balance_temperature = 3200;
    }
    if (pass == 5) {
      request.reference_file_path.clear();
      request.has_comparison_reference_state = true;
    }
    if (pass == 6) request.state.exposure = 1.5;
    const auto result = render_preview_request_for_test(request);
    const auto image = pass < 2 ? result.preview_image : result.reference_preview_image;
    stages.append(QVariantMap{{"pass", pass}, {"case", names[pass]}, {"success", result.success}, {"decodeBackend", result.decode_backend},
        {"backend", result.backend}, {"fallbackReason", result.fallback_reason},
        {"gpuDispatches", result.adjustment_gpu_kernel_dispatch_count}, {"renderMs", result.total_elapsed_ms},
        {"decodeMs", result.decode_elapsed_ms}, {"editMs", result.edit_elapsed_ms},
        {"linearMs", result.adjustment_linear_stage_elapsed_ms},
        {"pipelineMs", result.adjustment_pipeline_elapsed_ms},
        {"outputMs", result.adjustment_output_elapsed_ms},
        {"outputPackMs", result.output_linear_pack_elapsed_ms},
        {"outputConvertMs", result.output_color_convert_elapsed_ms},
        {"histogramMs", result.histogram_scope_elapsed_ms},
        {"fastGlobalMs", result.adjustment_fast_global_ms},
        {"toneMaskMs", result.adjustment_tone_mask_ms},
        {"postColorMs", result.adjustment_post_color_ms},
        {"detailMs", result.adjustment_detail_ms},
        {"decodedCacheHit", result.decoded_cache_hit},
        {"failureStage", result.failure_stage}, {"failureDetail", result.failure_detail}});
    if (!result.success || image.isNull()) {
      std::cerr << QJsonDocument::fromVariant(stages).toJson().constData(); return 1;
    }
    if (pass >= 2 && (result.preview_image.isNull() || result.preview_image == before)) {
      std::cerr << "The comparison's edited image must actually change with exposure\n"; return 1;
    }
    QFile image_file(output.filePath(names[pass] + ".png"));
    if (!image_file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !image.save(&image_file, "PNG")) return 1;
    if (pass == 0) { before = image; continue; }
    if (before.size() != image.size()) return 1;
    qint64 case_changed = 0;
    int case_maximum = 0;
    for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) {
      const auto a = before.pixelColor(x, y), b = image.pixelColor(x, y);
      const auto difference = std::max({std::abs(a.red() - b.red()), std::abs(a.green() - b.green()),
          std::abs(a.blue() - b.blue()), std::abs(a.alpha() - b.alpha())});
      maximum_error = std::max(maximum_error, difference);
      changed_pixels += difference != 0;
      case_changed += difference != 0;
      case_maximum = std::max(case_maximum, difference);
    }
    auto stage = stages.last().toMap();
    stage["changedPixels"] = case_changed; stage["maximumRgba8Difference"] = case_maximum;
    stages.last() = stage;
  }
  request.has_comparison_reference_state = false;
  request.reference_file_path = runtime.filePath("missing-reference.dng");
  const auto missing = render_preview_request_for_test(request);
  if (missing.success || !missing.failure_stage.startsWith("reference_")) {
    std::cerr << "Missing references must fail closed at the reference boundary\n"; return 1;
  }
  request.reference_file_path.clear();
  request.has_comparison_reference_state = true;
  request.use_in_memory_preview = false;
  QString first_reference_path;
  QImage first_reference;
  for (int pass = 0; pass < 3; ++pass) {
    request.comparison_reference_state.exposure = pass == 1 ? 0.75 : 0.25;
    const auto disk = render_preview_request_for_test(request);
    const auto path = QUrl(disk.reference_preview_source).toLocalFile();
    const QImage reference(path);
    if (!disk.success || reference.isNull()) {
      std::cerr << "Disk reference rendering must produce a readable image\n"; return 1;
    }
    if (pass == 0) { first_reference_path = path; first_reference = reference; }
    if ((pass == 1 && (path == first_reference_path || reference == first_reference)) ||
        (pass == 2 && (path != first_reference_path || reference != first_reference))) {
      std::cerr << "Reference disk cache must distinguish full snapshot recipes and reuse the matching one\n"; return 1;
    }
  }
  // A real local adjustment, not only a zero-coverage control. The rightmost
  // quarter is safely outside the bilinear transition of this half-image mask.
  request.compare_mode = false;
  request.has_comparison_reference_state = false;
  request.use_in_memory_preview = true;
  request.state = EditState{};
  request.state.photographic_rendering_model = rendering_model;
  QByteArray local_alpha(256, '\0');
  for (int y = 0; y < 16; ++y) for (int x = 0; x < 8; ++x)
    local_alpha[y * 16 + x] = char(0xff);
  request.state.mask_stack = {QVariantMap{{"id", "local-temperature"}, {"kind", "background"},
      {"enabled", true}, {"operation", "add"}, {"temperature", 12.0}, {"opacity", 1.0},
      {"density", 1.0}, {"feather", 0.0}, {"segmentationWidth", 16}, {"segmentationHeight", 16},
      {"segmentationData", local_alpha}}};
  EditorPreviewControllerTestAccess::clear();
  const auto local = render_preview_request_for_test(request);
  if (!local.success || local.preview_image.size() != before.size() || local.preview_image == before) return 1;
  qint64 protected_changed = 0;
  for (int y = 0; y < before.height(); ++y) for (int x = before.width() * 3 / 4; x < before.width(); ++x)
    protected_changed += local.preview_image.pixelColor(x, y) != before.pixelColor(x, y);
  if (protected_changed != 0) {
    std::cerr << "The local temperature edit changed protected pixels\n"; return 1;
  }
  QFile local_file(output.filePath("local-temperature.png"));
  if (!local_file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || !local.preview_image.save(&local_file, "PNG")) return 1;
  const QVariantMap local_proof{{"renderMs", local.total_elapsed_ms},
      {"pipelineMs", local.adjustment_pipeline_elapsed_ms}, {"outputMs", local.adjustment_output_elapsed_ms},
      {"decodeMs", local.decode_elapsed_ms}, {"backend", local.backend},
      {"protectedChangedPixels", protected_changed}};
  const QVariantMap proof{{"scope", "preview-zero-alpha-mask-and-independent-reference"}, {"stages", stages},
      {"localTemperature", local_proof},
      {"renderingModel", rendering_model}, {"fullResolution", request.full_resolution},
      {"missingReferenceRejected", true}, {"missingReferenceFailureStage", missing.failure_stage},
      {"diskReferenceRecipeIsolationPassed", true},
      {"width", before.width()}, {"height", before.height()}, {"changedPixels", changed_pixels},
      {"maximumRgba8Difference", maximum_error}, {"passed", changed_pixels == 0}};
  const auto bytes = QJsonDocument::fromVariant(proof).toJson();
  QFile receipt(output.filePath("receipt.json"));
  if (!receipt.open(QIODevice::WriteOnly | QIODevice::NewOnly) || receipt.write(bytes) != bytes.size()) return 1;
  std::cout << bytes.constData();
  return changed_pixels == 0 ? 0 : 1;
}
