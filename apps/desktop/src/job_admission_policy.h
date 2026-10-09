#pragma once

#include "job_store.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#if defined(Q_OS_UNIX)
#include <sys/stat.h>
#endif

namespace photo_editor::desktop {

enum class JobResourceLane { Exclusive, HeavyRender, LightIo };
struct JobResourceClaim {
  JobResourceLane lane = JobResourceLane::Exclusive;
  QByteArray specification;
  QStringList reads;
  QStringList writes;
  QStringList read_file_ids;
  QStringList write_file_ids;
  qint64 additional_memory_bytes = 0;
  int import_metadata_threads = 0;
};
struct JobAdmissionBudget {
  int maximum_jobs = 1;
  qint64 additional_memory_bytes = 0;
  bool temporarily_limited = false;
};

inline JobAdmissionBudget job_admission_budget(int processors, quint64 memory_bytes,
                                              bool pressure, bool foreground_active) {
  constexpr quint64 gib = 1024ULL * 1024 * 1024;
  if (processors < 4 || memory_bytes < 8 * gib) return {};
  const auto reserve = static_cast<qint64>(std::min(memory_bytes / 32, gib / 4));
  return {pressure || foreground_active ? 1 : 2, reserve, pressure || foreground_active};
}

inline QByteArray job_execution_specification(const PersistentJobRecord& job) {
  const QJsonObject inputs{{"kind", job.job_kind}, {"asset", job.asset_id}, {"media", job.media_type},
      {"source", job.source_path}, {"preview", job.edited_preview_source},
      {"preset", job.preset_name}, {"destination", job.destination_path}, {"output", job.output_path},
      {"import", job.job_kind == QStringLiteral("catalog_import") ? job.metadata_json : QJsonObject{}}};
  return QCryptographicHash::hash(QJsonDocument(inputs).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256);
}

// Resolve existing ancestors too: a not-yet-created output below a symlinked
// directory must reserve the same tree as its canonical spelling.
inline QString job_resource_path(QString path) {
  path = path.trimmed();
  if (path.isEmpty()) return {};
  const QUrl url(path);
  if (url.isValid() && url.isLocalFile()) path = url.toLocalFile();
  else if (!url.scheme().isEmpty() && url.scheme().size() != 1) return {};
  path = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  QStringList tail;
  auto ancestor = path;
  while (!QFileInfo::exists(ancestor)) {
    const QFileInfo info(ancestor);
    const auto parent = info.absolutePath();
    if (parent == ancestor) return {};
    tail.prepend(info.fileName());
    ancestor = parent;
  }
  auto canonical = QFileInfo(ancestor).canonicalFilePath();
  if (canonical.isEmpty()) return {};
  for (const auto& part : tail) canonical = QDir(canonical).filePath(part);
  return QDir::cleanPath(canonical);
}

inline bool job_paths_overlap(const QString& left, const QString& right) {
  // Folding is conservative on case-sensitive volumes and prevents aliases on
  // the usual macOS/Windows volumes. Tree reservations intentionally overlap files.
  const auto a = left.toCaseFolded().normalized(QString::NormalizationForm_C);
  const auto b = right.toCaseFolded().normalized(QString::NormalizationForm_C);
  return a == b || a.startsWith(b.endsWith('/') ? b : b + '/') ||
      b.startsWith(a.endsWith('/') ? a : a + '/');
}

inline QString job_file_identity(const QString& path) {
#if defined(Q_OS_UNIX)
  struct stat info{};
  if (::stat(QFile::encodeName(path).constData(), &info) == 0 && S_ISREG(info.st_mode))
    return QString::number(static_cast<qulonglong>(info.st_dev)) + ':' +
        QString::number(static_cast<qulonglong>(info.st_ino));
#else
  Q_UNUSED(path);
#endif
  return {};
}

inline bool job_claims_conflict(const JobResourceClaim& a, const JobResourceClaim& b) {
  for (const auto& id : a.write_file_ids)
    if (b.read_file_ids.contains(id) || b.write_file_ids.contains(id)) return true;
  for (const auto& id : b.write_file_ids) if (a.read_file_ids.contains(id)) return true;
  for (const auto& output : a.writes) {
    for (const auto& input : b.reads) if (job_paths_overlap(output, input)) return true;
    for (const auto& other : b.writes) if (job_paths_overlap(output, other)) return true;
  }
  for (const auto& output : b.writes)
    for (const auto& input : a.reads) if (job_paths_overlap(output, input)) return true;
  return false;
}

inline JobResourceLane potential_job_resource_lane(const PersistentJobRecord& job) {
  if (job.job_kind == QStringLiteral("timeline_render")) return JobResourceLane::HeavyRender;
  if (job.job_kind == QStringLiteral("asset_export")) {
    const auto media = job.media_type.trimmed().toLower();
    const bool photo = media == QStringLiteral("photo") || media == QStringLiteral("image");
    return photo && job.preset_name.trimmed().compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) != 0
        ? JobResourceLane::HeavyRender : JobResourceLane::LightIo;
  }
  if (job.job_kind == QStringLiteral("catalog_import")) {
    const auto policy = job.metadata_json.value("duplicatePolicy").toString().trimmed().toLower();
    const auto paths = job.metadata_json.value("selectedPaths").toArray();
    if (!paths.isEmpty() && paths.size() <= 256 &&
        (policy.isEmpty() || policy == "skip" || policy == "reuse-identical")) return JobResourceLane::LightIo;
  }
  return JobResourceLane::Exclusive;
}

inline bool job_order_less_than(const PersistentJobRecord& lhs, const PersistentJobRecord& rhs) {
  const auto left = lhs.metadata_json.value("queueOrder").toVariant().toLongLong();
  const auto right = rhs.metadata_json.value("queueOrder").toVariant().toLongLong();
  // Mixing pairwise explicit-order and timestamp comparisons can form a cycle.
  // Keep explicitly ordered jobs together, then order legacy jobs by timestamp.
  if ((left > 0) != (right > 0)) return left > 0;
  if (left > 0 && left != right) return left < right;
  const auto left_created = lhs.created_at.trimmed(), right_created = rhs.created_at.trimmed();
  if (left_created != right_created) return left_created < right_created;
  return lhs.job_id < rhs.job_id;
}

// Run during the background queue read. A conservative Exclusive claim keeps
// ordinary serial execution available when facts are missing or work is large.
inline JobResourceClaim inspect_job_resources(const PersistentJobRecord& job,
                                               const QVector<JobResourceClaim>& active = {}) {
  constexpr qint64 mib = 1024 * 1024;
  JobResourceClaim claim;
  claim.specification = job_execution_specification(job);
  const auto source = job_resource_path(job.source_path);
  if (source.isEmpty()) return claim;
  const auto add_read = [&](const QString& path) {
    claim.reads.push_back(path);
    const auto id = job_file_identity(path);
    if (!id.isEmpty()) claim.read_file_ids.push_back(id);
  };
  add_read(source);
  const auto add_write = [&](const QString& path) {
    const auto normalized = job_resource_path(path);
    if (normalized.isEmpty()) return false;
    claim.writes.push_back(normalized);
    const auto id = job_file_identity(normalized);
    if (!id.isEmpty()) claim.write_file_ids.push_back(id);
    return true;
  };
  if (job.job_kind == QStringLiteral("timeline_render")) {
    if (!add_write(job.output_path) || !add_write(job.source_path)) return claim;
    // Includes output sidecars and transient encoder files in the same folder.
    if (!add_write(QFileInfo(job_resource_path(job.output_path)).absolutePath())) return claim;
    claim.lane = JobResourceLane::HeavyRender;
  } else if (job.job_kind == QStringLiteral("asset_export")) {
    if (!add_write(job.destination_path)) return claim;
    if (!job.output_path.isEmpty() && !add_write(QFileInfo(job_resource_path(job.output_path)).absolutePath())) return claim;
    const auto preview = job_resource_path(job.edited_preview_source);
    if (!preview.isEmpty()) add_read(preview);
    // Only byte-copy delivery is light. Rendered photos share the heavy GPU/CPU
    // lane regardless of the selected output dimensions or requested priority.
    const auto media = job.media_type.trimmed().toLower();
    const bool photo = media == QStringLiteral("photo") || media == QStringLiteral("image");
    const bool original = job.preset_name.trimmed().compare(QStringLiteral("Original Copy"), Qt::CaseInsensitive) == 0;
    if (photo && !original) claim.lane = JobResourceLane::HeavyRender;
    else {
      for (const auto& input : claim.reads) {
        const QFileInfo info(input);
        if (!info.isFile() || info.size() < 0 || info.size() > 256 * mib) return claim;
      }
      claim.lane = JobResourceLane::LightIo;
      claim.additional_memory_bytes = 16 * mib;
    }
  } else if (job.job_kind == QStringLiteral("catalog_import")) {
    if (!QFileInfo(source).isDir()) return claim;
    const auto policy = job.metadata_json.value("duplicatePolicy").toString().trimmed().toLower();
    if (!policy.isEmpty() && policy != "skip" && policy != "reuse-identical") return claim;
    const auto selected = job.metadata_json.value("selectedPaths").toArray();
    if (selected.isEmpty() || selected.size() > 256) return claim;
    const auto preflight = job.metadata_json.value("importPreflight").toObject();
    const auto mode = job.metadata_json.value("importMode").toString().trimmed();
    if (!mode.isEmpty() && mode != "referenced" && mode != "managed") return claim;
    if (mode == "managed" && !add_write(preflight.value("photoLibraryRoot").toString())) return claim;
    const auto backup = preflight.value("backupRoot").toString();
    if (!backup.isEmpty() && !add_write(backup)) return claim;
    for (const auto& current : active) if (job_claims_conflict(claim, current)) return claim;
    qint64 bytes = 0;
    for (const auto& value : selected) {
      auto input = value.toString();
      if (QFileInfo(input).isRelative()) input = QDir(source).filePath(input);
      input = job_resource_path(input);
      const QFileInfo info(input);
      if (input.isEmpty() || !info.isFile() || info.size() < 0 || info.size() > 64 * mib ||
          !job_paths_overlap(input, source)) return claim;
      bytes += info.size();
      if (bytes > 512 * mib) return claim;
      add_read(input);
    }
    claim.lane = JobResourceLane::LightIo;
    // Admission estimate for bounded metadata parsing/catalog records; copy and
    // checksum buffers stream. This is an admission reserve, not a process RSS cap.
    claim.additional_memory_bytes = 256 * mib;
    claim.import_metadata_threads = 2;
  }
  return claim;
}

inline bool admit_job_resources(const JobResourceClaim& candidate,
                                const QVector<JobResourceClaim>& active,
                                const JobAdmissionBudget& budget) {
  if (active.isEmpty()) return true;
  if (active.size() >= budget.maximum_jobs || candidate.lane == JobResourceLane::Exclusive) return false;
  qint64 additional = candidate.additional_memory_bytes;
  for (const auto& current : active) {
    if (current.lane == JobResourceLane::Exclusive || current.lane == candidate.lane ||
        job_claims_conflict(candidate, current)) return false;
    additional += current.additional_memory_bytes;
  }
  return additional <= budget.additional_memory_bytes;
}
}  // namespace photo_editor::desktop
