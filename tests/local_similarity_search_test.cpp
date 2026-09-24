#include "local_similarity_search.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QTimer>
#include <iostream>

using namespace photo_editor::desktop;
namespace {
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
bool wait_until(auto predicate, int timeout = 10000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout) { QCoreApplication::processEvents(); QThread::msleep(1); }
  return predicate();
}
bool write(const QString& path, const QByteArray& bytes) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QVariantMap asset(const QString& id, const QString& path) {
  return {{"assetId", id}, {"filePath", path + ".raw"}, {"thumbnailSource", path},
          {"mediaType", "photo"}, {"searchPreviewCurrent", true}, {"adjustmentPatchJson", "{}"}};
}
bool search(LocalSimilaritySearch& controller, const QString& query, const QVariantList& assets,
            const QString& catalog = "catalog-one", int timeout = 10000) {
  controller.search(query, assets, catalog, 32);
  if (!wait_until([&] { return !controller.running(); }, timeout)) return false;
  if (!controller.status().contains("Not verified matches")) {
    std::cerr << controller.status().toStdString() << '\n'; return false;
  }
  return true;
}
int live(const QStringList& args) {
  if (args.size() != 6) return fail("usage: --live python model-root fixture-manifest cpu|mps");
  QFile file(args.at(4));
  if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return fail("invalid live manifest");
  const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
  QVariantList assets;
  for (const auto& value : manifest.value("images").toArray()) {
    const auto image = value.toObject();
    assets.append(asset(image.value("id").toString(), image.value("path").toString()));
  }
  if (assets.isEmpty() || assets.size() > 64) return fail("invalid live images");
  QTemporaryDir root;
  LocalSimilaritySearch controller;
  const bool configured = args.at(1) == "--live-executable"
      ? controller.configureExecutable(args.at(2), args.at(3), args.at(5), root.filePath("index.sqlite3"))
      : controller.configure(args.at(2), QStringLiteral(PHOTO_EDITOR_SOURCE_ROOT "/scripts/search_model_worker.py"),
                             args.at(3), args.at(5), root.filePath("index.sqlite3"));
  if (!root.isValid() || !configured)
    return fail("live configure failed");
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(10);
  QJsonArray receipts;
  for (int round = 0; round < 2; ++round) {
    for (const auto& value : manifest.value("queries").toArray()) {
      const auto query = value.toObject();
      QElapsedTimer elapsed; elapsed.start();
      const auto before = ticks;
      if (!search(controller, query.value("text").toString(), assets, "live-catalog", 180000)) return fail("live coordinator failed");
      if (controller.resultIds().isEmpty() || !query.value("expectedIds").toArray().contains(controller.resultIds().first()))
        return fail("live coordinator top result not expected");
      if (!receipts.isEmpty() && (controller.reused() != assets.size() || controller.encoded() != 0))
        return fail("live warm query re-encoded unchanged photos");
      receipts.append(QJsonObject{{"query", query.value("text")}, {"round", round}, {"milliseconds", elapsed.elapsed()},
          {"heartbeatTicks", ticks - before}, {"reused", controller.reused()}, {"encoded", controller.encoded()}, {"expectedFirst", true}});
    }
  }
  // A restricted query may not admit a better-scoring photo outside that view.
  if (!search(controller, "a white dog outdoors", {assets.first()}, "live-catalog", 35000) ||
      controller.resultIds() != QStringList{assets.first().toMap().value("assetId").toString()})
    return fail("live restricted scope broadened");
  controller.stop();
  if (!wait_until([&] { return !controller.running(); }, 5000)) return fail("live owner did not stop");
  const auto first_query = manifest.value("queries").toArray().first().toObject();
  if (!search(controller, first_query.value("text").toString(), assets, "live-catalog", 180000) ||
      controller.reused() != assets.size() || controller.encoded() != 0 ||
      !first_query.value("expectedIds").toArray().contains(controller.resultIds().value(0)))
    return fail("coordinator restart lost persisted vectors or correct ranking");
  controller.stop();
  if (!wait_until([&] { return !controller.running(); }, 5000)) return fail("restarted live owner did not stop");
  std::cout << QJsonDocument(QJsonObject{{"actualInference", true}, {"queries", receipts}, {"restrictedScope", true},
      {"persistedVectorsReusedAfterRestart", assets.size()},
      {"ownedProcessStopped", true}, {"qmlFindPhotosExercised", false}}).toJson().constData();
  return 0;
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--live") || app.arguments().contains("--live-executable")) return live(app.arguments());
  QTemporaryDir root;
  const auto script = root.filePath("worker.py");
  if (!root.isValid() || !write(script, R"PY(import json,sys,time
from pathlib import Path
root=Path(sys.argv[sys.argv.index('--model-root')+1])
print(json.dumps(dict(type='ready',protocol=1,spaceKey='a'*64,dimensions=768,
 revision='75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2',device='cpu')),flush=True)
for line in sys.stdin:
 r=json.loads(line)
 with (root/'requests').open('a') as log: log.write(r['kind']+'\n')
 if r['kind']=='image':
  time.sleep(.15)
  content=Path(r['path']).read_bytes().decode()
 else:
  time.sleep(.03)
  content=r['text']
 if content=='FAIL':
  print(json.dumps(dict(type='error',id=r['id'],message='Fixture decode failed')),flush=True)
  continue
 v=[0.0]*768
 v[0 if content=='A' else 1]=1.0
 print(json.dumps(dict(type='embedding',id=r['id'],kind=r['kind'],spaceKey='a'*64,
  contentKey='b'*64,vector=v,milliseconds=1)),flush=True)
)PY")) return fail("fixture setup failed");
  const auto a = root.filePath("a.preview"), b = root.filePath("b.preview");
  if (!write(a, "A") || !write(b, "B")) return fail("preview setup failed");
  const auto aa = asset("a", a), bb = asset("b", b);
  LocalSimilaritySearch controller;
  if (!controller.configure(QStringLiteral(PHOTO_EDITOR_TEST_PYTHON), script, root.path(), "cpu", root.filePath("index.sqlite3")))
    return fail("configure rejected");
  int ticks = 0;
  QTimer heartbeat; heartbeat.start(10);
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  if (!search(controller, "A", {aa, bb}) || controller.resultIds() != QStringList{"a", "b"} ||
      controller.encoded() != 2 || ticks < 10) return fail("background initial indexing/ranking/heartbeats failed");
  if (!search(controller, "A", {bb}) || controller.resultIds() != QStringList{"b"} ||
      controller.reused() != 1 || controller.encoded() != 0) return fail("filter scope expanded or unchanged image re-encoded");
  if (!search(controller, "A", {aa, bb}) || controller.reused() != 2) return fail("filter absence erased unrelated vectors");
  if (!search(controller, "A", {aa}, "catalog-two") || controller.encoded() != 1) return fail("catalog namespaces leaked");
  auto changed = aa; changed.insert("adjustmentPatchJson", "{changed:true}");
  if (!search(controller, "A", {changed}) || controller.encoded() != 1) return fail("recipe change reused old vector");
  if (!write(a, "B") || !search(controller, "A", {changed}) || controller.encoded() != 1)
    return fail("same-path changed bytes reused old vector");
  auto stale = aa; stale.insert("thumbnailStaleMismatch", true); stale.insert("searchPreviewCurrent", false);
  if (!search(controller, "A", {stale}) || !controller.results().isEmpty() || !controller.indexWarning().startsWith("1 photos"))
    return fail("stale thumbnail was searched or missing coverage concealed");
  auto transient = aa; transient.insert("searchPreviewCurrent", false);
  if (!search(controller, "A", {transient}) || !controller.results().isEmpty()) return fail("transient preview accepted");
  auto remote = aa; remote.insert("thumbnailSource", "https://example.invalid/photo.jpg");
  if (!search(controller, "A", {remote}) || !controller.results().isEmpty()) return fail("remote preview or original fallback accepted");
  if (!write(a, "FAIL") || !search(controller, "A", {aa, bb}) || controller.resultIds() != QStringList{"b"})
    return fail("image decode failure did not retain honest partial coverage");
  controller.search("A", {aa, aa}, "catalog-one");
  if (controller.running() || !controller.results().isEmpty() || !controller.status().contains("duplicate"))
    return fail("duplicate scope admitted");
  // Invalidate while the actual owned child is sleeping inside image inference.
  if (!write(a, "A") || !write(root.filePath("requests"), "")) return fail("cancel fixture reset failed");
  int finished = 0;
  QObject::connect(&controller, &LocalSimilaritySearch::batchFinished, [&] { ++finished; });
  controller.search("A", {aa}, "new-catalog");
  if (!wait_until([&] {
    QFile requests(root.filePath("requests"));
    return requests.open(QIODevice::ReadOnly) && requests.readAll().endsWith("image\n");
  })) return fail("in-flight image not reached");
  QElapsedTimer cancel_time; cancel_time.start();
  controller.invalidate();
  if (cancel_time.elapsed() >= 50) return fail("invalidation blocked");
  controller.search("B", {bb}, "replacement-must-not-queue");
  if (!wait_until([&] { return !controller.running(); }) || finished != 0 || !controller.results().isEmpty())
    return fail("invalidated inference published or queued replacement work");
  if (!search(controller, "B", {bb}, "after-delete-recreate") || controller.resultIds() != QStringList{"b"})
    return fail("post-invalidation replacement failed");
  controller.invalidate();
  if (!controller.results().isEmpty()) return fail("completed results survived scope invalidation");
  controller.stop();
  if (!wait_until([&] { return !controller.running(); }, 5000)) return fail("owner stop failed");
  if (!search(controller, "B", {bb}, "after-delete-recreate") || controller.reused() != 1 || controller.encoded() != 0)
    return fail("restarted coordinator lost unchanged persisted vectors");
  controller.stop();
  if (!wait_until([&] { return !controller.running(); }, 5000)) return fail("restarted owner stop failed");
  return 0;
}
