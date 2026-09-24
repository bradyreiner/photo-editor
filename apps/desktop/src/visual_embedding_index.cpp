#include "visual_embedding_index.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <QtEndian>

#include <sqlite3.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <queue>

namespace photo_editor::desktop {
namespace {
using Database = std::unique_ptr<sqlite3, decltype(&sqlite3_close)>;
using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

bool key_valid(const QString& key) {
  if (key.size() != 64) return false;
  for (const auto c : key) if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) return false;
  return true;
}
bool space_valid(const VisualEmbeddingSpace& space) {
  return key_valid(space.key) && space.dimensions > 0 && space.dimensions <= 4096;
}
bool candidate_valid(const VisualEmbeddingCandidate& candidate) {
  return key_valid(candidate.asset_key) && key_valid(candidate.content_key);
}
Database open(const QString& path, bool write) {
  const QFileInfo info(path);
  if (path.isEmpty() || !info.isAbsolute() || info.isSymLink() ||
      (write && !QDir().mkpath(info.absolutePath()))) return {nullptr, sqlite3_close};
  sqlite3* raw = nullptr;
  if (sqlite3_open_v2(QFile::encodeName(path).constData(), &raw,
      write ? SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE : SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    if (raw) sqlite3_close(raw);
    return {nullptr, sqlite3_close};
  }
  Database db(raw, sqlite3_close);
  sqlite3_busy_timeout(raw, 1000);
  if (write && (!QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner) ||
      sqlite3_exec(raw,
          "CREATE TABLE IF NOT EXISTS visual_embeddings_v1("
          "space TEXT NOT NULL, asset TEXT NOT NULL, content TEXT NOT NULL,"
          "dimensions INTEGER NOT NULL, ticket TEXT NOT NULL, vector BLOB,"
          "PRIMARY KEY(space,asset)) WITHOUT ROWID", nullptr, nullptr, nullptr) != SQLITE_OK))
    return {nullptr, sqlite3_close};
  return db;
}
Statement prepare(sqlite3* db, const char* sql) {
  sqlite3_stmt* raw = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
    if (raw) sqlite3_finalize(raw);
    return {nullptr, sqlite3_finalize};
  }
  return {raw, sqlite3_finalize};
}
bool bind(sqlite3_stmt* statement, int index, const QString& value) {
  const auto bytes = value.toUtf8();
  return sqlite3_bind_text(statement, index, bytes.constData(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT) == SQLITE_OK;
}
QString column(sqlite3_stmt* statement, int index) {
  return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(statement, index)), sqlite3_column_bytes(statement, index));
}
QVector<float> normalized(const QVector<float>& input, int dimensions) {
  if (input.size() != dimensions) return {};
  double squared = 0;
  for (const auto value : input) {
    if (!std::isfinite(value)) return {};
    squared += static_cast<double>(value) * value;
  }
  if (!std::isfinite(squared) || squared <= 0) return {};
  const auto magnitude = std::sqrt(squared);
  QVector<float> result;
  result.reserve(dimensions);
  for (const auto value : input) result.append(static_cast<float>(value / magnitude));
  return result;
}
QByteArray encode(const QVector<float>& values) {
  QByteArray bytes(values.size() * 4, Qt::Uninitialized);
  for (qsizetype i = 0; i < values.size(); ++i)
    qToLittleEndian<quint32>(std::bit_cast<quint32>(values.at(i)), bytes.data() + i * 4);
  return bytes;
}
VisualEmbeddingWrite failure(const QString& message) { return {false, false, message}; }
}  // namespace

VisualEmbeddingTicket VisualEmbeddingIndex::stage(const QString& path, const VisualEmbeddingSpace& space,
                                                  const VisualEmbeddingCandidate& candidate) {
  if (!space_valid(space) || !candidate_valid(candidate)) return {{}, false, QStringLiteral("Invalid embedding identity.")};
  auto db = open(path, true);
  if (!db || sqlite3_exec(db.get(), "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK)
    return {{}, false, QStringLiteral("Cannot open or lock the embedding index.")};
  auto existing = prepare(db.get(), "SELECT content,dimensions,ticket,vector IS NOT NULL FROM visual_embeddings_v1 WHERE space=?1 AND asset=?2");
  if (!existing || !bind(existing.get(), 1, space.key) || !bind(existing.get(), 2, candidate.asset_key))
    return {{}, false, QStringLiteral("Cannot read the embedding identity.")};
  const auto state = sqlite3_step(existing.get());
  if (state != SQLITE_ROW && state != SQLITE_DONE) return {{}, false, QStringLiteral("Cannot read the embedding identity.")};
  if (state == SQLITE_ROW && column(existing.get(), 0) == candidate.content_key &&
      sqlite3_column_int(existing.get(), 1) == space.dimensions) {
    VisualEmbeddingTicket ticket{column(existing.get(), 2), sqlite3_column_int(existing.get(), 3) != 0, {}};
    existing.reset();
    if (sqlite3_exec(db.get(), "COMMIT", nullptr, nullptr, nullptr) == SQLITE_OK) return ticket;
    return {{}, false, QStringLiteral("Cannot finish reading the embedding identity.")};
  }
  existing.reset();
  const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  auto insert = prepare(db.get(), "INSERT INTO visual_embeddings_v1(space,asset,content,dimensions,ticket,vector) VALUES(?1,?2,?3,?4,?5,NULL) "
      "ON CONFLICT(space,asset) DO UPDATE SET content=excluded.content,dimensions=excluded.dimensions,ticket=excluded.ticket,vector=NULL");
  if (!insert || !bind(insert.get(), 1, space.key) || !bind(insert.get(), 2, candidate.asset_key) ||
      !bind(insert.get(), 3, candidate.content_key) || sqlite3_bind_int(insert.get(), 4, space.dimensions) != SQLITE_OK ||
      !bind(insert.get(), 5, token) || sqlite3_step(insert.get()) != SQLITE_DONE ||
      sqlite3_exec(db.get(), "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK)
    return {{}, false, QStringLiteral("Cannot stage the embedding revision.")};
  return {token, false, {}};
}

VisualEmbeddingWrite VisualEmbeddingIndex::commit(const QString& path, const VisualEmbeddingSpace& space,
    const VisualEmbeddingCandidate& candidate, const QString& ticket, const QVector<float>& embedding) {
  if (!space_valid(space) || !candidate_valid(candidate) || QUuid(ticket).isNull())
    return failure(QStringLiteral("Invalid embedding identity or ticket."));
  const auto values = normalized(embedding, space.dimensions);
  if (values.isEmpty()) return failure(QStringLiteral("Embedding must be finite, nonzero and dimensionally correct."));
  auto db = open(path, true);
  if (!db) return failure(QStringLiteral("Cannot open the embedding index."));
  // UPDATE only: a late worker must never recreate a removed row. First commit
  // wins for one ticket; duplicate workers cannot overwrite an accepted vector.
  auto update = prepare(db.get(), "UPDATE visual_embeddings_v1 SET vector=?1 WHERE space=?2 AND asset=?3 AND content=?4 AND dimensions=?5 AND ticket=?6 AND vector IS NULL");
  const auto bytes = encode(values);
  if (!update || sqlite3_bind_blob(update.get(), 1, bytes.constData(), static_cast<int>(bytes.size()), SQLITE_TRANSIENT) != SQLITE_OK ||
      !bind(update.get(), 2, space.key) || !bind(update.get(), 3, candidate.asset_key) || !bind(update.get(), 4, candidate.content_key) ||
      sqlite3_bind_int(update.get(), 5, space.dimensions) != SQLITE_OK || !bind(update.get(), 6, ticket) || sqlite3_step(update.get()) != SQLITE_DONE)
    return failure(QStringLiteral("Cannot save the embedding."));
  return {sqlite3_changes(db.get()) == 1, sqlite3_changes(db.get()) == 0, {}};
}

VisualEmbeddingWrite VisualEmbeddingIndex::erase(const QString& path, const VisualEmbeddingSpace& space, const QString& asset_key) {
  if (!space_valid(space) || !key_valid(asset_key)) return failure(QStringLiteral("Invalid embedding identity."));
  auto db = open(path, true);
  if (!db) return failure(QStringLiteral("Cannot open the embedding index."));
  auto remove = prepare(db.get(), "DELETE FROM visual_embeddings_v1 WHERE space=?1 AND asset=?2");
  if (!remove || !bind(remove.get(), 1, space.key) || !bind(remove.get(), 2, asset_key) || sqlite3_step(remove.get()) != SQLITE_DONE)
    return failure(QStringLiteral("Cannot remove the embedding."));
  return {true, false, {}};
}

VisualEmbeddingResult VisualEmbeddingIndex::search(const QString& path, const VisualEmbeddingSpace& space,
    const QVector<VisualEmbeddingCandidate>& scope, const QVector<float>& query, int limit, const std::atomic_bool* cancel) {
  VisualEmbeddingResult result;
  const auto fail = [&result](const QString& error) {
    result.matches.clear(); result.ready = 0; result.missing = 0; result.error = error; return result;
  };
  if (!space_valid(space) || scope.size() > 100000 || limit < 1 || limit > 1000)
    return fail(QStringLiteral("Invalid embedding search bounds or identity."));
  const auto values = normalized(query, space.dimensions);
  if (values.isEmpty()) return fail(QStringLiteral("Invalid query embedding."));
  QSet<QString> seen;
  for (const auto& candidate : scope) {
    if (cancel && cancel->load()) { result.cancelled = true; return result; }
    if (!candidate_valid(candidate) || seen.contains(candidate.asset_key)) return fail(QStringLiteral("Invalid or duplicate search candidate."));
    seen.insert(candidate.asset_key);
  }
  if (cancel && cancel->load()) { result.cancelled = true; return result; }
  if (scope.isEmpty()) return result;
  // A missing file is an unindexed scope; an existing unreadable/corrupt file is
  // an error. Do not create a database from a read-only search.
  if (path.isEmpty() || !QFileInfo(path).isAbsolute() || QFileInfo(path).isSymLink())
    return fail(QStringLiteral("Invalid embedding index path."));
  if (!QFileInfo::exists(path)) { result.missing = static_cast<int>(scope.size()); return result; }
  auto db = open(path, false);
  if (!db || sqlite3_exec(db.get(), "BEGIN", nullptr, nullptr, nullptr) != SQLITE_OK)
    return fail(QStringLiteral("Cannot read the embedding index."));
  auto lookup = prepare(db.get(), "SELECT vector FROM visual_embeddings_v1 WHERE space=?1 AND asset=?2 AND content=?3 AND dimensions=?4 AND vector IS NOT NULL");
  if (!lookup) return fail(QStringLiteral("Cannot read the embedding table."));
  struct Ranked { int ordinal; double score; };
  const auto better = [](const Ranked& a, const Ranked& b) { return a.score != b.score ? a.score > b.score : a.ordinal < b.ordinal; };
  std::priority_queue<Ranked, std::vector<Ranked>, decltype(better)> best(better);
  for (qsizetype i = 0; i < scope.size(); ++i) {
    if (cancel && cancel->load()) { result = {}; result.cancelled = true; return result; }
    sqlite3_reset(lookup.get());
    if (!bind(lookup.get(), 1, space.key) || !bind(lookup.get(), 2, scope.at(i).asset_key) || !bind(lookup.get(), 3, scope.at(i).content_key) ||
        sqlite3_bind_int(lookup.get(), 4, space.dimensions) != SQLITE_OK)
      return fail(QStringLiteral("Cannot bind the embedding lookup."));
    const auto status = sqlite3_step(lookup.get());
    if (status == SQLITE_DONE) { ++result.missing; continue; }
    if (status != SQLITE_ROW || sqlite3_column_type(lookup.get(), 0) != SQLITE_BLOB ||
        sqlite3_column_bytes(lookup.get(), 0) != space.dimensions * 4)
      return fail(QStringLiteral("Unreadable or corrupt embedding vector."));
    const auto* bytes = static_cast<const char*>(sqlite3_column_blob(lookup.get(), 0));
    double score = 0, norm = 0;
    for (int j = 0; j < space.dimensions; ++j) {
      const auto value = std::bit_cast<float>(qFromLittleEndian<quint32>(bytes + j * 4));
      if (!std::isfinite(value)) return fail(QStringLiteral("Nonfinite stored embedding vector."));
      norm += static_cast<double>(value) * value;
      score += static_cast<double>(value) * values.at(j);
    }
    if (std::abs(norm - 1.0) > 0.0001) return fail(QStringLiteral("Invalid stored embedding normalization."));
    ++result.ready;
    const Ranked ranked{static_cast<int>(i), std::clamp(score, -1.0, 1.0)};
    if (best.size() < static_cast<std::size_t>(limit)) best.push(ranked);
    else if (better(ranked, best.top())) { best.pop(); best.push(ranked); }
  }
  QVector<Ranked> ordered;
  while (!best.empty()) { ordered.append(best.top()); best.pop(); }
  std::sort(ordered.begin(), ordered.end(), better);
  for (const auto& ranked : ordered) result.matches.append({scope.at(ranked.ordinal).asset_key, ranked.score});
  return result;
}

}  // namespace photo_editor::desktop
