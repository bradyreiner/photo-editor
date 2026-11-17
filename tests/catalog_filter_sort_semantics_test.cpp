#include "catalog_asset_model.h"

#include <QDateTime>
#include <QGuiApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include <iostream>

namespace photo_editor::desktop {

struct CatalogFilterBenchmarkAccess final {
  static void apply(CatalogAssetModel& model) {
    model.applyFilters(model.selectedAssetId());
  }

  static bool hasSortProjection(const CatalogAssetModel& model) {
    return model.catalog_sort_projection_cache_.valid;
  }

  static bool pathUsesData(
      CatalogAssetModel& model,
      const QString& asset_id,
      const QChar* data) {
    const auto index = model.all_asset_index_by_id_.value(asset_id, -1);
    if (index < 0 || index >= model.all_assets_.size()) return false;
    return model.all_assets_.at(index).file_path.constData() == data;
  }

  static void setCaptureTimeAndApply(
      CatalogAssetModel& model,
      const QString& asset_id,
      const QString& capture_time) {
    for (const auto& item : model.all_assets_) {
      if (item.asset_id != asset_id) continue;
      model.asset_metadata_.insert(
          model.metadataAssetId(item),
          {{QStringLiteral("captureTime"), capture_time}});
      apply(model);
      return;
    }
  }

  static void setCaptureFallbackAndApply(
      CatalogAssetModel& model,
      const QString& asset_id,
      const QDateTime& capture_timestamp,
      const QDate& capture_date) {
    for (auto& item : model.all_assets_) {
      if (item.asset_id != asset_id) continue;
      item.capture_timestamp = capture_timestamp;
      item.capture_date_value = capture_date;
      apply(model);
      return;
    }
  }

  static void setRatings(
      CatalogAssetModel& model,
      const QHash<QString, int>& ratings) {
    for (auto& item : model.all_assets_) {
      const auto rating = ratings.constFind(item.asset_id);
      if (rating != ratings.cend()) item.rating = rating.value();
    }
    model.applyFilters(model.selectedAssetId());
  }

  static void setCaptureTime(
      CatalogAssetModel& model,
      const QString& asset_id,
      const QString& capture_time) {
    for (const auto& item : model.all_assets_) {
      if (item.asset_id != asset_id) continue;
      const auto metadata_id = model.metadataAssetId(item);
      model.asset_metadata_.insert(
          metadata_id,
          {{QStringLiteral("captureTime"), capture_time}});
      return;
    }
  }

  static void setSearchMetadata(
      CatalogAssetModel& model,
      const QString& asset_id,
      const QVariantMap& metadata) {
    const auto index = model.all_asset_index_by_id_.value(asset_id, -1);
    if (index < 0) return;
    const auto metadata_id = model.metadataAssetId(model.all_assets_.at(index));
    model.asset_metadata_.insert(metadata_id, metadata);
    model.invalidateSearchMetadataCache(metadata_id);
    model.applyFilters(model.selectedAssetId());
  }
};

}  // namespace photo_editor::desktop

namespace {

using photo_editor::desktop::CatalogAssetItem;
using photo_editor::desktop::CatalogAssetModel;

int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& filename,
    int rating) {
  CatalogAssetItem item;
  item.asset_id = asset_id;
  item.file_path = QStringLiteral("/isolated-catalog-sort/%1").arg(filename);
  item.media_type = QStringLiteral("photo");
  item.online = false;
  item.rating = rating;
  return item;
}

QVector<CatalogAssetItem> fixture() {
  auto root = make_asset(QStringLiteral("root"), QStringLiteral("photo_10.JPG"), 5);
  root.capture_timestamp = QDateTime::fromString(
      QStringLiteral("2025-01-10T12:00:00.000Z"), Qt::ISODateWithMs);

  auto meta_lower = make_asset(QStringLiteral("a-meta"), QStringLiteral("photo_2.jpg"), 4);
  meta_lower.capture_timestamp = QDateTime::fromString(
      QStringLiteral("2025-01-05T00:00:00.000Z"), Qt::ISODateWithMs);

  auto meta_upper = make_asset(QStringLiteral("z-meta"), QStringLiteral("PHOTO_2.JPG"), 5);

  auto date_fallback = make_asset(QStringLiteral("date-fallback"), QStringLiteral("photo_3.jpg"), 3);
  date_fallback.capture_date_value = QDate(2025, 1, 1);

  auto time_fallback = make_asset(QStringLiteral("time-fallback"), QStringLiteral("photo_4.jpg"), 2);
  time_fallback.capture_timestamp = QDateTime::fromString(
      QStringLiteral("2024-12-31T20:00:00.000Z"), Qt::ISODateWithMs);

  auto invalid_time = make_asset(QStringLiteral("invalid-time"), QStringLiteral("photo_1.jpg"), 1);

  return {root, meta_lower, meta_upper, date_fallback, time_fallback, invalid_time};
}

QStringList visible_ids(const CatalogAssetModel& model) {
  QStringList ids;
  ids.reserve(model.rowCount());
  for (int row = 0; row < model.rowCount(); ++row) {
    ids.push_back(model.data(model.index(row, 0), CatalogAssetModel::AssetIdRole).toString());
  }
  return ids;
}

QString first_visible_id(const CatalogAssetModel& model) {
  return model.rowCount() > 0
      ? model.data(model.index(0, 0), CatalogAssetModel::AssetIdRole).toString()
      : QString{};
}

bool expect_order(
    CatalogAssetModel& model,
    const QString& sort_order,
    const QStringList& expected) {
  model.setSortOrder(sort_order);
  if (visible_ids(model) == expected) return true;
  std::cerr << "Unexpected order for " << sort_order.toStdString() << ": ";
  for (const auto& id : visible_ids(model)) std::cerr << id.toStdString() << ' ';
  std::cerr << '\n';
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir storage;
  if (!storage.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath(QStringLiteral("data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/catalog_sort_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());

  CatalogAssetModel model;
  model.replaceAssetsForTesting(fixture());
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTime(
      model, QStringLiteral("root"), QStringLiteral("2025-01-02T12:00:00.000Z"));
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTime(
      model, QStringLiteral("a-meta"), QStringLiteral("2025-01-01T10:00:00.250Z"));
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTime(
      model, QStringLiteral("z-meta"), QStringLiteral("2025-01-01T10:00:00.250Z"));
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTime(
      model, QStringLiteral("invalid-time"), QStringLiteral("not a timestamp"));

  const auto virtual_copy_id = model.createVirtualCopyForAsset(
      QStringLiteral("root"), QStringLiteral("sort parity copy"));
  if (virtual_copy_id != QStringLiteral("root::variant-1")) {
    return fail("The source fixture should create one catalog virtual copy");
  }
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setRatings(
      model,
      {{QStringLiteral("root"), 5},
       {virtual_copy_id, 5},
       {QStringLiteral("a-meta"), 4},
       {QStringLiteral("z-meta"), 5},
       {QStringLiteral("date-fallback"), 3},
       {QStringLiteral("time-fallback"), 2},
       {QStringLiteral("invalid-time"), 1}});

  const QStringList imported_order = {
      "root", "a-meta", "z-meta", "date-fallback", "time-fallback", "invalid-time", virtual_copy_id};
  if (!expect_order(model, QStringLiteral("imported-desc"), imported_order)) {
    return fail("Imported ordering must preserve source and appended virtual-copy order");
  }

  const QStringList filename_order = {
      "invalid-time", "a-meta", "z-meta", "date-fallback", "time-fallback", "root", virtual_copy_id};
  if (!expect_order(model, QStringLiteral("filename-asc"), filename_order)) {
    return fail("Filename order must be case-insensitive, numeric, and asset-ID stable on ties");
  }

  const QStringList rating_order = {
      "root", virtual_copy_id, "z-meta", "a-meta", "date-fallback", "time-fallback", "invalid-time"};
  if (!expect_order(model, QStringLiteral("rating-desc"), rating_order)) {
    return fail("Rating order must keep descending ratings and deterministic ID ties");
  }

  const QStringList capture_ascending = {
      "time-fallback", "date-fallback", "z-meta", "a-meta", "root", virtual_copy_id, "invalid-time"};
  if (!expect_order(model, QStringLiteral("capture-asc"), capture_ascending)) {
    return fail("Capture order must honor metadata overrides, fallbacks, tie keys, and invalid dates last");
  }

  const QStringList capture_descending = {
      "root", virtual_copy_id, "z-meta", "a-meta", "date-fallback", "time-fallback", "invalid-time"};
  if (!expect_order(model, QStringLiteral("capture-desc"), capture_descending)) {
    return fail("Reverse capture order must retain ties and keep invalid dates last");
  }

  auto raw_path = QStringLiteral("/isolated-sort-projection/zzz_001.jpg");
  QVector<QChar> raw_path_buffer(raw_path.begin(), raw_path.end());
  QVector<CatalogAssetItem> projection_items;
  projection_items.reserve(512);
  for (int index = 0; index < 512; ++index) {
    const auto filename = index == 1
        ? QStringLiteral("zzz_001.jpg")
        : QStringLiteral("item_%1.jpg").arg(index, 3, 10, QLatin1Char('0'));
    auto item = make_asset(
        QStringLiteral("projection-%1").arg(index, 3, 10, QLatin1Char('0')),
        filename,
        0);
    if (index == 1) {
      item.file_path = QString::fromRawData(
          raw_path_buffer.constData(), raw_path_buffer.size());
    }
    item.capture_date_value = QDate(2025, 1, 1);
    projection_items.push_back(std::move(item));
  }
  CatalogAssetModel projection_model;
  projection_model.replaceAssetsForTesting(projection_items);
  if (!photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model) ||
      first_visible_id(projection_model) != QStringLiteral("projection-000")) {
    return fail("Broad capture sorting should build and apply a reusable source projection");
  }

  if (!photo_editor::desktop::CatalogFilterBenchmarkAccess::pathUsesData(
          projection_model, QStringLiteral("projection-001"),
          raw_path_buffer.constData())) {
    return fail("Mutable-backed path fixture should remain borrowed by the catalog row");
  }
  const auto basename_offset = raw_path.indexOf(QStringLiteral("zzz"));
  for (qsizetype offset = 0; offset < 3; ++offset) {
    raw_path_buffer[basename_offset + offset] = QLatin1Char('a');
  }
  photo_editor::desktop::CatalogFilterBenchmarkAccess::apply(projection_model);
  if (first_visible_id(projection_model) != QStringLiteral("projection-001")) {
    return fail("A mutation behind borrowed path storage must invalidate cached filename keys");
  }
  projection_model.setSearchFilter(QStringLiteral("item_010"));
  if (projection_model.rowCount() != 1 ||
      photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("A nonempty search should release its unused sort projection");
  }
  projection_model.setSearchFilter({});
  if (!photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("Clearing search should lazily rebuild a broad sort projection");
  }
  projection_model.setVisualSearchResults(
      {QStringLiteral("projection-001"), QStringLiteral("projection-000")});
  if (projection_model.rowCount() != 2 ||
      photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("Visual search should release a projection whose order is not used");
  }
  projection_model.clearVisualSearchResults();
  if (!photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("Clearing visual search should rebuild the eligible broad projection");
  }
  projection_model.setSortOrder(QStringLiteral("rating-desc"));
  if (photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("Unsupported rating sorting should release the unused projection");
  }
  projection_model.setSortOrder(QStringLiteral("capture-asc"));
  if (!photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(projection_model)) {
    return fail("Returning to capture sorting should rebuild the broad projection");
  }

  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTimeAndApply(
      projection_model,
      QStringLiteral("projection-300"),
      QStringLiteral("2019-12-31T00:00:00.000Z"));
  if (first_visible_id(projection_model) != QStringLiteral("projection-300")) {
    return fail("Metadata capture-time changes must invalidate cached capture keys");
  }
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureTimeAndApply(
      projection_model, QStringLiteral("projection-300"), QString{});
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureFallbackAndApply(
      projection_model,
      QStringLiteral("projection-301"),
      QDateTime::fromString(QStringLiteral("2018-01-01T00:00:00.000Z"), Qt::ISODateWithMs),
      QDate(2025, 1, 1));
  if (first_visible_id(projection_model) != QStringLiteral("projection-301")) {
    return fail("Item capture timestamps must invalidate cached fallback keys");
  }
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureFallbackAndApply(
      projection_model,
      QStringLiteral("projection-301"),
      QDateTime{},
      QDate(2025, 1, 1));
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setCaptureFallbackAndApply(
      projection_model,
      QStringLiteral("projection-302"),
      QDateTime{},
      QDate(2017, 1, 1));
  if (first_visible_id(projection_model) != QStringLiteral("projection-302")) {
    return fail("Capture-date fallback changes must invalidate cached date keys");
  }

  QVector<CatalogAssetItem> oversized_projection_items;
  oversized_projection_items.reserve(512);
  const auto long_suffix = QString(32768, QLatin1Char('x')) + QStringLiteral(".jpg");
  for (int index = 0; index < 512; ++index) {
    const auto prefix = QStringLiteral("%1_").arg(index, 3, 10, QLatin1Char('0'));
    auto item = make_asset(
        QStringLiteral("oversized-%1").arg(index, 3, 10, QLatin1Char('0')),
        prefix + long_suffix,
        0);
    item.capture_date_value = QDate(2025, 1, 1);
    oversized_projection_items.push_back(std::move(item));
  }
  CatalogAssetModel oversized_projection_model;
  oversized_projection_model.replaceAssetsForTesting(oversized_projection_items);
  if (photo_editor::desktop::CatalogFilterBenchmarkAccess::hasSortProjection(oversized_projection_model) ||
      oversized_projection_model.rowCount() != oversized_projection_items.size() ||
      first_visible_id(oversized_projection_model) != QStringLiteral("oversized-000")) {
    return fail("Projection storage above budget should fall back to the existing sort path");
  }

  auto search_item = make_asset(
      QStringLiteral("search-item"), QStringLiteral("capture_body.jpg"), 0);
  search_item.camera_label = QStringLiteral("Camera Body");
  CatalogAssetModel search_model;
  search_model.replaceAssetsForTesting({search_item});
  search_model.selectIndex(0);
  search_model.setSearchFilter(QStringLiteral("needle title"));
  if (search_model.rowCount() != 0) return fail("An empty metadata map must not match a non-empty query");

  photo_editor::desktop::CatalogFilterBenchmarkAccess::setSearchMetadata(
      search_model,
      QStringLiteral("search-item"),
      {{QStringLiteral("title"), QStringLiteral("Needle Title Featured")},
       {QStringLiteral("keywords"), QVariantList{QStringLiteral("Alpha"), QStringLiteral("Beta")}},
       {QStringLiteral("legacyNumeric"), 42}});
  if (search_model.rowCount() != 1) {
    return fail("Metadata changes must invalidate cached non-matches");
  }
  search_model.setSearchFilter(QStringLiteral("ha be"));
  if (search_model.rowCount() != 1) {
    return fail("Keyword search must preserve the joined-list substring behavior");
  }
  search_model.setSearchFilter(QStringLiteral("42"));
  if (search_model.rowCount() != 1) {
    return fail("Metadata scalar values must preserve QVariant string conversion");
  }
  search_model.setSearchFilter(QStringLiteral("camera bod"));
  if (search_model.rowCount() != 1) {
    return fail("Cheap camera-field search must remain case-insensitive substring matching");
  }
  search_model.setSearchFilter(QStringLiteral("needle title"));
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setSearchMetadata(
      search_model,
      QStringLiteral("search-item"),
      {{QStringLiteral("title"), QStringLiteral("Updated Title")}});
  if (search_model.rowCount() != 0) {
    return fail("Metadata changes must invalidate cached matches");
  }
  photo_editor::desktop::CatalogFilterBenchmarkAccess::setSearchMetadata(
      search_model,
      QStringLiteral("search-item"),
      {{QStringLiteral("title"), QStringLiteral("Ärger am Morgen")}});
  search_model.setSearchFilter(QStringLiteral("ärger"));
  if (search_model.rowCount() != 1) {
    return fail("Non-ASCII case-insensitive substring matching must keep Qt behavior");
  }

  model.setSearchFilter({});
  model.clearSelection();
  model.selectIndex(0);
  if (!model.saveMetadataToSelected(
          {{QStringLiteral("title"), QStringLiteral("First Title")}})) {
    return fail("Metadata fixture should be saveable");
  }
  model.setSearchFilter(QStringLiteral("first title"));
  const auto shared_metadata_matches = visible_ids(model);
  if (model.rowCount() != 2 || !shared_metadata_matches.contains(QStringLiteral("root")) ||
      !shared_metadata_matches.contains(QStringLiteral("root::variant-1"))) {
    return fail("Saved source metadata should match both the original and its virtual copy");
  }
  if (!model.saveMetadataToSelected(
          {{QStringLiteral("title"), QStringLiteral("Second Title")}})) {
    return fail("Selected metadata should be writable while the current match is visible");
  }
  if (model.rowCount() != 0) {
    return fail("Saving metadata must invalidate a cached positive search result");
  }

  std::cout << "Catalog sort-key semantics passed for all sort orders\n";
  return 0;
}
