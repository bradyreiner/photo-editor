#pragma once

#include <QAbstractListModel>
#include <QDate>
#include <QDateTime>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

namespace photo_editor::desktop {

struct CatalogAssetItem final {
  QString asset_id;
  QString capture_date;
  QDate capture_date_value;
  QString camera_label;
  QString lens_model;
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
  bool online = false;
};

struct ProjectRecord final {
  QString name;
  QString created_at;
  QSet<QString> asset_ids;
};

class CatalogAssetModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
  Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
  Q_PROPERTY(bool importing READ importing NOTIFY importingChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int assetCount READ assetCount NOTIFY assetCountChanged)
  Q_PROPERTY(int totalAssetCount READ totalAssetCount NOTIFY totalAssetCountChanged)
  Q_PROPERTY(int driveCount READ driveCount NOTIFY driveCountChanged)
  Q_PROPERTY(QVariantList importSources READ importSources NOTIFY importSourcesChanged)
  Q_PROPERTY(QStringList importFileNameFilters READ importFileNameFilters CONSTANT)
  Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectedIndexChanged)
  Q_PROPERTY(QString selectedAssetId READ selectedAssetId NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCameraLabel READ selectedCameraLabel NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedCaptureDate READ selectedCaptureDate NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedImportedAt READ selectedImportedAt NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedLensModel READ selectedLensModel NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedFilePath READ selectedFilePath NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedLocationPath READ selectedLocationPath NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedThumbnailSource READ selectedThumbnailSource NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedRecipeJson READ selectedRecipeJson NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedProjectSummary READ selectedProjectSummary NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedMediaType READ selectedMediaType NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedDurationText READ selectedDurationText NOTIFY selectedAssetChanged)
  Q_PROPERTY(QString selectedTimelineDocumentPath READ selectedTimelineDocumentPath NOTIFY selectedAssetChanged)
  Q_PROPERTY(QStringList projectNames READ projectNames NOTIFY projectNamesChanged)
  Q_PROPERTY(int projectStatsRevision READ projectStatsRevision NOTIFY projectStatsRevisionChanged)
  Q_PROPERTY(QStringList captureYears READ captureYears NOTIFY captureYearsChanged)
  Q_PROPERTY(QString projectFilter READ projectFilter WRITE setProjectFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString importDateFilter READ importDateFilter WRITE setImportDateFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString captureYearFilter READ captureYearFilter WRITE setCaptureYearFilter NOTIFY filtersChanged)
  Q_PROPERTY(QString mediaTypeFilter READ mediaTypeFilter WRITE setMediaTypeFilter NOTIFY filtersChanged)

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
  [[nodiscard]] bool busy() const;
  [[nodiscard]] int assetCount() const;
  [[nodiscard]] int totalAssetCount() const;
  [[nodiscard]] int driveCount() const;
  [[nodiscard]] QVariantList importSources() const;
  [[nodiscard]] QStringList importFileNameFilters() const;
  [[nodiscard]] int selectedIndex() const;
  [[nodiscard]] QString selectedAssetId() const;
  [[nodiscard]] QString selectedCameraLabel() const;
  [[nodiscard]] QString selectedCaptureDate() const;
  [[nodiscard]] QString selectedImportedAt() const;
  [[nodiscard]] QString selectedLensModel() const;
  [[nodiscard]] QString selectedFilePath() const;
  [[nodiscard]] QString selectedLocationPath() const;
  [[nodiscard]] QString selectedThumbnailSource() const;
  [[nodiscard]] QString selectedRecipeJson() const;
  [[nodiscard]] QString selectedProjectSummary() const;
  [[nodiscard]] QString selectedMediaType() const;
  [[nodiscard]] QString selectedDurationText() const;
  [[nodiscard]] QString selectedTimelineDocumentPath() const;
  [[nodiscard]] QStringList projectNames() const;
  [[nodiscard]] int projectStatsRevision() const;
  [[nodiscard]] QStringList captureYears() const;
  [[nodiscard]] QString projectFilter() const;
  [[nodiscard]] QString importDateFilter() const;
  [[nodiscard]] QString captureYearFilter() const;
  [[nodiscard]] QString mediaTypeFilter() const;

  Q_INVOKABLE void refresh();
  Q_INVOKABLE void refreshImportSources();
  Q_INVOKABLE void importFolder();
  Q_INVOKABLE void importFolderPath(const QString& folder_path);
  Q_INVOKABLE void importSelection(
      const QString& source_root_path,
      const QStringList& file_paths,
      const QString& project_name);
  Q_INVOKABLE void selectIndex(int index);
  Q_INVOKABLE QString pickFolder(const QString& title = QStringLiteral("Browse Media Folder")) const;
  Q_INVOKABLE QString localPathForUrl(const QString& url) const;
  Q_INVOKABLE QString parentFolderUrl(const QString& url) const;
  Q_INVOKABLE void createProject(const QString& name);
  Q_INVOKABLE void openProject(const QString& project_name);
  Q_INVOKABLE void addAssetToProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE void removeAssetFromProject(const QString& asset_id, const QString& project_name);
  Q_INVOKABLE bool assetInProject(const QString& asset_id, const QString& project_name) const;
  Q_INVOKABLE int assetCountForProject(const QString& project_name) const;
  Q_INVOKABLE QVariantList timelinePickerAssets(const QString& project_name = QString()) const;

  void setProjectFilter(const QString& project_filter);
  void setImportDateFilter(const QString& import_date_filter);
  void setCaptureYearFilter(const QString& capture_year_filter);
  void setMediaTypeFilter(const QString& media_type_filter);

 signals:
  void statusChanged();
  void errorMessageChanged();
  void loadingChanged();
  void importingChanged();
  void busyChanged();
  void assetCountChanged();
  void totalAssetCountChanged();
  void driveCountChanged();
  void importSourcesChanged();
  void selectedIndexChanged();
  void selectedAssetChanged();
  void projectNamesChanged();
  void projectStatsRevisionChanged();
  void captureYearsChanged();
  void filtersChanged();

 private slots:
  void handleSnapshotFinished(int exit_code, QProcess::ExitStatus exit_status);
  void handleImportFinished(int exit_code, QProcess::ExitStatus exit_status);

 private:
  void setStatus(const QString& status);
  void setErrorMessage(const QString& message);
  void setLoading(bool loading);
  void setImporting(bool importing);
  void setDriveCount(int drive_count);
  void updateAssets(const QVector<CatalogAssetItem>& items);
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
  void loadTimelineAssets();
  void loadProjects();
  void saveProjects() const;
  void addAssetsToProject(const QStringList& asset_ids, const QString& project_name);
  void finalizePendingImportProjectAssignment();
  void bumpProjectStatsRevision();
  [[nodiscard]] QString projectsFilePath() const;
  [[nodiscard]] ProjectRecord* findProject(const QString& project_name);
  [[nodiscard]] const ProjectRecord* findProject(const QString& project_name) const;
  [[nodiscard]] QString normalizedProjectName(const QString& project_name) const;
  [[nodiscard]] bool matchesImportDateFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesCaptureYearFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesProjectFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] bool matchesMediaTypeFilter(const CatalogAssetItem& item) const;
  [[nodiscard]] const CatalogAssetItem* selectedItem() const;
  [[nodiscard]] QVariantMap assetMap(const CatalogAssetItem& item) const;

  QString snapshotProgram() const;
  QStringList snapshotArguments() const;
  QString importProgram() const;
  QStringList importArguments(const QString& folder_path) const;
  bool useBuiltSnapshotBinary() const;
  bool useBuiltImportBinary() const;
  QString builtBinaryPath(const QString& binary_name) const;

  QVector<CatalogAssetItem> all_assets_;
  QVector<CatalogAssetItem> assets_;
  QVector<ProjectRecord> projects_;
  QStringList capture_years_;
  QVariantList import_sources_;
  QProcess snapshot_process_;
  QProcess import_process_;
  QString status_ = QStringLiteral("Idle");
  QString error_message_;
  bool loading_ = false;
  bool importing_ = false;
  int drive_count_ = 0;
  int selected_index_ = -1;
  int project_stats_revision_ = 0;
  QString project_filter_ = QStringLiteral("all");
  QString import_date_filter_ = QStringLiteral("all");
  QString capture_year_filter_ = QStringLiteral("all");
  QString media_type_filter_ = QStringLiteral("all");
  QString import_selection_manifest_path_;
  QStringList pending_import_file_paths_;
  QString pending_import_project_name_;
};

}  // namespace photo_editor::desktop
