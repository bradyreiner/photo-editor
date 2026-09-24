#pragma once

#include "local_image_text_encoder.h"
#include "visual_embedding_index.h"

#include <QFutureWatcher>
#include <QHash>
#include <QTemporaryDir>
#include <QVariantList>

#include <atomic>
#include <memory>

namespace photo_editor::desktop {

struct SimilaritySearchWork {
  QString error;
  VisualEmbeddingCandidate candidate;
  VisualEmbeddingTicket ticket;
  VisualEmbeddingResult ranking;
  std::shared_ptr<QTemporaryDir> preview;
};

// On-demand indexing of an explicit catalog snapshot, not catalog authority.
// All file reads, hashing and SQLite operations run off the owner/UI thread.
// The caller must invalidate synchronously on catalog/scope changes. Filtered
// absence is never interpreted as deletion; old derived rows are unreachable
// outside an explicitly supplied current asset/content scope.
class LocalSimilaritySearch final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool configured READ configured NOTIFY changed)
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString query READ query NOTIFY changed)
  Q_PROPERTY(QVariantList results READ results NOTIFY changed)
  Q_PROPERTY(QStringList resultIds READ resultIds NOTIFY changed)
  Q_PROPERTY(int inspected READ inspected NOTIFY changed)
  Q_PROPERTY(int reused READ reused NOTIFY changed)
  Q_PROPERTY(int encoded READ encoded NOTIFY changed)
  Q_PROPERTY(QString indexWarning READ indexWarning NOTIFY changed)
  Q_PROPERTY(QString modelName READ modelName CONSTANT)
  Q_PROPERTY(bool hasMore READ hasMore CONSTANT)
 public:
  explicit LocalSimilaritySearch(QObject* parent = nullptr);
  ~LocalSimilaritySearch() override;
  // Explicit trusted local runtime only. No download or machine configuration.
  bool configure(const QString& python, const QString& worker, const QString& model_root,
                 const QString& device, const QString& index_path);
  bool configureExecutable(const QString& executable, const QString& model_root,
                           const QString& device, const QString& index_path);
  bool configured() const { return !worker_.isEmpty(); }
  void setSetupStatus(const QString& status);
  void clearConfiguration();
  bool running() const;
  QString status() const { return status_; }
  QString query() const { return query_; }
  QVariantList results() const { return results_; }
  QStringList resultIds() const;
  int inspected() const { return static_cast<int>(scope_.size()); }
  int reused() const { return reused_; }
  int encoded() const { return encoded_; }
  QString indexWarning() const;
  QString modelName() const { return QStringLiteral("SigLIP 2 · similarity, not verified matches"); }
  bool hasMore() const { return false; }
  // catalog_key is an explicit stable namespace, never an inferred global scope.
  Q_INVOKABLE void search(const QString& query, const QVariantList& assets,
                          const QString& catalog_key, int limit = 32);
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void invalidate();
  Q_INVOKABLE void stop();
 signals:
  void changed();
  void batchFinished();
 private:
  bool configureRuntime(const QString& python, const QString& worker, const QString& model_root,
                        const QString& device, const QString& index_path, bool executable);
  void pump();
  void finishWork();
  void fail(const QString& message);
  LocalImageTextEncoder encoder_;
  QFutureWatcher<SimilaritySearchWork> work_;
  std::shared_ptr<std::atomic_bool> cancelled_;
  bool active_ = false, work_pending_ = false;
  bool executable_ = false;
  quint64 generation_ = 0, work_generation_ = 0;
  QString phase_, request_id_, python_, worker_, model_root_, device_, index_path_;
  QString catalog_key_, query_, status_ = QStringLiteral("Similarity search needs an explicitly configured local encoder.");
  QVariantList assets_, results_;
  QVector<VisualEmbeddingCandidate> scope_;
  QHash<QString, QString> asset_ids_;
  VisualEmbeddingSpace space_;
  QVector<float> text_vector_;
  SimilaritySearchWork prepared_;
  int cursor_ = 0, skipped_ = 0, reused_ = 0, encoded_ = 0, limit_ = 32;
};

}  // namespace photo_editor::desktop
