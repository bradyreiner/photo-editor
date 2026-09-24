#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>
#include <QVector>
#include <atomic>
#include <memory>

namespace photo_editor::desktop {

struct AgentModelArtifactFile {
  QString relative_path;
  QByteArray sha256;
  qint64 bytes = 0;
};

struct AgentModelArtifact {
  QString model;
  QString license;
  QString source;
  QVector<AgentModelArtifactFile> files;
  qint64 downloadBytes() const {
    qint64 total = 0;
    for (const auto& file : files) total += file.bytes;
    return total;
  }
};

// Single authoritative application pin, verified against the public Ollama
// registry on 2026-09-07. Includes the exact raw manifest, not only weight names.
inline AgentModelArtifact pinned_agent_model_artifact(const QString& model) {
  if (model != QStringLiteral("qwen3-vl:32b")) return {};
  return {model, QStringLiteral("Apache-2.0"),
      QStringLiteral("https://registry.ollama.ai/v2/library/qwen3-vl/manifests/32b"), {
      {QStringLiteral("manifests/registry.ollama.ai/library/qwen3-vl/32b"),
       "ff2e46876908853b8d0872595047c46d599d970056277e4864c8a1f0fd93fd3b", 895},
      {QStringLiteral("blobs/sha256-6e416d39200aae1cec3ea197c5a5ebbaf214ccddc9561bcc0ec7157c83b2a99b"),
       "6e416d39200aae1cec3ea197c5a5ebbaf214ccddc9561bcc0ec7157c83b2a99b", 20910274496LL},
      {QStringLiteral("blobs/sha256-50fcece1bf41295af164c935e4d43412440f4803ba6418f418b5b8672d0d956f"),
       "50fcece1bf41295af164c935e4d43412440f4803ba6418f418b5b8672d0d956f", 552},
      {QStringLiteral("blobs/sha256-7339fa418c9ad3e8e12e74ad0fd26a9cc4be8703f9c110728a992b193be85cb2"),
       "7339fa418c9ad3e8e12e74ad0fd26a9cc4be8703f9c110728a992b193be85cb2", 11355},
      {QStringLiteral("blobs/sha256-f6417cb1e26962991f8e875a93f3cb0f92bc9b4955e004881251ccbf934a19d2"),
       "f6417cb1e26962991f8e875a93f3cb0f92bc9b4955e004881251ccbf934a19d2", 42}}};
}

// Cheap invalidation key for an in-memory, full-hash verification result. Never
// persisted as a substitute for hashing: every new app session verifies again.
inline QString agent_model_artifact_fingerprint(const QString& root, const AgentModelArtifact& artifact) {
  const auto canonical_root = QFileInfo(root).canonicalFilePath();
  if (canonical_root.isEmpty() || artifact.files.isEmpty()) return {};
  QByteArray signature;
  const QRegularExpression digest_pattern(QStringLiteral("^[0-9a-f]{64}$"));
  for (const auto& file : artifact.files) {
    if (file.bytes <= 0 || !digest_pattern.match(QString::fromLatin1(file.sha256)).hasMatch() ||
        QFileInfo(file.relative_path).isAbsolute() ||
        QDir::cleanPath(file.relative_path) != file.relative_path ||
        file.relative_path.startsWith(QStringLiteral("../"))) return {};
    const auto path = QDir(canonical_root).filePath(file.relative_path);
    const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.canonicalFilePath() != path || info.size() != file.bytes) return {};
    signature += file.relative_path.toUtf8() + '\0' + file.sha256 + '\0' + QByteArray::number(info.size()) + '\0' +
        QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '\0' +
        QByteArray::number(info.fileTime(QFileDevice::FileMetadataChangeTime).toMSecsSinceEpoch()) + '\n';
  }
  return QString::fromLatin1(QCryptographicHash::hash(signature, QCryptographicHash::Sha256).toHex());
}

// Runs on a worker. Reads in bounded chunks, honors cancellation, never writes,
// deletes, repairs, or adopts another model store.
inline QVariantMap verify_agent_model_artifact(
    const QString& root, const AgentModelArtifact& artifact,
    const std::shared_ptr<std::atomic_bool>& canceled = {}) {
  QVariantMap result{{"ok", false}, {"model", artifact.model}, {"modelsPath", root}};
  const auto fingerprint = agent_model_artifact_fingerprint(root, artifact);
  result.insert(QStringLiteral("fingerprint"), fingerprint);
  if (fingerprint.isEmpty()) {
    result.insert(QStringLiteral("error"), QStringLiteral("Pinned model files are missing, incomplete, or outside the private store."));
    return result;
  }
  qint64 verified_bytes = 0;
  for (const auto& file : artifact.files) {
    QFile input(QDir(root).filePath(file.relative_path));
    if (!input.open(QIODevice::ReadOnly)) {
      result.insert(QStringLiteral("error"), QStringLiteral("Cannot read a pinned model file."));
      return result;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 remaining = file.bytes;
    while (remaining > 0) {
      if (canceled && canceled->load(std::memory_order_relaxed)) {
        result.insert(QStringLiteral("error"), QStringLiteral("Model verification canceled."));
        return result;
      }
      const auto chunk = input.read(qMin(remaining, 4LL * 1024LL * 1024LL));
      if (chunk.isEmpty() || input.error() != QFileDevice::NoError) {
        result.insert(QStringLiteral("error"), QStringLiteral("Unable to read the complete model file."));
        return result;
      }
      hash.addData(chunk);
      verified_bytes += chunk.size();
      remaining -= chunk.size();
    }
    if (input.size() != file.bytes || hash.result().toHex() != file.sha256) {
      result.insert(QStringLiteral("error"), QStringLiteral("Model SHA-256 does not match the application pin: %1").arg(file.relative_path));
      return result;
    }
  }
  if (fingerprint != agent_model_artifact_fingerprint(root, artifact)) {
    result.insert(QStringLiteral("error"), QStringLiteral("Model files changed during verification. Retry verification."));
    return result;
  }
  result.insert(QStringLiteral("ok"), true);
  result.insert(QStringLiteral("verifiedBytes"), verified_bytes);
  result.insert(QStringLiteral("verifiedFiles"), artifact.files.size());
  return result;
}

}  // namespace photo_editor::desktop
