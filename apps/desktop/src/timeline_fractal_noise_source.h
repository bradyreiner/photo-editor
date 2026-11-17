#pragma once

#include "timeline_fractal_noise.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>

namespace photo_editor::desktop::timeline_motion {

struct FractalNoiseImageCacheResult final {
  bool ok = false;
  bool cache_hit = false;
  QSize image_size;
  QString cache_key;
  QString image_path;
  QString error;
  QImage image;
  qint64 encoded_bytes = 0;
};

// Provider ids contain a versioned, base64url-encoded JSON object with the
// exact dimensions and typed fractal-noise parameters. Empty means invalid.
[[nodiscard]] QString fractal_noise_image_provider_id(
    const QSize& image_size,
    const FractalNoiseParameters& parameters);
[[nodiscard]] QString fractal_noise_image_provider_url(
    const QSize& image_size,
    const FractalNoiseParameters& parameters);
[[nodiscard]] bool parse_fractal_noise_image_provider_id(
    const QString& id,
    QSize* image_size,
    FractalNoiseParameters* parameters,
    QString* error_message = nullptr);

// Creates or validates one content-addressed still PNG under the trusted
// application cache root. The cache key includes image dimensions, typed
// parameters, and the renderer format version. Writes use QSaveFile and are
// revalidated by manifest checksum, PNG dimensions, and grayscale-channel
// checks before being returned.
[[nodiscard]] FractalNoiseImageCacheResult ensure_fractal_noise_image_cached(
    const QString& cache_root,
    const QSize& image_size,
    const FractalNoiseParameters& parameters,
    const std::function<bool()>& cancellation_check = {});

class FractalNoiseImageProvider final : public QQuickAsyncImageProvider {
 public:
  explicit FractalNoiseImageProvider(QString cache_root);

  QQuickImageResponse* requestImageResponse(
      const QString& id,
      const QSize& requested_size) override;

 private:
  QString cache_root_;
};

}  // namespace photo_editor::desktop::timeline_motion
