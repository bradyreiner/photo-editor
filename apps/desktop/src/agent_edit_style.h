#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace photo_editor::desktop {

inline constexpr int kCurrentAgentEditStyleSchemaVersion = 1;

struct AgentEditStyleValidationResult final {
  bool valid = false;
  QStringList errors;
  QStringList warnings;
};

struct AgentEditStyle final {
  QString id;
  QString name;
  QString goal;
  QVariantMap allowed_tools;
  QVariantMap constraints;
  QVariantMap validation;
  QVariantMap batch_behavior;
  QVariantMap reference_policy;
  QVariantMap learned_profile;
  QVariantMap tool_knowledge;
  QVariantMap quality_model;
  QVariantMap project_workflow;
  QString source;
};

[[nodiscard]] AgentEditStyleValidationResult validate_agent_edit_style_object(
    const QJsonObject& object);
[[nodiscard]] AgentEditStyleValidationResult validate_agent_edit_style_json(
    const QString& style_json);
[[nodiscard]] AgentEditStyle parse_agent_edit_style_object(
    const QJsonObject& object,
    const QString& source = QString());
[[nodiscard]] QVariantMap agent_edit_style_map(const AgentEditStyle& style);
[[nodiscard]] QVariantList built_in_agent_edit_styles();
[[nodiscard]] QVariantList load_agent_edit_styles_from_directory(
    const QString& directory_path,
    QStringList* errors = nullptr);
[[nodiscard]] bool save_agent_edit_style_to_directory(
    const QString& directory_path,
    const QVariantMap& style,
    QString* error_message = nullptr);

}  // namespace photo_editor::desktop
