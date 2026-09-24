#pragma once

#include "customer_edit_access.h"

#include <QAbstractListModel>
#include <QDate>
#include <QDateTime>
#include <QFutureWatcher>
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
#include <memory>
#include <vector>

#include "job_store.h"

namespace photo_editor::desktop {

struct EditRecipeState;

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
  // Set only by a thumbnail completion matching the current source and recipe.
  // Historical stale-result diagnostics are not current-preview provenance.
  bool search_preview_current = false;
  bool thumbnail_stale_detected = false;
  bool thumbnail_stale_mismatch = false;
  QString thumbnail_stale_detection_reason;
  QString thumbnail_stale_previous_recipe_hash;
  QString thumbnail_stale_current_recipe_hash;
  QString thumbnail_stale_detection_signature;
  QString adjustment_patch_json;
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

struct ThumbnailTaskResult final {
  QString asset_id;
  QString file_path;
  QString adjustment_patch_json;
  QString thumbnail_source;
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
  Q_PROPERTY(QString captureYearFilter READ captureYearFilter WRITE setCaptureYearFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString cameraFilter READ cameraFilter WRITE setCameraFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureDateFromFilter READ captureDateFromFilter WRITE setCaptureDateFromFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureDateToFilter READ captureDateToFilter WRITE setCaptureDateToFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString mediaTypeFilter READ mediaTypeFilter WRITE setMediaTypeFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString reviewFilter READ reviewFilter WRITE setReviewFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString availabilityFilter READ availabilityFilter WRITE setAvailabilityFilter NOTIFY filtersChanged)
  Q_PROPERTY(int minimumRatingFilter READ minimumRatingFilter WRITE setMinimumRatingFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString searchFilter READ searchFilter WRITE setSearchFilter NOTIFY filtersChanged)
  Q_PROPERTY(bool visualSearchActive READ visualSearchActive NOTIFY filtersChanged)

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
  [[nodiscard]] QString captureYearFilter() const;
  [[nodiscard]] QString cameraFilter() const;
  [[nodiscard]] QString captureDateFromFilter() const;
  [[nodiscard]] QString captureDateToFilter() const;
  [[nodiscard]] QString mediaTypeFilter() const;
  [[nodiscard]] QString reviewFilter() const;
  [[nodiscard]] QString availabilityFilter() const;
  [[nodiscard]] int minimumRatingFilter() const;
  [[nodiscard]] QString searchFilter() const;

  Q_INVOKABLE void refresh();
  void beginCatalogStartup();
  void reportCatalogStartupFailure(const QString& error_message);
  Q_INVOKABLE void clearFilters();
  Q_INVOKABLE void refreshImportSources(bool force = false);
  Q_INVOKABLE void importFolder();
  Q_INVOKABLE void importFolderPath(const QString& folder_path);
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
  Q_INVOKABLE void openProject(const QString& project_name);
  Q_INVOKABLE void addAssetToProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void addSelectedAssetsToProject(const QString& project_name);
  Q_INVOKABLE void removeAssetFromProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void deleteSelectedAssets();
  Q_INVOKABLE bool assetInProject(const QString& asset_id, const QString& project_name) const;
  Q_INVOKABLE int assetCountForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList visibleAssets() const;
  Q_INVOKABLE QVariantList assetsForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList timelinePickerAssets(const QString& project_name = QString()) const;
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
  void setReviewFilter(const QString& review_filter);
  void setAvailabilityFilter(const QString& availability_filter);
  void setMinimumRatingFilter(int minimum_rating_filter);
  void setSearchFilter(const QString& search_filter);
  [[nodiscard]] bool visualSearchActive() const { return visual_search_active_; }
  Q_INVOKABLE void setVisualSearchResults(const QStringList& ranked_asset_ids);
  Q_INVOKABLE void clearVisualSearchResults();

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
  void handleImportFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleThumbnailFinished();
  void handleDeleteFinished();
  void pollActiveImportJob();
  void handleCullingPersistenceFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleRecipePersistenceFinished(int exit_code, QProcess::ExitStatus exit_status);

 private:
  CustomerEditAccess customer_edit_access_;
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
      int selection_count,
      const QString& import_mode,
      const QJsonObject& import_preflight = {});
  void finishImportJob(bool success, const QString& message);
  void setDriveCount(int drive_count);
  void updateAssets(const QVector<CatalogAssetItem>& items);
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
      const QVariantMap& reviewed_preflight = {});
  void applyFilters(const QString& preferred_asset_id = {});
  void rebuildAllAssetIndexMap();
  void rebuildVisibleAssetIndexMap();
  void rebuildImportSources();
  void rebuildProjectMetadata();
  void rebuildCaptureYears();
  void rebuildBurstMetadata();
  void rebuildIntelligenceMetadata();
  void reconcileSelection(const QString& preferred_asset_id = {});
  void loadTimelineAssets();
  void loadProjects();
  void saveProjects() const;
  void scheduleProjectsSave();
  bool runCatalogStateCommand(const QStringList& command_arguments, QString* error_message = nullptr) const;
  void loadPersistedLibraryState(const QJsonObject& root);
  void addAssetsToProject(const QStringList& asset_ids, const QString& project_name);
  void finalizePendingImportProjectAssignment();
  void bumpProjectStatsRevision();
  [[nodiscard]] QString projectsFilePath() const;
  [[nodiscard]] CullingStateRecord cullingStateForAsset(const QString& asset_id) const;
  void setAssetCullingState(const QString& asset_id, const CullingStateRecord& state);
  void queueCullingPersistence(const QString& asset_id, const CullingStateRecord& state);
  void startNextCullingPersistence();
  void queueAdjustmentPatchPersistence(
      const QString& asset_id,
      const QString& adjustment_patch_json);
  void startNextAdjustmentPatchPersistence();
  int applyRecipeStateToAssetIds(
      const QStringList& asset_ids,
      const EditRecipeState& state,
      const QStringList& groups,
      bool transient_preview = false,
      bool clipboard_paste = false);
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
  [[nodiscard]] QVariantMap assetMap(const CatalogAssetItem& item) const;
  [[nodiscard]] QString adjustmentPatchProjection(
      const QString& adjustment_payload) const;
  void refreshVisibleCullingStats();
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
  QHash<QString, qsizetype> all_asset_index_by_id_;
  QHash<QString, qsizetype> visible_asset_index_by_id_;
  QVector<ProjectRecord> projects_;
  QHash<QString, CullingStateRecord> culling_states_;
  QVector<CullingHistoryEntry> culling_undo_stack_;
  QVector<CullingHistoryEntry> culling_redo_stack_;
  QVector<BatchRecipeHistoryEntry> batch_recipe_undo_stack_;
  QVector<BatchRecipeHistoryEntry> batch_recipe_redo_stack_;
  QHash<QString, QString>
      adjustment_patch_overrides_;
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
  QProcess import_process_;
  QProcess culling_persistence_process_;
  QTimer projects_save_timer_;
  QHash<QString, CullingStateRecord> pending_culling_persistence_;
  QStringList pending_culling_persistence_order_;
  QProcess recipe_persistence_process_;
  QHash<QString, QString>
      pending_adjustment_patch_persistence_;
  QStringList
      pending_adjustment_patch_persistence_order_;
  std::vector<std::unique_ptr<QFutureWatcher<ThumbnailTaskResult>>> thumbnail_watchers_;
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
  QString capture_year_filter_ = QStringLiteral("all");
  QString camera_filter_ = QStringLiteral("all");
  QString capture_date_from_filter_;
  QString capture_date_to_filter_;
  QString media_type_filter_ = QStringLiteral("all");
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
  int interactive_render_priority_depth_ = 0;
  int visible_thumbnail_first_index_ = -1;
  int visible_thumbnail_last_index_ = -1;
  QString import_selection_manifest_path_;
  QStringList pending_import_file_paths_;
  QString pending_import_project_name_;
  PersistentJobRecord active_import_job_;
  QString import_job_observation_started_at_;
  QSet<QString> observed_finished_import_job_ids_;
};

}  // namespace photo_editor::desktop
