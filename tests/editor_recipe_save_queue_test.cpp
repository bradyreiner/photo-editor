#include "editor_recipe_save_queue.h"
#include <QCoreApplication>
#include <iostream>
using namespace photo_editor::desktop;
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  EditorRecipeSaveQueue queue;
  EditorRecipeSaveSnapshot a{.asset_id = "a", .file_path = "/a.dng"};
  a.state.exposure = 1;
  queue.enqueue(a);
  a.state.exposure = 2;
  queue.enqueue(a);
  auto b = a; b.asset_id = "b"; b.file_path = "/b.jpg";
  queue.enqueue(b);
  const auto* active = queue.begin();
  if (!active || active->asset_id != "a" || active->file_path != "/a.dng" || active->state.exposure != 2 || queue.begin()) return 1;
  a.state.exposure = 3;
  queue.enqueue(a);
  if (active->state.exposure != 2) return 1;
  queue.complete(false);
  active = queue.begin();
  if (!active || active->asset_id != "b") return 1;
  queue.complete(true);
  active = queue.begin();
  if (!active || active->asset_id != "a" || active->state.exposure != 3) return 1;
  queue.complete(false);
  active = queue.begin();
  if (!active || active->state.exposure != 3) return 1;
  queue.complete(true);
  if (queue.pending() || queue.active() || queue.begin()) return 1;
  a.migration_evidence.insert("migrationPending", true);
  queue.enqueue(a);
  if (!queue.begin()) return 1;
  a.state.exposure = 4;
  a.migration_evidence.clear();
  queue.enqueue(a);
  queue.complete(false);
  active = queue.begin();
  if (!active || active->state.exposure != 3 || !active->migration_evidence.value("migrationPending").toBool()) return 1;
  queue.complete(true);
  active = queue.begin();
  if (!active || active->state.exposure != 4 || !active->migration_evidence.isEmpty()) return 1;
  queue.complete(true);
  // Recovery treats an unacknowledged ordinary save as a failure without
  // resurrecting it over the newer pending revision of the same asset.
  queue.enqueue(a);
  if (!queue.begin()) return 1;
  a.state.exposure = 5;
  queue.enqueue(a);
  EditorRecipeSaveQueue recovered;
  recovered.restore(queue.checkpoint());
  active = recovered.begin();
  if (!active || active->state.exposure != 5 || recovered.begin()) return 1;
  recovered.complete(true);
  if (recovered.pending() || recovered.active()) return 1;
  // A paste supersedes an older queued/in-flight save without mutating the
  // immutable active snapshot or re-enqueuing assets with no outstanding save.
  EditorRecipeSaveQueue pasted;
  if (pasted.supersede(a)) return 1;
  pasted.enqueue(a);
  const auto* old_revision = pasted.begin();
  auto newer = a;
  newer.state.exposure = 8;
  if (!pasted.supersede(newer) || old_revision->state.exposure != 5) return 1;
  newer.state.exposure = 9;
  if (!pasted.supersede(newer)) return 1;
  pasted.complete(false);
  const auto* pasted_revision = pasted.begin();
  if (!pasted_revision || pasted_revision->state.exposure != 9) return 1;
  pasted.complete(true);
  if (pasted.pending() || pasted.active()) return 1;
  // A newer manual revision must not inherit ownership from the active agent
  // save, including when an older helper fails or the outbox is reopened.
  EditorRecipeSaveQueue attributed;
  a.agent_task_id = QStringLiteral("task-a");
  attributed.enqueue(a);
  const auto* owned = attributed.begin();
  auto manual = a;
  manual.agent_task_id.clear();
  manual.state.exposure = 10;
  attributed.enqueue(manual);
  if (!owned || owned->agent_task_id != QStringLiteral("task-a")) return 1;
  attributed.complete(false);
  EditorRecipeSaveQueue attributed_recovered;
  attributed_recovered.restore(attributed.checkpoint());
  const auto* recovered_manual = attributed_recovered.begin();
  if (!recovered_manual || !recovered_manual->agent_task_id.isEmpty() ||
      recovered_manual->state.exposure != 10) return 1;
  std::cout << "Asset-bound coalescing, immutable active revision, failure ordering and retry passed.\n";
}
