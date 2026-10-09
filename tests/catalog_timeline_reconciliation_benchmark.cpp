#include "catalog_asset_model.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QSettings>
#include <QTemporaryDir>
#include <algorithm>
#include <iostream>
#include <vector>
#include <sys/resource.h>

namespace photo_editor::desktop {
struct CatalogTimelineReconciliationTestAccess {
  static QVariantMap phases(const CatalogAssetModel& model) { return model.last_timeline_reconciliation_; }
  static void project(CatalogAssetModel& model, const QString& id) {
    model.projects_.append(ProjectRecord{.name = QStringLiteral("Fixture project"), .created_at = {}, .asset_ids = {id}});
  }
  static bool metadata(CatalogAssetModel& model) {
    const auto before = model.all_assets_;
    model.rebuildProjectMetadata();
    model.rebuildBurstMetadata();
    model.rebuildIntelligenceMetadata();
    return before == model.all_assets_;
  }
  static bool indexes(const CatalogAssetModel& model) {
    for (qsizetype i = 0; i < model.all_assets_.size(); ++i)
      if (model.all_asset_index_by_id_.value(model.all_assets_[i].asset_id, -1) != i) return false;
    for (qsizetype i = 0; i < model.assets_.size(); ++i)
      if (model.visible_asset_index_by_id_.value(model.assets_[i].asset_id, -1) != i) return false;
    return true;
  }
};
}
using namespace photo_editor::desktop;
namespace {
CatalogAssetItem asset(const QString& id, bool timeline) {
  CatalogAssetItem item;
  item.asset_id = id;
  item.file_path = "/isolated-fixture/" + id;
  item.camera_label = id;
  item.capture_date_value = QDate(2026, 9, 28);
  item.media_type = timeline ? "timeline" : "photo";
  item.online = false;
  return item;
}
int fail(const char* text) { std::cerr << text << '\n'; return 1; }
double p95(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  const auto pos = 0.95 * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<std::size_t>(pos);
  const auto hi = std::min(lo + 1, values.size() - 1);
  return values[lo] + (values[hi] - values[lo]) * (pos - static_cast<double>(lo));
}
}
namespace {
bool verify_contracts() {
  CatalogAssetModel model;
  model.replaceAssetsForTesting({asset("photo-a", false), asset("photo-b", false)});
  model.setVisualSearchResults({"timeline-b", "photo-a", "timeline-a", "timeline-c", "photo-b"});
  QPersistentModelIndex photo(model.index(0, 0));
  bool valid_indexes = true;
  int resets = 0;
  QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
  QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] {
    valid_indexes &= CatalogTimelineReconciliationTestAccess::indexes(model);
  });
  QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] {
    valid_indexes &= CatalogTimelineReconciliationTestAccess::indexes(model);
  });
  auto a = asset("timeline-a", true);
  auto b = asset("timeline-b", true);
  auto c = asset("timeline-c", true);
  auto ignored_duplicate = a;
  ignored_duplicate.camera_label = "wrong duplicate";
  model.replaceTimelineAssetsForTesting({c, a, b, ignored_duplicate, asset("  ", true)});
  const QStringList order{"timeline-b", "photo-a", "timeline-a", "timeline-c", "photo-b"};
  if (model.rowCount() != order.size() || !photo.isValid() || photo.row() != 1) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  for (int row = 0; row < order.size(); ++row) {
    if (model.data(model.index(row, 0), CatalogAssetModel::AssetIdRole).toString() != order.at(row)) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  }
  model.selectIndex(0);
  model.replaceTimelineAssetsForTesting({a});
  if (!photo.isValid() || photo.row() != 0 || model.selectedAssetId() != "photo-a" ||
      model.rowCount() != 3 || !valid_indexes || resets) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }

  model.clearVisualSearchResults();
  model.setSearchFilter("timeline-a");
  if (model.rowCount() != 1) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  a.camera_label = "renamed document";
  // The file path still matches the search, so use a name unique to the updated metadata.
  model.setSearchFilter("renamed document");
  if (model.rowCount() != 0) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  model.replaceTimelineAssetsForTesting({a});
  if (model.rowCount() != 1) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  a.camera_label = "another name";
  model.replaceTimelineAssetsForTesting({a});
  if (model.rowCount() != 0) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  model.setSearchFilter({});
  model.setMediaTypeFilter("photos");
  model.replaceTimelineAssetsForTesting({a, b, c});
  if (model.rowCount() != 2) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  model.setMediaTypeFilter("timelines");
  if (model.rowCount() != 3) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  model.selectIndex(0);
  model.setSelectedRating(5);
  model.setMinimumRatingFilter(5);
  model.replaceTimelineAssetsForTesting({a, b, c});
  if (model.rowCount() != 1 || !CatalogTimelineReconciliationTestAccess::indexes(model)) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }

  CatalogAssetModel groups;
  groups.setMediaTypeFilter(QStringLiteral("all"));
  groups.setMinimumRatingFilter(0);
  auto left = asset("left", false);
  auto right = asset("right", false);
  left.file_path = right.file_path = "/isolated-fixture/IMG_0001.jpg";
  left.camera_label = right.camera_label = "camera";
  groups.replaceAssetsForTesting({left, a, b, right});
  groups.replaceTimelineAssetsForTesting({});
  if (!CatalogTimelineReconciliationTestAccess::metadata(groups)) { std::cerr << "contract line " << __LINE__ << '\n'; return false; }
  // Replacing a photo ID with a Timeline must also rebuild the photo groups.
  auto collision = asset("left", true);
  CatalogTimelineReconciliationTestAccess::project(groups, collision.asset_id);
  groups.replaceTimelineAssetsForTesting({collision});
  if (groups.rowCount() != 2) { std::cerr << "collision count " << groups.rowCount() << '\n'; return false; }
  if (!CatalogTimelineReconciliationTestAccess::metadata(groups)) { std::cerr << "collision metadata\n"; return false; }
  return CatalogTimelineReconciliationTestAccess::indexes(groups);
}
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir root;
  if (!root.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", root.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/timeline_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root.path());
  const auto verify = app.arguments().contains("--verify");
  QJsonArray cases;
  for (int timeline_count : {10, 100, 1000}) {
    CatalogAssetModel model;
    QVector<CatalogAssetItem> photos;
    for (int i = 0; i < 10000; ++i) photos.append(asset(QStringLiteral("photo-%1").arg(i), false));
    model.replaceAssetsForTesting(photos);
    model.selectIndex(123);
    const auto selection = model.selectedAssetId();
    QPersistentModelIndex stable(model.index(123, 0));
    int resets = 0, inserts = 0, removes = 0, changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::rowsInserted, [&] { ++inserts; });
    QObject::connect(&model, &QAbstractItemModel::rowsRemoved, [&] { ++removes; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    QVector<CatalogAssetItem> timelines;
    for (int i = 0; i < timeline_count; ++i) timelines.append(asset(QStringLiteral("timeline-%1").arg(i), true));
    QElapsedTimer timer;
    timer.start();
    model.replaceTimelineAssetsForTesting(timelines);
    const auto add_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    const auto add_signals = inserts;
    if (model.rowCount() != 10000 + timeline_count) return fail("Timeline insertion count incorrect");
    std::vector<double> no_op, edit;
    QJsonArray no_op_phases, edit_phases;
    changes = 0;
    for (int i = 0; i < 7; ++i) {
      timer.restart();
      model.replaceTimelineAssetsForTesting(timelines);
      no_op.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      no_op_phases.append(QJsonObject::fromVariantMap(CatalogTimelineReconciliationTestAccess::phases(model)));
    }
    const auto no_op_signals = changes;
    changes = 0;
    for (int i = 0; i < 7; ++i) {
      timelines[0].camera_label = QStringLiteral("renamed-%1").arg(i);
      timer.restart();
      model.replaceTimelineAssetsForTesting(timelines);
      edit.push_back(static_cast<double>(timer.nsecsElapsed()) / 1e6);
      edit_phases.append(QJsonObject::fromVariantMap(CatalogTimelineReconciliationTestAccess::phases(model)));
    }
    const auto edit_signals = changes;
    timer.restart();
    model.replaceTimelineAssetsForTesting({});
    const auto remove_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
    if (resets || !stable.isValid() || stable.row() != 123 || model.selectedAssetId() != selection ||
        model.rowCount() != 10000 || !CatalogTimelineReconciliationTestAccess::indexes(model))
      return fail("Reconciliation invalidated photo indexes, selection, or model identity");
    if (verify && (add_signals != 1 || removes != 1 || no_op_signals != 0 || edit_signals != 7))
      return fail("Reconciliation emitted redundant row/data signals");
    cases.append(QJsonObject{{"photos", 10000}, {"timelines", timeline_count},
        {"add_ms", add_ms}, {"remove_ms", remove_ms}, {"no_op_p95_ms", p95(no_op)}, {"edit_p95_ms", p95(edit)},
        {"add_signals", add_signals}, {"remove_signals", removes}, {"no_op_data_signals", no_op_signals},
        {"edit_data_signals", edit_signals}, {"no_op_phases", no_op_phases}, {"edit_phases", edit_phases}});
  }
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  const qint64 peak_bytes = static_cast<qint64>(usage.ru_maxrss)
#if !defined(Q_OS_MACOS)
      * 1024
#endif
      ;
  if (verify && !verify_contracts()) return fail("Rank, filter, selection, or notification contract failed");
  QJsonObject result{{"cases", cases}, {"peak_rss_bytes", peak_bytes}, {"verified", verify}};
  const auto output_index = app.arguments().indexOf("--output");
  if (output_index >= 0 && output_index + 1 < app.arguments().size()) {
    QFile file(app.arguments().at(output_index + 1));
    if (!file.open(QIODevice::WriteOnly)) return fail("Cannot write benchmark artifact");
    file.write(QJsonDocument(result).toJson());
  }
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return 0;
}
