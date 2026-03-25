#include "catalog_asset_model.h"

#include <algorithm>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

namespace photo_editor::desktop {

namespace {

constexpr int kThumbnailEdge = 640;

QString format_date(const QDate& date, const QString& fallback) {
  if (!date.isValid()) {
    return fallback;
  }

  return QLocale().toString(date, QStringLiteral("MMM d, yyyy"));
}

QString format_datetime(const QDateTime& datetime, const QString& fallback) {
  if (!datetime.isValid()) {
    return fallback;
  }

  return QLocale().toString(datetime, QStringLiteral("MMM d, yyyy  h:mm AP"));
}

QDate parse_capture_date(const QJsonObject& asset) {
  const auto year = asset.value("capture_year").toInt();
  const auto month = asset.value("capture_month").toInt();
  const auto day = asset.value("capture_day").toInt();

  const QDate capture_date(year, month, day);
  return capture_date.isValid() ? capture_date : QDate{};
}

QString format_capture_date(const QJsonObject& asset, QDate* capture_date_value) {
  const auto capture_date = parse_capture_date(asset);
  if (capture_date_value != nullptr) {
    *capture_date_value = capture_date;
  }

  return format_date(capture_date, QStringLiteral("Unknown capture date"));
}

QDateTime parse_imported_at(const QJsonObject& asset) {
  const auto imported_at = asset.value("imported_at").toString().trimmed();
  if (imported_at.isEmpty()) {
    return {};
  }

  auto parsed = QDateTime::fromString(imported_at, Qt::ISODateWithMs);
  if (!parsed.isValid()) {
    parsed = QDateTime::fromString(imported_at, Qt::ISODate);
  }
  if (!parsed.isValid()) {
    return {};
  }

  if (parsed.timeSpec() == Qt::UTC || parsed.timeSpec() == Qt::OffsetFromUTC) {
    parsed = parsed.toLocalTime();
  }

  return parsed;
}

QString format_camera_label(const QJsonObject& asset) {
  const auto make = asset.value("camera_make").toString().trimmed();
  const auto model = asset.value("camera_model").toString().trimmed();

  if (make.isEmpty() && model.isEmpty()) {
    return QStringLiteral("Unknown camera");
  }

  if (make.isEmpty()) {
    return model;
  }

  if (model.isEmpty()) {
    return make;
  }

  return QStringLiteral("%1 %2").arg(make, model);
}

QString format_location_path(const QJsonObject& asset) {
  const auto location = asset.value("location").toObject();
  const auto root_path = location.value("root_path").toString();
  const auto relative_path = location.value("relative_path").toString();

  if (root_path.isEmpty()) {
    return relative_path;
  }

  if (relative_path.isEmpty()) {
    return root_path;
  }

  return QStringLiteral("%1/%2").arg(root_path, relative_path);
}

QString filesystem_path(const QJsonObject& asset) {
  const auto location = asset.value("location").toObject();
  const auto root_path = location.value("root_path").toString();
  const auto relative_path = location.value("relative_path").toString();

  if (root_path.isEmpty()) {
    return relative_path;
  }

  return QDir(root_path).filePath(relative_path);
}

bool preview_supported(const QString& file_path) {
  const QFileInfo file_info(file_path);
  const auto suffix = file_info.suffix().toLower().toUtf8();
  return QImageReader::supportedImageFormats().contains(suffix);
}

QString thumbnail_cache_root() {
  const auto cache_root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (cache_root.isEmpty()) {
    return QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-cache"));
  }

  return QDir(cache_root).filePath(QStringLiteral("thumbnails"));
}

QString thumbnail_cache_path_for_file(const QString& file_path) {
  const auto hash =
      QCryptographicHash::hash(file_path.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QDir(thumbnail_cache_root()).filePath(QStringLiteral("%1.jpg").arg(QString::fromLatin1(hash)));
}

QString ensure_thumbnail(const QString& file_path) {
  if (!preview_supported(file_path)) {
    return {};
  }

  const QFileInfo source_info(file_path);
  if (!source_info.exists() || !source_info.isReadable()) {
    return {};
  }

  const auto cache_path = thumbnail_cache_path_for_file(file_path);
  const QFileInfo cache_info(cache_path);

  if (cache_info.exists() && cache_info.lastModified() >= source_info.lastModified()) {
    return QUrl::fromLocalFile(cache_path).toString();
  }

  QDir().mkpath(thumbnail_cache_root());

  QImageReader reader(file_path);
  if (!reader.canRead()) {
    return {};
  }

  const auto original_size = reader.size();
  if (original_size.isValid()) {
    const auto scaled = original_size.scaled(
        kThumbnailEdge,
        kThumbnailEdge,
        Qt::KeepAspectRatio);
    reader.setScaledSize(scaled);
  }

  const QImage image = reader.read();
  if (image.isNull()) {
    return {};
  }

  QImageWriter writer(cache_path, "jpg");
  writer.setQuality(86);
  if (!writer.write(image)) {
    return {};
  }

  return QUrl::fromLocalFile(cache_path).toString();
}

QString thumbnail_source_for_path(const QString& file_path) {
  return ensure_thumbnail(file_path);
}

QString derive_drive_id(const QString& folder_path) {
  const auto digest = QCryptographicHash::hash(folder_path.toUtf8(), QCryptographicHash::Sha1).toHex();
  return QStringLiteral("drive-%1").arg(QString::fromLatin1(digest.left(12)));
}

QString derive_drive_name(const QString& folder_path) {
  const QFileInfo info(folder_path);
  if (!info.fileName().isEmpty()) {
    return info.fileName();
  }

  return folder_path;
}

QString project_summary_for_names(const QStringList& project_names) {
  if (project_names.isEmpty()) {
    return QStringLiteral("No projects");
  }

  if (project_names.size() == 1) {
    return project_names.first();
  }

  return QStringLiteral("%1  +%2").arg(project_names.first()).arg(project_names.size() - 1);
}

QVector<CatalogAssetItem> parse_assets(const QJsonArray& asset_array) {
  QVector<CatalogAssetItem> items;
  items.reserve(asset_array.size());

  for (const auto& asset_value : asset_array) {
    const auto asset = asset_value.toObject();
    const auto location = asset.value("location").toObject();

    CatalogAssetItem item;
    item.asset_id = asset.value("asset_id").toString();
    item.capture_date = format_capture_date(asset, &item.capture_date_value);
    item.camera_label = format_camera_label(asset);
    item.lens_model = asset.value("lens_model").toString().trimmed();
    item.location_path = format_location_path(asset);
    item.file_path = filesystem_path(asset);
    item.thumbnail_source = thumbnail_source_for_path(item.file_path);
    item.imported_at_value = parse_imported_at(asset);
    item.imported_at = format_datetime(item.imported_at_value, QStringLiteral("Unknown import time"));
    const auto recipe_value = asset.value("edit_recipe");
    if (recipe_value.isObject()) {
      item.recipe_json = QString::fromUtf8(QJsonDocument(recipe_value.toObject()).toJson(QJsonDocument::Compact));
    }
    item.drive_id = location.value("drive_id").toString();
    item.import_mode = asset.value("import_mode").toString();
    item.project_summary = QStringLiteral("No projects");
    item.online = location.value("online").toBool();
    items.push_back(item);
  }

  return items;
}

}  // namespace

CatalogAssetModel::CatalogAssetModel(QObject* parent) : QAbstractListModel(parent) {
  loadProjects();

  snapshot_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &snapshot_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleSnapshotFinished);

  import_process_.setProcessChannelMode(QProcess::MergedChannels);
  connect(
      &import_process_,
      &QProcess::finished,
      this,
      &CatalogAssetModel::handleImportFinished);
}

int CatalogAssetModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) {
    return 0;
  }

  return static_cast<int>(assets_.size());
}

QVariant CatalogAssetModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= assets_.size()) {
    return {};
  }

  const auto& item = assets_.at(index.row());

  switch (role) {
    case AssetIdRole:
      return item.asset_id;
    case CaptureDateRole:
      return item.capture_date;
    case CameraLabelRole:
      return item.camera_label;
    case LensModelRole:
      return item.lens_model;
    case LocationPathRole:
      return item.location_path;
    case FilePathRole:
      return item.file_path;
    case ThumbnailSourceRole:
      return item.thumbnail_source;
    case RecipeJsonRole:
      return item.recipe_json;
    case DriveIdRole:
      return item.drive_id;
    case ImportModeRole:
      return item.import_mode;
    case ImportedAtRole:
      return item.imported_at;
    case ProjectNamesRole:
      return item.project_names;
    case ProjectSummaryRole:
      return item.project_summary;
    case OnlineRole:
      return item.online;
    default:
      return {};
  }
}

QHash<int, QByteArray> CatalogAssetModel::roleNames() const {
  return {
      {AssetIdRole, "assetId"},
      {CaptureDateRole, "captureDate"},
      {CameraLabelRole, "cameraLabel"},
      {LensModelRole, "lensModel"},
      {LocationPathRole, "locationPath"},
      {FilePathRole, "filePath"},
      {ThumbnailSourceRole, "thumbnailSource"},
      {RecipeJsonRole, "recipeJson"},
      {DriveIdRole, "driveId"},
      {ImportModeRole, "importMode"},
      {ImportedAtRole, "importedAt"},
      {ProjectNamesRole, "projectNames"},
      {ProjectSummaryRole, "projectSummary"},
      {OnlineRole, "online"},
  };
}

QString CatalogAssetModel::status() const { return status_; }

QString CatalogAssetModel::errorMessage() const { return error_message_; }

bool CatalogAssetModel::loading() const { return loading_; }

bool CatalogAssetModel::importing() const { return importing_; }

bool CatalogAssetModel::busy() const { return loading_ || importing_; }

int CatalogAssetModel::assetCount() const { return static_cast<int>(assets_.size()); }

int CatalogAssetModel::totalAssetCount() const { return static_cast<int>(all_assets_.size()); }

int CatalogAssetModel::driveCount() const { return drive_count_; }

int CatalogAssetModel::selectedIndex() const { return selected_index_; }

QString CatalogAssetModel::selectedAssetId() const {
  if (const auto* item = selectedItem()) {
    return item->asset_id;
  }
  return {};
}

QString CatalogAssetModel::selectedCameraLabel() const {
  if (const auto* item = selectedItem()) {
    return item->camera_label;
  }
  return {};
}

QString CatalogAssetModel::selectedCaptureDate() const {
  if (const auto* item = selectedItem()) {
    return item->capture_date;
  }
  return {};
}

QString CatalogAssetModel::selectedImportedAt() const {
  if (const auto* item = selectedItem()) {
    return item->imported_at;
  }
  return {};
}

QString CatalogAssetModel::selectedLensModel() const {
  if (const auto* item = selectedItem()) {
    return item->lens_model;
  }
  return {};
}

QString CatalogAssetModel::selectedFilePath() const {
  if (const auto* item = selectedItem()) {
    return item->file_path;
  }
  return {};
}

QString CatalogAssetModel::selectedLocationPath() const {
  if (const auto* item = selectedItem()) {
    return item->location_path;
  }
  return {};
}

QString CatalogAssetModel::selectedThumbnailSource() const {
  if (const auto* item = selectedItem()) {
    return item->thumbnail_source;
  }
  return {};
}

QString CatalogAssetModel::selectedRecipeJson() const {
  if (const auto* item = selectedItem()) {
    return item->recipe_json;
  }
  return {};
}

QString CatalogAssetModel::selectedProjectSummary() const {
  if (const auto* item = selectedItem()) {
    return item->project_summary;
  }
  return QStringLiteral("No projects");
}

QStringList CatalogAssetModel::projectNames() const {
  QStringList names;
  names.reserve(projects_.size());
  for (const auto& project : projects_) {
    names.push_back(project.name);
  }
  return names;
}

QStringList CatalogAssetModel::captureYears() const { return capture_years_; }

QString CatalogAssetModel::projectFilter() const { return project_filter_; }

QString CatalogAssetModel::importDateFilter() const { return import_date_filter_; }

QString CatalogAssetModel::captureYearFilter() const { return capture_year_filter_; }

void CatalogAssetModel::refresh() {
  if (snapshot_process_.state() != QProcess::NotRunning) {
    snapshot_process_.kill();
    snapshot_process_.waitForFinished(1000);
  }

  setLoading(true);
  setErrorMessage({});
  setStatus(QStringLiteral("Loading catalog snapshot..."));

  snapshot_process_.setWorkingDirectory(QDir::currentPath());
  auto environment = QProcessEnvironment::systemEnvironment();
  snapshot_process_.setProcessEnvironment(environment);
  snapshot_process_.start(snapshotProgram(), snapshotArguments());
}

void CatalogAssetModel::importFolder() {
  if (busy()) {
    return;
  }

  const auto folder_path = QFileDialog::getExistingDirectory(
      nullptr,
      QStringLiteral("Import Photo Folder"),
      QDir::homePath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (folder_path.isEmpty()) {
    return;
  }

  startImport(folder_path);
}

void CatalogAssetModel::importFolderPath(const QString& folder_path) {
  if (busy()) {
    return;
  }

  const auto normalized_path = folder_path.trimmed();
  if (normalized_path.isEmpty()) {
    return;
  }

  startImport(normalized_path);
}

void CatalogAssetModel::startImport(const QString& folder_path) {
  setImporting(true);
  setErrorMessage({});
  setStatus(QStringLiteral("Importing folder into catalog..."));

  import_process_.setWorkingDirectory(QDir::currentPath());
  auto environment = QProcessEnvironment::systemEnvironment();
  import_process_.setProcessEnvironment(environment);
  import_process_.start(importProgram(), importArguments(folder_path));
}

void CatalogAssetModel::selectIndex(int index) { setSelectedIndexInternal(index); }

QString CatalogAssetModel::pickFolder(const QString& title) const {
  const auto folder_path = QFileDialog::getExistingDirectory(
      nullptr,
      title.isEmpty() ? QStringLiteral("Browse Photo Folder") : title,
      QDir::homePath(),
      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

  if (folder_path.isEmpty()) {
    return {};
  }

  return QUrl::fromLocalFile(folder_path).toString();
}

QString CatalogAssetModel::localPathForUrl(const QString& url) const {
  const auto parsed = QUrl(url);
  if (!parsed.isValid()) {
    return url;
  }

  return parsed.isLocalFile() ? parsed.toLocalFile() : url;
}

QString CatalogAssetModel::parentFolderUrl(const QString& url) const {
  const auto local_path = localPathForUrl(url);
  if (local_path.isEmpty()) {
    return {};
  }

  const QFileInfo info(local_path);
  const auto parent_path = info.dir().absolutePath();
  if (parent_path.isEmpty() || parent_path == local_path) {
    return QUrl::fromLocalFile(local_path).toString();
  }

  return QUrl::fromLocalFile(parent_path).toString();
}

void CatalogAssetModel::createProject(const QString& name) {
  const auto trimmed_name = normalizedProjectName(name);
  if (trimmed_name.isEmpty()) {
    return;
  }

  if (findProject(trimmed_name) != nullptr) {
    setStatus(QStringLiteral("Project '%1' already exists.").arg(trimmed_name));
    return;
  }

  ProjectRecord project;
  project.name = trimmed_name;
  project.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  projects_.push_back(project);
  saveProjects();
  emit projectNamesChanged();
  setProjectFilter(trimmed_name);
  setStatus(QStringLiteral("Created project '%1'.").arg(trimmed_name));
}

void CatalogAssetModel::addAssetToProject(const QString& asset_id, const QString& project_name) {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return;
  }

  auto* project = findProject(trimmed_name);
  if (project == nullptr) {
    ProjectRecord new_project;
    new_project.name = trimmed_name;
    new_project.created_at = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    projects_.push_back(new_project);
    emit projectNamesChanged();
    project = &projects_.last();
  }

  if (project->asset_ids.contains(asset_id)) {
    setStatus(QStringLiteral("Asset is already in '%1'.").arg(trimmed_name));
    return;
  }

  project->asset_ids.insert(asset_id);
  saveProjects();
  rebuildProjectMetadata();
  applyFilters(asset_id);
  setStatus(QStringLiteral("Added photo to '%1'.").arg(trimmed_name));
}

void CatalogAssetModel::removeAssetFromProject(const QString& asset_id, const QString& project_name) {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return;
  }

  auto* project = findProject(trimmed_name);
  if (project == nullptr || !project->asset_ids.contains(asset_id)) {
    return;
  }

  project->asset_ids.remove(asset_id);
  saveProjects();
  rebuildProjectMetadata();
  applyFilters(asset_id);
  setStatus(QStringLiteral("Removed photo from '%1'.").arg(trimmed_name));
}

bool CatalogAssetModel::assetInProject(const QString& asset_id, const QString& project_name) const {
  const auto trimmed_name = normalizedProjectName(project_name);
  if (asset_id.isEmpty() || trimmed_name.isEmpty()) {
    return false;
  }

  const auto* project = findProject(trimmed_name);
  return project != nullptr && project->asset_ids.contains(asset_id);
}

void CatalogAssetModel::setProjectFilter(const QString& project_filter) {
  auto next_filter = normalizedProjectName(project_filter);
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (project_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  project_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setImportDateFilter(const QString& import_date_filter) {
  auto next_filter = import_date_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (import_date_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  import_date_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setCaptureYearFilter(const QString& capture_year_filter) {
  auto next_filter = capture_year_filter.trimmed().toLower();
  if (next_filter.isEmpty()) {
    next_filter = QStringLiteral("all");
  }

  if (capture_year_filter_ == next_filter) {
    return;
  }

  const auto preferred_asset_id = selectedAssetId();
  capture_year_filter_ = next_filter;
  emit filtersChanged();
  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::handleSnapshotFinished(int exit_code, QProcess::ExitStatus exit_status) {
  setLoading(false);

  if (exit_status != QProcess::NormalExit || exit_code != 0) {
    setErrorMessage(QString::fromUtf8(snapshot_process_.readAllStandardOutput()).trimmed());
    setStatus(QStringLiteral("Catalog unavailable"));
    updateAssets({});
    setDriveCount(0);
    return;
  }

  const auto payload = QJsonDocument::fromJson(snapshot_process_.readAllStandardOutput());
  if (!payload.isObject()) {
    setErrorMessage(QStringLiteral("Catalog snapshot output was not valid JSON."));
    setStatus(QStringLiteral("Catalog unavailable"));
    updateAssets({});
    setDriveCount(0);
    return;
  }

  const auto root = payload.object();
  updateAssets(parse_assets(root.value("assets").toArray()));
  setDriveCount(static_cast<int>(root.value("drives").toArray().size()));
  setStatus(QStringLiteral("Loaded %1 photos across %2 drives.").arg(all_assets_.size()).arg(drive_count_));
}

void CatalogAssetModel::handleImportFinished(int exit_code, QProcess::ExitStatus exit_status) {
  setImporting(false);

  if (exit_status != QProcess::NormalExit || exit_code != 0) {
    setErrorMessage(QString::fromUtf8(import_process_.readAllStandardOutput()).trimmed());
    setStatus(QStringLiteral("Import failed"));
    return;
  }

  setStatus(QString::fromUtf8(import_process_.readAllStandardOutput()).trimmed());
  refresh();
}

void CatalogAssetModel::setStatus(const QString& status) {
  if (status_ == status) {
    return;
  }

  status_ = status;
  emit statusChanged();
}

void CatalogAssetModel::setErrorMessage(const QString& message) {
  if (error_message_ == message) {
    return;
  }

  error_message_ = message;
  emit errorMessageChanged();
}

void CatalogAssetModel::setLoading(bool loading) {
  if (loading_ == loading) {
    return;
  }

  loading_ = loading;
  emit loadingChanged();
  emit busyChanged();
}

void CatalogAssetModel::setImporting(bool importing) {
  if (importing_ == importing) {
    return;
  }

  importing_ = importing;
  emit importingChanged();
  emit busyChanged();
}

void CatalogAssetModel::setDriveCount(int drive_count) {
  if (drive_count_ == drive_count) {
    return;
  }

  drive_count_ = drive_count;
  emit driveCountChanged();
}

void CatalogAssetModel::updateAssets(const QVector<CatalogAssetItem>& items) {
  const auto preferred_asset_id = selectedAssetId();
  const auto previous_total_count = all_assets_.size();

  all_assets_ = items;
  rebuildProjectMetadata();
  rebuildCaptureYears();

  if (previous_total_count != all_assets_.size()) {
    emit totalAssetCountChanged();
  }

  applyFilters(preferred_asset_id);
}

void CatalogAssetModel::setSelectedIndexInternal(int index) {
  if (assets_.isEmpty()) {
    index = -1;
  } else if (index < 0 || index >= assets_.size()) {
    index = 0;
  }

  if (selected_index_ == index) {
    return;
  }

  selected_index_ = index;
  emit selectedIndexChanged();
  emit selectedAssetChanged();
}

void CatalogAssetModel::applyFilters(const QString& preferred_asset_id) {
  QVector<CatalogAssetItem> filtered_assets;
  filtered_assets.reserve(all_assets_.size());

  for (const auto& item : all_assets_) {
    if (!matchesProjectFilter(item) || !matchesImportDateFilter(item) || !matchesCaptureYearFilter(item)) {
      continue;
    }
    filtered_assets.push_back(item);
  }

  const auto previous_count = assets_.size();
  beginResetModel();
  assets_ = filtered_assets;
  endResetModel();

  if (previous_count != assets_.size()) {
    emit assetCountChanged();
  }

  int next_index = -1;
  if (!preferred_asset_id.isEmpty()) {
    for (qsizetype index = 0; index < assets_.size(); ++index) {
      if (assets_.at(index).asset_id == preferred_asset_id) {
        next_index = static_cast<int>(index);
        break;
      }
    }
  }

  if (next_index < 0 && !assets_.isEmpty()) {
    next_index = 0;
  }

  const auto selection_changed = selected_index_ != next_index;
  selected_index_ = next_index;
  if (selection_changed) {
    emit selectedIndexChanged();
  }
  emit selectedAssetChanged();
}

void CatalogAssetModel::rebuildProjectMetadata() {
  for (auto& item : all_assets_) {
    QStringList names;
    for (const auto& project : projects_) {
      if (project.asset_ids.contains(item.asset_id)) {
        names.push_back(project.name);
      }
    }

    std::sort(
        names.begin(),
        names.end(),
        [](const QString& left, const QString& right) {
          return left.localeAwareCompare(right) < 0;
        });

    item.project_names = names;
    item.project_summary = project_summary_for_names(names);
  }
}

void CatalogAssetModel::rebuildCaptureYears() {
  QStringList years;
  for (const auto& item : all_assets_) {
    if (!item.capture_date_value.isValid()) {
      continue;
    }

    const auto year = QString::number(item.capture_date_value.year());
    if (!years.contains(year)) {
      years.push_back(year);
    }
  }

  std::sort(
      years.begin(),
      years.end(),
      [](const QString& left, const QString& right) { return left.toInt() > right.toInt(); });

  if (capture_years_ == years) {
    return;
  }

  capture_years_ = years;
  emit captureYearsChanged();
}

void CatalogAssetModel::loadProjects() {
  projects_.clear();

  QFile file(projectsFilePath());
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return;
  }

  const auto document = QJsonDocument::fromJson(file.readAll());
  if (!document.isObject()) {
    return;
  }

  const auto project_array = document.object().value(QStringLiteral("projects")).toArray();
  for (const auto& project_value : project_array) {
    const auto project_object = project_value.toObject();
    const auto name = normalizedProjectName(project_object.value(QStringLiteral("name")).toString());
    if (name.isEmpty() || findProject(name) != nullptr) {
      continue;
    }

    ProjectRecord project;
    project.name = name;
    project.created_at = project_object.value(QStringLiteral("createdAt")).toString();

    const auto asset_ids = project_object.value(QStringLiteral("assetIds")).toArray();
    for (const auto& asset_id_value : asset_ids) {
      const auto asset_id = asset_id_value.toString().trimmed();
      if (!asset_id.isEmpty()) {
        project.asset_ids.insert(asset_id);
      }
    }

    projects_.push_back(project);
  }
}

void CatalogAssetModel::saveProjects() const {
  const QFileInfo destination_info(projectsFilePath());
  QDir().mkpath(destination_info.dir().absolutePath());

  QJsonArray project_array;
  for (const auto& project : projects_) {
    QStringList asset_ids = project.asset_ids.values();
    std::sort(asset_ids.begin(), asset_ids.end());

    QJsonArray asset_id_array;
    for (const auto& asset_id : asset_ids) {
      asset_id_array.push_back(asset_id);
    }

    QJsonObject project_object;
    project_object.insert(QStringLiteral("name"), project.name);
    project_object.insert(QStringLiteral("createdAt"), project.created_at);
    project_object.insert(QStringLiteral("assetIds"), asset_id_array);
    project_array.push_back(project_object);
  }

  QJsonObject root;
  root.insert(QStringLiteral("projects"), project_array);

  QSaveFile file(projectsFilePath());
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.commit();
}

QString CatalogAssetModel::projectsFilePath() const {
  auto data_root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (data_root.isEmpty()) {
    data_root = QDir::current().absoluteFilePath(QStringLiteral(".photo-editor-data"));
  }

  return QDir(data_root).filePath(QStringLiteral("projects.json"));
}

ProjectRecord* CatalogAssetModel::findProject(const QString& project_name) {
  const auto normalized_name = normalizedProjectName(project_name);
  for (auto& project : projects_) {
    if (normalizedProjectName(project.name).compare(normalized_name, Qt::CaseInsensitive) == 0) {
      return &project;
    }
  }

  return nullptr;
}

const ProjectRecord* CatalogAssetModel::findProject(const QString& project_name) const {
  const auto normalized_name = normalizedProjectName(project_name);
  for (const auto& project : projects_) {
    if (normalizedProjectName(project.name).compare(normalized_name, Qt::CaseInsensitive) == 0) {
      return &project;
    }
  }

  return nullptr;
}

QString CatalogAssetModel::normalizedProjectName(const QString& project_name) const {
  return project_name.simplified();
}

bool CatalogAssetModel::matchesImportDateFilter(const CatalogAssetItem& item) const {
  if (import_date_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (!item.imported_at_value.isValid()) {
    return false;
  }

  const auto imported_date = item.imported_at_value.date();
  const auto today = QDate::currentDate();

  if (import_date_filter_ == QStringLiteral("today")) {
    return imported_date == today;
  }

  if (import_date_filter_ == QStringLiteral("7d")) {
    return imported_date >= today.addDays(-6) && imported_date <= today;
  }

  if (import_date_filter_ == QStringLiteral("30d")) {
    return imported_date >= today.addDays(-29) && imported_date <= today;
  }

  if (import_date_filter_ == QStringLiteral("year")) {
    return imported_date.year() == today.year();
  }

  return true;
}

bool CatalogAssetModel::matchesCaptureYearFilter(const CatalogAssetItem& item) const {
  if (capture_year_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (capture_year_filter_ == QStringLiteral("unknown")) {
    return !item.capture_date_value.isValid();
  }

  return item.capture_date_value.isValid()
      && QString::number(item.capture_date_value.year()) == capture_year_filter_;
}

bool CatalogAssetModel::matchesProjectFilter(const CatalogAssetItem& item) const {
  if (project_filter_ == QStringLiteral("all")) {
    return true;
  }

  if (project_filter_ == QStringLiteral("unassigned")) {
    return item.project_names.isEmpty();
  }

  return item.project_names.contains(project_filter_, Qt::CaseInsensitive);
}

const CatalogAssetItem* CatalogAssetModel::selectedItem() const {
  if (selected_index_ < 0 || selected_index_ >= assets_.size()) {
    return nullptr;
  }
  return &assets_.at(selected_index_);
}

QString CatalogAssetModel::snapshotProgram() const {
  if (useBuiltSnapshotBinary()) {
    return builtBinaryPath(QStringLiteral("catalog_snapshot"));
  }

  return QStringLiteral("cargo");
}

QStringList CatalogAssetModel::snapshotArguments() const {
  if (useBuiltSnapshotBinary()) {
    return {QStringLiteral("0")};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("catalog_snapshot"),
      QStringLiteral("--"),
      QStringLiteral("0"),
  };
}

QString CatalogAssetModel::importProgram() const {
  if (useBuiltImportBinary()) {
    return builtBinaryPath(QStringLiteral("import_catalog"));
  }

  return QStringLiteral("cargo");
}

QStringList CatalogAssetModel::importArguments(const QString& folder_path) const {
  const auto drive_id = derive_drive_id(folder_path);
  const auto drive_name = derive_drive_name(folder_path);
  const auto import_mode = QStringLiteral("referenced");

  if (useBuiltImportBinary()) {
    return {folder_path, drive_id, drive_name, import_mode};
  }

  return {
      QStringLiteral("run"),
      QStringLiteral("--quiet"),
      QStringLiteral("--manifest-path"),
      QStringLiteral("rust/Cargo.toml"),
      QStringLiteral("-p"),
      QStringLiteral("photo-catalog"),
      QStringLiteral("--bin"),
      QStringLiteral("import_catalog"),
      QStringLiteral("--"),
      folder_path,
      drive_id,
      drive_name,
      import_mode,
  };
}

bool CatalogAssetModel::useBuiltSnapshotBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("catalog_snapshot")));
  return binary.exists() && binary.isExecutable();
}

bool CatalogAssetModel::useBuiltImportBinary() const {
  const QFileInfo binary(builtBinaryPath(QStringLiteral("import_catalog")));
  return binary.exists() && binary.isExecutable();
}

QString CatalogAssetModel::builtBinaryPath(const QString& binary_name) const {
  return QDir::current().absoluteFilePath(QStringLiteral("rust/target/debug/%1").arg(binary_name));
}

}  // namespace photo_editor::desktop
