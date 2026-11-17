#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop::timeline_compound::producer {

enum class IdentityState {
  Matching,
  NotRunning,
  Mismatch,
  Unavailable,
};

enum class CandidateState {
  UniqueMatch,
  NoMatch,
  Ambiguous,
  Unavailable,
};

// Derives and validates the five exact task-owned siblings for one compound.
[[nodiscard]] bool validate_task_output_paths(
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message = nullptr);
[[nodiscard]] QString partial_render_path(const QString& render_path);
[[nodiscard]] QString filter_graph_sidecar_path(const QString& render_path);
[[nodiscard]] QString receipt_path(const QString& render_path);

// Seal the exact executable and argv before process launch so the task journal
// can persist the specification before any ffmpeg writer starts.
[[nodiscard]] QVariantMap make_launch_specification(
    const QString& executable_path,
    const QStringList& arguments,
    const QString& operation_id,
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message = nullptr);
[[nodiscard]] bool validate_launch_specification(
    const QVariantMap& launch_spec,
    QString* error_message = nullptr);

// Capture and atomically persist the exact running producer identity. The
// argument list excludes argv[0], which is the canonical executable.
[[nodiscard]] QVariantMap capture_receipt(
    qint64 process_id,
    const QString& executable_path,
    const QStringList& arguments,
    const QString& operation_id,
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message = nullptr);
[[nodiscard]] bool write_receipt(
    const QString& receipt_file_path,
    const QVariantMap& receipt,
    QString* error_message = nullptr);
[[nodiscard]] QVariantMap read_receipt(
    const QString& receipt_file_path,
    QString* error_message = nullptr);

// The launch spec is used only for the crash window after launch but before
// receipt persistence. A zero or ambiguous match stays fail-closed.
[[nodiscard]] CandidateState find_unique_candidate(
    const QVariantMap& launch_spec,
    QVariantMap* receipt,
    QString* error_message = nullptr);
[[nodiscard]] IdentityState inspect_receipt(
    const QVariantMap& receipt,
    QString* error_message = nullptr);
[[nodiscard]] bool stop_verified_producer(
    const QVariantMap& receipt,
    int graceful_timeout_ms,
    bool* process_stopped,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop::timeline_compound::producer
