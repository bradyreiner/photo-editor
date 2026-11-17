#include "catalog_asset_model.h"
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPersistentModelIndex>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <iostream>
#include <sys/resource.h>

namespace photo_editor::desktop {
struct CatalogFilterBenchmarkAccess {
  template <typename Model>
  static bool sortProjectionSupported(const Model& model) {
    if constexpr (requires { model.catalog_sort_projection_cache_.valid; }) {
      return true;
    } else {
      return false;
    }
  }
  template <typename Model>
  static bool sortProjectionValid(const Model& model) {
    if constexpr (requires { model.catalog_sort_projection_cache_.valid; }) {
      return model.catalog_sort_projection_cache_.valid;
    } else {
      return false;
    }
  }
  static void apply(CatalogAssetModel& model) { model.applyFilters(model.selectedAssetId()); }
  static void edit(CatalogAssetModel& model, int i) {
    model.all_assets_[2].lens_model = QString::number(i);
    apply(model);
  }
  static void setSearchMetadata(CatalogAssetModel& model, int target_index) {
    const QVariantMap common_metadata{
        {QStringLiteral("title"), QString(384, QLatin1Char('x'))},
        {QStringLiteral("caption"), QString(384, QLatin1Char('y'))},
        {QStringLiteral("creator"), QString(192, QLatin1Char('z'))},
        {QStringLiteral("copyright"), QString(192, QLatin1Char('c'))},
        {QStringLiteral("location"), QString(192, QLatin1Char('l'))},
        {QStringLiteral("keywords"), QStringList{QString(64, QLatin1Char('k')), QString(64, QLatin1Char('m'))}},
    };
    for (qsizetype i = 0; i < model.all_assets_.size(); ++i) {
      auto metadata = common_metadata;
      if (i == target_index) metadata.insert(QStringLiteral("title"), QStringLiteral("archive needle-target image"));
      model.asset_metadata_.insert(model.metadataAssetId(model.all_assets_.at(i)), std::move(metadata));
    }
  }
  static int searchMetadataCacheCost(const CatalogAssetModel& model) {
    return static_cast<int>(model.search_metadata_fields_cache_.totalCost());
  }
  static int searchMetadataCacheLimit(const CatalogAssetModel& model) {
    return static_cast<int>(model.search_metadata_fields_cache_.maxCost());
  }
  static bool indexes(const CatalogAssetModel& model) {
    for (qsizetype i = 0; i < model.assets_.size(); ++i)
      if (model.visible_asset_index_by_id_.value(model.assets_.at(i).asset_id, -1) != i) return false;
    return true;
  }
};
}
using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
QVector<CatalogAssetItem> fixture(int count) {
  QVector<CatalogAssetItem> items;
  items.reserve(count);
  for (int i = 0; i < count; ++i) {
    CatalogAssetItem item;
    item.asset_id = QStringLiteral("photo-%1").arg(i);
    item.file_path = "/isolated-filter-fixture/" + item.asset_id + ".jpg";
    item.camera_label = (i % 2 == 0) ? "Camera A" : "Camera B";
    item.media_type = "photo";
    item.online = false;
    items.append(item);
  }
  return items;
}
double p95(QList<double> values) {
  std::sort(values.begin(), values.end());
  const auto pos = 0.95 * static_cast<double>(values.size() - 1);
  const auto lo = static_cast<qsizetype>(pos);
  const auto hi = std::min(lo + 1, values.size() - 1);
  return values.at(lo) + (values.at(hi) - values.at(lo)) * (pos - static_cast<double>(lo));
}
void settle(int milliseconds) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < milliseconds) {
    QCoreApplication::processEvents();
    QThread::msleep(1);
  }
}
QJsonObject qml_contract(bool verify) {
  CatalogAssetModel model;
  model.replaceAssetsForTesting(fixture(10000));
  QQmlEngine engine;
  engine.rootContext()->setContextProperty("catalog", &model);
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
Item {
    id: root
    width: 600; height: 400
    property int created: 0
    property int destroyed: 0
    property alias scrollY: grid.contentY
    GridView {
        id: grid
        anchors.fill: parent
        cellWidth: 100; cellHeight: 100
        model: catalog
        delegate: Rectangle {
            required property string assetId
            width: 98; height: 98
            color: "#8899aa"
            Component.onCompleted: root.created++
            Component.onDestruction: root.destroyed++
        }
    }
})QML", QUrl("file:///isolated-filter.qml"));
  if (component.isError()) { std::cerr << component.errorString().toStdString(); return {}; }
  std::unique_ptr<QObject> root(component.create());
  auto* item = qobject_cast<QQuickItem*>(root.get());
  if (!item) return {};
  QQuickWindow window;
  int frames = 0;
  QObject::connect(&window, &QQuickWindow::frameSwapped, [&] { ++frames; });
  window.resize(600, 400);
  item->setParentItem(window.contentItem());
  window.show();
  settle(120);
  root->setProperty("scrollY", 5000.0);
  settle(100);
  const auto old_scroll = root->property("scrollY").toDouble();
  const auto created = root->property("created").toInt();
  const auto destroyed = root->property("destroyed").toInt();
  QElapsedTimer timer;
  timer.start();
  model.setSearchFilter("photo");
  const auto setter_ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
  window.requestUpdate();
  settle(100);
  const auto new_created = root->property("created").toInt() - created;
  const auto new_destroyed = root->property("destroyed").toInt() - destroyed;
  const auto scroll = root->property("scrollY").toDouble();
  const auto valid = created > 0 && frames > 0 && old_scroll > 0;
  const auto passed = valid && (!verify || (new_created == 0 && new_destroyed == 0 && scroll == old_scroll));
  // Destroy delegates while their root counters and catalog are still alive.
  item->setParentItem(nullptr);
  root.reset();
  return {{"passed", passed}, {"frames", frames}, {"initial_created", created},
          {"created_on_filter", new_created}, {"destroyed_on_filter", new_destroyed},
          {"scroll_before", old_scroll}, {"scroll_after", scroll}, {"setter_ms", setter_ms}};
}
}
int main(int argc, char** argv) {
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
  QGuiApplication app(argc, argv);
  QTemporaryDir storage;
  if (!storage.isValid()) return fail("Temporary storage unavailable");
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", storage.filePath("data").toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", storage.filePath("cache").toUtf8());
  qputenv("PHOTO_EDITOR_DATABASE_URL", "postgresql://127.0.0.1:1/filter_fixture");
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, storage.path());
  const bool verify = app.arguments().contains("--verify");
  QJsonArray cases;
  for (int count : {10000, 100000}) {
    const auto filter_items = fixture(count);
    CatalogAssetModel model;
    QElapsedTimer initial_filter_apply_timer;
    initial_filter_apply_timer.start();
    model.replaceAssetsForTesting(filter_items);
    const auto initial_filter_apply_ms =
        static_cast<double>(initial_filter_apply_timer.nsecsElapsed()) / 1e6;
    model.selectIndex(2);
    QPersistentModelIndex persistent(model.index(2, 0));
    int resets = 0, changes = 0;
    QObject::connect(&model, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, [&] { ++changes; });
    QList<double> unchanged, edit, narrow, restore;
    const auto measure = [](const auto& action) {
      QElapsedTimer timer; timer.start(); action();
      return static_cast<double>(timer.nsecsElapsed()) / 1e6;
    };
    for (int i = 0; i < 7; ++i) unchanged.append(measure([&] { CatalogFilterBenchmarkAccess::apply(model); }));
    const auto unchanged_changes = changes;
    for (int i = 0; i < 7; ++i) edit.append(measure([&] { CatalogFilterBenchmarkAccess::edit(model, i); }));
    const auto resets_before_membership_filters = resets;
    if (verify && (resets_before_membership_filters != 0 || unchanged_changes != 0 || changes != 7 ||
        !persistent.isValid() || persistent.row() != 2 || model.selectedAssetId() != "photo-2"))
      return fail("Unchanged filters and same-order edits should preserve rows, selection, and indexes");
    for (int i = 0; i < 7; ++i) {
      narrow.append(measure([&] { model.setCameraFilter("Camera A"); }));
      if (model.rowCount() != count / 2) return fail("Alternating filter has wrong count");
      for (int row = 0; row < model.rowCount(); ++row)
        if (model.data(model.index(row, 0), CatalogAssetModel::AssetIdRole).toString() != QStringLiteral("photo-%1").arg(row * 2))
          return fail("Alternating filter has wrong order");
      restore.append(measure([&] { model.setCameraFilter({}); }));
      if (model.rowCount() != count || !CatalogFilterBenchmarkAccess::indexes(model)) return fail("Restored filter/index mismatch");
    }
    const auto selected_identity_preserved = model.selectedAssetId() == QStringLiteral("photo-2");
    if (verify && (resets - resets_before_membership_filters != 14 || persistent.isValid() ||
        !selected_identity_preserved))
      return fail("Structural membership changes should reset rows while preserving the selected asset");
    cases.append(QJsonObject{{"photos", count}, {"unchanged_p95_ms", p95(unchanged)}, {"edit_p95_ms", p95(edit)},
        {"initial_filter_apply_ms", initial_filter_apply_ms},
        {"narrow_p95_ms", p95(narrow)}, {"restore_p95_ms", p95(restore)}, {"resets", resets},
        {"resets_before_membership_filters", resets_before_membership_filters},
        {"data_changes", changes}, {"persistent_index_valid", persistent.isValid()},
        {"selected_identity_preserved", selected_identity_preserved}});

    CatalogAssetModel search_model;
    search_model.replaceAssetsForTesting(fixture(count));
    const auto target_index = count / 2;
    CatalogFilterBenchmarkAccess::setSearchMetadata(search_model, target_index);
    const auto search_projection_supported =
        CatalogFilterBenchmarkAccess::sortProjectionSupported(search_model);
    const auto search_projection_before_query =
        CatalogFilterBenchmarkAccess::sortProjectionValid(search_model);
    if (verify && search_projection_supported && !search_projection_before_query)
      return fail("Eligible search fixture should have built its broad sort projection");
    QElapsedTimer first_metadata_search_timer;
    first_metadata_search_timer.start();
    search_model.setSearchFilter(QStringLiteral("needle-target"));
    const auto metadata_search_first_query_ms =
        static_cast<double>(first_metadata_search_timer.nsecsElapsed()) / 1e6;
    const auto search_projection_after_query =
        CatalogFilterBenchmarkAccess::sortProjectionValid(search_model);
    if (verify && search_projection_before_query && search_projection_after_query)
      return fail("A nonempty search should release the unused sort projection");
    if (search_model.rowCount() != 1 ||
        search_model.data(search_model.index(0, 0), CatalogAssetModel::AssetIdRole).toString() !=
            QStringLiteral("photo-%1").arg(target_index)) return fail("Metadata substring search changed its result");
    const auto metadata_search_cache_cost =
        CatalogFilterBenchmarkAccess::searchMetadataCacheCost(search_model);
    QList<double> metadata_search_repeat;
    for (int i = 0; i < 8; ++i) {
      metadata_search_repeat.append(measure([&] { search_model.setCameraFilter(QStringLiteral("Camera A")); }));
      metadata_search_repeat.append(measure([&] { search_model.setCameraFilter({}); }));
    }
    const QStringList metadata_search_queries{
        QStringLiteral("needle"), QStringLiteral("needle-target"), QStringLiteral("archive needle"),
        QStringLiteral("no-match-a"), QStringLiteral("target image"), QStringLiteral("NEEDLE"),
        QStringLiteral("no-match-b"), QStringLiteral("needle target")};
    const QList<int> expected_metadata_search_counts{1, 1, 1, 0, 1, 1, 0, 0};
    QList<double> metadata_search_query_changes;
    for (int repeat = 0; repeat < 3; ++repeat) {
      for (qsizetype query_index = 0; query_index < metadata_search_queries.size(); ++query_index) {
        metadata_search_query_changes.append(measure([&] {
          search_model.setSearchFilter(metadata_search_queries.at(query_index));
        }));
        if (search_model.rowCount() != expected_metadata_search_counts.at(query_index))
          return fail("Changing metadata query altered case-insensitive substring results");
      }
    }
    const auto metadata_search_cache_cost_after_queries =
        CatalogFilterBenchmarkAccess::searchMetadataCacheCost(search_model);
    const auto metadata_search_cache_limit =
        CatalogFilterBenchmarkAccess::searchMetadataCacheLimit(search_model);
    if (metadata_search_cache_cost_after_queries < metadata_search_cache_cost ||
        metadata_search_cache_cost_after_queries > metadata_search_cache_limit)
      return fail("Searchable metadata fields must remain admitted within the cache budget");
    const auto metadata_search_result_count = search_model.rowCount();
    QElapsedTimer clear_metadata_search_timer;
    clear_metadata_search_timer.start();
    search_model.setSearchFilter({});
    const auto metadata_search_clear_query_ms =
        static_cast<double>(clear_metadata_search_timer.nsecsElapsed()) / 1e6;
    const auto search_projection_after_clear =
        CatalogFilterBenchmarkAccess::sortProjectionValid(search_model);
    if (search_model.rowCount() != count ||
        (verify && search_projection_supported && !search_projection_after_clear))
      return fail("Clearing search should restore all rows and rebuild the eligible source projection");
    cases.append(QJsonObject{
        {"photos", count},
        {"sort_projection_supported", search_projection_supported},
        {"sort_projection_before_first_query", search_projection_before_query},
        {"sort_projection_after_first_query", search_projection_after_query},
        {"sort_projection_after_clear_query", search_projection_after_clear},
        {"metadata_search_first_query_ms", metadata_search_first_query_ms},
        {"metadata_search_clear_query_ms", metadata_search_clear_query_ms},
        {"metadata_search_repeat_p95_ms", p95(metadata_search_repeat)},
        {"metadata_search_query_change_p95_ms", p95(metadata_search_query_changes)},
        {"metadata_search_cache_cost_bytes", metadata_search_cache_cost_after_queries},
        {"metadata_search_cache_limit_bytes", metadata_search_cache_limit},
        {"metadata_search_result_count", metadata_search_result_count},
    });
  }
  struct rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  qint64 peak = static_cast<qint64>(usage.ru_maxrss);
#if !defined(Q_OS_MACOS)
  peak *= 1024;
#endif
  const auto qml = qml_contract(verify);
  const QJsonObject result{{"cases", cases}, {"peak_rss_bytes", peak}, {"qml", qml}, {"verified", verify}};
  const auto output = app.arguments().indexOf("--output");
  if (output >= 0 && output + 1 < app.arguments().size()) {
    QFile file(app.arguments().at(output + 1));
    if (!file.open(QIODevice::WriteOnly)) return fail("Artifact unavailable");
    file.write(QJsonDocument(result).toJson());
  }
  std::cout << QJsonDocument(result).toJson(QJsonDocument::Compact).constData() << '\n';
  return qml.value("passed").toBool() ? 0 : fail("QML delegate/frame/scroll contract failed");
}
