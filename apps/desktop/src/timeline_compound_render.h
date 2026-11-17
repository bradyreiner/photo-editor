#pragma once

#include <QSize>
#include <QString>

namespace photo_editor::desktop::timeline_compound {

// Task-owned compound files use the child timeline ID in each basename so a
// recovery journal can name only the exact files created by that operation.
[[nodiscard]] bool validate_task_output_paths(
    const QString& child_timeline_id,
    const QString& document_path,
    const QString& render_path,
    QString* error_message = nullptr);

[[nodiscard]] QString partial_render_path(const QString& render_path);

[[nodiscard]] QString filter_graph_sidecar_path(const QString& render_path);

// Accept a child cache only when ffprobe confirms a ProRes 4444 alpha stream at
// the exact sequence dimensions and ffmpeg decodes at least one sampled frame
// containing both transparent and visible pixels. The caller atomically
// publishes the validated staging file at the task-owned final path afterward.
[[nodiscard]] bool verify_transparent_prores_render(
    const QString& render_path,
    const QSize& expected_size,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop::timeline_compound
