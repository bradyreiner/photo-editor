#include "agent_task_permissions.h"
#include <iostream>

using photo_editor::desktop::agent_task_capabilities_for_plan;
#define CHECK(c, m) do { if (!(c)) { std::cerr << m << '\n'; return 1; } } while (false)

int main() {
  CHECK(agent_task_capabilities_for_plan({{"prompt", "add text and delete originals"}}).isEmpty(),
      "Free-form prompt text cannot grant native capability");
  const QVariantMap thumbnail{{"actions", QVariantList{
      QVariantMap{{"kind", "framing"}, {"summary", "Frame the photo for YouTube."}},
      QVariantMap{{"kind", "color"}, {"summary", "Brighten the person."}},
      QVariantMap{{"kind", "masking"}, {"summary", "Separate the person from the title."}},
      QVariantMap{{"kind", "text"}, {"summary", "Place the approved title behind the person."}}}}};
  const auto allowed = agent_task_capabilities_for_plan(thumbnail);
  CHECK(allowed.contains("crop") && allowed.contains("masks") && allowed.contains("graphics") &&
      allowed.contains("layers_luts") && allowed.contains("global_color") &&
      !allowed.contains("retouch") && !allowed.contains("export") && !allowed.contains("delete"),
      "Approved thumbnail actions grant only the required bounded editing categories");
  CHECK(agent_task_capabilities_for_plan({{"actions", QVariantList{
      QVariantMap{{"kind", "color"}, {"summary", "Adjust color."}}}}}) ==
      QStringList({"global_color", "curves", "hsl", "color_grading", "detail"}),
      "A color-only brief cannot grant graphics or layers");
  CHECK(agent_task_capabilities_for_plan({{"actions", QVariantList{
      QVariantMap{{"kind", "text"}, {"summary", "Add title."}},
      QVariantMap{{"kind", "delete"}, {"summary", "Remove source files."}}}}}).isEmpty(),
      "Unknown action fails closed for the entire grant");
  CHECK(agent_task_capabilities_for_plan({{"actions", QVariantList{
      QVariantMap{{"kind", "text"}, {"summary", ""}}}}}).isEmpty() &&
      agent_task_capabilities_for_plan({{"actions", QVariantList{
      QVariantMap{{"kind", "text"}, {"summary", "Add title."}, {"grant", "all"}}}}}).isEmpty(),
      "Invisible or injected action fields cannot grant authority");
  std::cout << "Approved action capability derivation passed.\n";
  return 0;
}
