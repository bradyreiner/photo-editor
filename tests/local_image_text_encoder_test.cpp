#include "local_image_text_encoder.h"
#include "visual_embedding_index.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>

using namespace photo_editor::desktop;
namespace photo_editor::desktop {
struct LocalImageTextEncoderTestAccess {
  static void deadline(LocalImageTextEncoder& encoder, int ms) { encoder.deadline_.start(ms); }
};
}
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
QVector<float> vector(const QVariantMap& result) {
  QVector<float> values;
  for (const auto& item : result.value("vector").toList()) values.append(item.toFloat());
  return values;
}
QString hash(const QString& value) { return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex()); }

int live(const QStringList& args) {
  if (args.size() < 5) return fail("usage: --live python model-root fixture-manifest [cpu|mps]");
  QFile manifest_file(args.at(4));
  if (!manifest_file.open(QIODevice::ReadOnly) || manifest_file.size() > 1024 * 1024) return fail("invalid fixture manifest");
  const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
  const auto images = manifest.value("images").toArray(), queries = manifest.value("queries").toArray();
  if (images.isEmpty() || images.size() > 64 || queries.isEmpty() || queries.size() > 64) return fail("invalid live scope");
  QTemporaryDir index_root;
  if (!index_root.isValid()) return fail("index fixture unavailable");
  LocalImageTextEncoder encoder;
  QVariantMap last;
  QString last_id, error;
  QObject::connect(&encoder, &LocalImageTextEncoder::embeddingReady, [&](const QString& id, const QVariantMap& result) { last_id = id; last = result; });
  QObject::connect(&encoder, &LocalImageTextEncoder::requestFailed, [&](const QString&, const QString& message) { error = message; });
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
  heartbeat.start(10);
  const auto device = args.size() > 5 ? args.at(5) : QStringLiteral("cpu");
  const auto script = QStringLiteral(PHOTO_EDITOR_SOURCE_ROOT "/scripts/search_model_worker.py");
  QElapsedTimer timer; timer.start();
  if (!encoder.start(args.at(2), script, args.at(3), device)) return fail("owned encoder start rejected");
  const auto start_admission_ms = timer.elapsed();
  if (!wait_until([&] { return encoder.snapshot().value("ready").toBool() || encoder.snapshot().value("state") == "failed"; }, 120000) ||
      !encoder.snapshot().value("ready").toBool()) { std::cerr << encoder.snapshot().value("error").toString().toStdString(); return 1; }
  const auto startup_ms = timer.elapsed();
  const auto startup_ticks = ticks;
  if (start_admission_ms >= 50 || startup_ticks < 10) return fail("model loading blocked the native event loop");
  const auto pid = encoder.snapshot().value("processId").toLongLong();
  const VisualEmbeddingSpace space{encoder.snapshot().value("spaceKey").toString(), 768};
  QVector<VisualEmbeddingCandidate> scope;
  const auto index_path = index_root.filePath("actual-vectors.sqlite3");
  QJsonArray image_receipts, query_receipts;
  auto await = [&](const QString& id) {
    return !id.isEmpty() && wait_until([&] { return last_id == id || !error.isEmpty(); }, 35000) && error.isEmpty() && last_id == id;
  };
  for (const auto& entry : images) {
    const auto image = entry.toObject();
    timer.restart();
    if (!await(encoder.encodeImage(image.value("path").toString()))) return fail("owned image encoding failed");
    const VisualEmbeddingCandidate candidate{hash(image.value("id").toString()), last.value("contentKey").toString()};
    const auto ticket = VisualEmbeddingIndex::stage(index_path, space, candidate);
    if (!ticket.error.isEmpty() || !VisualEmbeddingIndex::commit(index_path, space, candidate, ticket.token, vector(last)).ok)
      return fail("native persistence rejected owned-worker embedding");
    scope.append(candidate);
    image_receipts.append(QJsonObject{{"id", image.value("id")}, {"roundTripAndCommitMs", timer.elapsed()}, {"encodeMs", last.value("milliseconds").toDouble()}});
  }
  for (const auto& entry : queries) {
    const auto query = entry.toObject();
    QStringList expected;
    for (const auto& item : query.value("expectedIds").toArray()) expected.append(hash(item.toString()));
    for (int repeat = 0; repeat < 2; ++repeat) {
      timer.restart();
      if (!await(encoder.encodeText(query.value("text").toString()))) return fail("owned query encoding failed");
      const auto found = VisualEmbeddingIndex::search(index_path, space, scope, vector(last), 1);
      if (!found.error.isEmpty() || found.ready != scope.size() || found.matches.size() != 1 ||
          !expected.contains(found.matches.first().asset_key) || encoder.snapshot().value("processId").toLongLong() != pid)
        return fail("owned encoder retrieval missed expected image or reloaded worker");
      query_receipts.append(QJsonObject{{"query", query.value("text")}, {"repeat", repeat}, {"expectedFirst", true},
          {"roundTripAndRetrievalMs", timer.elapsed()}, {"encodeMs", last.value("milliseconds").toDouble()}});
    }
  }
  const auto previous_reply = last_id;
  timer.restart();
  const auto cancelled_request = encoder.encodeText(queries.first().toObject().value("text").toString());
  encoder.cancel();
  const auto cancel_admission_ms = timer.elapsed();
  if (cancelled_request.isEmpty() || !encoder.snapshot().value("cancelPending").toBool() ||
      !wait_until([&] { return !encoder.snapshot().value("busy").toBool(); }, 35000) ||
      last_id != previous_reply || !error.isEmpty() || cancel_admission_ms >= 50)
    return fail("actual inference cancellation published a late reply or blocked");
  encoder.stop();
  if (!wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }, 5000)) return fail("owned encoder did not stop");
  if (!encoder.start(args.at(2), script, args.at(3), device) || !wait_until([&] { return encoder.snapshot().value("ready").toBool(); }, 120000))
    return fail("owned encoder restart failed");
  const auto restarted_pid = encoder.snapshot().value("processId").toLongLong();
  if (pid == restarted_pid || encoder.snapshot().value("spaceKey").toString() != space.key ||
      !await(encoder.encodeText(queries.first().toObject().value("text").toString()))) return fail("restart changed artifact identity or replayed request");
  const auto reused = VisualEmbeddingIndex::search(index_path, space, scope, vector(last), 1);
  if (reused.ready != scope.size() || reused.matches.size() != 1) return fail("restart lost persisted image vectors");
  QStringList restarted_expected;
  for (const auto& item : queries.first().toObject().value("expectedIds").toArray()) restarted_expected.append(hash(item.toString()));
  if (!restarted_expected.contains(reused.matches.first().asset_key)) return fail("restarted query no longer matches its expected image");
  encoder.stop();
  if (!wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }, 5000)) return fail("restarted owner did not stop");
  std::cout << QJsonDocument(QJsonObject{{"actualInference", true}, {"device", device}, {"startAdmissionMs", start_admission_ms},
      {"startupMs", startup_ms}, {"startupHeartbeatTicks", startup_ticks}, {"firstProcessId", pid}, {"restartedProcessId", restarted_pid},
      {"stableSpaceAcrossRestart", true}, {"persistedVectorsReusedAfterRestart", reused.ready}, {"ownedProcessesStopped", true},
      {"cancelAdmissionMs", cancel_admission_ms}, {"actualCancelledReplySuppressed", true},
      {"images", image_receipts}, {"queries", query_receipts}}).toJson().constData();
  return 0;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  if (app.arguments().contains("--live")) return live(app.arguments());
  QTemporaryDir root;
  const auto script = root.filePath("fake.py");
  if (!root.isValid() || !write(script, R"PY(import json,sys,time
from pathlib import Path
root=Path(sys.argv[sys.argv.index('--model-root')+1])
mode=(root/'mode').read_text() if (root/'mode').exists() else ''
if mode=='hang': time.sleep(5)
print(json.dumps(dict(type='ready',protocol=1,spaceKey='a'*64,dimensions=768,revision='bad' if mode=='bad' else '75de2d55ec2d0b4efc50b3e9ad70dba96a7b2fa2',device='cpu')),flush=True)
for line in sys.stdin:
 r=json.loads(line); text=r.get('text','')
 if text=='delay': time.sleep(.25)
 if text=='exit': sys.exit(3)
 if text=='flood': print('x'*150000,flush=True); continue
 if text=='error': print(json.dumps(dict(type='error',id=r['id'],message='fixture error')),flush=True); continue
 v=[1.0]+[0.0]*767
 if text=='zero': v=[0.0]*768
 if text=='dimension': v=[1.0]
 print(json.dumps(dict(type='embedding',id='wrong' if text=='stale' else r['id'],kind=r['kind'],spaceKey='a'*64,contentKey='b'*64,vector=v,milliseconds=1)),flush=True)
)PY")) return fail("fake worker fixture unavailable");
  LocalImageTextEncoder encoder;
  int delivered = 0, failures = 0, cancellations = 0;
  QObject::connect(&encoder, &LocalImageTextEncoder::embeddingReady, [&](const QString&, const QVariantMap&) { ++delivered; });
  QObject::connect(&encoder, &LocalImageTextEncoder::requestFailed, [&](const QString&, const QString&) { ++failures; });
  QObject::connect(&encoder, &LocalImageTextEncoder::requestCancelled, [&](const QString&) { ++cancellations; });
  auto start = [&] { return encoder.start(QStringLiteral(PHOTO_EDITOR_TEST_PYTHON), script, root.path(), "cpu") &&
      wait_until([&] { return encoder.snapshot().value("ready").toBool(); }); };
  auto stop = [&] { encoder.stop(); return wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }); };
  if (!encoder.encodeText("before-ready").isEmpty() || !start()) return fail("readiness admission failed");
  const auto pid = encoder.snapshot().value("processId").toLongLong();
  if (encoder.encodeText("ok").isEmpty() || !wait_until([&] { return delivered == 1; }) ||
      encoder.snapshot().value("processId").toLongLong() != pid) return fail("normal worker response failed");
  int ticks = 0;
  QTimer heartbeat;
  QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start(10);
  QElapsedTimer timer; timer.start();
  if (encoder.encodeText("delay").isEmpty()) return 1;
  encoder.cancel(); encoder.cancel();
  const auto cancel_ms = timer.elapsed();
  if (!encoder.encodeText("must-not-queue").isEmpty() || !wait_until([&] { return !encoder.snapshot().value("busy").toBool(); }) ||
      delivered != 1 || cancellations != 1 || failures != 0 || ticks < 15 || cancel_ms >= 50)
    return fail("cancel, backpressure or heartbeat contract failed");
  if (encoder.encodeText("error").isEmpty() || !wait_until([&] { return failures == 1; }) || !encoder.snapshot().value("ready").toBool())
    return fail("request error must release slot without replay/restart");
  if (encoder.encodeText("after-error").isEmpty() || !wait_until([&] { return delivered == 2; }) ||
      encoder.snapshot().value("processId").toLongLong() != pid) return fail("request error discarded the reusable worker");
  for (const auto& request : {"zero", "dimension", "stale", "flood", "exit"}) {
    const auto before = failures;
    if (encoder.encodeText(request).isEmpty() || !wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }) ||
        encoder.snapshot().value("state") != "failed" || failures != before + 1 || delivered != 2 || !start())
      return fail("invalid reply or worker exit was accepted/replayed");
  }
  const auto before_timeout = failures;
  if (encoder.encodeText("delay").isEmpty()) return 1;
  LocalImageTextEncoderTestAccess::deadline(encoder, 20);
  if (!wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }) || failures != before_timeout + 1 || !stop())
    return fail("request deadline failed");
  if (!write(root.filePath("mode"), "bad") || !encoder.start(QStringLiteral(PHOTO_EDITOR_TEST_PYTHON), script, root.path(), "cpu") ||
      !wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }) || encoder.snapshot().value("ready").toBool())
    return fail("wrong revision handshake accepted");
  if (!write(root.filePath("mode"), "hang") || !encoder.start(QStringLiteral(PHOTO_EDITOR_TEST_PYTHON), script, root.path(), "cpu")) return 1;
  LocalImageTextEncoderTestAccess::deadline(encoder, 20);
  if (!wait_until([&] { return !encoder.snapshot().value("ownsProcess").toBool(); }) || encoder.snapshot().value("state") != "failed")
    return fail("startup deadline failed");
  std::cout << "owned worker readiness, normalized vectors, cancellation/backpressure, errors, invalid replies, exit and deadlines passed; cancel="
            << cancel_ms << " ms; heartbeats=" << ticks << '\n';
  return 0;
}
