#include "timeline_fractal_noise_source.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QImageWriter>
#include <QLockFile>
#include <QSaveFile>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

#include <cmath>
#include <memory>
#include <utility>

namespace photo_editor::desktop::timeline_motion {
namespace {

constexpr int kProviderVersion = 1;
constexpr qint64 kMaximumCachePngBytes = 64LL * 1024LL * 1024LL;
constexpr qint64 kMaximumManifestBytes = 16LL * 1024LL;
constexpr int kCacheLockTimeoutMilliseconds = 30000;
const QString kProviderAuthority = QStringLiteral("timeline-fractal-noise");
const QString kCacheAlgorithmTag = QStringLiteral("looksy-svg-fractal-noise-v1");

struct ParsedRequest {
  QSize image_size;
  FractalNoiseParameters parameters;
};

struct CachePaths {
  QString cache_directory;
  QString image_path;
  QString manifest_path;
  QString lock_path;
};

QJsonObject request_json(const QSize& image_size,
                         const FractalNoiseParameters& parameters) {
  return {{QStringLiteral("baseFrequencyX"), parameters.base_frequency_x},
          {QStringLiteral("baseFrequencyY"), parameters.base_frequency_y},
          {QStringLiteral("height"), image_size.height()},
          {QStringLiteral("numOctaves"), parameters.num_octaves},
          {QStringLiteral("seed"), static_cast<double>(parameters.seed)},
          {QStringLiteral("stitchTiles"), parameters.stitch_tiles},
          {QStringLiteral("version"), kProviderVersion},
          {QStringLiteral("width"), image_size.width()}};
}

QString request_key(const QSize& image_size,
                    const FractalNoiseParameters& parameters) {
  const auto request = QJsonDocument(request_json(image_size, parameters))
                           .toJson(QJsonDocument::Compact);
  const auto cache_identity = kCacheAlgorithmTag.toUtf8() + QByteArrayLiteral("|") + request;
  return QString::fromLatin1(
      QCryptographicHash::hash(cache_identity, QCryptographicHash::Sha256).toHex());
}

bool read_integer(const QJsonValue& value,
                  qint64 minimum,
                  qint64 maximum,
                  qint64* output) {
  if (output == nullptr || !value.isDouble()) return false;
  const auto number = value.toDouble();
  if (!std::isfinite(number) || std::trunc(number) != number ||
      number < static_cast<double>(minimum) ||
      number > static_cast<double>(maximum)) {
    return false;
  }
  *output = static_cast<qint64>(number);
  return true;
}

bool parse_request(const QString& id,
                   ParsedRequest* parsed,
                   QString* error_message) {
  auto invalid = [error_message](const QString& message) {
    if (error_message != nullptr) *error_message = message;
    return false;
  };
  if (parsed == nullptr || id.isEmpty() || id.size() > 4096) {
    return invalid(QStringLiteral("The native fractal-noise image id is invalid."));
  }
  auto normalized_id = id;
  if (normalized_id.startsWith(QLatin1Char('/'))) normalized_id.remove(0, 1);
  const auto parts = normalized_id.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  if (parts.size() != 2 || parts.at(0) != QStringLiteral("v1") ||
      parts.at(1).isEmpty() || parts.at(1).contains(QLatin1Char('='))) {
    return invalid(QStringLiteral("The native fractal-noise image id has an unsupported version or encoding."));
  }
  const auto encoded = parts.at(1).toLatin1();
  const auto bytes = QByteArray::fromBase64(
      encoded, QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
  if (bytes.isEmpty()) {
    return invalid(QStringLiteral("The native fractal-noise image id is not valid base64url."));
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return invalid(QStringLiteral("The native fractal-noise image parameters are not valid JSON."));
  }
  const auto object = document.object();
  const QStringList required_keys{
      QStringLiteral("baseFrequencyX"), QStringLiteral("baseFrequencyY"),
      QStringLiteral("height"), QStringLiteral("numOctaves"),
      QStringLiteral("seed"), QStringLiteral("stitchTiles"),
      QStringLiteral("version"), QStringLiteral("width")};
  if (object.size() != required_keys.size()) {
    return invalid(QStringLiteral("The native fractal-noise image parameters are incomplete or contain unknown fields."));
  }
  for (const auto& key : required_keys) {
    if (!object.contains(key)) {
      return invalid(QStringLiteral("The native fractal-noise image parameters are incomplete or contain unknown fields."));
    }
  }

  qint64 version = 0;
  qint64 width = 0;
  qint64 height = 0;
  if (!read_integer(object.value(QStringLiteral("version")), kProviderVersion,
                    kProviderVersion, &version) || version != kProviderVersion ||
      !read_integer(object.value(QStringLiteral("width")), 1, 16384, &width) ||
      !read_integer(object.value(QStringLiteral("height")), 1, 16384, &height)) {
    return invalid(QStringLiteral("The native fractal-noise dimensions or version are out of range."));
  }
  QVariantMap parameter_map{
      {QStringLiteral("baseFrequencyX"),
       object.value(QStringLiteral("baseFrequencyX")).toVariant()},
      {QStringLiteral("baseFrequencyY"),
       object.value(QStringLiteral("baseFrequencyY")).toVariant()},
      {QStringLiteral("numOctaves"),
       object.value(QStringLiteral("numOctaves")).toVariant()},
      {QStringLiteral("seed"), object.value(QStringLiteral("seed")).toVariant()},
      {QStringLiteral("stitchTiles"),
       object.value(QStringLiteral("stitchTiles")).toVariant()},
  };
  FractalNoiseParameters parameters;
  QString parameter_error;
  if (!fractal_noise_parameters_from_variant_map(
          parameter_map, &parameters, nullptr, &parameter_error)) {
    return invalid(parameter_error);
  }
  const QSize image_size(static_cast<int>(width), static_cast<int>(height));
  const auto size_error = validate_fractal_noise_frame_size(image_size);
  if (!size_error.isEmpty()) {
    return invalid(size_error);
  }
  parsed->image_size = image_size;
  parsed->parameters = parameters;
  if (error_message != nullptr) error_message->clear();
  return true;
}

bool grayscale_rgba_image(const QImage& image) {
  if (image.isNull() || image.format() != QImage::Format_RGBA8888) return false;
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
      const auto offset = x * 4;
      if (row[offset] != row[offset + 1] || row[offset + 1] != row[offset + 2]) {
        return false;
      }
    }
  }
  return true;
}

bool cache_paths(const QString& cache_root,
                 const QString& key,
                 CachePaths* paths,
                 QString* error_message) {
  if (paths == nullptr || cache_root.trimmed().isEmpty() ||
      key.size() != 64) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache root or key is invalid.");
    }
    return false;
  }
  QDir root_directory(cache_root);
  if (!root_directory.mkpath(QStringLiteral("."))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache root cannot be created.");
    }
    return false;
  }
  const auto canonical_root = QFileInfo(root_directory.absolutePath()).canonicalFilePath();
  if (canonical_root.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache root cannot be resolved.");
    }
    return false;
  }
  const auto expected_directory = QDir(canonical_root).filePath(
      QStringLiteral("timeline-fractal-noise/v1"));
  if (!QDir().mkpath(expected_directory)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache directory cannot be created.");
    }
    return false;
  }
  const auto canonical_directory = QFileInfo(expected_directory).canonicalFilePath();
  if (canonical_directory.isEmpty() ||
      QDir::cleanPath(canonical_directory) != QDir::cleanPath(expected_directory)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache directory resolves outside its owned root.");
    }
    return false;
  }
  paths->cache_directory = canonical_directory;
  paths->image_path = QDir(canonical_directory).filePath(key + QStringLiteral(".png"));
  paths->manifest_path = QDir(canonical_directory).filePath(key + QStringLiteral(".json"));
  paths->lock_path = QDir(canonical_directory).filePath(key + QStringLiteral(".lock"));
  if (QFileInfo(paths->image_path).absolutePath() != canonical_directory ||
      QFileInfo(paths->manifest_path).absolutePath() != canonical_directory ||
      QFileInfo(paths->lock_path).absolutePath() != canonical_directory) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The native fractal-noise cache paths failed root validation.");
    }
    return false;
  }
  if (error_message != nullptr) error_message->clear();
  return true;
}

bool has_symlink_cache_file(const CachePaths& paths, QString* error_message) {
  for (const auto& path : {paths.image_path, paths.manifest_path, paths.lock_path}) {
    if (QFileInfo(path).isSymLink()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("A native fractal-noise cache path is a symbolic link.");
      }
      return true;
    }
  }
  return false;
}

QString file_sha256(const QString& path, QString* error_message) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("A native fractal-noise cache image cannot be read.");
    }
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto chunk = file.read(1024 * 1024);
    if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("A native fractal-noise cache image could not be checksummed.");
      }
      return {};
    }
    hash.addData(chunk);
  }
  if (error_message != nullptr) error_message->clear();
  return QString::fromLatin1(hash.result().toHex());
}

FractalNoiseImageCacheResult validate_cached_image(
    const CachePaths& paths,
    const QString& key,
    const QSize& expected_size) {
  FractalNoiseImageCacheResult result;
  result.cache_key = key;
  result.image_size = expected_size;
  const QFileInfo image_info(paths.image_path);
  const QFileInfo manifest_info(paths.manifest_path);
  if (!image_info.exists() && !manifest_info.exists()) return result;
  if (!image_info.isFile() || image_info.size() <= 0 ||
      image_info.size() > kMaximumCachePngBytes ||
      !manifest_info.isFile() || manifest_info.size() <= 0 ||
      manifest_info.size() > kMaximumManifestBytes) {
    return result;
  }

  QFile manifest_file(paths.manifest_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) return result;
  QJsonParseError parse_error;
  const auto manifest = QJsonDocument::fromJson(manifest_file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !manifest.isObject()) return result;
  const auto object = manifest.object();
  if (object.size() != 5 ||
      object.value(QStringLiteral("cacheKey")).toString() != key ||
      object.value(QStringLiteral("imageSha256")).toString().size() != 64 ||
      object.value(QStringLiteral("width")).toInt(-1) != expected_size.width() ||
      object.value(QStringLiteral("height")).toInt(-1) != expected_size.height() ||
      object.value(QStringLiteral("format")).toString() != QStringLiteral("rgba8888-png")) {
    return result;
  }
  QString hash_error;
  const auto actual_hash = file_sha256(paths.image_path, &hash_error);
  if (actual_hash.isEmpty() ||
      actual_hash != object.value(QStringLiteral("imageSha256")).toString()) {
    return result;
  }

  QImageReader reader(paths.image_path, "png");
  reader.setAutoTransform(false);
  if (reader.size() != expected_size) return result;
  const auto loaded = reader.read();
  if (loaded.isNull() || loaded.size() != expected_size) return result;
  const auto rgba = loaded.convertToFormat(QImage::Format_RGBA8888);
  if (!grayscale_rgba_image(rgba)) return result;
  result.ok = true;
  result.cache_hit = true;
  result.image_path = paths.image_path;
  result.image = rgba;
  result.encoded_bytes = image_info.size();
  return result;
}

bool wait_for_cache_lock(QLockFile& lock,
                         const std::function<bool()>& cancellation_check,
                         QString* error_message) {
  QElapsedTimer timer;
  timer.start();
  while (true) {
    if (cancellation_check && cancellation_check()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Native fractal-noise cache preparation was cancelled.");
      }
      return false;
    }
    if (lock.tryLock(50)) return true;
    if (lock.error() != QLockFile::LockFailedError) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("The native fractal-noise cache lock cannot be acquired.");
      }
      return false;
    }
    if (timer.elapsed() >= kCacheLockTimeoutMilliseconds) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Timed out waiting for the native fractal-noise cache lock.");
      }
      return false;
    }
  }
}

FractalNoiseImageCacheResult cache_failure(const QString& key,
                                           const QSize& image_size,
                                           const QString& message) {
  FractalNoiseImageCacheResult result;
  result.cache_key = key;
  result.image_size = image_size;
  result.error = message;
  return result;
}

class FractalNoiseImageResponse final : public QQuickImageResponse {
 public:
  explicit FractalNoiseImageResponse(QString cache_root)
      : cache_root_(std::move(cache_root)), cancelled_(std::make_shared<std::atomic_bool>(false)) {
    QObject::connect(&watcher_, &QFutureWatcher<FractalNoiseImageCacheResult>::finished,
                     this, [this]() {
                       const auto future = watcher_.future();
                       if (future.isCanceled() || future.resultCount() == 0) {
                         image_ = QImage{};
                         error_ = QStringLiteral("The fractal-noise image request was cancelled.");
                         emit finished();
                         return;
                       }
                       const auto result = future.result();
                       image_ = result.image;
                       error_ = result.error;
                       emit finished();
                     });
  }

  void start(const QString& id,
             const QSize& requested_size,
             const QString& immediate_error = {}) {
    if (!immediate_error.isEmpty()) {
      QTimer::singleShot(0, this, [this, immediate_error]() {
        error_ = immediate_error;
        emit finished();
      });
      return;
    }
    ParsedRequest parsed;
    QString parse_error;
    if (!parse_request(id, &parsed, &parse_error)) {
      QTimer::singleShot(0, this, [this, parse_error]() {
        error_ = parse_error;
        emit finished();
      });
      return;
    }
    if (!requested_size.isEmpty() && requested_size != parsed.image_size) {
      QTimer::singleShot(0, this, [this]() {
        error_ = QStringLiteral("The requested fractal-noise image size does not match its typed provider id.");
        emit finished();
      });
      return;
    }
    const auto cache_root = cache_root_;
    const auto cancellation = cancelled_;
    const auto image_size = parsed.image_size;
    const auto parameters = parsed.parameters;
    watcher_.setFuture(QtConcurrent::run([cache_root, image_size, parameters, cancellation]() {
      return ensure_fractal_noise_image_cached(
          cache_root, image_size, parameters,
          [cancellation]() { return cancellation->load(std::memory_order_relaxed); });
    }));
  }

  QQuickTextureFactory* textureFactory() const override {
    if (image_.isNull()) return nullptr;
    return QQuickTextureFactory::textureFactoryForImage(image_);
  }

  QString errorString() const override {
    return error_;
  }

  void cancel() override {
    cancelled_->store(true, std::memory_order_relaxed);
    watcher_.cancel();
  }

 private:
  QString cache_root_;
  QFutureWatcher<FractalNoiseImageCacheResult> watcher_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  QImage image_;
  QString error_;
};

}  // namespace

QString fractal_noise_image_provider_id(
    const QSize& image_size,
    const FractalNoiseParameters& parameters) {
  const auto size_error =
      validate_fractal_noise_output_size(image_size, parameters);
  if (!size_error.isEmpty() ||
      canonical_fractal_noise_parameters(parameters).isEmpty()) return {};
  const auto json = QJsonDocument(request_json(image_size, parameters))
                        .toJson(QJsonDocument::Compact);
  const auto encoded = json.toBase64(QByteArray::Base64UrlEncoding |
                                     QByteArray::OmitTrailingEquals);
  return QStringLiteral("v1/") + QString::fromLatin1(encoded);
}

QString fractal_noise_image_provider_url(
    const QSize& image_size,
    const FractalNoiseParameters& parameters) {
  const auto id = fractal_noise_image_provider_id(image_size, parameters);
  if (id.isEmpty()) return {};
  return QStringLiteral("image://%1/%2").arg(kProviderAuthority, id);
}

bool parse_fractal_noise_image_provider_id(
    const QString& id,
    QSize* image_size,
    FractalNoiseParameters* parameters,
    QString* error_message) {
  if (image_size == nullptr || parameters == nullptr) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Native fractal-noise id output values are required.");
    }
    return false;
  }
  ParsedRequest parsed;
  if (!parse_request(id, &parsed, error_message)) return false;
  *image_size = parsed.image_size;
  *parameters = parsed.parameters;
  if (error_message != nullptr) error_message->clear();
  return true;
}

FractalNoiseImageCacheResult ensure_fractal_noise_image_cached(
    const QString& cache_root,
    const QSize& image_size,
    const FractalNoiseParameters& parameters,
    const std::function<bool()>& cancellation_check) {
  const auto key = request_key(image_size, parameters);
  const auto size_error =
      validate_fractal_noise_output_size(image_size, parameters);
  const auto canonical_parameters = canonical_fractal_noise_parameters(parameters);
  if (!size_error.isEmpty() || canonical_parameters.isEmpty()) {
    return cache_failure(key, image_size,
                         !size_error.isEmpty()
                             ? size_error
                             : QStringLiteral("The native SVG noise parameters are invalid or cannot be represented exactly."));
  }
  if (cancellation_check && cancellation_check()) {
    return cache_failure(key, image_size,
                         QStringLiteral("Native fractal-noise cache preparation was cancelled."));
  }
  CachePaths paths;
  QString path_error;
  if (!cache_paths(cache_root, key, &paths, &path_error)) {
    return cache_failure(key, image_size, path_error);
  }
  if (has_symlink_cache_file(paths, &path_error)) {
    return cache_failure(key, image_size, path_error);
  }
  auto cached = validate_cached_image(paths, key, image_size);
  if (cached.ok) return cached;

  QLockFile lock(paths.lock_path);
  lock.setStaleLockTime(30000);
  if (!wait_for_cache_lock(lock, cancellation_check, &path_error)) {
    return cache_failure(key, image_size, path_error);
  }
  if (has_symlink_cache_file(paths, &path_error)) {
    return cache_failure(key, image_size, path_error);
  }
  cached = validate_cached_image(paths, key, image_size);
  if (cached.ok) return cached;

  QString render_error;
  auto image = render_fractal_noise_frame(
      image_size, parameters, &render_error, cancellation_check);
  if (image.isNull() || !grayscale_rgba_image(image)) {
    return cache_failure(key, image_size,
                         render_error.isEmpty()
                             ? QStringLiteral("The native fractal-noise renderer returned an invalid image.")
                             : render_error);
  }
  if (cancellation_check && cancellation_check()) {
    return cache_failure(key, image_size,
                         QStringLiteral("Native fractal-noise cache preparation was cancelled."));
  }

  QSaveFile image_file(paths.image_path);
  image_file.setDirectWriteFallback(false);
  if (!image_file.open(QIODevice::WriteOnly)) {
    return cache_failure(key, image_size,
                         QStringLiteral("The native fractal-noise PNG cache cannot be opened for writing."));
  }
  QImageWriter writer(&image_file, "png");
  writer.setCompression(25);
  if (!writer.write(image) ||
      image_file.size() > kMaximumCachePngBytes ||
      (cancellation_check && cancellation_check())) {
    image_file.cancelWriting();
    return cache_failure(key, image_size,
                         cancellation_check && cancellation_check()
                             ? QStringLiteral("Native fractal-noise cache preparation was cancelled.")
                             : QStringLiteral("The native fractal-noise PNG exceeds its cache limit or could not be encoded."));
  }
  if (!image_file.commit()) {
    return cache_failure(key, image_size,
                         QStringLiteral("The native fractal-noise PNG could not be atomically published."));
  }
  const QFileInfo image_info(paths.image_path);
  if (!image_info.isFile() || image_info.isSymLink() || image_info.size() <= 0 ||
      image_info.size() > kMaximumCachePngBytes) {
    QFile::remove(paths.image_path);
    return cache_failure(key, image_size,
                         QStringLiteral("The native fractal-noise PNG failed its encoded-size check."));
  }
  QString checksum_error;
  const auto checksum = file_sha256(paths.image_path, &checksum_error);
  if (checksum.isEmpty()) {
    QFile::remove(paths.image_path);
    return cache_failure(key, image_size, checksum_error);
  }
  const auto manifest = QJsonDocument(QJsonObject{
      {QStringLiteral("cacheKey"), key},
      {QStringLiteral("format"), QStringLiteral("rgba8888-png")},
      {QStringLiteral("height"), image_size.height()},
      {QStringLiteral("imageSha256"), checksum},
      {QStringLiteral("width"), image_size.width()},
  }).toJson(QJsonDocument::Compact);
  QSaveFile manifest_file(paths.manifest_path);
  manifest_file.setDirectWriteFallback(false);
  if (!manifest_file.open(QIODevice::WriteOnly) ||
      manifest_file.write(manifest) != manifest.size() ||
      (cancellation_check && cancellation_check()) ||
      !manifest_file.commit()) {
    manifest_file.cancelWriting();
    QFile::remove(paths.image_path);
    return cache_failure(key, image_size,
                         cancellation_check && cancellation_check()
                             ? QStringLiteral("Native fractal-noise cache preparation was cancelled.")
                             : QStringLiteral("The native fractal-noise cache manifest could not be atomically published."));
  }
  auto published = validate_cached_image(paths, key, image_size);
  if (!published.ok) {
    QFile::remove(paths.manifest_path);
    QFile::remove(paths.image_path);
    return cache_failure(key, image_size,
                         QStringLiteral("The native fractal-noise cache failed post-publication verification."));
  }
  published.cache_hit = false;
  return published;
}

FractalNoiseImageProvider::FractalNoiseImageProvider(QString cache_root)
    : cache_root_(std::move(cache_root)) {}

QQuickImageResponse* FractalNoiseImageProvider::requestImageResponse(
    const QString& id,
    const QSize& requested_size) {
  auto* response = new FractalNoiseImageResponse(cache_root_);
  QString error;
  ParsedRequest parsed;
  if (!parse_request(id, &parsed, &error)) {
    response->start(id, requested_size, error);
  } else if (!requested_size.isEmpty() && requested_size != parsed.image_size) {
    response->start(id, requested_size,
                    QStringLiteral("The requested fractal-noise image size does not match its typed provider id."));
  } else {
    response->start(id, requested_size);
  }
  return response;
}

}  // namespace photo_editor::desktop::timeline_motion
