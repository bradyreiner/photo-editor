#include "visual_embedding_index.h"

#include <QCoreApplication>
#include <QFile>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>

#include <sqlite3.h>

#include <cmath>
#include <algorithm>
#include <bit>
#include <chrono>
#include <future>
#include <iostream>
#include <limits>
#include <thread>

using namespace photo_editor::desktop;
namespace {
QString key(char value) { return QString(64, QLatin1Char(value)); }
int fail(const char* message) { std::cerr << message << '\n'; return 1; }

int real_vectors(const QString& index_path, const QString& receipt_path) {
  QFile file(receipt_path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > 10 * 1024 * 1024) return fail("invalid real-vector receipt file");
  const auto receipt = QJsonDocument::fromJson(file.readAll()).object();
  if (receipt.value("schema") != "real-visual-embeddings-v1" || !receipt.value("artifactVerified").toBool() ||
      !receipt.value("pythonNetworkDisabled").toBool()) return fail("real encoder proof receipt missing");
  const auto images = receipt.value("images").toArray(), queries = receipt.value("queries").toArray();
  if (images.isEmpty() || images.size() > 64 || queries.isEmpty() || queries.size() > 64) return fail("invalid real-vector scope");
  const VisualEmbeddingSpace space{receipt.value("spaceKey").toString(), receipt.value("dimensions").toInt()};
  const auto vector = [](const QJsonValue& input) {
    QVector<float> result;
    if (!input.isArray()) return result;
    for (const auto& value : input.toArray()) {
      if (!value.isDouble() || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > std::numeric_limits<float>::max()) return QVector<float>{};
      result.append(static_cast<float>(value.toDouble()));
    }
    return result;
  };
  QVector<VisualEmbeddingCandidate> scope;
  QHash<QString, QString> asset_ids;
  for (const auto& entry : images) {
    const auto image = entry.toObject();
    const VisualEmbeddingCandidate candidate{image.value("assetKey").toString(), image.value("contentKey").toString()};
    if (asset_ids.contains(candidate.asset_key)) return fail("duplicate real-vector identity");
    asset_ids.insert(candidate.asset_key, image.value("id").toString());
    const auto ticket = VisualEmbeddingIndex::stage(index_path, space, candidate);
    if (!ticket.error.isEmpty() || !VisualEmbeddingIndex::commit(index_path, space, candidate, ticket.token, vector(image.value("vector"))).ok)
      return fail("native store rejected actual image vector");
    scope.append(candidate);
  }
  QJsonArray results;
  bool all_expected = true;
  for (const auto& entry : queries) {
    const auto query = entry.toObject();
    const auto expected = query.value("expectedAssetKeys").toArray();
    QElapsedTimer timer; timer.start();
    const auto found = VisualEmbeddingIndex::search(index_path, space, scope, vector(query.value("vector")), static_cast<int>(scope.size()));
    const auto elapsed = timer.nsecsElapsed();
    if (!found.error.isEmpty() || found.ready != scope.size() || found.matches.size() != scope.size()) return fail("native real-image retrieval incomplete");
    const auto reference = query.value("pythonRanking").toArray();
    if (reference.size() != found.matches.size()) return fail("encoder ranking reference missing");
    QJsonArray ranking;
    for (qsizetype i = 0; i < found.matches.size(); ++i) {
      const auto& match = found.matches.at(i);
      const auto id = asset_ids.value(match.asset_key);
      if (id != reference.at(i).toObject().value("id").toString() ||
          std::abs(match.similarity - reference.at(i).toObject().value("similarity").toDouble()) > 0.00001)
        return fail("native persisted ranking differs from actual encoder dot products");
      ranking.append(QJsonObject{{"id", id}, {"similarity", match.similarity}});
    }
    const bool expected_first = expected.contains(found.matches.first().asset_key);
    all_expected = all_expected && expected_first;
    results.append(QJsonObject{{"query", query.value("text")}, {"ranking", ranking},
        {"expectedFirst", expected_first}, {"nativeRetrievalMicroseconds", static_cast<double>(elapsed) / 1000.0}});
  }
  std::cout << QJsonDocument(QJsonObject{{"scope", "local-fixture-offline-encoder-to-native-store-smoke-not-corpus-quality"},
      {"actualImageVectors", images.size()}, {"dimensions", space.dimensions}, {"allExpectedFirst", all_expected},
      {"nativeEncoderRankingParity", true}, {"queries", results}}).toJson().constData();
  return all_expected ? 0 : fail("real-photo query did not rank an expected image first");
}

int benchmark(const QString& path) {
  constexpr int count = 20000, dimensions = 768, limit = 32;
  const VisualEmbeddingSpace space{key('a'), dimensions};
  const VisualEmbeddingCandidate first{key('1'), key('c')};
  if (VisualEmbeddingIndex::stage(path, space, first).token.isEmpty()) return fail("benchmark schema creation failed");
  sqlite3* db = nullptr;
  if (sqlite3_open(QFile::encodeName(path).constData(), &db) != SQLITE_OK) return fail("benchmark open failed");
  if (sqlite3_exec(db, "BEGIN IMMEDIATE; DELETE FROM visual_embeddings_v1", nullptr, nullptr, nullptr) != SQLITE_OK) {
    sqlite3_close(db); return fail("benchmark seed transaction failed");
  }
  sqlite3_stmt* insert = nullptr;
  if (sqlite3_prepare_v2(db, "INSERT INTO visual_embeddings_v1 VALUES(?1,?2,?3,768,'benchmark',?4)", -1, &insert, nullptr) != SQLITE_OK) {
    sqlite3_close(db); return fail("benchmark insert prepare failed");
  }
  QVector<VisualEmbeddingCandidate> scope;
  QVector<VisualEmbeddingMatch> oracle;
  QVector<float> query(dimensions, 0);
  query[0] = 1;
  const auto space_bytes = space.key.toUtf8(), content_bytes = first.content_key.toUtf8();
  bool seeded = true;
  for (int i = 0; i < count; ++i) {
    const auto asset = QString::number(i + 1, 16).rightJustified(64, QLatin1Char('0'));
    QVector<float> vector;
    double squared = 0;
    quint32 state = static_cast<quint32>(i) + 1701;
    for (int j = 0; j < dimensions; ++j) {
      state ^= state << 13; state ^= state >> 17; state ^= state << 5;
      const float value = static_cast<float>(state & 65535U) / 32768.0F - 1.0F;
      squared += static_cast<double>(value) * value;
      vector.append(value);
    }
    const auto magnitude = std::sqrt(squared);
    QByteArray bytes(dimensions * 4, Qt::Uninitialized);
    for (int j = 0; j < dimensions; ++j) {
      vector[j] = static_cast<float>(vector.at(j) / magnitude);
      qToLittleEndian<quint32>(std::bit_cast<quint32>(vector.at(j)), bytes.data() + j * 4);
    }
    scope.append({asset, first.content_key});
    oracle.append({asset, vector.first()});
    const auto asset_bytes = asset.toUtf8();
    sqlite3_reset(insert);
    seeded = sqlite3_bind_text(insert, 1, space_bytes.constData(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_text(insert, 2, asset_bytes.constData(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_text(insert, 3, content_bytes.constData(), -1, SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_bind_blob(insert, 4, bytes.constData(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
        sqlite3_step(insert) == SQLITE_DONE;
    if (!seeded) break;
  }
  sqlite3_finalize(insert);
  const bool committed = seeded && sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK;
  sqlite3_close(db);
  if (!committed) return fail("benchmark vector seeding failed");
  std::stable_sort(oracle.begin(), oracle.end(), [](const auto& a, const auto& b) { return a.similarity > b.similarity; });
  QJsonArray durations;
  for (int run = 0; run < 6; ++run) {
    QElapsedTimer timer;
    timer.start();
    const auto result = VisualEmbeddingIndex::search(path, space, scope, query, limit);
    durations.append(timer.elapsed());
    if (!result.error.isEmpty() || result.ready != count || result.missing != 0 || result.matches.size() != limit)
      return fail("benchmark retrieval incomplete");
    for (int i = 0; i < limit; ++i)
      if (result.matches.at(i).asset_key != oracle.at(i).asset_key || std::abs(result.matches.at(i).similarity - oracle.at(i).similarity) > 0.000001)
        return fail("bounded top-K disagrees with independently full-sorted oracle");
  }
  std::atomic_bool cancel{false}, started{false};
  auto running = std::async(std::launch::async, [&] {
    started.store(true);
    return VisualEmbeddingIndex::search(path, space, scope, query, limit, &cancel);
  });
  while (!started.load()) std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  QElapsedTimer cancel_timer;
  cancel_timer.start();
  cancel.store(true);
  const auto cancelled = running.get();
  if (!cancelled.cancelled || !cancelled.matches.isEmpty() || cancelled.ready != 0)
    return fail("running retrieval published a partial or uncancelled result");
  std::cout << QJsonDocument(QJsonObject{{"syntheticVectors", true}, {"encoderExercised", false},
      {"count", count}, {"dimensions", dimensions}, {"topK", limit},
      {"retrievalMilliseconds", durations}, {"cancellationMilliseconds", cancel_timer.elapsed()},
      {"fullSortOracleMatched", true}}).toJson().constData();
  return 0;
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  if (!temporary.isValid()) return fail("temporary directory failed");
  const auto path = temporary.filePath("vectors.sqlite3");
  const auto real_option = app.arguments().indexOf("--real-vectors");
  if (real_option >= 0) {
    if (real_option + 1 >= app.arguments().size()) return fail("--real-vectors requires a receipt path");
    return real_vectors(path, app.arguments().at(real_option + 1));
  }
  if (app.arguments().contains("--benchmark")) return benchmark(path);
  const VisualEmbeddingSpace space{key('a'), 3}, alternate{key('b'), 3};
  const VisualEmbeddingCandidate first{key('1'), key('c')}, second{key('2'), key('d')}, third{key('3'), key('e')};
  const QVector<VisualEmbeddingCandidate> scope{first, second, third};
  const QVector<float> query{1, 0, 0};
  auto result = VisualEmbeddingIndex::search(path, space, scope, query, 2);
  if (!result.error.isEmpty() || result.missing != 3 || QFile::exists(path)) return fail("missing read must not create index");
  auto ticket = VisualEmbeddingIndex::stage(path, space, first);
  if (ticket.token.isEmpty() || !ticket.error.isEmpty() || ticket.ready) return fail("initial stage failed");
  if (!VisualEmbeddingIndex::commit(path, space, first, ticket.token, {3, 0, 0}).ok) return fail("normalized commit failed");
  const auto reused = VisualEmbeddingIndex::stage(path, space, first);
  if (!reused.ready || reused.token != ticket.token) return fail("same revision must reuse ready vector");
  if (!VisualEmbeddingIndex::commit(path, space, first, ticket.token, {-1, 0, 0}).stale) return fail("duplicate worker overwrote accepted vector");
  for (const auto& candidate : {second, third}) {
    const auto next = VisualEmbeddingIndex::stage(path, space, candidate);
    if (!VisualEmbeddingIndex::commit(path, space, candidate, next.token, candidate.asset_key == second.asset_key ? QVector<float>{2, 0, 0} : QVector<float>{-1, 0, 0}).ok)
      return fail("additional vector save failed");
  }
  result = VisualEmbeddingIndex::search(path, space, scope, query, 2);
  if (!result.error.isEmpty() || result.ready != 3 || result.missing != 0 || result.matches.size() != 2 ||
      result.matches.at(0).asset_key != first.asset_key || result.matches.at(1).asset_key != second.asset_key ||
      std::abs(result.matches.first().similarity - 1.0) > 0.00001) return fail("top-K/tie/normalization failed");
  result = VisualEmbeddingIndex::search(path, space, {third}, query, 5);
  if (result.matches.size() != 1 || result.matches.first().asset_key != third.asset_key || result.matches.first().similarity != -1)
    return fail("retrieval broadened scope or imposed a hidden confidence threshold");
  result = VisualEmbeddingIndex::search(path, alternate, scope, query, 2);
  if (!result.matches.isEmpty() || result.missing != 3) return fail("model/catalog space isolation failed");
  const VisualEmbeddingCandidate edited{first.asset_key, key('f')};
  result = VisualEmbeddingIndex::search(path, space, {edited}, query, 2);
  if (!result.matches.isEmpty() || result.missing != 1) return fail("stale recipe/content was returned before staging caught up");
  const auto edit_ticket = VisualEmbeddingIndex::stage(path, space, edited);
  if (edit_ticket.ready || edit_ticket.token == ticket.token) return fail("edit did not invalidate old embedding");
  if (!VisualEmbeddingIndex::commit(path, space, first, ticket.token, query).stale) return fail("late original worker restored an edited row");
  if (!VisualEmbeddingIndex::erase(path, space, first.asset_key).ok ||
      !VisualEmbeddingIndex::commit(path, space, edited, edit_ticket.token, query).stale) return fail("late worker resurrected deleted asset");
  const auto recreated = VisualEmbeddingIndex::stage(path, space, edited);
  if (recreated.token == edit_ticket.token || !VisualEmbeddingIndex::commit(path, space, edited, edit_ticket.token, query).stale)
    return fail("delete/recreate reused old inference authority");
  // Race two workers for exactly one current ticket. SQLite CAS must admit one.
  auto worker = [&] { return VisualEmbeddingIndex::commit(path, space, edited, recreated.token, query); };
  auto future = std::async(std::launch::async, worker);
  const auto a = worker(), b = future.get();
  if (static_cast<int>(a.ok) + static_cast<int>(b.ok) != 1 || static_cast<int>(a.stale) + static_cast<int>(b.stale) != 1)
    return fail("competing workers did not yield exactly one committed embedding");
  for (const QVector<float>& invalid : {QVector<float>{0, 0, 0}, QVector<float>{1, 0},
      QVector<float>{std::numeric_limits<float>::quiet_NaN(), 0, 0}, QVector<float>{std::numeric_limits<float>::infinity(), 0, 0}}) {
    if (VisualEmbeddingIndex::commit(path, space, edited, recreated.token, invalid).error.isEmpty() ||
        VisualEmbeddingIndex::search(path, space, scope, invalid, 2).error.isEmpty()) return fail("invalid vectors accepted");
  }
  if (VisualEmbeddingIndex::search(path, space, {second, second}, query, 2).error.isEmpty() ||
      VisualEmbeddingIndex::search(path, space, scope, query, 0).error.isEmpty() ||
      VisualEmbeddingIndex::stage(path, {"path-not-a-hash", 3}, first).error.isEmpty()) return fail("invalid bounds/identity accepted");
  std::atomic_bool cancel{true};
  result = VisualEmbeddingIndex::search(path, space, scope, query, 2, &cancel);
  if (!result.cancelled || !result.matches.isEmpty() || result.ready != 0 || result.missing != 0) return fail("cancel published partial results");
  const auto symlink = temporary.filePath("link.sqlite3");
  if (!QFile::link(path, symlink) || VisualEmbeddingIndex::stage(symlink, space, first).error.isEmpty() ||
      VisualEmbeddingIndex::search(symlink, space, scope, query, 2).error.isEmpty()) return fail("symlink index accepted");
  // A malformed persisted vector fails the whole query, not a plausible partial
  // ranking. Keep this corruption in the disposable test database only.
  sqlite3* raw = nullptr;
  if (sqlite3_open(QFile::encodeName(path).constData(), &raw) != SQLITE_OK) return fail("corruption fixture open failed");
  bool corruption_rejected = true;
  for (const auto& bad : {QStringLiteral("x'00000000'"), QStringLiteral("x'000000000000000000000000'"),
                         QStringLiteral("x'0000c07f0000000000000000'"), QStringLiteral("'aaaaaaaaaaaa'")}) {
    const auto sql = QStringLiteral("UPDATE visual_embeddings_v1 SET vector=%1 WHERE asset='%2'").arg(bad, second.asset_key).toUtf8();
    const auto status = sqlite3_exec(raw, sql.constData(), nullptr, nullptr, nullptr);
    // The first row is valid: the second must invalidate even that partial result.
    result = VisualEmbeddingIndex::search(path, space, {edited, second}, query, 2);
    corruption_rejected = corruption_rejected && status == SQLITE_OK && !result.error.isEmpty() && result.matches.isEmpty() && result.ready == 0;
  }
  sqlite3_close(raw);
  if (!corruption_rejected) return fail("corrupt vector admitted partial ranking");
  std::cout << "visual embedding index lifecycle, exact retrieval and stale-worker tests passed\n";
  return 0;
}
