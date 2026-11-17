#pragma once

#include "edit_recipe_state.h"

#include <QFuture>
#include <QImage>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <functional>
#include <memory>

namespace photo_editor::desktop {

struct CreativeExampleThumbnailResult final {
  QString cache_key;
  QImage image;
  QString error;
  bool cancelled = false;
};

struct CreativeExampleThumbnailTicket final {
  QFuture<CreativeExampleThumbnailResult> future;
  QImage cached_image;
  QString error;
  std::function<void(bool)> release;
};

struct CreativeExampleThumbnailServiceSnapshot final {
  int registered_requests = 0;
  int cached_images = 0;
  int in_flight_jobs = 0;
  int active_workers = 0;
  int peak_active_workers = 0;
  qsizetype cached_bytes = 0;
  quint64 cache_hits = 0;
  quint64 cache_misses = 0;
};

// Owns short-lived immutable render requests and small in-memory output
// thumbnails. URL creation only registers the input; rendering begins when an
// image provider asks for that URL.
class CreativeExampleThumbnailService final {
 public:
  static CreativeExampleThumbnailService& shared();

  CreativeExampleThumbnailService(const CreativeExampleThumbnailService&) = delete;
  CreativeExampleThumbnailService& operator=(const CreativeExampleThumbnailService&) = delete;
  ~CreativeExampleThumbnailService();

  [[nodiscard]] QString sourceForRecipe(
      const QString& example_identity,
      const EditRecipeState& state);
  [[nodiscard]] CreativeExampleThumbnailTicket request(
      const QString& image_id);
  [[nodiscard]] QImage cachedImageForSource(const QString& source) const;
  [[nodiscard]] CreativeExampleThumbnailServiceSnapshot snapshot() const;

  // The test reset is valid only when no requests are in flight.
  [[nodiscard]] bool clearForTests();

 private:
  CreativeExampleThumbnailService();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] QString creative_example_original_thumbnail_source();
[[nodiscard]] QImage creative_example_thumbnail_cached_image(
    const QString& source);
[[nodiscard]] CreativeExampleThumbnailServiceSnapshot
creative_example_thumbnail_service_snapshot();
[[nodiscard]] bool clear_creative_example_thumbnail_service_for_tests();

}  // namespace photo_editor::desktop
