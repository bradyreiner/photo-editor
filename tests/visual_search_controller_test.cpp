#include "visual_search_controller.h"
#include "visual_search_index.h"
#include "visual_search_test_server.h"
#include "agent_runtime_paths.h"
#include "agent_model_artifact.h"
#include "managed_agent_runtime.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <functional>
#include <iostream>

using photo_editor::desktop::VisualSearchController;
namespace {
bool wait_until(const std::function<bool()>& predicate, int timeout = 10000) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  return predicate();
}
int fail(const char* message) { std::cerr << message << '\n'; return EXIT_FAILURE; }

using visual_search_test::Server;
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto args = app.arguments();
  if (args.contains("--live")) {
    // Explicit opt-in: one provided local fixture, no downloads or catalog mutations.
    const auto index = args.indexOf("--live");
    if (args.size() < index + 4) return fail("usage: --live image-path model query");
    photo_editor::desktop::ManagedAgentRuntime runtime;
    auto model = QVariantMap{{"backend", "ollama"}, {"vision", true}, {"model", args.at(index + 2)},
                            {"endpoint", "http://127.0.0.1:11434"}};
    const auto managed_option = args.indexOf("--managed-runtime");
    if (managed_option >= 0) {
      if (managed_option + 2 >= args.size()) return fail("usage: --managed-runtime binary existing-private-model-store");
      if (!runtime.start(args.at(managed_option + 1), args.at(managed_option + 2)) ||
          !wait_until([&] {
            const auto state = runtime.snapshot();
            return state.value("verifiedModels").toStringList().contains(args.at(index + 2)) ||
                   state.value("state").toString() == "failed";
          }, 120000) || !runtime.snapshot().value("verifiedModels").toStringList().contains(args.at(index + 2)))
        return fail("owned runtime did not verify the requested installed model; no image was sent");
      model.insert("runtime", "managed");
      model.insert("endpoint", runtime.snapshot().value("endpoint"));
    }
    const auto verified_runtime = runtime.snapshot();
    VisualSearchController controller;
    const auto index_option = args.indexOf("--index-path");
    if (index_option >= 0 && index_option + 1 < args.size()) {
      controller.setIndexPath(args.at(index_option + 1));
      controller.setUseIndex(true);
    }
    QElapsedTimer live_timer;
    live_timer.start();
    controller.search(args.at(index + 3), {QVariantMap{{"assetId", "live-fixture"}, {"mediaType", "photo"},
        {"thumbnailSource", QUrl::fromLocalFile(args.at(index + 1)).toString()}}},
        model);
    if (!wait_until([&] { return !controller.running(); }, 240000)) return fail("live vision search timed out");
    const auto search_elapsed = live_timer.elapsed();
    runtime.stop();
    if (managed_option >= 0 && !wait_until([&] { return !runtime.snapshot().value("ownsProcess").toBool(); }, 5000))
      return fail("owned model runtime did not stop after the live search");
    std::cout << QJsonDocument(QJsonObject{{"status", controller.status()}, {"inspected", controller.inspected()}, {"elapsedMs", search_elapsed},
        {"runtime", managed_option >= 0 ? "managed" : "external"},
        {"managedRuntimeBeforeSearch", managed_option >= 0 ? QJsonValue(QJsonObject::fromVariantMap(verified_runtime)) : QJsonValue()},
        {"ownedRuntimeStopped", managed_option >= 0 ? QJsonValue(!runtime.snapshot().value("ownsProcess").toBool()) : QJsonValue()},
        {"detailVerifiedCount", controller.detailVerifiedCount()},
        {"reused", controller.reusedDescriptions()}, {"newDescriptions", controller.newDescriptions()},
        {"indexWarning", controller.indexWarning()}, {"results", QJsonArray::fromVariantList(controller.results())}}).toJson().constData();
    return controller.inspected() == 1 ? EXIT_SUCCESS : EXIT_FAILURE;
  }
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("temporary directory unavailable");
  QVariantList assets;
  for (int i = 0; i < 3; ++i) {
    QImage image(1200, 800, QImage::Format_RGB32);
    image.fill(i == 1 ? Qt::blue : Qt::red);
    const auto path = temporary.filePath(QStringLiteral("secret-filename-%1.jpg").arg(i));
    if (!image.save(path)) return fail("fixture save failed");
    assets.append(QVariantMap{{"assetId", QString::number(i)}, {"mediaType", "photo"},
        {"thumbnailSource", QUrl::fromLocalFile(path).toString()}, {"filePath", path}});
  }
  Server server;
  if (!server.server.isListening()) return fail("loopback test server could not start");
  VisualSearchController controller;
  auto complete = [&] { return wait_until([&] { return !controller.running(); }); };
  controller.search("red scene without people", assets, server.model());
  if (!complete() || controller.inspected() != 3 || controller.resultIds() != QStringList{"0", "2"} ||
      !server.protocol_ok) return fail("visual ranking / bounded image protocol failed");
  const auto chats = server.chats;
  controller.search("red scene without people", assets, server.model());
  if (!complete() || server.chats != chats) return fail("identical preview/query/model should reuse cached scores");
  server.digest = "updated-model";
  controller.search("red scene without people", assets, server.model());
  if (!complete() || server.chats != chats + 2) return fail("model revision must invalidate cached scores");
  QImage replacement(200, 200, QImage::Format_RGB32);
  replacement.fill(Qt::blue);
  if (!replacement.save(assets.first().toMap().value("filePath").toString())) return fail("replacement save failed");
  controller.search("red scene without people", assets, server.model());
  if (!complete() || controller.resultIds() != QStringList{"2"} || server.chats != chats + 3)
    return fail("preview content changes must invalidate cached results");
  server.cloud = true;
  const auto before_cloud = server.chats;
  controller.search("new query", assets, server.model());
  if (!complete() || server.chats != before_cloud || controller.inspected() != 0)
    return fail("cloud-backed model must receive no image");
  server.cloud = false;
  auto remote = server.model();
  remote.insert("endpoint", "https://example.com:443");
  const auto before_remote = server.requests;
  controller.search("new query", assets, remote);
  if (controller.running() || server.requests != before_remote) return fail("nonlocal endpoint was accepted");
  server.redirect = true;
  controller.search("new query", assets, server.model());
  if (!complete() || controller.inspected() != 0) return fail("redirect should fail closed");
  server.redirect = false;
  server.malformed = true;
  controller.search("invalid query response", assets, server.model());
  if (!complete() || controller.inspected() != 0 || !controller.results().isEmpty()) return fail("malformed score accepted");
  server.malformed = false;
  server.alternate_field = true;
  controller.search("alternate structured response", assets, server.model());
  if (!complete() || controller.inspected() != 3) return fail("complete alternate-field schema object should be accepted");
  server.free_reasoning = true;
  controller.search("free-form reasoning", assets, server.model());
  if (!complete() || controller.inspected() != 0) return fail("free-form reasoning must never be extracted or displayed");
  server.free_reasoning = false;
  server.alternate_field = false;
  server.delay = 100;
  const auto before_delayed = server.chats;
  controller.search("stale query", assets, server.model());
  if (!wait_until([&] { return server.chats > before_delayed; })) return fail("delayed chat never started");
  controller.cancel();
  controller.search("replacement query", {assets.last()}, server.model());
  if (!complete() || controller.total() != 1 || controller.inspected() != 1 || controller.query() != "replacement query")
    return fail("cancelled reply contaminated replacement query");
  server.delay = 0;
  QVariantList large;
  for (int i = 0; i < 35; ++i) {
    auto asset = assets.last().toMap();
    asset.insert("assetId", QStringLiteral("batch-%1").arg(i));
    large.append(asset);
  }
  controller.search("bounded batch", large, server.model());
  if (!complete() || controller.processed() != 32 || !controller.hasMore()) return fail("scan must stop at batch bound");
  controller.searchMore();
  if (!complete() || controller.processed() != 35 || controller.hasMore() || controller.results().size() != 35)
    return fail("continue scan should retain and merge ranked matches");
  controller.search("unreadable", {QVariantMap{{"assetId", "missing-raw"}, {"mediaType", "photo"}, {"filePath", "/missing.dng"}}}, server.model());
  if (!complete() || controller.skipped() != 1 || controller.inspected() != 0) return fail("missing RAW preview must be reported as skipped");
  controller.clear();
  if (controller.total() != 0 || !controller.results().isEmpty() || controller.hasMore()) return fail("clear retained stale scope");

  const auto index_path = temporary.filePath("index/descriptions.sqlite3");
  {
    VisualSearchController indexed;
    indexed.setIndexPath(index_path);
    indexed.setUseIndex(true);
    indexed.search("red", assets, server.model());
    if (!wait_until([&] { return !indexed.running(); }) || indexed.inspected() != 3 ||
        indexed.resultIds() != QStringList{"2"} || indexed.newDescriptions() != 3 ||
        !indexed.indexWarning().isEmpty()) return fail("first indexed query should persist descriptions and rank the batch");
  }
  const auto saved_descriptions = server.descriptions;
  {
    Server details;
    details.ranking_evidence = "insufficient";
    details.ranking_score = 50;
    VisualSearchController hybrid;
    hybrid.setIndexPath(temporary.filePath("detail-index.sqlite3"));
    hybrid.setUseIndex(true);
    hybrid.search("red detail omitted from description", {assets.last()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.resultIds() != QStringList{"2"} ||
        hybrid.inspected() != 1 || hybrid.property("detailVerifiedCount").toInt() != 1 ||
        hybrid.results().first().toMap().value("evidenceSource") != "image-verification")
      return fail("insufficient description evidence must trigger image verification, not silently discard a match");
    const auto chats_before_reuse = details.chats;
    hybrid.search("red detail omitted from description", {assets.last()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.newDescriptions() != 0 || hybrid.reusedDescriptions() != 1 ||
        hybrid.resultIds() != QStringList{"2"} || details.chats != chats_before_reuse + 1)
      return fail("repeated detail verification should reuse the content/query-specific direct score");
    details.ranking_score = 90;
    hybrid.search("red detail despite an uncertain high rank", {assets.first()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || !hybrid.results().isEmpty() || hybrid.detailVerifiedCount() != 1)
      return fail("insufficient evidence must not publish a high description score without image verification");
    details.ranking_evidence = "match";
    hybrid.search("red explicit description", {assets.last()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.detailVerifiedCount() != 0 || hybrid.inspected() != 1 ||
        hybrid.results().first().toMap().value("evidenceSource") != "saved-description")
      return fail("explicit descriptive matches should retain their cheap path and honest evidence source");
    details.ranking_evidence = "contradiction";
    details.ranking_score = 20;
    hybrid.search("explicit contradictory description", {assets.last()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.detailVerifiedCount() != 0 || !hybrid.results().isEmpty())
      return fail("explicit contradictions should not require an image check");
    details.ranking_evidence = "invalid-evidence";
    const auto before_invalid = details.chats;
    hybrid.search("invalid evidence", {assets.last()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 0 || !hybrid.hasMore() ||
        !hybrid.results().isEmpty() || details.chats != before_invalid + 1)
      return fail("invalid evidence classification must fail atomically before image verification");
    details.ranking_evidence = "insufficient";
    details.ranking_score = 50;
    details.malformed = true;
    hybrid.searchMore();
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 0 || !hybrid.hasMore() ||
        !hybrid.status().startsWith("Photo detail check failed"))
      return fail("failed detail checks must remain explicitly retryable without false completion");
    details.malformed = false;
    const auto descriptions_before_retry = details.descriptions;
    hybrid.searchMore();
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 1 || hybrid.resultIds() != QStringList{"2"} ||
        hybrid.detailVerifiedCount() != 1 || details.descriptions != descriptions_before_retry)
      return fail("detail retry must preserve description work and count each target once");
    details.delay = 50;
    const auto before_cancel = details.chats;
    hybrid.search("cancel detail request", {assets.last()}, details.model());
    if (!wait_until([&] { return details.chats >= before_cancel + 2; }))
      return fail("delayed direct verification did not start");
    hybrid.cancel();
    hybrid.search("replacement detail request", {assets.first()}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 1 ||
        !hybrid.results().isEmpty() || hybrid.detailVerifiedCount() != 1)
      return fail("canceled detail verification must not contaminate replacement search");
    details.delay = 0;
    hybrid.search("bounded uncertain batch", large, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 32 ||
        hybrid.detailVerifiedCount() != 32 || !hybrid.hasMore())
      return fail("automatic image verification must stay inside the current 32-photo batch");
    hybrid.searchMore();
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 35 ||
        hybrid.detailVerifiedCount() != 35 || hybrid.results().size() != 35 || hybrid.hasMore())
      return fail("detail continuation must merge the frozen scope exactly once");
    details.ranking_evidence.clear();
    details.ranking_score = -1;
    details.insufficient_indices = {0};
    auto early = assets.last().toMap(); early.insert("assetId", "early");
    auto late = early; late.insert("assetId", "late");
    hybrid.search("red mixed evidence tie", {early, late}, details.model());
    if (!wait_until([&] { return !hybrid.running(); }) || hybrid.inspected() != 2 || hybrid.detailVerifiedCount() != 1 ||
        hybrid.resultIds() != QStringList{"early", "late"})
      return fail("mixed descriptive and image-verified ties must retain original catalog order");
    std::cout << "insufficient evidence verification, cache reuse, negative checks, retry, cancellation and batch bounds passed\n";
  }
  {
    // Mechanism test only: the supervisor registry is injected; no real weights
    // are represented as verified by this fake-server regression.
    Server first_runtime;
    Server second_runtime;
    const auto pin = photo_editor::desktop::pinned_agent_model_artifact("qwen3-vl:32b");
    first_runtime.model_name = second_runtime.model_name = pin.model;
    first_runtime.digest = second_runtime.digest = QString::fromLatin1(pin.files.first().sha256);
    const auto store = temporary.filePath("managed-models");
    QDir().mkpath(store);
    const auto managed_model = [](const Server& runtime) {
      auto config = runtime.model();
      config.insert("runtime", "managed");
      return config;
    };
    const auto publish = [&](const Server& runtime, qint64 pid, bool verified = true) {
      photo_editor::desktop::publish_managed_agent_runtime({
          {"ready", true}, {"ownsProcess", true}, {"processId", pid},
          {"endpoint", runtime.model().value("endpoint")}, {"modelsPath", store},
          {"verifiedModels", verified ? QStringList{pin.model} : QStringList{}}});
    };
    const auto managed_index = temporary.filePath("managed-index.sqlite3");
    const QVariantList one_asset{assets.first()};
    publish(first_runtime, 1001);
    {
      VisualSearchController first;
      first.setIndexPath(managed_index); first.setUseIndex(true);
      first.search("blue", one_asset, managed_model(first_runtime));
      if (!wait_until([&] { return !first.running(); }) || first.newDescriptions() != 1)
        return fail("managed first search must generate its description");
    }
    publish(second_runtime, 1002);
    VisualSearchController second;
    second.setIndexPath(managed_index); second.setUseIndex(true);
    second.search("blue after managed restart", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return !second.running(); }) || second.reusedDescriptions() != 1 ||
        second.newDescriptions() != 0 || second_runtime.descriptions != 0 || second.inspected() != 1)
      return fail("verified managed restart on a new port must reuse the same saved image description");
    const auto before_unverified = second_runtime.requests;
    publish(second_runtime, 1002, false);
    second.search("unverified", one_asset, managed_model(second_runtime));
    if (second.running() || second_runtime.requests != before_unverified || second.inspected() != 0)
      return fail("managed search must reject an unverified model before any request");
    publish(second_runtime, 1002);
    second_runtime.digest = "different-artifact";
    const auto before_changed_digest = second_runtime.chats;
    second.search("changed artifact", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return !second.running(); }) || second.inspected() != 0 ||
        second_runtime.chats != before_changed_digest)
      return fail("managed search must reject a server digest that differs from the verified artifact pin");
    second_runtime.digest = first_runtime.digest;
    second_runtime.delay = 50;
    const auto before_restart = second_runtime.rankings;
    second.search("runtime replacement", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return second_runtime.rankings > before_restart; }))
      return fail("managed ranking should reach the delayed response");
    publish(second_runtime, 1003);
    if (!wait_until([&] { return !second.running(); }) || second.inspected() != 0 || !second.results().isEmpty())
      return fail("a replaced managed runtime must not publish the previous process response");
    second_runtime.delay = 0;
    VisualSearchController cached_direct;
    cached_direct.search("cached direct score", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return !cached_direct.running(); }) || cached_direct.inspected() != 1)
      return fail("direct managed search should establish a session score cache");
    const auto before_cached = second_runtime.chats;
    bool invalidated = false;
    QObject::connect(&cached_direct, &VisualSearchController::changed, &cached_direct, [&] {
      if (!invalidated && cached_direct.status().startsWith("Looking at photo")) {
        invalidated = true;
        publish(second_runtime, 1003, false);
      }
    });
    cached_direct.search("cached direct score", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return !cached_direct.running(); }) || !invalidated ||
        cached_direct.inspected() != 0 || second_runtime.chats != before_cached)
      return fail("verification lost during preparation must not publish a cached direct score");
    publish(second_runtime, 1003);
    auto different_store = photo_editor::desktop::managed_agent_runtime_snapshot();
    const auto other_store = temporary.filePath("other-managed-models");
    QDir().mkpath(other_store);
    different_store.insert("modelsPath", other_store);
    photo_editor::desktop::publish_managed_agent_runtime(different_store);
    second.search("different private store", one_asset, managed_model(second_runtime));
    if (!wait_until([&] { return !second.running(); }) || second.newDescriptions() != 1 || second.reusedDescriptions() != 0)
      return fail("managed descriptions must remain isolated across private model stores");
    photo_editor::desktop::publish_managed_agent_runtime({});
    VisualSearchController external;
    external.setIndexPath(temporary.filePath("external-endpoint-index.sqlite3")); external.setUseIndex(true);
    external.search("external first", one_asset, first_runtime.model());
    if (!wait_until([&] { return !external.running(); }) || external.newDescriptions() != 1)
      return fail("external endpoint should establish its own description");
    external.search("external second", one_asset, second_runtime.model());
    if (!wait_until([&] { return !external.running(); }) || external.newDescriptions() != 1 || external.reusedDescriptions() != 0)
      return fail("different external endpoints must not share saved descriptions");
    std::cout << "managed restart reuse, private-store isolation, verified artifact and stale runtime/cache guards passed\n";
  }
  VisualSearchController restarted;
  restarted.setIndexPath(index_path);
  restarted.setUseIndex(true);
  restarted.search("blue", assets, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.reusedDescriptions() != 3 ||
      restarted.newDescriptions() != 0 || server.descriptions != saved_descriptions ||
      restarted.resultIds() != QStringList{"0", "1"}) return fail("new query after restart must reuse descriptions and retain stable tie order");
  const auto permissions = QFile::permissions(index_path);
  if (permissions.testFlag(QFile::ReadGroup) || permissions.testFlag(QFile::ReadOther) ||
      permissions.testFlag(QFile::WriteGroup) || permissions.testFlag(QFile::WriteOther))
    return fail("saved image descriptions must be owner-private");
  restarted.search("red excluded by current view", {assets.first()}, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || !restarted.results().isEmpty() || restarted.total() != 1)
    return fail("persistent index must not resurrect assets outside the supplied catalog view");
  server.bad_ranking = true;
  restarted.search("red invalid rank", assets, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.inspected() != 0 ||
      !restarted.results().isEmpty() || !restarted.hasMore()) return fail("invalid batch IDs must fail atomically and remain retryable");
  server.bad_ranking = false;
  restarted.searchMore();
  if (!wait_until([&] { return !restarted.running(); }) || restarted.inspected() != 3 ||
      server.descriptions != saved_descriptions) return fail("ranking retry must not re-inspect images");
  server.cloud = true;
  const auto before_cloud_ranking = server.rankings;
  restarted.search("blue cloud", assets, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || server.rankings != before_cloud_ranking)
    return fail("saved descriptions must not be sent to cloud-backed models");
  server.cloud = false;
  server.digest = "new-index-model";
  restarted.search("red model change", assets, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.newDescriptions() != 3)
    return fail("index must invalidate descriptions on model revision");
  restarted.search("red bounded", large, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.inspected() != 32 || !restarted.hasMore())
    return fail("indexed search batch must remain bounded");
  restarted.searchMore();
  if (!wait_until([&] { return !restarted.running(); }) || restarted.inspected() != 35 || restarted.hasMore())
    return fail("indexed continuation should finish remaining candidates");
  replacement.fill(Qt::red);
  if (!replacement.save(assets.first().toMap().value("filePath").toString())) return fail("index replacement save failed");
  restarted.search("red changed preview", {assets.first()}, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.newDescriptions() != 1 || restarted.results().size() != 1)
    return fail("changed preview content must invalidate its saved description");
  server.delay = 100;
  const auto before_cancel_rank = server.rankings;
  restarted.search("red cancelled rank", assets, server.model());
  if (!wait_until([&] { return server.rankings > before_cancel_rank; })) return fail("indexed ranking never started");
  restarted.cancel();
  restarted.search("blue replacement rank", {assets.at(1)}, server.model());
  if (!wait_until([&] { return !restarted.running(); }) || restarted.inspected() != 1 || restarted.resultIds() != QStringList{"1"})
    return fail("cancelled indexed ranking must not contaminate the next search");
  server.delay = 0;
  const auto key = QString(64, QLatin1Char('a'));
  using photo_editor::desktop::VisualSearchIndex;
  if (!VisualSearchIndex::save(index_path, key, "Private description") ||
      VisualSearchIndex::lookup(index_path, key) != "Private description" ||
      VisualSearchIndex::save(index_path, "../bad", "bad") ||
      !VisualSearchIndex::lookup(index_path, "../bad").isEmpty()) return fail("persistent index contract failed");
  VisualSearchController unavailable;
  unavailable.setIndexPath(temporary.path()); // A directory cannot be a SQLite database.
  unavailable.setUseIndex(true);
  unavailable.search("red storage failure", {assets.last()}, server.model());
  if (!wait_until([&] { return !unavailable.running(); }) || unavailable.inspected() != 1 || unavailable.indexWarning().isEmpty())
    return fail("index write failure must preserve search and expose nonpersistent status");
  if (!server.protocol_ok) return fail("indexed ranking leaked image payload or metadata");
  restarted.forgetIndex();
  if (!wait_until([&] { return !restarted.running(); }) || !VisualSearchIndex::lookup(index_path, key).isEmpty())
    return fail("forget index must remove persisted descriptions");
  std::cout << "visual search protocol, privacy, caching, cancellation, ranking and batching passed\n";
  return EXIT_SUCCESS;
}
