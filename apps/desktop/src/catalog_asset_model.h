#pragma once

#include "customer_edit_access.h"
#include "catalog_duplicate_analysis.h"

#include <QAbstractListModel>
#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
#include <QLockFile>
#include <QHash>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "job_store.h"
#include "agent_task_journal.h"

class QThreadPool;
class QTemporaryFile;

namespace photo_editor::desktop {

struct EditRecipeState;
struct ImportPreflightRequest;
struct ImportPreflightScan;

struct CatalogMediaDurationResult final {
  QString file_path;
  QStringList asset_ids;
  quint64 generation = 0;
  double seconds = 0.0;
};

struct CatalogAssetItem final {
  bool operator==(const CatalogAssetItem&) const = default;
  QString asset_id;
  QString capture_date;
  QDate capture_date_value;
  QDateTime capture_timestamp;
  QString camera_label;
  QString lens_model;
  QString iso_label;
  QString shutter_speed;
  QString aperture_label;
  QString location_path;
  QString file_path;
  QString thumbnail_source;
  // Set only by a thumbnail completion matching the current source and recipe.
  // Historical stale-result diagnostics are not current-preview provenance.
  bool search_preview_current = false;
  QString thumbnail_provenance_token;
  QHash<QString, QString> thumbnail_dependency_revisions;
  bool thumbnail_stale_detected = false;
  bool thumbnail_stale_mismatch = false;
  QString thumbnail_stale_detection_reason;
  QString thumbnail_stale_previous_recipe_hash;
  QString thumbnail_stale_current_recipe_hash;
  QString thumbnail_stale_detection_signature;
  QString adjustment_patch_json;
  QString recipe_snapshot_patch_json;
  QString legacy_recipe_json;
  QString canonical_recipe_payload;
  QString canonical_recipe_cache_key;
  QString canonical_recipe_provenance_payload;
  QString drive_id;
  QString import_mode;
  QString imported_at;
  QDateTime imported_at_value;
  QStringList project_names;
  QString project_summary;
  QString media_type = QStringLiteral("photo");
  QString duration_text;
  double duration_seconds = 0.0;
  QString timeline_document_path;
  int rating = 0;
  bool picked = false;
  bool rejected = false;
  QString color_label;
  QString cull_status = QStringLiteral("unmarked");
  QString burst_key;
  int burst_size = 1;
  int burst_position = 1;
  bool burst_time_based = false;
  QString duplicate_group_key;
  int duplicate_group_size = 1;
  int duplicate_group_position = 1;
  bool best_shot_suggested = false;
  QString sync_group_key;
  int sync_group_size = 1;
  bool online = false;
  bool virtual_copy = false;
  QString source_asset_id;
  QString variant_name;
  int variant_index = 0;
};

struct CullingStateRecord final {
  int rating = 0;
  bool picked = false;
  bool rejected = false;
  QString color_label;
};

struct CullingHistoryEntry final {
  QString asset_id;
  CullingStateRecord before;
  CullingStateRecord after;
  QString label;
};

struct BatchRecipeHistoryEntry final {
  QHash<QString, QString> before_adjustment_patch_jsons;
  QHash<QString, QString> after_adjustment_patch_jsons;
  QString label;
};

struct ProjectRecord final {
  QString name;
  QString created_at;
  QSet<QString> asset_ids;
};

struct CatalogLibraryState final {
  bool present = false;
  QVector<ProjectRecord> projects;
  QHash<QString, CullingStateRecord> review_states;
  QHash<QString, QString> relinked_paths;
  QHash<QString, QVariantMap> asset_metadata;
};

struct CatalogSnapshotResult final {
  quint64 generation = 0;
  QVector<CatalogAssetItem> assets;
  QHash<QString, QString> reusable_thumbnail_tokens;
  CatalogLibraryState library_state;
  int drive_count = 0;
  qint64 payload_bytes = 0;
  qint64 preparation_ms = 0;
  QString error;
};

struct ThumbnailTaskResult final {
  QString asset_id;
  QString file_path;
  QString adjustment_patch_json;
  QString thumbnail_source;
  QString thumbnail_provenance_token;
  QHash<QString, QString> thumbnail_dependency_revisions;
  bool source_changed = false;
  qint64 thumbnail_cache_bytes = 0;
  int thumbnail_cache_entries = 0;
  qint64 thumbnail_cache_probe_ms = 0;
  qint64 queue_wait_ms = 0;
  qint64 elapsed_ms = 0;
  qint64 pool_wait_ms = 0;
  qint64 finished_at_msecs = 0;
  QString queue_priority;
};

struct DeleteAssetsTaskResult final {
  QStringList deleted_asset_ids;
  QString error_message;
  int total_count = 0;
  quint64 request_generation = 0;
  bool canceled = false;
};

class CatalogAssetModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
  Q_PROPERTY(bool deleting READ deleting NOTIFY deletingChanged)
  Q_PROPERTY(int deleteProgress READ deleteProgress NOTIFY deleteProgressChanged)
  Q_PROPERTY(int deleteTargetCount READ deleteTargetCount NOTIFY deleteProgressChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int assetCount READ assetCount NOTIFY assetCountChanged)
  Q_PROPERTY(int firstVisiblePhotoIndex READ firstVisiblePhotoIndex NOTIFY firstVisiblePhotoIndexChanged)
  Q_PROPERTY(int totalAssetCount READ totalAssetCount NOTIFY totalAssetCountChanged)
  Q_PROPERTY(int driveCount READ driveCount NOTIFY driveCountChanged)
  Q_PROPERTY(QStringList selectedAssetIds READ selectedAssetIds NOTIFY selectionChanged)
  Q_PROPERTY(int selectedAssetCount READ selectedAssetCount NOTIFY selectionChanged)
  Q_PROPERTY(bool hasMultiSelection READ hasMultiSelection NOTIFY selectionChanged)
  Q_PROPERTY(QVariantList selectedSurveyAssets READ selectedSurveyAssets NOTIFY surveyAssetsChanged)
  Q_PROPERTY(int selectedSurveyAssetCount READ selectedSurveyAssetCount NOTIFY surveyAssetsChanged)
  Q_PROPERTY(int visiblePickedCount READ visiblePickedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleRejectedCount READ visibleRejectedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleUnmarkedCount READ visibleUnmarkedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleRatedCount READ visibleRatedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleUnratedCount READ visibleUnratedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleOnlineCount READ visibleOnlineCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleOfflineCount READ visibleOfflineCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(QVariantList importSources READ importSources NOTIFY importSourcesChanged)
  Q_PROPERTY(QVariantMap importPreflightResult READ importPreflightResult NOTIFY importPreflightChanged)
  Q_PROPERTY(bool importPreflightBusy READ importPreflightBusy NOTIFY importPreflightChanged)
  Q_PROPERTY(bool blankCanvasBusy READ blankCanvasBusy NOTIFY blankCanvasBusyChanged)
  Q_PROPERTY(int importPreflightScannedCount READ importPreflightScannedCount NOTIFY importPreflightChanged)
  Q_PROPERTY(QStringList importFileNameFilters READ importFileNameFilters CONSTANT)
  Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
  Q_PROPERTY(QString selectedAssetId READ selectedAssetId NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCameraLabel READ selectedCameraLabel NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCaptureDate READ selectedCaptureDate NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedImportedAt READ selectedImportedAt NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedLensModel READ selectedLensModel NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedIso READ selectedIso NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedShutterSpeed READ selectedShutterSpeed NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedAperture READ selectedAperture NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedFilePath READ selectedFilePath NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedLocationPath READ selectedLocationPath NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedThumbnailSource READ selectedThumbnailSource NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedAdjustmentPatchJson READ selectedAdjustmentPatchJson NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedProjectSummary READ selectedProjectSummary NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedMediaType READ selectedMediaType NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedDurationText READ selectedDurationText NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedTimelineDocumentPath READ selectedTimelineDocumentPath NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedRating READ selectedRating NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedPicked READ selectedPicked NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedRejected READ selectedRejected NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedColorLabel READ selectedColorLabel NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCullStatus READ selectedCullStatus NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool canUndoCulling READ canUndoCulling NOTIFY cullingHistoryChanged)
  Q_PROPERTY(bool canRedoCulling READ canRedoCulling NOTIFY cullingHistoryChanged)
  Q_PROPERTY(QString cullingUndoLabel READ cullingUndoLabel NOTIFY cullingHistoryChanged)
  Q_PROPERTY(QString cullingRedoLabel READ cullingRedoLabel NOTIFY cullingHistoryChanged)
  Q_PROPERTY(bool canUndoBatchRecipe READ canUndoBatchRecipe NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(bool canRedoBatchRecipe READ canRedoBatchRecipe NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(QString batchRecipeUndoLabel READ batchRecipeUndoLabel NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(QString batchRecipeRedoLabel READ batchRecipeRedoLabel NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(QVariantList batchRecipeHistoryEntries READ batchRecipeHistoryEntries NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(int batchRecipeHistoryCurrentIndex READ batchRecipeHistoryCurrentIndex NOTIFY batchRecipeHistoryChanged)
  Q_PROPERTY(int selectedBurstSize READ selectedBurstSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedBurstPosition READ selectedBurstPosition NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedDuplicateGroupSize READ selectedDuplicateGroupSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedBestShotSuggested READ selectedBestShotSuggested NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedSyncGroupSize READ selectedSyncGroupSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedOnline READ selectedOnline NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedAvailabilityState READ selectedAvailabilityState NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedAvailabilityTitle READ selectedAvailabilityTitle NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedAvailabilityDetail READ selectedAvailabilityDetail NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedVirtualCopy READ selectedVirtualCopy NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedSourceAssetId READ selectedSourceAssetId NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedVariantName READ selectedVariantName NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedVariantIndex READ selectedVariantIndex NOTIFY selectedAssetChanged)
  Q_PROPERTY(QStringList projectNames READ projectNames NOTIFY projectNamesChanged)
  Q_PROPERTY(int projectStatsRevision READ projectStatsRevision NOTIFY projectStatsRevisionChanged)
  Q_PROPERTY(QStringList captureYears READ captureYears NOTIFY captureYearsChanged)
  Q_PROPERTY(QStringList cameraLabels READ cameraLabels NOTIFY cameraLabelsChanged)
  Q_PROPERTY(QString projectFilter READ projectFilter WRITE setProjectFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString importDateFilter READ importDateFilter WRITE setImportDateFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString importSessionFromFilter READ importSessionFromFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString importSessionToFilter READ importSessionToFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureYearFilter READ captureYearFilter WRITE setCaptureYearFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString cameraFilter READ cameraFilter WRITE setCameraFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureDateFromFilter READ captureDateFromFilter WRITE setCaptureDateFromFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureDateToFilter READ captureDateToFilter WRITE setCaptureDateToFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString mediaTypeFilter READ mediaTypeFilter WRITE setMediaTypeFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString mediaCollectionFilter READ mediaCollectionFilter WRITE setMediaCollectionFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString reviewFilter READ reviewFilter WRITE setReviewFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString availabilityFilter READ availabilityFilter WRITE setAvailabilityFilter NOTIFY filtersChanged)
  Q_PROPERTY(int minimumRatingFilter READ minimumRatingFilter WRITE setMinimumRatingFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString searchFilter READ searchFilter WRITE setSearchFilter NOTIFY filtersChanged)
  Q_PROPERTY(bool visualSearchActive READ visualSearchActive NOTIFY filtersChanged)
  Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY filtersChanged)
  Q_PROPERTY(QVariantMap selectedMetadata READ selectedMetadata NOTIFY selectedAssetChanged)
  Q_PROPERTY(int metadataPendingCount READ metadataPendingCount NOTIFY metadataStateChanged)
  Q_PROPERTY(QString metadataStatus READ metadataStatus NOTIFY metadataStateChanged)
  Q_PROPERTY(bool metadataFileBusy READ metadataFileBusy NOTIFY metadataStateChanged)
  Q_PROPERTY(bool duplicateAnalysisBusy READ duplicateAnalysisBusy NOTIFY duplicateAnalysisChanged)
  Q_PROPERTY(QString duplicateAnalysisStatus READ duplicateAnalysisStatus NOTIFY duplicateAnalysisChanged)
  Q_PROPERTY(int confirmedDuplicateGroupCount READ confirmedDuplicateGroupCount NOTIFY duplicateAnalysisChanged)

 public:
  void setEditAccessPolicy(std::function<bool()> policy) { customer_edit_access_.setPolicy(std::move(policy)); }
  enum Roles {
    AssetIdRole = Qt::UserRole + 1,
    CaptureDateRole,
    CameraLabelRole,
    LensModelRole,
    LocationPathRole,
    FilePathRole,
    ThumbnailSourceRole,
    ThumbnailStaleDetectedRole,
    ThumbnailStaleMismatchRole,
    ThumbnailStaleDetectionReasonRole,
    ThumbnailStalePreviousRecipeHashRole,
    ThumbnailStaleCurrentRecipeHashRole,
    ThumbnailStaleDetectionSignatureRole,
    AdjustmentPatchJsonRole,
    DriveIdRole,
    ImportModeRole,
    ImportedAtRole,
    ProjectNamesRole,
    ProjectSummaryRole,
    MediaTypeRole,
    DurationTextRole,
    DurationSecondsRole,
    TimelineDocumentPathRole,
    RatingRole,
    PickedRole,
    RejectedRole,
    ColorLabelRole,
    CullStatusRole,
    BurstKeyRole,
    BurstSizeRole,
    BurstPositionRole,
    DuplicateGroupKeyRole,
    DuplicateGroupSizeRole,
    DuplicateGroupPositionRole,
    BestShotSuggestedRole,
    SyncGroupKeyRole,
    SyncGroupSizeRole,
    OnlineRole,
    VirtualCopyRole,
    SourceAssetIdRole,
    VariantNameRole,
    VariantIndexRole,
  };

  explicit CatalogAssetModel(QObject* parent = nullptr);
  ~CatalogAssetModel() override;

  [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QString status() const;
  [[nodiscard]] QString errorMessage() const;
  [[nodiscard]] bool loading() const;
  [[nodiscard]] bool importing() const;
  [[nodiscard]] bool deleting() const;
  [[nodiscard]] int deleteProgress() const;
  [[nodiscard]] int deleteTargetCount() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] int assetCount() const;
  [[nodiscard]] int firstVisiblePhotoIndex() const { return first_visible_photo_index_; }
  [[nodiscard]] int totalAssetCount() const;
  [[nodiscard]] int driveCount() const;
  [[nodiscard]] QStringList selectedAssetIds() const;
  [[nodiscard]] int selectedAssetCount() const;
  [[nodiscard]] bool hasMultiSelection() const;
  [[nodiscard]] QVariantList selectedSurveyAssets() const;
  [[nodiscard]] int selectedSurveyAssetCount() const;
  [[nodiscard]] int visiblePickedCount() const;
  [[nodiscard]] int visibleRejectedCount() const;
  [[nodiscard]] int visibleUnmarkedCount() const;
  [[nodiscard]] int visibleRatedCount() const;
  [[nodiscard]] int visibleUnratedCount() const;
  [[nodiscard]] int visibleOnlineCount() const;
  [[nodiscard]] int visibleOfflineCount() const;
  [[nodiscard]] QVariantList importSources() const;
  [[nodiscard]] QStringList importFileNameFilters() const;
  [[nodiscard]] int selectedIndex() const;
  [[nodiscard]] QString selectedAssetId() const;
  [[nodiscard]] QString selectedCameraLabel() const;
  [[nodiscard]] QString selectedCaptureDate() const;
  [[nodiscard]] QString selectedImportedAt() const;
  [[nodiscard]] QString selectedLensModel() const;
  [[nodiscard]] QString selectedIso() const;
  [[nodiscard]] QString selectedShutterSpeed() const;
  [[nodiscard]] QString selectedAperture() const;
  [[nodiscard]] QString selectedFilePath() const;
  [[nodiscard]] QString selectedLocationPath() const;
  [[nodiscard]] QString selectedThumbnailSource() const;
  [[nodiscard]] QString selectedAdjustmentPatchJson() const;
  [[nodiscard]] QString selectedProjectSummary() const;
  [[nodiscard]] QString selectedMediaType() const;
  [[nodiscard]] QString selectedDurationText() const;
  [[nodiscard]] QString selectedTimelineDocumentPath() const;
  [[nodiscard]] int selectedRating() const;
  [[nodiscard]] bool selectedPicked() const;
  [[nodiscard]] bool selectedRejected() const;
  [[nodiscard]] QString selectedColorLabel() const;
  [[nodiscard]] QString selectedCullStatus() const;
  [[nodiscard]] bool canUndoCulling() const;
  [[nodiscard]] bool canRedoCulling() const;
  [[nodiscard]] QString cullingUndoLabel() const;
  [[nodiscard]] QString cullingRedoLabel() const;
  [[nodiscard]] bool canUndoBatchRecipe() const;
  [[nodiscard]] bool canRedoBatchRecipe() const;
  [[nodiscard]] QString batchRecipeUndoLabel() const;
  [[nodiscard]] QString batchRecipeRedoLabel() const;
  [[nodiscard]] QVariantList batchRecipeHistoryEntries() const;
  [[nodiscard]] int batchRecipeHistoryCurrentIndex() const;
  [[nodiscard]] int selectedBurstSize() const;
  [[nodiscard]] int selectedBurstPosition() const;
  [[nodiscard]] int selectedDuplicateGroupSize() const;
  [[nodiscard]] bool selectedBestShotSuggested() const;
  [[nodiscard]] int selectedSyncGroupSize() const;
  [[nodiscard]] bool selectedOnline() const;
  [[nodiscard]] QString selectedAvailabilityState() const;
  [[nodiscard]] QString selectedAvailabilityTitle() const;
  [[nodiscard]] QString selectedAvailabilityDetail() const;
  [[nodiscard]] bool selectedVirtualCopy() const;
  [[nodiscard]] QString selectedSourceAssetId() const;
  [[nodiscard]] QString selectedVariantName() const;
  [[nodiscard]] int selectedVariantIndex() const;
  [[nodiscard]] QStringList projectNames() const;
  [[nodiscard]] int projectStatsRevision() const;
  [[nodiscard]] QStringList captureYears() const;
  [[nodiscard]] QStringList cameraLabels() const;
  [[nodiscard]] QString projectFilter() const;
  [[nodiscard]] QString importDateFilter() const;
  [[nodiscard]] QString importSessionFromFilter() const;
  [[nodiscard]] QString importSessionToFilter() const;
  [[nodiscard]] QString captureYearFilter() const;
  [[nodiscard]] QString cameraFilter() const;
  [[nodiscard]] QString captureDateFromFilter() const;
  [[nodiscard]] QString captureDateToFilter() const;
  [[nodiscard]] QString mediaTypeFilter() const;
  [[nodiscard]] QString mediaCollectionFilter() const;
  [[nodiscard]] QString reviewFilter() const;
  [[nodiscard]] QString availabilityFilter() const;
  [[nodiscard]] int minimumRatingFilter() const;
  [[nodiscard]] QString searchFilter() const;

  Q_INVOKABLE void refresh();
  void beginCatalogStartup();
  void reportCatalogStartupFailure(const QString& error_message);
  Q_INVOKABLE void clearFilters();
  Q_INVOKABLE void refreshDocumentSourceExclusions();
  QString sortOrder() const { return sort_order_; }
  void setSortOrder(const QString& order);
  QVariantMap selectedMetadata() const;
  int metadataPendingCount() const { return static_cast<int>(pending_metadata_.size()); }
  QString metadataStatus() const { return metadata_status_; }
  bool metadataFileBusy() const { return metadata_file_busy_; }
  Q_INVOKABLE bool saveMetadataToSelected(const QVariantMap& patch);
  Q_INVOKABLE void retryMetadataPersistence();
  Q_INVOKABLE bool readSelectedMetadataSidecars();
  Q_INVOKABLE bool writeSelectedMetadataSidecars();
  bool duplicateAnalysisBusy() const { return duplicate_analysis_busy_; }
  QString duplicateAnalysisStatus() const { return duplicate_analysis_status_; }
  int confirmedDuplicateGroupCount() const;
  Q_INVOKABLE bool analyzeDuplicates();
  Q_INVOKABLE void cancelDuplicateAnalysis();
  Q_INVOKABLE QString originalPreviewSource(const QString& file_path) const;
  Q_INVOKABLE void setImportSessionFilter(const QString& from, const QString& to);
  Q_INVOKABLE void refreshImportSources(bool force = false);
  Q_INVOKABLE void importFolder();
  Q_INVOKABLE void importFolderPath(const QString& folder_path);
  Q_INVOKABLE bool createBlankCanvas(const QVariantMap& settings);
  bool blankCanvasBusy() const;
  bool startPreparedBlankCanvas(const QString& task_id, const QString& document_id,
                                const QString& import_job_id, const QVariantMap& settings);
  bool acceptPreparedBlankCanvasSource(const QString& task_id, const QString& source_sha256);
  bool cancelPreparedBlankCanvas(const QString& task_id);
  [[nodiscard]] bool preparedBlankCanvasBusy(const QString& task_id) const;
  [[nodiscard]] QVariantMap preparedBlankCanvasStatus(const QString& task_id) const;
  [[nodiscard]] QByteArray preparedBlankCanvasCatalogSnapshot(
      const QString& source_path, const QString& task_id, const QString& import_job_id) const;
  [[nodiscard]] AgentTaskJournal::ResourceAdapter preparedBlankCanvasCatalogResourceAdapter(
      const QString& source_path, const QString& task_id, const QString& import_job_id);
  bool restorePreparedBlankCanvasCatalogSnapshot(
      const QString& source_path, const QByteArray& expected, const QByteArray& before, const QString& task_id,
      const QString& import_job_id, QString* error_message = nullptr);
  [[nodiscard]] AgentTaskJournal::ResourceAdapter agentTaskRecipeResourceAdapter(
      const QString& asset_id, const QString& task_id = {});
  // Task writes are admitted before the native save starts. Completion returns
  // a full catalog receipt only when both source identity and the exact authored
  // recipe tuple match the persisted row after the owner has drained its queue.
  bool registerAgentTaskRecipeCheckpoint(
      const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
      const QVariantMap& exact_recipes, const QString& preparing_revision_id = {});
  bool cancelAgentTaskRecipeCheckpoint(
      const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
      const QString& terminal_revision_id = {});
  [[nodiscard]] QByteArray completeAgentTaskRecipeCheckpoint(
      const QString& task_id, const QString& asset_id, const QString& terminal_revision_id = {});
  // Native-photo restoration may persist a blank recipe before the catalog
  // recipe owner reverses it. Register that one exact output before restoring.
  bool registerAgentTaskRecipeRestoreReceipt(
      const QString& task_id, const QString& asset_id, const QByteArray& expected_current,
      const QVariantMap& exact_recipes);
  [[nodiscard]] bool agentTaskRecipePersistenceSettled(const QString& asset_id) const;
  using AgentRecipePersistenceHandler =
      std::function<QVariantMap(const QString&, const QVariantMap&)>;
  void setAgentRecipePersistenceHandler(AgentRecipePersistenceHandler handler) {
    agent_recipe_persistence_handler_ = std::move(handler);
  }
  // Agent patches bind a task ID and immutable revision at edit admission; the
  // persistence handler receives prepare/accepted/finished with the exact
  // bounded stdin payload that the helper process receives.
  int applyAgentAdjustmentPatchJsonToAssetIds(
      const QString& task_id,
      const QStringList& asset_ids,
      const QString& patch_json,
      const QStringList& adjustment_groups = {});
  bool retryAgentAdjustmentPatchPersistence(
      const QString& task_id, const QString& asset_id);
  // Resume a retained task revision after the shared persistence owner reports
  // quiescence. This does not change the revision payload or its identity.
  bool resumeAgentAdjustmentPatchPersistence(const QString& task_id);
  Q_INVOKABLE void importSelection(
      const QString& source_root_path,
      const QStringList& file_paths,
      const QString& project_name);
  Q_INVOKABLE QVariantMap importPreflight(
      const QString& source_root_path,
      const QStringList& file_paths,
      const QString& import_mode,
      const QString& duplicate_policy = QStringLiteral("skip"),
      const QString& organization_template = QStringLiteral("source"),
      const QString& raw_jpeg_policy = QStringLiteral("both")) const;
  QVariantMap importPreflightResult() const { return import_preflight_result_; }
  bool importPreflightBusy() const { return import_preflight_requested_; }
  int importPreflightScannedCount() const { return import_preflight_scanned_count_; }
  Q_INVOKABLE void requestImportPreflight(
      const QString& source_root_path, const QStringList& file_paths,
      const QString& import_mode, const QString& duplicate_policy,
      const QString& organization_template, const QString& raw_jpeg_policy);
  Q_INVOKABLE void cancelImportPreflight();
  Q_INVOKABLE void importSelectionWithOptions(
      const QString& source_root_path,
      const QStringList& file_paths,
      const QString& project_name,
      const QString& import_mode,
      const QString& duplicate_policy = QStringLiteral("skip"),
      const QString& organization_template = QStringLiteral("source"),
      const QString& raw_jpeg_policy = QStringLiteral("both"));
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void selectIndex(int index);
  Q_INVOKABLE void selectIndexWithModifiers(int index, int modifiers, bool selection_mode = false);
  Q_INVOKABLE bool selectAssetById(const QString& asset_id, bool refresh_if_selected = false);
  Q_INVOKABLE QString assetIdForFilePath(const QString& file_path) const;
  Q_INVOKABLE void selectRelative(int offset);
  Q_INVOKABLE QStringList pickFiles(const QString& title = QStringLiteral("Choose Media Files")) const;
  Q_INVOKABLE QString pickFolder(const QString& title = QStringLiteral("Browse Media Folder")) const;
  Q_INVOKABLE QString localPathForUrl(const QString& url) const;
  Q_INVOKABLE QString parentFolderUrl(const QString& url) const;
  Q_INVOKABLE void createProject(const QString& name);
  Q_INVOKABLE void createProjectWithSelectedAssets(const QString& name);
  Q_INVOKABLE void openProject(const QString& project_name);
  Q_INVOKABLE void addAssetToProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE bool addAssetToProjectAsync(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void addSelectedAssetsToProject(const QString& project_name);
  Q_INVOKABLE void removeAssetFromProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void removeSelectedAssetsFromProject(const QString& project_name);
  Q_INVOKABLE void deleteSelectedAssets();
  Q_INVOKABLE bool assetInProject(const QString& asset_id, const QString& project_name) const;
  Q_INVOKABLE int assetCountForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList visibleAssets() const;
  Q_INVOKABLE QVariantList assetsForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList timelinePickerAssets(const QString& project_name = QString()) const;
  Q_INVOKABLE QVariantList mediaPickerAssets(
      const QString& project_name, const QStringList& media_types = {}) const;
  Q_INVOKABLE QVariantMap assetAt(int index) const;
  Q_INVOKABLE QVariantList galleryAssets() const;
  Q_INVOKABLE QVariantMap galleryAssetAt(int index) const;
  Q_INVOKABLE QVariantMap assetById(const QString& asset_id) const;
  Q_INVOKABLE QVariantList surveyAssets(int selected_index, int maximum_count = 6) const;
  Q_INVOKABLE QVariantMap surveyAssetAt(int selected_index, int position, int maximum_count = 6) const;
  Q_INVOKABLE int surveyAssetCount(int selected_index, int maximum_count = 6) const;
  Q_INVOKABLE bool assetSelected(const QString& asset_id) const;
  Q_INVOKABLE void setSelectedRating(int rating);
  Q_INVOKABLE void toggleSelectedPick();
  Q_INVOKABLE void toggleSelectedReject();
  Q_INVOKABLE void setSelectedColorLabel(const QString& color_label);
  Q_INVOKABLE void clearSelectedCullState();
  Q_INVOKABLE void undoCulling();
  Q_INVOKABLE void redoCulling();
  Q_INVOKABLE void toggleSelectionIndex(int index);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void clearSecondarySelection();
  Q_INVOKABLE int applyAdjustmentPatchJsonToAssetIds(
      const QStringList& asset_ids,
      const QString& patch_json,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE int pasteAdjustmentPatchJsonToAssetIds(
      const QStringList& asset_ids, const QString& patch_json);
  Q_INVOKABLE bool prepareRenderedPhotoForEditing(const QString& asset_id);
  int applyTransientAdjustmentPatchJsonToAssetIds(
      const QStringList& asset_ids,
      const QString& patch_json,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE void undoBatchRecipe();
  Q_INVOKABLE void redoBatchRecipe();
  Q_INVOKABLE void restoreBatchRecipeHistoryIndex(int index);
  Q_INVOKABLE void setImportPresetAdjustmentPatchJson(
      const QString& preset_patch_json,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE void clearImportPresetAdjustmentPatchJson();
  Q_INVOKABLE QString createVirtualCopyForAsset(
      const QString& asset_id,
      const QString& variant_name = QString());
  Q_INVOKABLE QString createVirtualCopyForSelectedAsset(const QString& variant_name = QString());
  Q_INVOKABLE QString createCropVariantForAsset(
      const QString& asset_id,
      const QString& variant_name,
      const QString& crop_preset,
      double crop_x = 0.0,
      double crop_y = 0.0,
      double crop_width = 0.0,
      double crop_height = 0.0);
  Q_INVOKABLE QString createCropVariantForSelectedAsset(
      const QString& variant_name,
      const QString& crop_preset,
      double crop_x = 0.0,
      double crop_y = 0.0,
      double crop_width = 0.0,
      double crop_height = 0.0);
  Q_INVOKABLE QStringList recipeVariantAssetIds(const QString& asset_id) const;
  Q_INVOKABLE void refreshOnlineState();
  Q_INVOKABLE int relinkSelectedAssetToFolder(const QString& folder_url);
  Q_INVOKABLE int relinkOfflineAssetsToFolder(const QString& folder_url);
  Q_INVOKABLE bool markSelectedAssetPermanentlyMissing(bool confirmed);
  Q_INVOKABLE void setVisibleAssetRange(int first_index, int last_index);
  void setInteractiveRenderingActive(bool active);
  [[nodiscard]] bool interactiveRenderingActive() const;
  [[nodiscard]] int interactiveRenderingPriorityDepth() const;
  [[nodiscard]] bool waitForInteractiveThumbnailWorkers(int timeout_ms);
  [[nodiscard]] int activeThumbnailWorkerCount() const;
  [[nodiscard]] int pendingThumbnailWorkerCount() const;
  void replaceAssetsForTesting(const QVector<CatalogAssetItem>& items);
  void replaceTimelineAssetsForTesting(const QVector<CatalogAssetItem>& items);
  void clearVirtualCopiesForTesting();
  bool applyThumbnailResultForTesting(
      const QString& asset_id,
      const QString& file_path,
      const QString& adjustment_patch_json,
      const QString& thumbnail_source);

  void setProjectFilter(const QString& project_filter);
  void setImportDateFilter(const QString& import_date_filter);
  void setCaptureYearFilter(const QString& capture_year_filter);
  void setCameraFilter(const QString& camera_filter);
  void setCaptureDateFromFilter(const QString& capture_date_from_filter);
  void setCaptureDateToFilter(const QString& capture_date_to_filter);
  void setMediaTypeFilter(const QString& media_type_filter);
  void setMediaCollectionFilter(const QString& media_collection_filter);
  void setReviewFilter(const QString& review_filter);
  void setAvailabilityFilter(const QString& availability_filter);
  void setMinimumRatingFilter(int minimum_rating_filter);
  void setSearchFilter(const QString& search_filter);
  [[nodiscard]] bool visualSearchActive() const { return visual_search_active_; }
  Q_INVOKABLE void setVisualSearchResults(const QStringList& ranked_asset_ids);
  Q_INVOKABLE void clearVisualSearchResults();

signals:
  void metadataStateChanged();
  void duplicateAnalysisChanged();
  void blankCanvasBusyChanged();
  void blankCanvasCreated(const QString& asset_id);
  void blankCanvasFailed(const QString& error);
  void preparedBlankCanvasSourceCreated(const QString& task_id, const QString& document_id,
                                        const QString& source_path, const QString& source_sha256);
  void preparedBlankCanvasImportAccepted(const QString& task_id, const QString& import_job_id,
                                         const QString& source_path, const QString& source_sha256);
  void preparedBlankCanvasFinished(const QString& task_id, const QString& import_job_id,
                                   const QString& status, const QString& asset_id,
                                   const QString& source_path, const QString& source_sha256,
                                   const QString& error);
  void preparedBlankCanvasChanged(const QString& task_id);
  void agentTaskRecipePersistenceChanged(const QString& asset_id);
  void statusChanged();
  void errorMessageChanged();
  void loadingChanged();
  void importingChanged();
  void deletingChanged();
  void deleteProgressChanged();
  void busyChanged();
  void assetCountChanged();
  void firstVisiblePhotoIndexChanged();
  void totalAssetCountChanged();
  void mediaDurationsChanged();
  void driveCountChanged();
  void importSourcesChanged();
  void importPreflightChanged();
  void selectedIndexChanged();
  void selectedAssetChanged();
  void selectionChanged();
  void surveyAssetsChanged();
  void projectNamesChanged();
  void projectStatsRevisionChanged();
  void captureYearsChanged();
  void cameraLabelsChanged();
  void cullingStatsChanged();
  void cullingHistoryChanged();
  void batchRecipeHistoryChanged();
  void photoRecipeChanged(const QString& asset_id, const QString& file_path,
      const QString& previous_patch, const QString& next_patch, bool restoring_history);
  void filtersChanged();
  // Source/recipe/preview authority changes, including assets outside the view.
  void searchInputsChanged(const QStringList& asset_ids);

 private slots:
  void handleSnapshotFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleSnapshotParsed();
  void handleImportFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleThumbnailFinished();
  void handleMediaDurationFinished();
  void handleTimelineAssetsLoaded();
  void handleDeleteFinished();
  void pollActiveImportJob();
  void handleImportJobPollFinished();
  void handleCullingPersistenceFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleRecipePersistenceFinished(int exit_code, QProcess::ExitStatus exit_status);

 private:
  friend struct CatalogImportPreflightTestAccess;
  friend struct CatalogThumbnailReuseTestAccess;
  friend struct CatalogTimelineReconciliationTestAccess;
  friend struct CatalogFilterBenchmarkAccess;
  friend struct CatalogSnapshotBenchmarkAccess;
  friend struct CatalogSnapshotContractAccess;
  static QThreadPool& importPreflightThreadPool();
  void queueImportPreflight(
      const QString& source_root_path, const QStringList& file_paths,
      const QString& import_mode, const QString& duplicate_policy,
      const QString& organization_template, const QString& raw_jpeg_policy,
      const QString& project_name, bool admission, const QString& prepared_task_id = {});
  void startNextImportPreflight();
  void handleImportPreflightFinished();
  void consumeImportedJobRecords(const QVector<PersistentJobRecord>& records);
  QFutureWatcher<QVariantMap> import_preflight_watcher_;
  QFutureWatcher<QVector<PersistentJobRecord>> import_job_poll_watcher_;
  QFutureWatcher<QVariantMap>* prepared_blank_canvas_watcher_ = nullptr;
  bool blank_canvas_generating_ = false;
  QString pending_blank_canvas_path_;
  bool blank_canvas_import_finished_ = false;
  void failPendingBlankCanvas(const QString& error);
  struct PreparedBlankCanvasOperation;
  void finishPreparedBlankCanvas(const QString& status, const QString& error = {});
  void resolvePreparedBlankCanvasAfterRefresh();
  [[nodiscard]] QByteArray recipeResourceSnapshot(const QString& asset_id) const;
  bool restoreRecipeResourceSnapshot(const QString& asset_id, const QByteArray& expected,
                                     const QByteArray& before, const QString& task_id,
                                     QString* error_message = nullptr);
  bool restorePreparedBlankCanvasCatalogSnapshotExpected(
      const QString& source_path, const QByteArray& expected, const QByteArray& before,
      const QString& task_id, const QString& import_job_id,
      QString* error_message = nullptr);
  QTimer import_preflight_progress_timer_;
  std::shared_ptr<ImportPreflightRequest> active_import_preflight_;
  std::shared_ptr<ImportPreflightRequest> pending_import_preflight_;
  std::shared_ptr<const ImportPreflightScan> import_preflight_scan_;
  QVariantMap import_preflight_result_;
  quint64 import_preflight_generation_ = 0;
  int import_preflight_scanned_count_ = 0;
  bool import_preflight_requested_ = false;
  bool import_preparing_ = false;
  void startSnapshotFetch();
  CustomerEditAccess customer_edit_access_;
  void setStatus(const QString& status);
  void setErrorMessage(const QString& message);
  void setLoading(bool loading);
  void setImporting(bool importing);
  void setDeleting(bool deleting);
  void setDeleteProgress(int delete_progress);
  void setDeleteTargetCount(int delete_target_count);
  bool beginImportJob(
      const QString& folder_path,
      const QString& project_name,
      const QStringList& selected_file_paths,
      int selection_count,
      const QString& import_mode,
      const QJsonObject& import_preflight = {},
      const QString& prepared_task_id = {},
      const QString& prepared_job_id = {},
      const QString& prepared_document_id = {},
      const QString& prepared_source_sha256 = {});
  void finishImportJob(bool success, const QString& message);
  void setDriveCount(int drive_count);
  void updateAssets(QVector<CatalogAssetItem> items,
                    const QHash<QString, QString>& reusable_thumbnail_tokens = {});
  void restartMediaDurationRefresh();
  void prioritizeMediaDurationTasks();
  void startNextMediaDurationTask();
  void removeDeletedAssets(const QStringList& asset_ids);
  void queueThumbnailRefreshes();
  void startNextThumbnailTask();
  bool applyThumbnailResult(const ThumbnailTaskResult& result);
  [[nodiscard]] QString thumbnailQueuePriorityForAssetId(const QString& asset_id) const;
  void setSelectedIndexInternal(int index);
  void startImport(const QString& folder_path);
  void startImport(
      const QString& folder_path,
      const QStringList& selected_file_paths,
      const QString& project_name,
      const QString& import_mode = QStringLiteral("managed"),
      const QString& duplicate_policy = QStringLiteral("skip"),
      const QVariantMap& reviewed_preflight = {},
      const QString& prepared_task_id = {});
  void applyFilters(const QString& preferred_asset_id = {});
  void rebuildAllAssetIndexMap();
  void rebuildVisibleAssetIndexMap();
  void rebuildImportSources();
  void rebuildProjectMetadata();
  void rebuildCaptureYears();
  void rebuildBurstMetadata();
  void rebuildIntelligenceMetadata();
  void reconcileSelection(const QString& preferred_asset_id = {});
  void startTimelineAssetsLoad();
  void loadTimelineAssets();
  void reconcileTimelineAssets();
  void loadProjects();
  void saveProjects() const;
  void scheduleProjectsSave();
  bool runCatalogStateCommand(const QStringList& command_arguments, QString* error_message = nullptr) const;
  void loadPersistedLibraryState(CatalogLibraryState state);
  void addAssetsToProject(const QStringList& asset_ids, const QString& project_name);
  bool changeProjectMemberships(const QString& project_name, const QStringList& asset_ids, bool add, bool open_project = false);
  void loadProjectChanges();
  bool saveProjectChanges(const QJsonObject& changes);
  void overlayProjectChanges(bool acknowledge_snapshot);
  void startNextProjectChange();
  void finalizePendingImportProjectAssignment();
  void bumpProjectStatsRevision();
  [[nodiscard]] QString projectsFilePath() const;
  [[nodiscard]] CullingStateRecord cullingStateForAsset(const QString& asset_id) const;
  void setAssetCullingState(const QString& asset_id, const CullingStateRecord& state);
  void queueCullingPersistence(const QString& asset_id, const CullingStateRecord& state);
  void startNextCullingPersistence();
  void loadCullingChanges();
  void initializeMetadataPersistence();
  bool saveMetadataOutbox(const QHash<QString, QVariantMap>& records);
  void startMetadataPersistence();
  void finishMetadataPersistence(int exit_code, QProcess::ExitStatus exit_status);
  QString metadataAssetId(const CatalogAssetItem& item) const;
  void initializeDuplicateAnalysis();
  void applyConfirmedDuplicateGroups();
  bool saveCullingChanges(const QHash<QString, CullingStateRecord>& states);
  void overlayCullingChanges();
  void queueAdjustmentPatchPersistence(
      const QString& asset_id,
      const QString& adjustment_patch_json,
      const QString& task_id = {});
  void startNextAdjustmentPatchPersistence();
  int applyRecipeStateToAssetIds(
      const QStringList& asset_ids,
      const EditRecipeState& state,
      const QStringList& groups,
      bool transient_preview = false,
      bool clipboard_paste = false,
      const QString& task_id = {});
  int applyExactAdjustmentPatches(
      const QHash<QString, QString>& adjustment_patch_jsons);
  void recordCullingChange(
      const QString& asset_id,
      const CullingStateRecord& before,
      const CullingStateRecord& after,
      const QString& label);
  [[nodiscard]] ProjectRecord* findProject(const QString& project_name);
  [[nodiscard]] const ProjectRecord* findProject(const QString& project_name) const;
  [[nodiscard]] QString normalizedProjectName(const QString& project_name) const;
  [[nodiscard]] bool matchesImportDateFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesImportSessionFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesCaptureYearFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesCameraFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesCaptureDateRangeFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesProjectFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesMediaTypeFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesReviewFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesAvailabilityFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesMinimumRatingFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesSearchFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] const CatalogAssetItem* selectedItem() const;
  [[nodiscard]] QVariantMap assetMap(const CatalogAssetItem& item, bool include_adjustment_patch = true) const;
  [[nodiscard]] QString adjustmentPatchProjection(
      const QString& adjustment_payload) const;
  void refreshVisibleCullingStats();
  void updateFirstVisiblePhotoIndex();
  bool refreshAssetOnlineState(const QString& asset_id);
  int relinkAssetsToFolder(const QString& folder_url, bool selected_only);

  QString snapshotProgram() const;
  QStringList snapshotArguments() const;
  QString importProgram() const;
  QStringList importArguments(const QString& folder_path) const;
  QString stateProgram() const;
  QStringList stateArguments(const QStringList& command_arguments) const;
  QVector<CatalogAssetItem> all_assets_;
  QVector<CatalogAssetItem> assets_;
  QVector<CatalogAssetItem> timeline_assets_;
  int first_visible_photo_index_ = -1;
  QHash<QString, qsizetype> all_asset_index_by_id_;
  QHash<QString, qsizetype> visible_asset_index_by_id_;
  QVariantMap last_timeline_reconciliation_;
  QVector<ProjectRecord> projects_;
  QJsonObject pending_project_changes_;
  QHash<QString, quint64> project_change_revisions_;
  QHash<QString, quint64> attempted_project_change_revisions_;
  QSet<QString> failed_project_changes_;
  quint64 next_project_change_revision_ = 0;
  bool project_changes_readable_ = true;
  bool project_refresh_after_save_ = false;
  QString active_project_change_;
  QFutureWatcher<QVariantMap> project_change_watcher_;
  std::shared_ptr<std::atomic_bool> project_change_cancellation_;
  QHash<QString, CullingStateRecord> culling_states_;
  QVector<CullingHistoryEntry> culling_undo_stack_;
  QVector<CullingHistoryEntry> culling_redo_stack_;
  QVector<BatchRecipeHistoryEntry> batch_recipe_undo_stack_;
  QVector<BatchRecipeHistoryEntry> batch_recipe_redo_stack_;
  QHash<QString, QString>
      adjustment_patch_overrides_;
  QHash<QString, QJsonObject> task_recipe_checkpoint_requests_;
  QHash<QString, QJsonObject> task_recipe_restore_receipts_;
  QHash<QString, QJsonObject> task_recipe_terminal_revisions_;
  QSet<QString> transient_adjustment_asset_ids_;
  mutable QHash<QString, QString>
      adjustment_patch_projection_cache_;
  QString import_preset_adjustment_patch_json_;
  QStringList import_preset_adjustment_groups_;
  QVector<CatalogAssetItem> virtual_copies_;
  QHash<QString, QString> relinked_file_paths_;
  QSet<QString> permanently_missing_asset_ids_;
  QStringList capture_years_;
  QStringList camera_labels_;
  QVariantList import_sources_;
  qint64 import_sources_last_rebuilt_msecs_ = 0;
  QProcess snapshot_process_;
  QFutureWatcher<CatalogSnapshotResult> snapshot_parse_watcher_;
  std::shared_ptr<std::atomic_bool> snapshot_parse_cancellation_;
  quint64 snapshot_request_generation_ = 0;
  quint64 snapshot_fetch_generation_ = 0;
  bool snapshot_fetch_active_ = false;
  bool snapshot_parse_active_ = false;
  QSet<QString> snapshot_changed_review_ids_;
  QSet<QString> snapshot_changed_relink_ids_;
  QSet<QString> snapshot_deleted_asset_ids_;
  QProcess import_process_;
  QProcess culling_persistence_process_;
  QString sort_order_ = QStringLiteral("imported-desc");
  QHash<QString, QVariantMap> asset_metadata_;
  QHash<QString, QVariantMap> pending_metadata_;
  std::unique_ptr<QLockFile> metadata_outbox_lock_;
  QSet<QString> deferred_metadata_ids_;
  QHash<QString, QVariantMap> active_metadata_;
  QSet<QString> changed_metadata_during_snapshot_;
  QProcess metadata_process_;
  QTimer metadata_timeout_;
  std::unique_ptr<QTemporaryFile> metadata_request_;
  QFutureWatcher<QVariantMap> metadata_file_watcher_;
  bool metadata_file_busy_ = false;
  QString metadata_status_;
  bool metadata_outbox_readable_ = true;
  bool metadata_persistence_blocked_ = false;
  QFutureWatcher<CatalogDuplicateAnalysisResult> duplicate_analysis_watcher_;
  bool duplicate_analysis_busy_ = false;
  CatalogDuplicateCancellationToken duplicate_analysis_cancellation_;
  QHash<QString, QString> duplicate_analysis_sources_;
  QHash<QString, QString> confirmed_duplicate_groups_;
  QString duplicate_analysis_status_;
  QTimer culling_save_timer_;
  QTimer culling_timeout_timer_;
  std::unique_ptr<QTemporaryFile> culling_request_;
  QHash<QString, CullingStateRecord> active_culling_persistence_;
  QSet<QString> failed_culling_persistence_;
  bool culling_outbox_readable_ = true;
  bool culling_outbox_dirty_ = false;
  bool culling_persistence_paused_ = false;
  QTimer projects_save_timer_;
  QHash<QString, CullingStateRecord> pending_culling_persistence_;
  QStringList pending_culling_persistence_order_;
  QProcess recipe_persistence_process_;
  struct AdjustmentPatchPersistenceRevision final {
    QString asset_id;
    QString patch_json;
    QString task_id;
    QString revision_id;
  };
  QVector<AdjustmentPatchPersistenceRevision> pending_adjustment_patch_persistence_;
  std::vector<std::unique_ptr<QFutureWatcher<ThumbnailTaskResult>>> thumbnail_watchers_;
  std::vector<std::unique_ptr<QFutureWatcher<CatalogMediaDurationResult>>> media_duration_watchers_;
  QSet<QFutureWatcher<CatalogMediaDurationResult>*> active_media_duration_watchers_;
  QTimer media_duration_notification_timer_;
  QVector<CatalogMediaDurationResult> pending_media_durations_;
  QHash<QString, qsizetype> pending_media_duration_index_by_path_;
  qsizetype next_media_duration_ = 0;
  quint64 media_duration_generation_ = 0;
  std::shared_ptr<std::atomic_bool> media_duration_cancellation_;
  QFutureWatcher<QVector<CatalogAssetItem>> timeline_assets_watcher_;
  QFutureWatcher<DeleteAssetsTaskResult> delete_watcher_;
  std::shared_ptr<std::atomic_bool> delete_cancellation_;
  quint64 delete_request_generation_ = 0;
  QString status_ = QStringLiteral("Idle");
  QString error_message_;
  bool loading_ = false;
  bool importing_ = false;
  bool deleting_ = false;
  int delete_progress_ = 0;
  int delete_target_count_ = 0;
  int drive_count_ = 0;
  int selected_index_ = -1;
  QString selection_anchor_asset_id_;
  QSet<QString> selected_asset_ids_;
  int project_stats_revision_ = 0;
  QString project_filter_ = QStringLiteral("all");
  QString import_date_filter_ = QStringLiteral("all");
  QString import_session_from_filter_;
  QString import_session_to_filter_;
  QString capture_year_filter_ = QStringLiteral("all");
  QString camera_filter_ = QStringLiteral("all");
  QString capture_date_from_filter_;
  QString capture_date_to_filter_;
  QString media_type_filter_ = QStringLiteral("all");
  QString media_collection_filter_ = QStringLiteral("all");
  QSet<QString> document_source_exclusions_;
  QFutureWatcher<QSet<QString>> document_source_exclusions_watcher_;
  bool document_source_exclusions_refresh_pending_ = false;
  QString review_filter_ = QStringLiteral("all");
  QString availability_filter_ = QStringLiteral("all");
  int minimum_rating_filter_ = 0;
  QString search_filter_;
  bool visual_search_active_ = false;
  QHash<QString, int> visual_search_ranks_;
  int visible_picked_count_ = 0;
  int visible_rejected_count_ = 0;
  int visible_unmarked_count_ = 0;
  int visible_rated_count_ = 0;
  int visible_unrated_count_ = 0;
  int visible_online_count_ = 0;
  int visible_offline_count_ = 0;
  QSet<QString> queued_thumbnail_asset_ids_;
  QHash<QString, qint64> thumbnail_queued_at_msecs_;
  QStringList pending_thumbnail_asset_ids_;
  QSet<QString> active_thumbnail_asset_ids_;
  QHash<QFutureWatcher<ThumbnailTaskResult>*, QString> active_thumbnail_asset_by_watcher_;
  QHash<QFutureWatcher<ThumbnailTaskResult>*, std::shared_ptr<std::atomic_bool>>
      active_thumbnail_cancellation_by_watcher_;
  bool thumbnail_work_deferred_for_interactive_render_ = false;
  bool timeline_asset_loading_suppressed_for_testing_ = false;
  int interactive_render_priority_depth_ = 0;
  int visible_thumbnail_first_index_ = -1;
  int visible_thumbnail_last_index_ = -1;
  bool timeline_assets_load_started_ = false;
  bool timeline_assets_loaded_ = false;
  QString import_selection_manifest_path_;
  QStringList pending_import_file_paths_;
  QString pending_import_project_name_;
  PersistentJobRecord active_import_job_;
  std::shared_ptr<PreparedBlankCanvasOperation> prepared_blank_canvas_operation_;
  QString active_adjustment_patch_persistence_asset_id_;
  QString active_adjustment_patch_persistence_task_id_;
  QString active_adjustment_patch_persistence_revision_id_;
  QVariantMap active_adjustment_patch_persistence_context_;
  bool active_adjustment_patch_persistence_owner_accepted_ = true;
  bool adjustment_patch_persistence_queue_blocked_ = false;
  QString adjustment_patch_persistence_waiting_task_id_;
  AgentRecipePersistenceHandler agent_recipe_persistence_handler_;
  QSet<QString> failed_adjustment_patch_persistence_asset_ids_;
  QString import_job_observation_started_at_;
  QSet<QString> observed_finished_import_job_ids_;
  bool import_job_poll_in_flight_ = false;
  bool import_job_poll_dirty_ = false;
};

}  // namespace photo_editor::desktop
