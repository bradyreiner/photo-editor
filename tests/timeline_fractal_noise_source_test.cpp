#include "timeline_fractal_noise_source.h"

#include <QGuiApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>

namespace {

using photo_editor::desktop::timeline_motion::FractalNoiseImageProvider;
using photo_editor::desktop::timeline_motion::FractalNoiseParameters;
using photo_editor::desktop::timeline_motion::ensure_fractal_noise_image_cached;
using photo_editor::desktop::timeline_motion::fractal_noise_image_provider_id;
using photo_editor::desktop::timeline_motion::fractal_noise_parameters_from_variant_map;
using photo_editor::desktop::timeline_motion::parse_fractal_noise_image_provider_id;
using photo_editor::desktop::timeline_motion::render_fractal_noise_frame;
using photo_editor::desktop::timeline_motion::resolved_fractal_noise_output_size;
using photo_editor::desktop::timeline_motion::validate_fractal_noise_output_size;

bool images_equal(const QImage& left, const QImage& right) {
  if (left.size() != right.size() || left.format() != right.format()) return false;
  for (int y = 0; y < left.height(); ++y) {
    const auto* left_row = left.constScanLine(y);
    const auto* right_row = right.constScanLine(y);
    for (int x = 0; x < left.bytesPerLine(); ++x) {
      if (left_row[x] != right_row[x]) return false;
    }
  }
  return true;
}

QString malformed_id(const QJsonObject& object) {
  const auto encoded = QJsonDocument(object).toJson(QJsonDocument::Compact)
                           .toBase64(QByteArray::Base64UrlEncoding |
                                     QByteArray::OmitTrailingEquals);
  return QStringLiteral("v1/") + QString::fromLatin1(encoded);
}

bool corrupt_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadWrite) || file.size() < 16) return false;
  const auto offset = file.size() / 2;
  if (!file.seek(offset)) return false;
  const auto old_byte = file.read(1);
  if (old_byte.size() != 1 || !file.seek(offset)) return false;
  const auto changed = static_cast<char>(old_byte.at(0) ^ 0x5a);
  return file.write(&changed, 1) == 1 && file.flush();
}

bool test_url_round_trip_and_strict_validation() {
  const QSize size(48, 32);
  const FractalNoiseParameters parameters;
  const auto id = fractal_noise_image_provider_id(size, parameters);
  if (id.isEmpty()) {
    std::cerr << "A valid native noise request should produce a provider id.\n";
    return false;
  }
  QSize decoded_size;
  FractalNoiseParameters decoded_parameters;
  QString error;
  if (!parse_fractal_noise_image_provider_id(id, &decoded_size,
                                              &decoded_parameters, &error) ||
      decoded_size != size ||
      decoded_parameters.base_frequency_x != parameters.base_frequency_x ||
      decoded_parameters.base_frequency_y != parameters.base_frequency_y ||
      decoded_parameters.num_octaves != parameters.num_octaves ||
      decoded_parameters.seed != parameters.seed ||
      decoded_parameters.stitch_tiles != parameters.stitch_tiles) {
    std::cerr << "The typed provider id did not round-trip exactly: "
              << error.toStdString() << "\n";
    return false;
  }

  const QJsonObject missing_field{
      {QStringLiteral("height"), size.height()},
      {QStringLiteral("numOctaves"), 3},
      {QStringLiteral("seed"), 19},
      {QStringLiteral("stitchTiles"), true},
      {QStringLiteral("version"), 1},
      {QStringLiteral("width"), size.width()},
  };
  error.clear();
  if (parse_fractal_noise_image_provider_id(
          malformed_id(missing_field), &decoded_size, &decoded_parameters,
          &error) || error.isEmpty()) {
    std::cerr << "Provider ids missing typed parameters must fail explicitly.\n";
    return false;
  }

  auto string_frequency = QJsonObject{
      {QStringLiteral("baseFrequencyX"), QStringLiteral("0.78")},
      {QStringLiteral("baseFrequencyY"), 0.78},
      {QStringLiteral("height"), size.height()},
      {QStringLiteral("numOctaves"), 3},
      {QStringLiteral("seed"), 19},
      {QStringLiteral("stitchTiles"), true},
      {QStringLiteral("version"), 1},
      {QStringLiteral("width"), size.width()},
  };
  error.clear();
  if (parse_fractal_noise_image_provider_id(
          malformed_id(string_frequency), &decoded_size, &decoded_parameters,
          &error) || error.isEmpty()) {
    std::cerr << "String frequencies must not silently default to a valid value.\n";
    return false;
  }
  return true;
}

bool test_canonical_variant_map_boundary() {
  FractalNoiseParameters parameters;
  QVariantMap canonical;
  QString error;
  if (!fractal_noise_parameters_from_variant_map(
          {}, &parameters, &canonical, &error) ||
      canonical.value(QStringLiteral("baseFrequencyX")).toDouble() != 0.78 ||
      canonical.value(QStringLiteral("baseFrequencyY")).toDouble() != 0.78 ||
      canonical.value(QStringLiteral("numOctaves")).toInt() != 3 ||
      canonical.value(QStringLiteral("seed")).toLongLong() != 19 ||
      !canonical.value(QStringLiteral("stitchTiles")).toBool() ||
      canonical.contains(QStringLiteral("outputWidth")) ||
      canonical.contains(QStringLiteral("outputHeight"))) {
    std::cerr << "Missing generator settings should canonicalize to the source defaults.\n";
    return false;
  }

  QVariantMap values{{QStringLiteral("baseFrequencyX"), 0.5},
                     {QStringLiteral("numOctaves"), 4.0},
                     {QStringLiteral("seed"), 21.0},
                     {QStringLiteral("stitchTiles"), false}};
  if (!fractal_noise_parameters_from_variant_map(
          values, &parameters, &canonical, &error) ||
      parameters.base_frequency_x != 0.5 ||
      parameters.base_frequency_y != 0.78 || parameters.num_octaves != 4 ||
      parameters.seed != 21 || parameters.stitch_tiles ||
      canonical.value(QStringLiteral("baseFrequencyY")).toDouble() != 0.78) {
    std::cerr << "The shared boundary should validate and canonicalize partial valid settings.\n";
    return false;
  }

  values.insert(QStringLiteral("unknown"), 1.0);
  if (fractal_noise_parameters_from_variant_map(values, &parameters,
                                                &canonical, &error) ||
      error.isEmpty()) {
    std::cerr << "Unknown native noise settings must be rejected.\n";
    return false;
  }
  values.remove(QStringLiteral("unknown"));
  values.insert(QStringLiteral("seed"), QStringLiteral("21"));
  if (fractal_noise_parameters_from_variant_map(values, &parameters,
                                                &canonical, &error) ||
      error.isEmpty()) {
    std::cerr << "A string seed must not silently convert to an integer.\n";
    return false;
  }
  values.insert(QStringLiteral("seed"), 21.5);
  if (fractal_noise_parameters_from_variant_map(values, &parameters,
                                                &canonical, &error) ||
      error.isEmpty()) {
    std::cerr << "A fractional seed must be rejected.\n";
    return false;
  }
  values.insert(QStringLiteral("seed"), 21.0);
  values.insert(QStringLiteral("stitchTiles"), 1);
  if (fractal_noise_parameters_from_variant_map(values, &parameters,
                                                &canonical, &error) ||
      error.isEmpty()) {
    std::cerr << "A numeric stitchTiles value must not coerce to boolean.\n";
    return false;
  }
  return true;
}

bool test_requested_output_size_boundary_and_cache_identity() {
  const QSize fallback_size(1920, 1080);
  FractalNoiseParameters parameters;
  QVariantMap canonical;
  QString error;
  QVariantMap values{{QStringLiteral("outputWidth"), 1000},
                     {QStringLiteral("outputHeight"), 1250}};
  if (!fractal_noise_parameters_from_variant_map(
          values, &parameters, &canonical, &error) ||
      parameters.output_width != 1000 || parameters.output_height != 1250 ||
      canonical.value(QStringLiteral("outputWidth")).toInt() != 1000 ||
      canonical.value(QStringLiteral("outputHeight")).toInt() != 1250 ||
      resolved_fractal_noise_output_size(fallback_size, parameters) !=
          QSize(1000, 1250) ||
      !validate_fractal_noise_output_size(QSize(1000, 1250), parameters).isEmpty() ||
      validate_fractal_noise_output_size(QSize(1001, 1250), parameters).isEmpty()) {
    std::cerr << "A requested local noise size should be canonical and enforced.\n";
    return false;
  }

  const auto requested_id =
      fractal_noise_image_provider_id(QSize(1000, 1250), parameters);
  QSize decoded_requested_size;
  FractalNoiseParameters decoded_requested_parameters;
  if (requested_id.isEmpty() ||
      !parse_fractal_noise_image_provider_id(
          requested_id, &decoded_requested_size, &decoded_requested_parameters,
          &error) ||
      decoded_requested_size != QSize(1000, 1250) ||
      !fractal_noise_image_provider_id(QSize(1920, 1080), parameters).isEmpty()) {
    std::cerr << "Provider IDs must include and enforce the requested output dimensions.\n";
    return false;
  }

  const auto reject_values = [&parameters, &canonical, &error](
                                 const QVariantMap& invalid,
                                 const char* description) {
    error.clear();
    if (fractal_noise_parameters_from_variant_map(
            invalid, &parameters, &canonical, &error) || error.isEmpty()) {
      std::cerr << description << " must be rejected explicitly.\n";
      return false;
    }
    return true;
  };
  if (!reject_values({{QStringLiteral("outputWidth"), 1000}},
                     "A width without a height") ||
      !reject_values({{QStringLiteral("outputWidth"), 1000.5},
                      {QStringLiteral("outputHeight"), 1250}},
                     "A fractional output dimension") ||
      !reject_values({{QStringLiteral("outputWidth"), QStringLiteral("1000")},
                      {QStringLiteral("outputHeight"), 1250}},
                     "A string output dimension") ||
      !reject_values({{QStringLiteral("outputWidth"), 0},
                      {QStringLiteral("outputHeight"), 1250}},
                     "A zero output dimension") ||
      !reject_values({{QStringLiteral("outputWidth"), 16385},
                      {QStringLiteral("outputHeight"), 1}},
                     "An output dimension above its limit") ||
      !reject_values({{QStringLiteral("outputWidth"), 10000},
                      {QStringLiteral("outputHeight"), 10000}},
                     "A request exceeding the native pixel budget")) {
    return false;
  }

  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid()) return false;
  const QSize rendered_size(32, 20);
  FractalNoiseParameters local_parameters;
  local_parameters.output_width = rendered_size.width();
  local_parameters.output_height = rendered_size.height();
  const auto local_cache = ensure_fractal_noise_image_cached(
      temporary_directory.path(), rendered_size, local_parameters);
  const auto legacy_cache = ensure_fractal_noise_image_cached(
      temporary_directory.path(), rendered_size, FractalNoiseParameters{});
  const auto mismatched_cache = ensure_fractal_noise_image_cached(
      temporary_directory.path(), QSize(33, 20), local_parameters);
  if (!local_cache.ok || !legacy_cache.ok ||
      local_cache.cache_key != legacy_cache.cache_key ||
      mismatched_cache.ok || mismatched_cache.error.isEmpty()) {
    std::cerr << "The cache should key the actual size, preserve default identity, and reject size mismatches.\n";
    return false;
  }
  return true;
}

bool test_static_cache_reuse_and_corruption_recovery() {
  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid()) {
    std::cerr << "Cannot create a temporary fractal-noise cache directory.\n";
    return false;
  }
  const QSize size(48, 32);
  const FractalNoiseParameters parameters;
  const auto first = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, parameters);
  const auto canonical_root = QFileInfo(temporary_directory.path()).canonicalFilePath();
  const auto expected_cache_directory = QDir(canonical_root).filePath(
      QStringLiteral("timeline-fractal-noise/v1"));
  if (!first.ok || first.cache_hit || first.image_path.isEmpty() ||
      first.image_size != size || first.encoded_bytes <= 0 ||
      !QFileInfo::exists(first.image_path) ||
      QDir::cleanPath(QFileInfo(first.image_path).absolutePath()) !=
          QDir::cleanPath(expected_cache_directory)) {
    std::cerr << "The native still cache did not publish inside its owned root: "
              << first.error.toStdString() << "\n";
    return false;
  }
  QString render_error;
  const auto expected = render_fractal_noise_frame(size, parameters, &render_error);
  if (expected.isNull() || !images_equal(first.image, expected)) {
    std::cerr << "The cached still must be the exact output of the native renderer.\n";
    return false;
  }

  const auto second = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, parameters);
  if (!second.ok || !second.cache_hit || !images_equal(first.image, second.image)) {
    std::cerr << "A valid static noise image should be reused after checksum validation.\n";
    return false;
  }
  auto changed_seed = parameters;
  ++changed_seed.seed;
  const auto changed_seed_image = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, changed_seed);
  const auto changed_size_image = ensure_fractal_noise_image_cached(
      temporary_directory.path(), QSize(size.width() + 1, size.height()), parameters);
  if (!changed_seed_image.ok || !changed_size_image.ok ||
      changed_seed_image.cache_key == first.cache_key ||
      changed_size_image.cache_key == first.cache_key) {
    std::cerr << "The static cache key must include typed parameters and output dimensions.\n";
    return false;
  }

  if (!corrupt_file(second.image_path)) {
    std::cerr << "Cannot corrupt the cached PNG for the checksum recovery check.\n";
    return false;
  }
  const auto after_png_corruption = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, parameters);
  if (!after_png_corruption.ok || after_png_corruption.cache_hit ||
      !images_equal(expected, after_png_corruption.image)) {
    std::cerr << "A modified PNG must be detected and regenerated.\n";
    return false;
  }

  if (!corrupt_file(QDir(temporary_directory.path()).filePath(
          QStringLiteral("timeline-fractal-noise/v1/%1.json")
              .arg(after_png_corruption.cache_key)))) {
    std::cerr << "Cannot corrupt the cache manifest for checksum recovery.\n";
    return false;
  }
  const auto after_manifest_corruption = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, parameters);
  if (!after_manifest_corruption.ok || after_manifest_corruption.cache_hit ||
      !images_equal(expected, after_manifest_corruption.image)) {
    std::cerr << "A modified manifest must be detected and regenerated.\n";
    return false;
  }
  return true;
}

bool test_cancel_and_provider_malformed_request() {
  QTemporaryDir temporary_directory;
  if (!temporary_directory.isValid()) return false;
  const QSize size(48, 32);
  const FractalNoiseParameters parameters;
  const auto cancelled = ensure_fractal_noise_image_cached(
      temporary_directory.path(), size, parameters, []() { return true; });
  if (cancelled.ok || cancelled.error.isEmpty() ||
      !QDir(temporary_directory.path())
           .entryList(QDir::Files | QDir::NoDotAndDotDot).isEmpty()) {
    std::cerr << "A cancelled cache request must stop without publishing a still.\n";
    return false;
  }

  FractalNoiseImageProvider provider(temporary_directory.path());
  const auto valid_id = fractal_noise_image_provider_id(size, parameters);
  auto* valid_response = provider.requestImageResponse(valid_id, size);
  QEventLoop valid_loop;
  QObject::connect(valid_response, &QQuickImageResponse::finished,
                   &valid_loop, &QEventLoop::quit);
  QTimer::singleShot(5000, &valid_loop, &QEventLoop::quit);
  valid_loop.exec();
  auto* texture_factory = valid_response->textureFactory();
  if (texture_factory == nullptr ||
      texture_factory->image().size() != size ||
      !valid_response->errorString().isEmpty()) {
    delete texture_factory;
    delete valid_response;
    std::cerr << "The asynchronous provider should return its cached native image.\n";
    return false;
  }
  delete texture_factory;
  delete valid_response;

  auto* response = provider.requestImageResponse(QStringLiteral("v1/../external"), QSize{});
  if (response == nullptr) {
    std::cerr << "The asynchronous provider should return an error response for malformed ids.\n";
    return false;
  }
  QEventLoop error_loop;
  QObject::connect(response, &QQuickImageResponse::finished,
                   &error_loop, &QEventLoop::quit);
  QTimer::singleShot(5000, &error_loop, &QEventLoop::quit);
  error_loop.exec();
  if (response == nullptr || response->textureFactory() != nullptr ||
      response->errorString().isEmpty()) {
    delete response;
    std::cerr << "A malformed provider request must fail with a null image.\n";
    return false;
  }
  delete response;

  const auto cancel_id = fractal_noise_image_provider_id(QSize(512, 256), parameters);
  auto* cancel_response = provider.requestImageResponse(cancel_id, QSize(512, 256));
  QEventLoop cancel_loop;
  QObject::connect(cancel_response, &QQuickImageResponse::finished,
                   &cancel_loop, &QEventLoop::quit);
  QTimer::singleShot(5000, &cancel_loop, &QEventLoop::quit);
  cancel_response->cancel();
  cancel_loop.exec();
  if (cancel_response->textureFactory() != nullptr || cancel_response->errorString().isEmpty()) {
    delete cancel_response;
    std::cerr << "A cancelled asynchronous source must finish without reading a missing future result.\n";
    return false;
  }
  delete cancel_response;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  Q_UNUSED(application);
  if (!test_url_round_trip_and_strict_validation() ||
      !test_canonical_variant_map_boundary() ||
      !test_requested_output_size_boundary_and_cache_identity() ||
      !test_static_cache_reuse_and_corruption_recovery() ||
      !test_cancel_and_provider_malformed_request()) {
    return 1;
  }
  return 0;
}
