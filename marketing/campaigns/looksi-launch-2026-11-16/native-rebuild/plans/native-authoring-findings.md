# Native campaign authoring routes / current-source inspection

Read-only investigation, 16 November 2026. No app, session, clipboard, catalog or user source was modified. Files in this folder are **plans and unexecuted import inputs**, not created native projects or approval receipts.

## Recommended first move

Prove one small canary in the running app before producing all five campaigns:

1. Use current **Library > Boards > New Boards** to create an actual 1080×1350 blank document named `LOOKSi Native Canary`. The current UI routes through `createEditCanvas()`, creates an app-owned blank source, and saves a new native document.
2. Choose **Base Image** as the target. In Edit's **Presets** management menu, enter this folder in **Import folder** and press **Import**:
   `marketing/campaigns/looksi-launch-2026-11-16/native-rebuild/plans/canary-preset-bundle`
3. Select the uniquely task-named pack/preset `LOOKSi Native Canary 2026-11-16`, then **Apply**. The supplied preset only declares the `layers` adjustment group. Before importing, verify the same preset name is not an unrelated user preset: current UI passes `overwrite=true` on import.
4. Inspect the actual native Layers tree. Expected non-base elements: editable text, editable vector blue rule, one independent original JPEG source, and a fill background. There is no cover bitmap or pixelData payload.
5. Edit the headline in Looksy, Undo, and inspect the restored text. Save Boards through the app; wait for Saved. Export through the actual app route and open the output. Reopen the saved document through Library and verify the native layer kinds and original media references.

This uses an existing visible app-supported preset import, not a catalog insertion or a fabricated project file. It counts only if the running app actually accepts, edits, saves, reopens and exports it.

The input passed a field/type/source preflight against the current source validator. It has **not** passed native admission or an app execution. `candidate-preflight.json` makes that scope explicit.

## Why this route

Current `Main.qml` no longer exposes the old Carousel workspace as the working design surface. `activeCarouselView()` returns null, and navigation to the old workspace redirects to Edit boards. New Boards opens New Canvas, calls `CarouselDocumentController::createEditCanvas()`, activates the first board in the ordinary editor, and shows Layers.

Current Edit has a native preset-bundle import field and button. `importPresetSyncBundle()` reads `photo-editor-presets-sync.json`, parses a source-agnostic adjustment patch, stores the preset through the app's preset service, and applies it through native edit handling. A native layers preset can retain separate text, source-photo and vector definitions. The old Nepal native proof itself produced a native layers preset bundle; that historical file is a structural reference, not proof of this canary.

## Fallback: current native layer clipboard

`application/x-looksy-photo-layers-v1` is the current Edit-layer MIME type. The candidate payload is `canary-photo-layers-UNEXECUTED.json`.

Envelope:

```json
{
  "schema":"looksy-photo-layers-v1",
  "width":1080,
  "height":1350,
  "selectedIds":["candidate-canary-1","candidate-canary-2","candidate-canary-3","candidate-canary-4"],
  "recipe":{"recipeVersion":12,"...":"complete required native recipe objects and arrays"}
}
```

The actual file includes the complete recipe rather than the illustrative ellipsis above. Decoder limits: 64MiB, valid native recipe, positive canvas dimensions up to 1,000,000, 1–1024 unique non-base layer IDs, no baseImageEdit, selected IDs belonging to that stack, at most 4096 masks. The native paste operation generates fresh IDs, rebases masks, retains layer pixel coordinates, adds Undo, persists and returns the newly selected layer IDs. With matching canvas sizes, normal paste has zero centering delta; **Paste Layers in Place** also explicitly preserves placement.

A helper that places this exact native MIME on the system clipboard is a transport step, not a project save. Preserve the previous clipboard, apply only to a new task-owned blank document, use the real **Paste Layers / Paste Layers in Place** action, and record actual native acceptance. A plain text `pbcopy` will not create the required MIME type. This investigation did not alter the clipboard.

The current app paste owns persistence and fresh identities. Do not write the recipe directly into a catalog record, recovery file or app-data folder.

## Route comparison

| Route | Current source finding | Use |
|---|---|---|
| New Boards UI + native layers preset | Visible current Edit route; native preset service and undoable application | Preferred canary and production path if live app confirms |
| New Boards UI + native Photo Layer clipboard | Current Cmd+V / Cmd+Shift+V path; native recipe validation, fresh IDs and persistence | Practical fallback after exact MIME transport is available |
| UI-created boards + `photo.edit_composition` | Registered operations edit the exact open asset; direct document operations support ordinary Undo; active tasks route through their journal | Good for live text/typography/revision if server enables it |
| `app.invoke_command` for registered Edit primitives | Typed native registry; requires live availability and the native owner/draft contract | Discover exact IDs/schema first; do not guess signatures or forge admission |
| Reflected Carousel methods | Registered under `unregistered_owner` in main.cpp | Discoverable but mutations unavailable; do not bypass |
| Carousel clipboard boards/components | Real source parser and native implementation exist, but the old CarouselView UI is currently unreachable | Do not rely on it without live proof; not the preferred route |
| `.looksy-design.json` package | Only `brand-kit` or `component`, not a full native document | Not a full five-document importer; old UI availability also matters |
| Direct catalog helper `upsert` | Used in the historical Nepal save script after isolated native proof | Not an app-owned creative import route; do not reuse for this rebuild |
| Flattened V2 PNG/SVG/MP4 as final native composition | A media layer can hide missing editability | Rejected substitute; rebuild with native elements and primitive sources |

## Current native API discovery and ownership

Read `docs/looksy-agent-interface.md` and the live `app.agent_guide({workflow:"design"})`. The repository development MCP is a different server from the running editor. Session tokens remain local; do not copy them into plans/logs. A disabled authoritative session must not be bypassed by falling through to another app. Root handles the actual app connection/recovery.

Use `app.list_commands` and `app.command_schema`. Reflection does not grant authority: Carousel remains unowned; Edit's allowlist includes native text, source layers, fills, shapes and transforms only through its typed resource owner. `app.invoke_command` Edit mutations require an owned agent task/draft; honor unavailable/blocked receipts.

`photo.edit_composition` is distinct: its published description says direct open-document edits support ordinary Undo and are not agent drafts. The handler checks the exact open `assetId`; if an agent task is active it delegates to that task's journal. `add_text` and `add_paragraph_text` return `createdLayerId`; subsequent text/typography/transform operations must use that exact ID. Font inspection returns real family/face inventory. Do not infer a layer from coordinates.

The board asset identity is assembled by the native Main UI as:

`board:<savedDocumentId>:<imageId-or-boardId>`

Use the live inspection result, never an invented ID. `add_source_layer` in the composition operation duplicates the current base source; it does not accept an arbitrary file path. For a different photograph, use an actually available owned `addSourceLayer` native command, the native UI media action, or the supported native layer/preset import.

Read-only discovery and conditional mutation examples are in `native-api-plan.json`. They are not executed receipts.

## Native storage and source preservation

A saved document's current schema version is 11. Catalog envelopes use `document_id`, `name`, `document_format`, `slide_count`, `document_json`; a legacy fallback uses `documentId`, `document`, and an app-managed index. Native `saveDocument()` chooses the backend and writes atomically; callers must not impersonate it with hand-written records.

Edit-board compositions live in each slide's primary `adjustmentPatch.adjustmentState.photoLayers` (with identical compatibility `layers`). The native primary source/canvas remains independent. Source-photo layers retain `sourcePath`; text retains literal copy and font/position controls; vector elements retain `vectorPath` and fill/stroke controls. Those are the structures to inspect after reopening.

The canary uses the existing raw/source photograph `marketing/animated-trailer/public/media/IMG_5071.jpg` and links it independently. It does not use the V2 finished cover, carousel or video frame. Maintain a dedicated source folder and manifest for the final projects. The native paste retains source references; it does not itself promise portable media copying. After app save/reopen, verify references still resolve and retain actual source hashes.

## Historical evidence, scoped correctly

- `.photo-editor/nepal-near-far/production-native-document.json`: six boards; native Edit photoLayers, not one flattened final cover. Source structures include text, source photographs, fill and vectorPath-backed pixel layers.
- `.photo-editor/nepal-near-far/foreground/board-2.json`: actual historical readback of a stable `board:` asset,1920×1536, top-first layers, paintBusy false.
- `foreground/text-temporary.json` then `text-undo.json`: native text changed and restored in the same historical document. This is a useful model for the required new canary proof.
- `save_separate_document.py`: uses the catalog helper's `upsert`; do not mistake its saved record and receipt for a current app-owned authoring command. Its own receipt originally had foregroundUIVerified false.
- `marketing/outputs/nepal-shared-canvases/looksy-document.json`: a native structured project example. A JSON file and isolated pixel-proof harness do not establish the current foreground edit/save/reopen route.

## Five campaign documents

`five-native-document-plans.json` names the five separate task documents, their create-canvas settings, planned board counts and proof gates. These are **not** claimed created. Start with one accepted canary; then create each named document through UI, apply per-board native layer definitions via the accepted import path, edit/inspect in app, save, export and reopen. Every main headline/caption should remain a native text object; every photograph should remain a separate source layer; graphic marks should be native vectors/shapes/fills.

For recipe campaign03, apply the color treatment through the app's actual photo controls or native adjustment properties so its grade remains editable. Do not use the finished V2 PNG as the only photograph. For the casebook, reuse actual native Nepal layers through app copy/import where needed rather than substituting flattened board exports for an editable design.
