#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

class QNetworkReply;
class QJsonObject;

namespace photo_editor::desktop {

// Query-conditioned visual retrieval. It never edits assets or owns selection.
// Only reduced, color-managed previews are sent to a verified local vision model.
class VisualSearchController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool running READ running NOTIFY changed)
  Q_PROPERTY(QString status READ status NOTIFY changed)
  Q_PROPERTY(QString query READ query NOTIFY changed)
  Q_PROPERTY(QString modelName READ modelName NOTIFY changed)
  Q_PROPERTY(int processed READ processed NOTIFY changed)
  Q_PROPERTY(int total READ total NOTIFY changed)
  Q_PROPERTY(int inspected READ inspected NOTIFY changed)
  Q_PROPERTY(int skipped READ skipped NOTIFY changed)
  Q_PROPERTY(bool hasMore READ hasMore NOTIFY changed)
  Q_PROPERTY(QVariantList results READ results NOTIFY changed)
  Q_PROPERTY(QStringList resultIds READ resultIds NOTIFY changed)
  Q_PROPERTY(bool useIndex READ useIndex WRITE setUseIndex NOTIFY changed)
  Q_PROPERTY(int reusedDescriptions READ reusedDescriptions NOTIFY changed)
  Q_PROPERTY(int newDescriptions READ newDescriptions NOTIFY changed)
  Q_PROPERTY(int detailVerifiedCount READ detailVerifiedCount NOTIFY changed)
  Q_PROPERTY(QString indexWarning READ indexWarning NOTIFY changed)
  Q_PROPERTY(QVariantMap searchPlan READ searchPlan NOTIFY changed)

 public:
  explicit VisualSearchController(QObject* parent = nullptr);
  ~VisualSearchController() override;
  bool running() const { return running_; }
  QString status() const { return status_; }
  QString query() const { return query_; }
  QString modelName() const { return model_.value("name", model_.value("model")).toString(); }
  int processed() const { return cursor_; }
  int total() const { return static_cast<int>(assets_.size()); }
  int inspected() const { return inspected_; }
  int skipped() const { return skipped_; }
  bool hasMore() const { return !running_ && (cursor_ < total() || !pending_descriptions_.isEmpty() || !pending_details_.isEmpty()); }
  QVariantList results() const { return results_; }
  QStringList resultIds() const;
  bool useIndex() const { return use_index_; }
  void setUseIndex(bool enabled);
  void setIndexPath(const QString& path);
  int reusedDescriptions() const { return reused_descriptions_; }
  int newDescriptions() const { return new_descriptions_; }
  int detailVerifiedCount() const { return detail_verified_count_; }
  QString indexWarning() const { return index_warning_; }
  QVariantMap searchPlan() const { return search_plan_; }
  QVariantMap modelConfiguration() const { return model_; }
  Q_INVOKABLE void searchForEdit(const QString& instruction, const QVariantList& assets, const QVariantMap& model);
  Q_INVOKABLE void search(const QString& query, const QVariantList& assets, const QVariantMap& model);
  Q_INVOKABLE void searchMore();
  Q_INVOKABLE void cancel();
  Q_INVOKABLE void clear();
  Q_INVOKABLE void forgetIndex();

 signals:
  void changed();
  void batchFinished();
  void searchFailed(const QString& message);

 private:
  friend struct VisualSearchPreparationTestAccess;
  void preflight();
  void planInstruction();
  void next();
  void fail(const QString& message);
  void finish();
  void accept(const QVariantMap& score);
  void describe(const QVariantMap& prepared);
  void stageDescription(const QString& description);
  void rankDescriptions();
  void verifyDetails();
  void sortResults();
  bool managedRuntimeCurrent() const;
  QString modelCacheIdentity() const;
  void request(const QString& route, const QByteArray& body,
               const std::function<void(const QJsonObject&)>& completed);
  QNetworkAccessManager network_;
  QPointer<QNetworkReply> reply_;
  QFutureWatcher<QVariantMap> preparation_;
  QFutureWatcher<bool> index_write_;
  quint64 index_write_generation_ = 0;
  QString pending_description_;
  bool index_write_pending_ = false;
  bool forgetting_index_ = false;
  bool use_index_ = false;
  QString index_path_;
  QString description_key_;
  QString preparation_key_;
  QString index_warning_;
  QHash<QString, QString> descriptions_;
  QVariantList pending_descriptions_;
  QVariantList pending_details_;
  QPointer<VisualSearchController> detail_verifier_;
  int detail_verified_count_ = 0;
  QHash<QString, int> asset_order_;
  int reused_descriptions_ = 0;
  int new_descriptions_ = 0;
  quint64 generation_ = 0;
  quint64 preparation_generation_ = 0;
  bool running_ = false;
  bool model_ready_ = false;
  bool preparation_pending_ = false;
  QString status_ = QStringLiteral("Describe what is in the photo. Search runs locally, 32 photos at a time.");
  QString query_;
  QString pending_instruction_;
  QVariantMap search_plan_;
  QVariantMap model_;
  QVariantMap managed_runtime_;
  QVariantList assets_;
  QVariantList results_;
  int cursor_ = 0;
  int batch_end_ = 0;
  int inspected_ = 0;
  int skipped_ = 0;
  QString cache_key_;
  QString model_digest_;
  QHash<QString, QVariantMap> cache_;
};

}  // namespace photo_editor::desktop
