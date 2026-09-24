#include "local_similarity_search.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

namespace photo_editor::desktop {
namespace {
QString digest(const QByteArray& bytes) {
  return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QVector<float> values(const QVariantMap& result) {
  QVector<float> vector;
  for (const auto& value : result.value("vector").toList()) vector.append(value.toFloat());
  return vector;
}
SimilaritySearchWork prepare(const QVariantMap& asset, const QString& path,
                             const VisualEmbeddingSpace& space,
                             const std::shared_ptr<std::atomic_bool>& cancelled) {
  SimilaritySearchWork result;
  if (cancelled->load()) return result;
  // Only the catalog can assert that its rendered preview represents the current
  // recipe. No original-image fallback (especially not for RAW or edited JPG).
  if (asset.value("mediaType").toString() != "photo" ||
      !asset.value("searchPreviewCurrent").toBool()) return result;
  const auto source = asset.value("thumbnailSource").toString();
  const QUrl url(source);
  const auto local = url.isLocalFile() ? url.toLocalFile() : (url.scheme().isEmpty() ? source : QString());
  const QFileInfo info(local);
  if (local.isEmpty() || !info.isAbsolute() || !info.isFile()) return result;
  QFile file(local);
  constexpr qint64 max_bytes = 64 * 1024 * 1024;
  if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > max_bytes) return result;
  const auto bytes = file.read(max_bytes + 1);
  if (file.error() != QFileDevice::NoError || bytes.isEmpty() || bytes.size() > max_bytes || cancelled->load()) return result;
  const auto identity = QJsonDocument(QJsonArray{asset.value("filePath").toString(),
      asset.value("adjustmentPatchJson").toString(), local, digest(bytes)}).toJson(QJsonDocument::Compact);
  result.candidate = {digest(asset.value("assetId").toString().toUtf8()), digest(identity)};
  if (cancelled->load()) return result;
  result.ticket = VisualEmbeddingIndex::stage(path, space, result.candidate);
  result.error = result.ticket.error;
  if (!result.error.isEmpty() || result.ticket.ready || cancelled->load()) return result;
  // Encode the very bytes hashed above, not a mutable catalog path. One private
  // bounded snapshot is retained until the owned encoder drains its request.
  result.preview = std::make_shared<QTemporaryDir>(QDir::tempPath() + "/framehouse-search-XXXXXX");
  if (!result.preview->isValid()) { result.error = "Could not create a private search preview."; return result; }
  QFile snapshot(result.preview->filePath("preview"));
  if (!snapshot.open(QIODevice::WriteOnly) || !snapshot.setPermissions(QFile::ReadOwner | QFile::WriteOwner) ||
      snapshot.write(bytes) != bytes.size() || !snapshot.flush()) result.error = "Could not snapshot the local search preview.";
  return result;
}
}  // namespace

LocalSimilaritySearch::LocalSimilaritySearch(QObject* parent) : QObject(parent) {
  connect(&work_, &QFutureWatcher<SimilaritySearchWork>::finished, this, &LocalSimilaritySearch::finishWork);
  // The encoder emits changed during admission and before embeddingReady; defer
  // scheduling so the request ID and completed result are installed first.
  connect(&encoder_, &LocalImageTextEncoder::changed, this, [this] { pump(); emit changed(); }, Qt::QueuedConnection);
  connect(&encoder_, &LocalImageTextEncoder::embeddingReady, this, [this](const QString& id, const QVariantMap& result) {
    if (!active_ || request_id_ != id) return;
    request_id_.clear();
    if (phase_ == "text") { text_vector_ = values(result); phase_ = "prepare"; pump(); return; }
    if (phase_ != "image") { fail("Unexpected search encoder response."); return; }
    phase_ = "commit";
    const auto prepared = prepared_;
    const auto path = index_path_;
    const auto space = space_;
    const auto vector = values(result);
    const auto cancelled = cancelled_;
    work_pending_ = true; work_generation_ = generation_;
    work_.setFuture(QtConcurrent::run([prepared, path, space, vector, cancelled] {
      auto committed = prepared;
      if (!cancelled->load()) {
        const auto write = VisualEmbeddingIndex::commit(path, space, prepared.candidate, prepared.ticket.token, vector);
        if (!write.ok) committed.error = write.error.isEmpty() ? QStringLiteral("Search preview changed before indexing completed.") : write.error;
      }
      return committed;
    }));
  });
  connect(&encoder_, &LocalImageTextEncoder::requestFailed, this, [this](const QString& id, const QString& message) {
    if (!active_ || request_id_ != id) return;
    request_id_.clear();
    // A decode failure is missing coverage, never evidence of a nonmatch.
    if (phase_ == "image" && encoder_.snapshot().value("ready").toBool()) {
      ++skipped_; ++cursor_; prepared_ = {}; phase_ = "prepare"; pump();
    } else fail(message);
  });
}

LocalSimilaritySearch::~LocalSimilaritySearch() {
  if (cancelled_) cancelled_->store(true);
  // Futures capture values, not this. No interactive wait for disk/SQLite work.
  work_.disconnect(this);
}

bool LocalSimilaritySearch::running() const {
  const auto state = encoder_.snapshot();
  return active_ || work_pending_ || state.value("busy").toBool() ||
      (state.value("ownsProcess").toBool() && !state.value("ready").toBool());
}
QStringList LocalSimilaritySearch::resultIds() const {
  QStringList ids;
  for (const auto& value : results_) ids.append(value.toMap().value("assetId").toString());
  return ids;
}
void LocalSimilaritySearch::setSetupStatus(const QString& status) {
  if (!configured() && !running()) { status_ = status; emit changed(); }
}
QString LocalSimilaritySearch::indexWarning() const {
  return skipped_ > 0 ? QStringLiteral("%1 photos have no usable current preview; they were not searched. Similarity does not verify every query detail.").arg(skipped_)
                      : QStringLiteral("Ranks visual similarity only; even unrelated photos can rank first. Use detail search before treating results as verified matches.");
}
bool LocalSimilaritySearch::configure(const QString& python, const QString& worker, const QString& model_root,
                                      const QString& device, const QString& index_path) {
  return configureRuntime(python, worker, model_root, device, index_path, false);
}
bool LocalSimilaritySearch::configureExecutable(const QString& executable, const QString& model_root,
                                                const QString& device, const QString& index_path) {
  return configureRuntime({}, executable, model_root, device, index_path, true);
}
bool LocalSimilaritySearch::configureRuntime(const QString& python, const QString& worker, const QString& model_root,
                                             const QString& device, const QString& index_path, bool executable) {
  if (running() || encoder_.snapshot().value("ownsProcess").toBool()) return false;
  if ((!executable && !QFileInfo(python).isAbsolute()) || !QFileInfo(worker).isAbsolute() ||
      !QFileInfo(model_root).isAbsolute() || !QFileInfo(index_path).isAbsolute() ||
      (device != "cpu" && device != "mps")) return false;
  invalidate();
  python_ = python; worker_ = worker; model_root_ = model_root; device_ = device; index_path_ = index_path;
  executable_ = executable;
  status_ = "Local similarity search is configured. First search builds vectors from current previews; later searches reuse them.";
  emit changed(); return true;
}

void LocalSimilaritySearch::search(const QString& query, const QVariantList& assets, const QString& catalog_key, int limit) {
  // Never queue replacement work behind canceled inference or a disk operation.
  if (running()) return;
  invalidate();
  if (!configured() || query.trimmed().isEmpty() || query.size() > 2000 || catalog_key.isEmpty() ||
      catalog_key.size() > 4096 || assets.size() > 100000 || limit < 1 || limit > 1000) {
    fail("Similarity search requires a configured encoder, query and explicit bounded catalog scope."); return;
  }
  QSet<QString> ids;
  qint64 metadata_bytes = 0;
  for (const auto& value : assets) {
    const auto asset = value.toMap();
    const auto id = asset.value("assetId").toString();
    metadata_bytes += asset.value("adjustmentPatchJson").toString().size() * 2;
    if (id.isEmpty() || id.size() > 4096 || ids.contains(id) || metadata_bytes > 128 * 1024 * 1024) {
      fail("Invalid, duplicate or oversized search snapshot; no partial scope was searched."); return;
    }
    ids.insert(id);
  }
  query_ = query.trimmed(); assets_ = assets; catalog_key_ = catalog_key; limit_ = limit;
  cursor_ = 0; skipped_ = 0; reused_ = 0; encoded_ = 0; scope_.clear(); asset_ids_.clear(); text_vector_.clear();
  cancelled_ = std::make_shared<std::atomic_bool>(false);
  active_ = true; phase_ = "start";
  status_ = "Loading the local similarity encoder…";
  if (!encoder_.snapshot().value("ready").toBool() &&
      !(executable_ ? encoder_.startExecutable(worker_, model_root_, device_)
                    : encoder_.start(python_, worker_, model_root_, device_))) {
    fail(encoder_.snapshot().value("error").toString()); return;
  }
  pump(); emit changed();
}

void LocalSimilaritySearch::cancel() {
  ++generation_;
  if (cancelled_) cancelled_->store(true);
  active_ = false; request_id_.clear(); results_.clear();
  encoder_.cancel();
  if (encoder_.snapshot().value("state").toString() == "starting") encoder_.stop();
  status_ = "Search stopped. Any in-flight work must drain before another search starts.";
  emit changed();
}
void LocalSimilaritySearch::invalidate() {
  cancel();
  status_ = "Search inputs changed. Run a new search for current photos and filters.";
  emit changed();
}
void LocalSimilaritySearch::stop() { cancel(); encoder_.stop(); }
void LocalSimilaritySearch::clearConfiguration() {
  stop();
  python_.clear(); worker_.clear(); model_root_.clear(); device_.clear(); index_path_.clear();
  status_ = "Verify the local search model before searching.";
  emit changed();
}
void LocalSimilaritySearch::fail(const QString& message) {
  cancel(); status_ = message.isEmpty() ? QStringLiteral("Local similarity search failed.") : message; emit changed();
}

void LocalSimilaritySearch::pump() {
  const auto state = encoder_.snapshot();
  if (!active_) {
    if (!work_pending_ && !state.value("busy").toBool()) prepared_ = {};
    return;
  }
  if (state.value("state").toString() == "failed") { fail(state.value("error").toString()); return; }
  if (!state.value("ready").toBool() || work_pending_ || !request_id_.isEmpty()) return;
  if (phase_ == "start") {
    space_ = {digest(QJsonDocument(QJsonArray{"catalog-preview-snapshot-v1", catalog_key_, state.value("spaceKey").toString()}).toJson(QJsonDocument::Compact)), 768};
    phase_ = "text"; request_id_ = encoder_.encodeText(query_);
    if (request_id_.isEmpty()) fail("Local query encoder is not available.");
    return;
  }
  if (phase_ != "prepare") return;
  work_pending_ = true; work_generation_ = generation_;
  const auto path = index_path_; const auto space = space_; const auto cancelled = cancelled_;
  if (cursor_ < assets_.size()) {
    status_ = QStringLiteral("Indexing current previews %1/%2 · %3 reused · %4 encoded").arg(cursor_).arg(assets_.size()).arg(reused_).arg(encoded_);
    const auto asset = assets_.at(cursor_).toMap();
    work_.setFuture(QtConcurrent::run([asset, path, space, cancelled] { return prepare(asset, path, space, cancelled); }));
  } else {
    phase_ = "rank"; status_ = "Ranking similar photos in the current view…";
    const auto scope = scope_; const auto vector = text_vector_; const auto limit = limit_;
    work_.setFuture(QtConcurrent::run([path, space, scope, vector, limit, cancelled] {
      SimilaritySearchWork result;
      result.ranking = VisualEmbeddingIndex::search(path, space, scope, vector, limit, cancelled.get());
      result.error = result.ranking.error; return result;
    }));
  }
  emit changed();
}

void LocalSimilaritySearch::finishWork() {
  work_pending_ = false;
  const auto result = work_.result();
  if (!active_ || work_generation_ != generation_) { pump(); emit changed(); return; }
  if (!result.error.isEmpty()) { fail(result.error); return; }
  if (phase_ == "rank") {
    if (result.ranking.cancelled || result.ranking.missing != 0) { fail("Search index changed; retry with the current photos."); return; }
    results_.clear();
    for (const auto& match : result.ranking.matches) results_.append(QVariantMap{
        {"assetId", asset_ids_.value(match.asset_key)}, {"similarity", match.similarity},
        {"evidenceSource", "visual-similarity"}, {"reason", "Similar visual content; specific query details have not been verified."}});
    active_ = false; phase_.clear(); prepared_ = {};
    status_ = QStringLiteral("%1 similar candidates from %2 indexed previews · %3 reused · %4 encoded. Not verified matches.")
        .arg(results_.size()).arg(scope_.size()).arg(reused_).arg(encoded_);
    emit changed(); emit batchFinished(); return;
  }
  if (phase_ == "prepare") {
    prepared_ = result;
    if (result.candidate.asset_key.isEmpty()) { ++skipped_; ++cursor_; pump(); return; }
    if (!result.ticket.ready) {
      phase_ = "image";
      request_id_ = encoder_.encodeImage(result.preview ? result.preview->filePath("preview") : QString());
      if (request_id_.isEmpty()) fail("Current preview could not be submitted to the local encoder.");
      return;
    }
    ++reused_;
  } else if (phase_ == "commit") ++encoded_;
  else { fail("Unexpected background search stage."); return; }
  scope_.append(result.candidate);
  asset_ids_.insert(result.candidate.asset_key, assets_.at(cursor_).toMap().value("assetId").toString());
  ++cursor_; prepared_ = {}; phase_ = "prepare"; pump();
}

}  // namespace photo_editor::desktop
