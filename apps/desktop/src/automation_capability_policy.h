#pragma once

#include <QString>
#include <QStringList>

namespace photo_editor::desktop {

inline QStringList automation_capability_names() {
  return {
      QStringLiteral("read"),
      QStringLiteral("draft"),
      QStringLiteral("export"),
      QStringLiteral("commit"),
  };
}

inline QString automation_capability_for_command(const QString& command) {
  static const QStringList read_commands{
      QStringLiteral("photo_live_status"),
      QStringLiteral("photo_search_edit_status"),
      QStringLiteral("photo_list_models"),
      QStringLiteral("photo_selected_assets"),
      QStringLiteral("photo_read_asset"),
      QStringLiteral("photo_read_adjustments"),
      QStringLiteral("photo_read_recipe"),
      QStringLiteral("photo_get_recipe_patch_schema"),
      QStringLiteral("photo_get_style_rulebook"),
      QStringLiteral("photo_validate_recipe_patch"),
      QStringLiteral("photo_render_proof"),
      QStringLiteral("photo_validate_edit"),
      QStringLiteral("photo_validate_batch"),
      QStringLiteral("photo_export_preflight"),
      QStringLiteral("photo_plan_bulk_edit"),
  };
  if (read_commands.contains(command)) {
    return QStringLiteral("read");
  }
  if (command == QStringLiteral("photo_export_batch")) {
    return QStringLiteral("export");
  }
  if (command == QStringLiteral("photo_commit_agent_draft") ||
      command == QStringLiteral("photo_resolve_agent_review_issue")) {
    return QStringLiteral("commit");
  }
  return QStringLiteral("draft");
}

}  // namespace photo_editor::desktop
