#pragma once

#include <QString>
#include <QVector>

#include <atomic>

namespace photo_editor::desktop {

// This is derived retrieval data, never catalog authority. Keys are SHA-256 hex
// digests: space includes catalog identity, pinned encoder revision and exact
// preprocessing; asset identifies a catalog asset; content identifies its current
// rendered preview (including recipe). Text and image vectors must share a space.
struct VisualEmbeddingSpace {
  QString key;
  int dimensions = 0;
};
struct VisualEmbeddingCandidate {
  QString asset_key;
  QString content_key;
};
struct VisualEmbeddingTicket {
  QString token;
  bool ready = false;
  QString error;
};
struct VisualEmbeddingWrite {
  bool ok = false;
  bool stale = false;
  QString error;
};
struct VisualEmbeddingMatch {
  QString asset_key;
  double similarity = 0;  // Cosine similarity, NOT match confidence.
};
struct VisualEmbeddingResult {
  QVector<VisualEmbeddingMatch> matches;
  int ready = 0;
  int missing = 0;
  bool cancelled = false;
  QString error;
};

// Blocking worker API; each call owns its SQLite connection. The catalog-side
// coordinator must serialize stage/erase in authoritative change order. Only
// commit may run out of order. Its ticket prevents stale inference from restoring
// an edited/deleted/recreated entry. No encoder or network work happens here.
class VisualEmbeddingIndex final {
 public:
  static VisualEmbeddingTicket stage(const QString& path, const VisualEmbeddingSpace& space,
                                    const VisualEmbeddingCandidate& candidate);
  static VisualEmbeddingWrite commit(const QString& path, const VisualEmbeddingSpace& space,
                                    const VisualEmbeddingCandidate& candidate, const QString& ticket,
                                    const QVector<float>& embedding);
  static VisualEmbeddingWrite erase(const QString& path, const VisualEmbeddingSpace& space,
                                   const QString& asset_key);
  // Exact, bounded-memory top-K over ONLY the caller's current catalog scope.
  // A consistent read snapshot never mixes revisions. Missing/stale content is
  // reported separately, not silently counted as a nonmatch. No score threshold
  // is imposed: a verifier/calibrated retrieval policy must decide relevance.
  static VisualEmbeddingResult search(const QString& path, const VisualEmbeddingSpace& space,
                                      const QVector<VisualEmbeddingCandidate>& scope,
                                      const QVector<float>& query, int limit,
                                      const std::atomic_bool* cancel = nullptr);
};

}  // namespace photo_editor::desktop
