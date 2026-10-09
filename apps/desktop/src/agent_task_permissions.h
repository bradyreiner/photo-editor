#pragma once

#include <QStringList>
#include <QVariantMap>

namespace photo_editor::desktop {

// Native code derives permissions from the visible, immutable approved actions.
// The model cannot provide raw tool grants or bypass the project constraints.
inline QStringList agent_task_capabilities_for_plan(const QVariantMap& plan) {
  QStringList capabilities;
  const auto append = [&capabilities](const QStringList& values) {
    for (const auto& value : values)
      if (!capabilities.contains(value)) capabilities.append(value);
  };
  const auto actions = plan.value(QStringLiteral("actions")).toList();
  if (actions.size() > 6) return {};
  for (const auto& value : actions) {
    const auto action = value.toMap();
    const auto summary = action.value(QStringLiteral("summary")).toString().trimmed();
    if (summary.isEmpty() || summary.size() > 180 || action.size() != 2) return {};
    const auto kind = action.value(QStringLiteral("kind")).toString();
    if (kind == "color") append({"global_color", "curves", "hsl", "color_grading", "detail"});
    else if (kind == "framing") append({"crop"});
    else if (kind == "masking") append({"masks"});
    else if (kind == "text") append({"graphics", "layers_luts"});
    else if (kind == "layers" || kind == "effects") append({"layers_luts"});
    else return {};
  }
  return capabilities;
}

}  // namespace photo_editor::desktop
