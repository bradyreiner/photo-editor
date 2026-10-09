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
  static void apply(CatalogAssetModel& model) { model.applyFilters(model.selectedAssetId()); }
  static void edit(CatalogAssetModel& model, int i) {
    model.all_assets_[2].lens_model = QString::number(i);
    apply(model);
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
    CatalogAssetModel model;
    model.replaceAssetsForTesting(fixture(count));
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
    for (int i = 0; i < 7; ++i) {
      narrow.append(measure([&] { model.setCameraFilter("Camera A"); }));
      if (model.rowCount() != count / 2) return fail("Alternating filter has wrong count");
      for (int row = 0; row < model.rowCount(); ++row)
        if (model.data(model.index(row, 0), CatalogAssetModel::AssetIdRole).toString() != QStringLiteral("photo-%1").arg(row * 2))
          return fail("Alternating filter has wrong order");
      restore.append(measure([&] { model.setCameraFilter({}); }));
      if (model.rowCount() != count || !CatalogFilterBenchmarkAccess::indexes(model)) return fail("Restored filter/index mismatch");
    }
    if (verify && (resets || unchanged_changes || changes != 7 || !persistent.isValid() || persistent.row() != 2 ||
        model.selectedAssetId() != "photo-2")) return fail("Filter reset, redundant publication, or persistent selection failure");
    cases.append(QJsonObject{{"photos", count}, {"unchanged_p95_ms", p95(unchanged)}, {"edit_p95_ms", p95(edit)},
        {"narrow_p95_ms", p95(narrow)}, {"restore_p95_ms", p95(restore)}, {"resets", resets},
        {"data_changes", changes}, {"persistent_index_valid", persistent.isValid()}});
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
