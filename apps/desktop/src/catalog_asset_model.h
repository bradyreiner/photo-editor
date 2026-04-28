#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

#include "job_store.h"

namespace photo_editor::desktop {

struct CatalogAssetItem final {
  QString asset_id;
  QString capture_date;
  QDate capture_date_value;
  QString camera_label;
  QString lens_model;
  QString iso_label;
  QString shutter_speed;
  QString aperture_label;
  QString location_path;
  QString file_path;
  QString thumbnail_source;
  QString recipe_json;
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
  QString cull_status = QStringLiteral("unmarked");
  QString burst_key;
  int burst_size = 1;
  int burst_position = 1;
  QString duplicate_group_key;
  int duplicate_group_size = 1;
  int duplicate_group_position = 1;
  bool best_shot_suggested = false;
  QString sync_group_key;
  int sync_group_size = 1;
  bool online = false;
};

struct CullingStateRecord final {
  int rating = 0;
  bool picked = false;
  bool rejected = false;
};

struct ProjectRecord final {
  QString name;
  QString created_at;
  QSet<QString> asset_ids;
};

struct ThumbnailTaskResult final {
  QString asset_id;
  QString file_path;
  QString recipe_json;
  QString thumbnail_source;
};

struct DeleteAssetsTaskResult final {
  QStringList deleted_asset_ids;
  QString error_message;
  int total_count = 0;
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
  Q_PROPERTY(int totalAssetCount READ totalAssetCount NOTIFY totalAssetCountChanged)
  Q_PROPERTY(int driveCount READ driveCount NOTIFY driveCountChanged)
  Q_PROPERTY(QStringList selectedAssetIds READ selectedAssetIds NOTIFY selectionChanged)
  Q_PROPERTY(int selectedAssetCount READ selectedAssetCount NOTIFY selectionChanged)
  Q_PROPERTY(bool hasMultiSelection READ hasMultiSelection NOTIFY selectionChanged)
  Q_PROPERTY(int visiblePickedCount READ visiblePickedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleRejectedCount READ visibleRejectedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleUnmarkedCount READ visibleUnmarkedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleRatedCount READ visibleRatedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleUnratedCount READ visibleUnratedCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleOnlineCount READ visibleOnlineCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(int visibleOfflineCount READ visibleOfflineCount NOTIFY cullingStatsChanged)
  Q_PROPERTY(QVariantList importSources READ importSources NOTIFY importSourcesChanged)
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
  Q_PROPERTY(QString selectedRecipeJson READ selectedRecipeJson NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedProjectSummary READ selectedProjectSummary NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedMediaType READ selectedMediaType NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedDurationText READ selectedDurationText NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedTimelineDocumentPath READ selectedTimelineDocumentPath NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedRating READ selectedRating NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedPicked READ selectedPicked NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedRejected READ selectedRejected NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCullStatus READ selectedCullStatus NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedBurstSize READ selectedBurstSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedBurstPosition READ selectedBurstPosition NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedDuplicateGroupSize READ selectedDuplicateGroupSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedBestShotSuggested READ selectedBestShotSuggested NOTIFY selectedAssetChanged)
  Q_PROPERTY(int selectedSyncGroupSize READ selectedSyncGroupSize NOTIFY selectedAssetChanged)
  Q_PROPERTY(bool selectedOnline READ selectedOnline NOTIFY selectedAssetChanged)
  Q_PROPERTY(QStringList projectNames READ projectNames NOTIFY projectNamesChanged)
  Q_PROPERTY(int projectStatsRevision READ projectStatsRevision NOTIFY projectStatsRevisionChanged)
  Q_PROPERTY(QStringList captureYears READ captureYears NOTIFY captureYearsChanged)
  Q_PROPERTY(QString projectFilter READ projectFilter WRITE setProjectFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString importDateFilter READ importDateFilter WRITE setImportDateFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureYearFilter READ captureYearFilter WRITE setCaptureYearFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString mediaTypeFilter READ mediaTypeFilter WRITE setMediaTypeFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString reviewFilter READ reviewFilter WRITE setReviewFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString availabilityFilter READ availabilityFilter WRITE setAvailabilityFilter NOTIFY filtersChanged)
  Q_PROPERTY(int minimumRatingFilter READ minimumRatingFilter WRITE setMinimumRatingFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString searchFilter READ searchFilter WRITE setSearchFilter NOTIFY filtersChanged)

 public:
  enum Roles {
    AssetIdRole = Qt::UserRole + 1,
    CaptureDateRole,
    CameraLabelRole,
    LensModelRole,
    LocationPathRole,
    FilePathRole,
    ThumbnailSourceRole,
    RecipeJsonRole,
    DriveIdRole,
    ImportModeRole,
    ImportedAtRole,
    ProjectNamesRole,
    ProjectSummaryRole,
    MediaTypeRole,
    DurationTextRole,
    TimelineDocumentPathRole,
    RatingRole,
    PickedRole,
    RejectedRole,
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
  };

  explicit CatalogAssetModel(QObject* parent = nullptr);

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
  [[nodiscard]] int totalAssetCount() const;
  [[nodiscard]] int driveCount() const;
  [[nodiscard]] QStringList selectedAssetIds() const;
  [[nodiscard]] int selectedAssetCount() const;
  [[nodiscard]] bool hasMultiSelection() const;
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
  [[nodiscard]] QString selectedRecipeJson() const;
  [[nodiscard]] QString selectedProjectSummary() const;
  [[nodiscard]] QString selectedMediaType() const;
  [[nodiscard]] QString selectedDurationText() const;
  [[nodiscard]] QString selectedTimelineDocumentPath() const;
  [[nodiscard]] int selectedRating() const;
  [[nodiscard]] bool selectedPicked() const;
  [[nodiscard]] bool selectedRejected() const;
  [[nodiscard]] QString selectedCullStatus() const;
  [[nodiscard]] int selectedBurstSize() const;
  [[nodiscard]] int selectedBurstPosition() const;
  [[nodiscard]] int selectedDuplicateGroupSize() const;
  [[nodiscard]] bool selectedBestShotSuggested() const;
  [[nodiscard]] int selectedSyncGroupSize() const;
  [[nodiscard]] bool selectedOnline() const;
  [[nodiscard]] QStringList projectNames() const;
  [[nodiscard]] int projectStatsRevision() const;
  [[nodiscard]] QStringList captureYears() const;
  [[nodiscard]] QString projectFilter() const;
  [[nodiscard]] QString importDateFilter() const;
  [[nodiscard]] QString captureYearFilter() const;
  [[nodiscard]] QString mediaTypeFilter() const;
  [[nodiscard]] QString reviewFilter() const;
  [[nodiscard]] QString availabilityFilter() const;
  [[nodiscard]] int minimumRatingFilter() const;
  [[nodiscard]] QString searchFilter() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void refreshImportSources();
  Q_INVOKABLE void importFolder();
  Q_INVOKABLE void importFolderPath(const QString& folder_path);
  Q_INVOKABLE void importSelection(
      const QString& source_root_path,
      const QStringList& file_paths,
      const QString& project_name);
  Q_INVOKABLE void selectAll();
  Q_INVOKABLE void selectIndex(int index);
  Q_INVOKABLE void selectRelative(int offset);
  Q_INVOKABLE QString pickFolder(const QString& title = QStringLiteral("Browse Media Folder")) const;
  Q_INVOKABLE QString localPathForUrl(const QString& url) const;
  Q_INVOKABLE QString parentFolderUrl(const QString& url) const;
  Q_INVOKABLE void createProject(const QString& name);
  Q_INVOKABLE void openProject(const QString& project_name);
  Q_INVOKABLE void addAssetToProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void addSelectedAssetsToProject(const QString& project_name);
  Q_INVOKABLE void removeAssetFromProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void deleteSelectedAssets();
  Q_INVOKABLE bool assetInProject(const QString& asset_id, const QString& project_name) const;
  Q_INVOKABLE int assetCountForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList timelinePickerAssets(const QString& project_name = QString()) const;
  Q_INVOKABLE QVariantMap assetAt(int index) const;
  Q_INVOKABLE bool assetSelected(const QString& asset_id) const;
  Q_INVOKABLE void setSelectedRating(int rating);
  Q_INVOKABLE void toggleSelectedPick();
  Q_INVOKABLE void toggleSelectedReject();
  Q_INVOKABLE void clearSelectedCullState();
  Q_INVOKABLE void toggleSelectionIndex(int index);
  Q_INVOKABLE void clearSelection();
  Q_INVOKABLE void clearSecondarySelection();
  Q_INVOKABLE int applyRecipeJsonToAssetIds(
      const QStringList& asset_ids,
      const QString& recipe_json,
      const QStringList& adjustment_groups = {});
  Q_INVOKABLE void refreshOnlineState();
  Q_INVOKABLE int relinkSelectedAssetToFolder(const QString& folder_url);
  Q_INVOKABLE int relinkOfflineAssetsToFolder(const QString& folder_url);
  void replaceAssetsForTesting(const QVector<CatalogAssetItem>& items);

  void setProjectFilter(const QString& project_filter);
  void setImportDateFilter(const QString& import_date_filter);
  void setCaptureYearFilter(const QString& capture_year_filter);
  void setMediaTypeFilter(const QString& media_type_filter);
  void setReviewFilter(const QString& review_filter);
  void setAvailabilityFilter(const QString& availability_filter);
  void setMinimumRatingFilter(int minimum_rating_filter);
  void setSearchFilter(const QString& search_filter);

 signals:
  void statusChanged();
  void errorMessageChanged();
  void loadingChanged();
  void importingChanged();
  void deletingChanged();
  void deleteProgressChanged();
  void busyChanged();
  void assetCountChanged();
  void totalAssetCountChanged();
  void driveCountChanged();
  void importSourcesChanged();
  void selectedIndexChanged();
  void selectedAssetChanged();
  void selectionChanged();
  void projectNamesChanged();
  void projectStatsRevisionChanged();
  void captureYearsChanged();
  void cullingStatsChanged();
  void filtersChanged();

 private slots:
  void handleSnapshotFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleImportFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleThumbnailFinished();
  void handleDeleteFinished();
  void pollActiveImportJob();

 private:
  void setStatus(const QString& status);
  void setErrorMessage(const QString& message);
  void setLoading(bool loading);
  void setImporting(bool importing);
  void setDeleting(bool deleting);
  void setDeleteProgress(int delete_progress);
  void setDeleteTargetCount(int delete_target_count);
  void beginImportJob(
      const QString& folder_path,
      const QString& project_name,
      const QStringList& selected_file_paths,
      int selection_count);
  void finishImportJob(bool success, const QString& message);
  void setDriveCount(int drive_count);
  void updateAssets(const QVector<CatalogAssetItem>& items);
  void removeDeletedAssets(const QStringList& asset_ids);
  void queueThumbnailRefreshes();
  void startNextThumbnailTask();
  void applyThumbnailResult(const ThumbnailTaskResult& result);
  void setSelectedIndexInternal(int index);
  void startImport(const QString& folder_path);
  void startImport(
      const QString& folder_path,
      const QStringList& selected_file_paths,
      const QString& project_name);
  void applyFilters(const QString& preferred_asset_id = {});
  void rebuildImportSources();
  void rebuildProjectMetadata();
  void rebuildCaptureYears();
  void rebuildBurstMetadata();
  void rebuildIntelligenceMetadata();
  void reconcileSelection(const QString& preferred_asset_id = {});
  void loadTimelineAssets();
  void loadProjects();
  void saveProjects() const;
  bool runCatalogStateCommand(const QStringList& command_arguments, QString* error_message = nullptr) const;
  void loadPersistedLibraryState(const QJsonObject& root);
  void addAssetsToProject(const QStringList& asset_ids, const QString& project_name);
  void finalizePendingImportProjectAssignment();
  void bumpProjectStatsRevision();
  [[nodiscard]] QString projectsFilePath() const;
  [[nodiscard]] CullingStateRecord cullingStateForAsset(const QString& asset_id) const;
  void setAssetCullingState(const QString& asset_id, const CullingStateRecord& state);
  [[nodiscard]] ProjectRecord* findProject(const QString& project_name);
  [[nodiscard]] const ProjectRecord* findProject(const QString& project_name) const;
  [[nodiscard]] QString normalizedProjectName(const QString& project_name) const;
  [[nodiscard]] bool matchesImportDateFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesCaptureYearFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesProjectFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesMediaTypeFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesReviewFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesAvailabilityFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesMinimumRatingFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesSearchFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] const CatalogAssetItem* selectedItem() const;
  [[nodiscard]] QVariantMap assetMap(const CatalogAssetItem& item) const;
  void refreshVisibleCullingStats();
  bool refreshAssetOnlineState(const QString& asset_id);
  int relinkAssetsToFolder(const QString& folder_url, bool selected_only);

  QString snapshotProgram() const;
  QStringList snapshotArguments() const;
  QString importProgram() const;
  QStringList importArguments(const QString& folder_path) const;
  QString stateProgram() const;
  QStringList stateArguments(const QStringList& command_arguments) const;
  bool useBuiltSnapshotBinary() const;
  bool useBuiltImportBinary() const;
  bool useBuiltStateBinary() const;
  QString builtBinaryPath(const QString& binary_name) const;

  QVector<CatalogAssetItem> all_assets_;
  QVector<CatalogAssetItem> assets_;
  QVector<ProjectRecord> projects_;
  QHash<QString, CullingStateRecord> culling_states_;
  QHash<QString, QString> recipe_overrides_;
  QHash<QString, QString> relinked_file_paths_;
  QStringList capture_years_;
  QVariantList import_sources_;
  QProcess snapshot_process_;
  QProcess import_process_;
  QFutureWatcher<ThumbnailTaskResult> thumbnail_watcher_;
  QFutureWatcher<DeleteAssetsTaskResult> delete_watcher_;
  QString status_ = QStringLiteral("Idle");
  QString error_message_;
  bool loading_ = false;
  bool importing_ = false;
  bool deleting_ = false;
  int delete_progress_ = 0;
  int delete_target_count_ = 0;
  int drive_count_ = 0;
  int selected_index_ = -1;
  QSet<QString> selected_asset_ids_;
  int project_stats_revision_ = 0;
  QString project_filter_ = QStringLiteral("all");
  QString import_date_filter_ = QStringLiteral("all");
  QString capture_year_filter_ = QStringLiteral("all");
  QString media_type_filter_ = QStringLiteral("all");
  QString review_filter_ = QStringLiteral("all");
  QString availability_filter_ = QStringLiteral("all");
  int minimum_rating_filter_ = 0;
  QString search_filter_;
  int visible_picked_count_ = 0;
  int visible_rejected_count_ = 0;
  int visible_unmarked_count_ = 0;
  int visible_rated_count_ = 0;
  int visible_unrated_count_ = 0;
  int visible_online_count_ = 0;
  int visible_offline_count_ = 0;
  QSet<QString> queued_thumbnail_asset_ids_;
  QStringList pending_thumbnail_asset_ids_;
  QString active_thumbnail_asset_id_;
  QString import_selection_manifest_path_;
  QStringList pending_import_file_paths_;
  QString pending_import_project_name_;
  PersistentJobRecord active_import_job_;
  QString import_job_observation_started_at_;
  QSet<QString> observed_finished_import_job_ids_;
};

}  // namespace photo_editor::desktop
