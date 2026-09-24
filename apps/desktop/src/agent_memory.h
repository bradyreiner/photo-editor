#pragma once

#include <QVariantMap>

class QString;

namespace photo_editor::desktop {

[[nodiscard]] QString agent_memory_root_directory(const QString& app_data_location);

[[nodiscard]] QString agent_memory_style_note_path(
    const QString& app_data_location,
    const QString& style_id);

[[nodiscard]] QString agent_memory_project_note_path(
    const QString& app_data_location,
    const QString& style_id,
    const QString& scope);

[[nodiscard]] QString agent_memory_run_note_path(
    const QString& app_data_location,
    const QString& run_id,
    const QString& mode);

[[nodiscard]] QString agent_memory_issue_note_path(
    const QString& app_data_location,
    const QString& run_id,
    const QString& mode);

[[nodiscard]] QString agent_memory_tool_note_path(
    const QString& app_data_location,
    const QString& style_id);

[[nodiscard]] QVariantMap agent_memory_vault_status(const QString& app_data_location);

[[nodiscard]] QVariantMap agent_memory_vault_snapshot(const QString& app_data_location);

[[nodiscard]] QVariantMap agent_memory_continuity_state(
    const QString& app_data_location);

[[nodiscard]] QVariantMap write_agent_memory_continuity_state(
    const QString& app_data_location,
    const QVariantMap& continuity_state);

[[nodiscard]] QVariantMap export_agent_memory_vault(
    const QString& app_data_location,
    const QString& destination_directory,
    bool redacted = true);

[[nodiscard]] QVariantMap reset_agent_memory_vault(
    const QString& app_data_location,
    bool confirmed = false);

[[nodiscard]] QVariantMap write_agent_style_memory_note(
    const QString& app_data_location,
    const QVariantMap& style);

[[nodiscard]] QVariantMap write_agent_project_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& project_report);

[[nodiscard]] QVariantMap write_agent_feedback_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QString& feedback,
    const QVariantMap& context,
    const QVariantMap& last_run_report);

[[nodiscard]] QVariantMap write_agent_run_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& run_report);

[[nodiscard]] QVariantMap write_agent_issue_memory_note(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& run_report);

[[nodiscard]] QVariantMap write_agent_tool_memory_note(
    const QString& app_data_location,
    const QVariantMap& style);

[[nodiscard]] QVariantMap agent_memory_packet_for_phase(
    const QString& app_data_location,
    const QVariantMap& style,
    const QVariantMap& last_run_report,
    const QString& phase);

}  // namespace photo_editor::desktop
