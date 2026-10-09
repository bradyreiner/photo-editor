#include "visual_search_controller.h"
#include "visual_search_index.h"
#include "visual_search_test_server.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <stdexcept>
#include <sqlite3.h>

namespace photo_editor::desktop {
struct VisualSearchPreparationTestAccess {
  static void observe(VisualSearchController& controller, int& preparations) {
    QObject::connect(&controller.preparation_, &QFutureWatcher<QVariantMap>::finished,
        &controller, [&controller, &preparations] {
      if (!controller.preparation_.result().value("image").toByteArray().isEmpty()) ++preparations;
    });
  }
};
}
using namespace photo_editor::desktop;
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
QJsonObject scan(VisualSearchController& controller, visual_search_test::Server& server,
                 const QVariantList& assets, int& preparations) {
  preparations = 0;
  const int descriptions = server.descriptions;
  const int rankings = server.rankings;
  QElapsedTimer timer; timer.start();
  controller.search("red scenes", assets, server.model());
  double first_batch_ms = -1;
  while (true) {
    while (controller.running() && timer.elapsed() < 120000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
      QThread::usleep(100);
    }
    require(!controller.running(), "Search timed out");
    if (first_batch_ms < 0) first_batch_ms = double(timer.nsecsElapsed()) / 1e6;
    if (!controller.hasMore()) break;
    const auto processed = controller.processed();
    controller.searchMore();
    require(controller.running() || controller.processed() > processed, "Search failed before finishing fixture");
  }
  const double full_ms = double(timer.nsecsElapsed()) / 1e6;
  require(controller.inspected() == assets.size() && controller.skipped() == 0 && server.protocol_ok,
      "Search count or bounded protocol differed");
  QStringList expected;
  for (const auto& asset : assets) expected.append(asset.toMap().value("assetId").toString());
  require(controller.resultIds() == expected, "Ranked asset identity or order differed");
  return {{"full_ms", full_ms}, {"first_batch_ms", first_batch_ms},
      {"prepared_images", preparations}, {"description_requests", server.descriptions - descriptions},
      {"ranking_requests", server.rankings - rankings}, {"reused", controller.reusedDescriptions()},
      {"exact", true}};
}
}
int main(int argc, char** argv) try {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  require(temporary.isValid(), "Temporary directory unavailable");
  const int count = argc > 2 ? QString::fromLocal8Bit(argv[2]).toInt() : 1000;
  QVariantList assets;
  for (int i = 0; i < count; ++i) {
    QImage image(1536, 1024, QImage::Format_RGB32);
    image.fill(Qt::red);
    QPainter painter(&image);
    for (int bit = 0; bit < 12; ++bit)
      painter.fillRect(128 + bit * 96, 128, 64, 768, (i & (1 << bit)) ? Qt::blue : Qt::white);
    painter.end();
    const auto path = temporary.filePath(QStringLiteral("preview-%1.jpg").arg(i));
    require(image.save(path, "JPEG", 90), "Preview creation failed");
    assets.append(QVariantMap{{"assetId", QString::number(i)}, {"mediaType", "photo"},
        {"thumbnailSource", QUrl::fromLocalFile(path).toString()}, {"filePath", path},
        {"searchPreviewCurrent", true}, {"adjustmentPatchJson", QStringLiteral("{\"exposure\":0.25}")}});
  }
  visual_search_test::Server server;
  require(server.server.isListening(), "Loopback fixture server unavailable");
  const auto path = temporary.filePath("descriptions.sqlite3");
  int preparations = 0;
  VisualSearchController controller;
  VisualSearchPreparationTestAccess::observe(controller, preparations);
  controller.setIndexPath(path); controller.setUseIndex(true);
  const auto cold = scan(controller, server, assets, preparations);
  const auto warm = scan(controller, server, assets, preparations);
  // A fresh controller proves that the warm path survives session replacement.
  VisualSearchController restarted;
  VisualSearchPreparationTestAccess::observe(restarted, preparations);
  restarted.setIndexPath(path); restarted.setUseIndex(true);
  const auto reopened = scan(restarted, server, assets, preparations);
  require(warm.value("description_requests").toInt() == 0 && reopened.value("description_requests").toInt() == 0,
      "Warm scan repeated model descriptions");
  if (app.arguments().contains("--contracts")) {
    require(warm.value("prepared_images").toInt() == 0 && reopened.value("prepared_images").toInt() == 0,
        "Warm/reopened scan repeated image preparation");
    auto first = assets[0].toMap();
    first.insert("adjustmentPatchJson", "{\"exposure\":0.5}"); assets[0] = first;
    require(scan(restarted, server, assets, preparations).value("prepared_images").toInt() == 1,
        "Recipe revision reused a preparation alias");
    first.insert("searchPreviewCurrent", false); assets[0] = first;
    require(scan(restarted, server, assets, preparations).value("prepared_images").toInt() == 1,
        "Unconfirmed preview reused a preparation alias");
    first.insert("searchPreviewCurrent", true);
    const auto original_path = temporary.filePath("source.raw");
    QFile original(original_path);
    require(original.open(QIODevice::WriteOnly) && original.write(QByteArray(128, 'a')) == 128, "Source fixture failed");
    original.close();
    first.insert("filePath", original_path); assets[0] = first;
    require(scan(restarted, server, assets, preparations).value("prepared_images").toInt() == 1,
        "Changed source path reused an alias");
    const auto timestamp = QFileInfo(original_path).lastModified();
    require(original.open(QIODevice::ReadWrite) && original.write(QByteArray(128, 'b')) == 128 &&
        original.setFileTime(timestamp, QFileDevice::FileModificationTime), "Source replacement failed");
    original.close();
    require(scan(restarted, server, assets, preparations).value("prepared_images").toInt() == 1,
        "Same-size restored-mtime source mutation reused an alias");
    QFile thumbnail(QUrl(first.value("thumbnailSource").toString()).toLocalFile());
    require(thumbnail.open(QIODevice::Append) && thumbnail.write("trailing fixture bytes") > 0, "Thumbnail mutation failed");
    thumbnail.close();
    require(scan(restarted, server, assets, preparations).value("prepared_images").toInt() == 1,
        "Thumbnail mutation reused an alias");
    server.digest = "replacement-model";
    const auto model_changed = scan(restarted, server, assets, preparations);
    require(model_changed.value("prepared_images").toInt() == count && model_changed.value("description_requests").toInt() == count,
        "Model revision reused old preparation/descriptions");
    restarted.forgetIndex();
    QElapsedTimer forgetting; forgetting.start();
    while (restarted.running() && forgetting.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 5); QThread::usleep(100);
    }
    require(!restarted.running(), "Forget operation timed out");
    const auto forgotten = scan(restarted, server, assets, preparations);
    require(forgotten.value("prepared_images").toInt() == count && forgotten.value("description_requests").toInt() == count,
        "Forget retained preparation aliases or descriptions");
    const auto legacy_path = temporary.filePath("legacy.sqlite3");
    sqlite3* database = nullptr;
    require(sqlite3_open(QFile::encodeName(legacy_path).constData(), &database) == SQLITE_OK, "Legacy index open failed");
    require(sqlite3_exec(database, "CREATE TABLE descriptions (key TEXT PRIMARY KEY, description TEXT NOT NULL, "
        "created_at INTEGER NOT NULL DEFAULT (unixepoch())); INSERT INTO descriptions(key,description) "
        "VALUES(printf('%064x',1),'Legacy red scene')", nullptr, nullptr, nullptr) == SQLITE_OK, "Legacy index fixture failed");
    sqlite3_close(database);
    const QString description_key = QStringLiteral("%1").arg(1, 64, 16, QChar('0'));
    const QString alias_key = QStringLiteral("%1").arg(2, 64, 16, QChar('0'));
    require(VisualSearchIndex::lookupPrepared(legacy_path, alias_key).isEmpty() &&
        VisualSearchIndex::savePreparation(legacy_path, alias_key, description_key) &&
        VisualSearchIndex::lookupPrepared(legacy_path, alias_key).value("description").toString() == "Legacy red scene",
        "Legacy index migration lost descriptions or failed to record the alias");
    require(sqlite3_open(QFile::encodeName(legacy_path).constData(), &database) == SQLITE_OK, "Retention index open failed");
    require(sqlite3_exec(database, "WITH RECURSIVE n(x) AS (VALUES(1) UNION ALL SELECT x+1 FROM n WHERE x<10010) "
        "INSERT OR REPLACE INTO preparation_aliases(key,description_key) SELECT printf('%064x',x),printf('%064x',1) FROM n",
        nullptr, nullptr, nullptr) == SQLITE_OK, "Retention fixture failed");
    sqlite3_close(database);
    require(VisualSearchIndex::savePreparation(legacy_path, alias_key, description_key), "Bounded alias save failed");
    require(sqlite3_open(QFile::encodeName(legacy_path).constData(), &database) == SQLITE_OK, "Retention read failed");
    sqlite3_stmt* statement = nullptr;
    require(sqlite3_prepare_v2(database, "SELECT count(*) FROM preparation_aliases", -1, &statement, nullptr) == SQLITE_OK &&
        sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_int(statement, 0) == 10000, "Alias retention exceeded 10000 entries");
    sqlite3_finalize(statement); sqlite3_close(database);
    require(VisualSearchIndex::clear(legacy_path) && VisualSearchIndex::lookupPrepared(legacy_path, alias_key).isEmpty(),
        "Clearing legacy index retained aliases");
    std::cout << "Revision/recipe/model invalidation, unconfirmed preview fallback, restart reuse, and forgetting passed.\n";
  }
  const QJsonObject result{{"count", count}, {"cold", cold}, {"warm", warm}, {"reopened", reopened},
      {"scope", "Actual controller with generated edited-preview fixtures and local protocol server; no real model/native UI"}};
  if (argc > 1) {
    QFile output(QString::fromLocal8Bit(argv[1]));
    require(output.open(QIODevice::WriteOnly) && output.write(QJsonDocument(result).toJson()) > 0, "Report write failed");
  }
  std::cout << QJsonDocument(result).toJson().constData();
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n';
  return 1;
}
