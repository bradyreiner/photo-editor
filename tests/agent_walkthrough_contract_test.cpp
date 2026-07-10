#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const std::string& path) {
  std::ifstream stream(path);
  std::ostringstream content;
  content << stream.rdbuf();
  return content.str();
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

bool contains(const std::string& source, const std::string& value) {
  return source.find(value) != std::string::npos;
}

}  // namespace

int main() {
  const std::string root = PHOTO_EDITOR_SOURCE_DIR;
  const auto dialog = read_file(root + "/apps/desktop/qml/components/AgentWalkthroughDialog.qml");
  const auto studio = read_file(root + "/apps/desktop/qml/workspaces/StudioWorkspace.qml");
  const auto edit = read_file(root + "/apps/desktop/qml/workspaces/EditView.qml");

  if (!contains(dialog, "function activeModelLabel()") ||
      !contains(dialog, "function runEvents()") ||
      !contains(dialog, "agentEditController.runEvents") ||
      !contains(dialog, "fillMode: Image.PreserveAspectFit") ||
      !contains(dialog, "Show run details") ||
      !contains(dialog, "Cancel run") ||
      !contains(dialog, "Create Draft") ||
      !contains(dialog, "Use fast fallback") ||
      !contains(dialog, "function friendlyFailureMessage()") ||
      !contains(dialog, "function elapsedLabel()") ||
      !contains(dialog, "text: \"Reject\"") ||
      !contains(dialog, "text: \"Make changes\"") ||
      !contains(dialog, "text: \"Apply edit\"") ||
      !contains(dialog, "Popup.NoAutoClose") ||
      contains(dialog, "root.setPage(3)")) {
    return fail("agent walkthrough should expose the real model, live events, full proof, details, safe review actions, and guarded running state");
  }

  if (!contains(studio, "root.agentEditController.runDraftForScope(") ||
      !contains(edit, "root.agentEditController.runDraftForScope(\"\", root.agentEditController.userInstructions)") ||
      !contains(studio, "objectName: \"agentDetailsToggleButton\"") ||
      !contains(studio, "visible: true")) {
    return fail("primary agent actions should create reviewable drafts and keep Studio diagnostics reachable");
  }

  return EXIT_SUCCESS;
}
