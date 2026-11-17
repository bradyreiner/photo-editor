#include "catalog_asset_model.h"

#include <QGuiApplication>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimeZone>

#include <iostream>
#include <utility>

namespace photo_editor::desktop {

struct CatalogFilterBenchmarkAccess final {
  static void editLensModel(CatalogAssetModel& model, const QString& asset_id, const QString& value) {
    for (auto& item : model.all_assets_) {
      if (item.asset_id != asset_id) continue;
      item.lens_model = value;
      break;
    }
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

QVector<CatalogAssetItem> fixture() {
  struct Entry final {
    const char* id;
    const char* filename;
    int day;
    const char* camera;
  };
  const Entry entries[] = {
      {"asset-10", "photo_10.jpg", 3, "Camera A"},
      {"asset-2", "photo_2.jpg", 1, "Camera B"},
      {"asset-1", "photo_1.jpg", 2, "Camera A"},
      {"asset-11", "photo_11.jpg", 4, "Camera A"},
  };

  QVector<CatalogAssetItem> assets;
  assets.reserve(4);
  for (const auto& entry : entries) {
    CatalogAssetItem item;
    item.asset_id = QString::fromLatin1(entry.id);
    item.file_path = QStringLiteral("/isolated-catalog-filter/%1").arg(
        QString::fromLatin1(entry.filename));
    item.camera_label = QString::fromLatin1(entry.camera);
    item.capture_timestamp = QDateTime(
        QDate(2025, 1, entry.day), QTime(12, 0), QTimeZone::UTC);
    item.capture_date_value = item.capture_timestamp.date();
    item.capture_date = item.capture_date_value.toString(Qt::ISODate);
    item.media_type = QStringLiteral("photo");
    assets.push_back(std::move(item));
  }
  return assets;
}

QStringList visible_ids(const CatalogAssetModel& model) {
  QStringList ids;
  ids.reserve(model.rowCount());
  for (int row = 0; row < model.rowCount(); ++row) {
    ids.push_back(model.data(model.index(row, 0), CatalogAssetModel::AssetIdRole).toString());
  }
  return ids;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QTemporaryDir storage;
  if (!storage.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath(QStringLiteral("data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/catalog_filter_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());

  CatalogAssetModel model;
  model.replaceAssetsForTesting(fixture());
  if (visible_ids(model) != QStringList({"asset-2", "asset-1", "asset-10", "asset-11"})) {
    return fail("Capture-time ordering did not match the fixture");
  }

  int model_resets = 0;
  int data_changes = 0;
  int changed_top = -1;
  QObject::connect(&model, &QAbstractItemModel::modelReset, &model, [&] { ++model_resets; });
  QObject::connect(
      &model,
      &QAbstractItemModel::dataChanged,
      &model,
      [&](const QModelIndex& top_left, const QModelIndex&, const QList<int>&) {
        ++data_changes;
        changed_top = top_left.row();
      });

  model.selectIndex(2);
  const QPersistentModelIndex selected(model.index(2, 0));
  model.setSearchFilter(QStringLiteral("photo"));
  if (model_resets != 0 || data_changes != 0 || !selected.isValid() || selected.row() != 2 ||
      visible_ids(model) != QStringList({"asset-2", "asset-1", "asset-10", "asset-11"})) {
    return fail("Same-membership search should preserve model rows and selection without publication");
  }

  photo_editor::desktop::CatalogFilterBenchmarkAccess::editLensModel(
      model, QStringLiteral("asset-10"), QStringLiteral("Updated lens"));
  if (model_resets != 0 || data_changes != 1 || changed_top != 2 ||
      !selected.isValid() || selected.row() != 2 ||
      model.data(selected, CatalogAssetModel::LensModelRole).toString() != QStringLiteral("Updated lens") ||
      model.selectedLensModel() != QStringLiteral("Updated lens")) {
    return fail("Same-order asset edits should update the changed row and selected metadata");
  }

  model.setSortOrder(QStringLiteral("filename-asc"));
  const QStringList filename_order = {"asset-1", "asset-2", "asset-10", "asset-11"};
  if (model_resets != 1 || model.rowCount() != 4 || visible_ids(model) != filename_order) {
    return fail("A same-membership sort that changes order should publish the filename order");
  }

  model.setCameraFilter(QStringLiteral("Camera A"));
  if (model_resets != 2 || model.rowCount() != 3 ||
      visible_ids(model) != QStringList({"asset-1", "asset-10", "asset-11"})) {
    return fail("A structural filter should reset to the correctly ordered subset");
  }

  std::cout << "Catalog same-order projection, row update, sort and structural filter passed\n";
  return 0;
}
