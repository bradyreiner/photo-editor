#include "library_original_image_provider.h"
#include "render_plan_store.h"

#include <QFileInfo>
#include <QMutexLocker>
#include <QSemaphore>
#include <QUrl>
#include <QUrlQuery>

#include "edit_recipe_state.h"
#include "linked_image_source.h"
#include "photo_render_utils.h"
#include "qimage_color_conversion.h"
#include "raw_decode_policy.h"
#include "raw_image_decoder.h"
#include "source_image_decode.h"

namespace photo_editor::desktop {
namespace {

constexpr qint64 kMaximumCachedBytes = 192LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumCachedImages = 3;
constexpr int kMaximumConcurrentDecodes = 2;

QSemaphore& decode_slots() {
  static QSemaphore decode_semaphore(kMaximumConcurrentDecodes);
  return decode_semaphore;
}

class DecodePermit final {
 public:
  explicit DecodePermit(QSemaphore& semaphore) : semaphore_(semaphore) {
    semaphore_.acquire();
  }
  ~DecodePermit() { semaphore_.release(); }

 private:
  QSemaphore& semaphore_;
};

}  // namespace

LibraryOriginalImageProvider::LibraryOriginalImageProvider()
    : QQuickImageProvider(
          QQuickImageProvider::Image,
          QQmlImageProviderBase::ForceAsynchronousImageLoading) {}

QImage LibraryOriginalImageProvider::requestImage(
    const QString& id,
    QSize* size,
    const QSize& requested_size) {
  Q_UNUSED(requested_size);
  const auto file_path = decoded_path(id);
  if (file_path.isEmpty()) {
    if (size != nullptr) *size = {};
    return {};
  }

  const auto key = revision_key(file_path);
  if (key.isEmpty()) {
    if (size != nullptr) *size = {};
    return {};
  }

  const auto find_cached_image = [this, &key, size]() {
    QMutexLocker locker(&mutex_);
    const auto iterator = cache_.constFind(key);
    if (iterator != cache_.cend()) {
      const auto image = iterator->image;
      most_recent_first_.removeAll(key);
      most_recent_first_.prepend(key);
      if (size != nullptr) *size = image.size();
      return image;
    }
    return QImage{};
  };
  auto cached_image = find_cached_image();
  if (!cached_image.isNull()) {
    return cached_image;
  }
  const DecodePermit decode_permit(decode_slots());
  cached_image = find_cached_image();
  if (!cached_image.isNull()) return cached_image;

  const EditRecipeState neutral_state;
  auto source = QImage{};
  auto metadata = photo_editor::pipeline::DecodedImageMetadata{};
  const auto is_openexr = file_path.endsWith(QStringLiteral(".exr"), Qt::CaseInsensitive);
  const auto decoded = raw_image_supported(file_path)
      ? decode_raw_linear_image(
            file_path, &source, 0, raw_decode_settings_for_state(neutral_state), &metadata)
      : decode_source_image(
            file_path, &source, is_openexr ? 0 : -1, neutral_state, &metadata);
  if (!decoded || source.isNull()) {
    if (size != nullptr) *size = {};
    return {};
  }

  const auto scene_linear = metadata.is_raw || is_openexr;
  auto display_image = scene_linear
      ? QImage{}
      : convert_tagged_qimage_color_space(source, QColorSpace(QColorSpace::SRgb),
            QImage::Format_ARGB32, QImageColorConversionPurpose::OutputPublication);
  if (scene_linear) {
    const auto admission = RenderPlanStore::shared().admit(
        QStringLiteral("library-original:%1").arg(key),
        file_path,
        neutral_state,
        source.size(),
        photo_editor::pipeline::RenderPurpose::Thumbnail,
        photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
        std::nullopt,
        &metadata);
    if (admission.admitted && admission.plan.has_value()) {
      display_image = render_output_image(
          source,
          neutral_state,
          QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32,
          &metadata,
          nullptr,
          {},
          photo_editor::pipeline::RemovalQuality::Final,
          RenderOutputPolicy::DisplayReferred,
          &*admission.plan);
    }
  }
  if (display_image.isNull()) {
    if (size != nullptr) *size = {};
    return {};
  }
  display_image.setDevicePixelRatio(1.0);
  if (size != nullptr) *size = display_image.size();
  cacheImage(key, display_image);
  return display_image;
}

QString LibraryOriginalImageProvider::sourceUrl(const QString& file_path) {
  if (file_path.trimmed().isEmpty()) return {};
  const auto normalized_path = QFileInfo(file_path).absoluteFilePath();
  const auto encoded_path = QString::fromLatin1(QUrl::toPercentEncoding(normalized_path));
  return QStringLiteral("image://library-original/source?path=") + encoded_path +
         QStringLiteral("&revision=") + QString::fromLatin1(QUrl::toPercentEncoding(
             linked_image_file_revision(normalized_path)));
}

QString LibraryOriginalImageProvider::decoded_path(const QString& id) {
  const QUrl request_url(QStringLiteral("image://library-original/") + id);
  const QUrlQuery query(request_url);
  const auto path = query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded).trimmed();
  return path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}

QString LibraryOriginalImageProvider::revision_key(const QString& file_path) {
  const QFileInfo info(file_path);
  if (!info.exists() || !info.isFile() || !info.isReadable()) return {};
  const auto canonical_path = info.canonicalFilePath();
  const auto stable_path = canonical_path.isEmpty() ? info.absoluteFilePath() : canonical_path;
  return stable_path + QLatin1Char('|') + linked_image_file_revision(stable_path);
}

void LibraryOriginalImageProvider::cacheImage(const QString& key, const QImage& image) {
  const auto bytes = static_cast<qint64>(image.sizeInBytes());
  if (key.isEmpty() || image.isNull() || bytes <= 0 || bytes > kMaximumCachedBytes) return;

  QMutexLocker locker(&mutex_);
  if (cache_.contains(key)) {
    cached_bytes_ -= cache_.value(key).bytes;
    cache_.remove(key);
    most_recent_first_.removeAll(key);
  }
  while (!most_recent_first_.isEmpty() &&
         (cache_.size() >= kMaximumCachedImages || cached_bytes_ + bytes > kMaximumCachedBytes)) {
    const auto oldest_key = most_recent_first_.takeLast();
    const auto oldest = cache_.take(oldest_key);
    cached_bytes_ -= oldest.bytes;
  }
  cache_.insert(key, CacheEntry{image, bytes});
  most_recent_first_.prepend(key);
  cached_bytes_ += bytes;
}

}  // namespace photo_editor::desktop
