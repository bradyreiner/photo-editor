#include "creative_example_thumbnail_service.h"

#include "edit_adjustment_patch.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_render_utils.h"
#include "preset_thumbnail_result_cache.h"
#include "render_plan_store.h"
#include "thumbnail_canonical_renderer.h"

#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <utility>

namespace photo_editor::desktop {
namespace {

constexpr int kFixtureWidth = 640;
constexpr int kFixtureHeight = 432;
constexpr int kMaximumRegisteredRequests = 512;
constexpr int kMaximumCachedImages = 96;
constexpr int kMaximumQueuedJobs = 256;
constexpr qsizetype kMaximumCacheBytes = 32 * 1024 * 1024;
constexpr auto kThumbnailAlgorithmTag = "creative-example-thumbnail:v1|mountain-preview.jpg";

const QString kProviderAuthority = QStringLiteral("creative-example");
const QString kFixtureResource =
    QStringLiteral(":/photo-editor/presets/mountain-preview.jpg");

QString image_key_from_id(QString id) {
  if (id.startsWith(QLatin1Char('/'))) id.remove(0, 1);
  const auto parts = id.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  if (parts.size() != 2 || parts.at(0) != QStringLiteral("v1") ||
      parts.at(1).size() != 64) {
    return {};
  }
  const auto key = parts.at(1);
  for (const auto character : key) {
    if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9')) ||
          (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
      return {};
    }
  }
  return key;
}

QString image_key_from_source(const QString& source) {
  const auto prefix = QStringLiteral("image://creative-example/");
  if (!source.startsWith(prefix)) return {};
  return image_key_from_id(source.mid(prefix.size()));
}

QImage sample_fixture() {
  static const QImage fixture = [] {
    QImage source(kFixtureResource);
    if (source.isNull()) return QImage{};
    const QColorSpace srgb(QColorSpace::SRgb);
    if (!source.colorSpace().isValid()) source.setColorSpace(srgb);
    const auto scaled = source.scaled(
        kFixtureWidth,
        kFixtureHeight,
        Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_ARGB32);
    if (scaled.isNull()) return QImage{};
    const QRect crop(
        (scaled.width() - kFixtureWidth) / 2,
        (scaled.height() - kFixtureHeight) / 2,
        kFixtureWidth,
        kFixtureHeight);
    auto result = scaled.copy(crop);
    if (result.colorSpace().isValid() && result.colorSpace() != srgb)
      result = result.convertedToColorSpace(srgb);
    else
      result.setColorSpace(srgb);
    return result;
  }();
  return fixture;
}

EditRecipeState normalized_example_recipe(EditRecipeState state) {
  // The shared fixture is already-decoded, so RAW-only exposure is not part
  // of its example. Keep every creative recipe field and normalize only the
  // output proof/display target so the same pixels are shown on every screen.
  state.raw_exposure = 0.0;
  state.preview_color_space = QStringLiteral("sRGB");
  state.preview_output_mode = QStringLiteral("Monitor");
  state.monitor_color_profile.clear();
  state.preview_icc_profile_asset = {};
  state.proof_icc_profile_asset = {};
  state.export_icc_profile_asset = {};
  state.output_color_space = QStringLiteral("sRGB");
  state.soft_proof_enabled = false;
  state.soft_proof_profile = QStringLiteral("sRGB");
  state.proof_simulate_paper_white = false;
  state.proof_simulate_ink_black = false;
  return state;
}

struct ExampleRequest final {
  QString key;
  EditRecipeState state;
  QImage fixture;
};

CreativeExampleThumbnailResult render_request(
    const ExampleRequest& request,
    const std::shared_ptr<std::atomic_bool>& cancelled) {
  CreativeExampleThumbnailResult result;
  result.cache_key = request.key;
  if (cancelled->load(std::memory_order_relaxed)) {
    result.cancelled = true;
    return result;
  }
  if (request.fixture.isNull()) {
    result.error = QStringLiteral("The bundled mountain example photo is unavailable.");
    return result;
  }

  const auto recipe_identity = adjustment_patch_identity_json(request.state);
  const auto recipe_digest = QString::fromLatin1(
      QCryptographicHash::hash(
          recipe_identity.toUtf8(),
          QCryptographicHash::Sha256)
          .toHex());
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("builtin:creative-example:%1").arg(request.key),
      QStringLiteral("builtin:creative-example-mountain"),
      request.state,
      request.fixture.size(),
      photo_editor::pipeline::RenderPurpose::Thumbnail,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
      std::nullopt,
      nullptr,
      recipe_digest);
  if (normalized_photographic_rendering_model_name(
          request.state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1") &&
      !admission.admitted) {
    result.error = admission.error.isEmpty()
                       ? QStringLiteral("The example render plan could not be admitted.")
                       : admission.error;
    return result;
  }
  if (cancelled->load(std::memory_order_relaxed)) {
    result.cancelled = true;
    return result;
  }

  auto rendered = render_canonical_thumbnail_image(
      request.fixture,
      request.state,
      QColorSpace(QColorSpace::SRgb),
      admission.admitted ? &*admission.plan : nullptr);
  if (cancelled->load(std::memory_order_relaxed)) {
    result.cancelled = true;
    return result;
  }
  if (rendered.isNull()) {
    result.error = QStringLiteral("The shared photo render pipeline could not render this example.");
    return result;
  }
  rendered.setColorSpace(QColorSpace(QColorSpace::SRgb));
  result.image = std::move(rendered);
  return result;
}

}  // namespace

struct CreativeExampleThumbnailService::Impl final {
  struct Job final {
    QString key;
    std::shared_ptr<std::atomic_bool> cancelled =
        std::make_shared<std::atomic_bool>(false);
    QFuture<CreativeExampleThumbnailResult> future;
    int subscribers = 0;
    bool completed = false;
  };

  mutable QMutex mutex;
  QThreadPool pool;
  QHash<QString, ExampleRequest> requests;
  QList<QString> request_lru;
  QHash<QString, QImage> images;
  QList<QString> image_lru;
  QHash<QString, std::shared_ptr<Job>> jobs;
  qsizetype cached_bytes = 0;
  quint64 cache_hits = 0;
  quint64 cache_misses = 0;
  int active_workers = 0;
  int peak_active_workers = 0;
  int pending_jobs = 0;

  Impl() {
    pool.setMaxThreadCount(2);
    pool.setExpiryTimeout(-1);
  }

  void touchRequest(const QString& key) {
    request_lru.removeAll(key);
    request_lru.append(key);
  }

  void touchImage(const QString& key) {
    image_lru.removeAll(key);
    image_lru.append(key);
  }

  void insertImage(const QString& key, const QImage& image) {
    if (key.isEmpty() || image.isNull()) return;
    const auto bytes = static_cast<qsizetype>(image.sizeInBytes());
    if (bytes <= 0 || bytes > kMaximumCacheBytes) return;
    const auto existing = images.find(key);
    if (existing != images.end()) {
      cached_bytes -= static_cast<qsizetype>(existing->sizeInBytes());
      image_lru.removeAll(key);
      images.erase(existing);
    }
    while (!image_lru.isEmpty() &&
           (images.size() >= kMaximumCachedImages ||
            cached_bytes + bytes > kMaximumCacheBytes)) {
      const auto oldest = image_lru.takeFirst();
      const auto victim = images.find(oldest);
      if (victim == images.end()) continue;
      cached_bytes -= static_cast<qsizetype>(victim->sizeInBytes());
      images.erase(victim);
    }
    images.insert(key, image);
    image_lru.append(key);
    cached_bytes += bytes;
  }
};

CreativeExampleThumbnailService& CreativeExampleThumbnailService::shared() {
  static CreativeExampleThumbnailService service;
  return service;
}

CreativeExampleThumbnailService::CreativeExampleThumbnailService()
    : impl_(std::make_unique<Impl>()) {}

CreativeExampleThumbnailService::~CreativeExampleThumbnailService() {
  {
    QMutexLocker lock(&impl_->mutex);
    for (const auto& job : impl_->jobs) {
      job->cancelled->store(true, std::memory_order_relaxed);
    }
  }
  impl_->pool.waitForDone();
}

QString CreativeExampleThumbnailService::sourceForRecipe(
    const QString& example_identity,
    const EditRecipeState& state) {
  const auto fixture = sample_fixture();
  if (example_identity.trimmed().isEmpty() || fixture.isNull()) return {};
  const auto normalized_state = normalized_example_recipe(state);
  const auto recipe_identity =
      QString::fromLatin1(kThumbnailAlgorithmTag) + QLatin1Char('|') +
      example_identity.trimmed() + QLatin1Char('|') +
      adjustment_patch_identity_json(normalized_state);
  const auto key = preset_thumbnail_result_cache_key(
      normalized_state,
      fixture,
      QColorSpace(QColorSpace::SRgb),
      recipe_identity);
  if (key.isEmpty()) return {};

  QMutexLocker lock(&impl_->mutex);
  if (!impl_->requests.contains(key)) {
    while (impl_->request_lru.size() >= kMaximumRegisteredRequests) {
      const auto oldest = impl_->request_lru.takeFirst();
      impl_->requests.remove(oldest);
    }
    impl_->requests.insert(key, ExampleRequest{key, normalized_state, fixture});
  }
  impl_->touchRequest(key);
  return QStringLiteral("image://%1/v1/%2").arg(kProviderAuthority, key);
}

CreativeExampleThumbnailTicket CreativeExampleThumbnailService::request(
    const QString& image_id) {
  CreativeExampleThumbnailTicket ticket;
  const auto key = image_key_from_id(image_id);
  if (key.isEmpty()) {
    ticket.error = QStringLiteral("The creative example thumbnail URL is invalid.");
    return ticket;
  }

  std::shared_ptr<Impl::Job> job;
  ExampleRequest request_snapshot;
  {
    QMutexLocker lock(&impl_->mutex);
    const auto cached = impl_->images.constFind(key);
    if (cached != impl_->images.cend()) {
      ticket.cached_image = cached.value();
      impl_->touchImage(key);
      ++impl_->cache_hits;
      return ticket;
    }
    ++impl_->cache_misses;
    const auto request = impl_->requests.constFind(key);
    if (request == impl_->requests.cend()) {
      ticket.error = QStringLiteral("The creative example thumbnail request has expired.");
      return ticket;
    }
    impl_->touchRequest(key);
    auto existing_job = impl_->jobs.find(key);
    if (existing_job != impl_->jobs.end() &&
        existing_job.value()->cancelled->load(std::memory_order_relaxed)) {
      // A cancelled scroll-away request must not poison a quick scroll-back.
      // Its worker retains its own snapshot and may finish independently.
      impl_->jobs.erase(existing_job);
      existing_job = impl_->jobs.end();
    }
    if (existing_job != impl_->jobs.end()) {
      job = existing_job.value();
      ++job->subscribers;
      ticket.future = job->future;
    } else {
      if (impl_->pending_jobs >= kMaximumQueuedJobs) {
        ticket.error = QStringLiteral("The creative example thumbnail queue is full.");
        return ticket;
      }
      request_snapshot = request.value();
      job = std::make_shared<Impl::Job>();
      job->key = key;
      job->subscribers = 1;
      impl_->jobs.insert(key, job);
      ++impl_->pending_jobs;
      const auto cancellation = job->cancelled;
      ticket.future = QtConcurrent::run(&impl_->pool, [this, job, request_snapshot, cancellation]() {
        {
          QMutexLocker lock(&impl_->mutex);
          ++impl_->active_workers;
          impl_->peak_active_workers = qMax(impl_->peak_active_workers, impl_->active_workers);
        }
        auto result = render_request(request_snapshot, cancellation);
        {
          QMutexLocker lock(&impl_->mutex);
          --impl_->active_workers;
          --impl_->pending_jobs;
          job->completed = true;
          if (!result.cancelled && result.error.isEmpty() && !result.image.isNull())
            impl_->insertImage(job->key, result.image);
          const auto current_job = impl_->jobs.find(job->key);
          if (current_job != impl_->jobs.end() && current_job.value() == job)
            impl_->jobs.erase(current_job);
        }
        return result;
      });
      job->future = ticket.future;
    }
  }

  const std::weak_ptr<Impl::Job> weak_job = job;
  ticket.release = [this, weak_job](bool cancel) {
    const auto held_job = weak_job.lock();
    if (!held_job) return;
    QMutexLocker lock(&impl_->mutex);
    if (held_job->subscribers > 0) --held_job->subscribers;
    if (cancel && held_job->subscribers == 0 && !held_job->completed)
      held_job->cancelled->store(true, std::memory_order_relaxed);
  };
  return ticket;
}

QImage CreativeExampleThumbnailService::cachedImageForSource(
    const QString& source) const {
  const auto key = image_key_from_source(source);
  if (key.isEmpty()) return {};
  QMutexLocker lock(&impl_->mutex);
  return impl_->images.value(key);
}

CreativeExampleThumbnailServiceSnapshot
CreativeExampleThumbnailService::snapshot() const {
  QMutexLocker lock(&impl_->mutex);
  return {
      .registered_requests = static_cast<int>(impl_->requests.size()),
      .cached_images = static_cast<int>(impl_->images.size()),
      .in_flight_jobs = impl_->pending_jobs,
      .active_workers = impl_->active_workers,
      .peak_active_workers = impl_->peak_active_workers,
      .cached_bytes = impl_->cached_bytes,
      .cache_hits = impl_->cache_hits,
      .cache_misses = impl_->cache_misses,
  };
}

bool CreativeExampleThumbnailService::clearForTests() {
  QMutexLocker lock(&impl_->mutex);
  if (impl_->pending_jobs != 0) return false;
  impl_->requests.clear();
  impl_->request_lru.clear();
  impl_->images.clear();
  impl_->image_lru.clear();
  impl_->cached_bytes = 0;
  impl_->cache_hits = 0;
  impl_->cache_misses = 0;
  impl_->peak_active_workers = 0;
  return true;
}

QString creative_example_original_thumbnail_source() {
  return CreativeExampleThumbnailService::shared().sourceForRecipe(
      QStringLiteral("original"), EditRecipeState{});
}

QImage creative_example_thumbnail_cached_image(const QString& source) {
  return CreativeExampleThumbnailService::shared().cachedImageForSource(source);
}

CreativeExampleThumbnailServiceSnapshot
creative_example_thumbnail_service_snapshot() {
  return CreativeExampleThumbnailService::shared().snapshot();
}

bool clear_creative_example_thumbnail_service_for_tests() {
  return CreativeExampleThumbnailService::shared().clearForTests();
}

}  // namespace photo_editor::desktop
