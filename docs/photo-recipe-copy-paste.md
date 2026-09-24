# Copy and paste photo edits

In Library or Edit:

1. Select the source photo and press **Ctrl+C** or **Cmd+C** on Mac, or choose **Edit → Copy Edits…**.
2. Choose Light, Color, Detail, Crop & Geometry, RAW, or Masks & Layers, then click **Copy Edits**. With multiple source photos selected, the active photo is the source named in the chooser.
3. Select one or more destination photos and press **Ctrl+V** or **Cmd+V**, or choose **Edit → Paste Edits**.

Click the first destination and Shift-click the last to select the entire range, including off-screen photos. Repeated Shift-clicks extend or shrink from the original anchor. Ctrl/Cmd-click adds or removes individual photos; Ctrl/Cmd+Shift-click adds a range. A normal click replaces the selection. With no batch checked, paste targets the active photo.

Paste replaces the selected groups and keeps the destination's other groups. Masks and layers transfer together, including local adjustments. Copying a neutral group resets that group on the destinations. Input and device profile bindings stay with each destination. Video selections are skipped.

The paste notification offers **Undo** for the entire operation. **Edit → Undo Pasted Edits** remains available after the notification closes. A single active-photo paste also appears in the editor's undo history. The existing batch history supports redo.

Text fields retain normal text copy/paste. Timeline clips and Carousel layers retain their own clipboard shortcuts. Copy uses a dedicated system clipboard format; copying unrelated text replaces the copied recipe. Canceling the chooser leaves the clipboard intact.

## Local checks

```sh
cmake --build build/release-macos --target actual_app_workflow_benchmark_driver editor_recipe_save_queue_test -j 6
ctest --test-dir build/release-macos -R '^(photo_recipe_clipboard_workflow_test|editor_recipe_save_queue_test)$' --output-on-failure
```

The macOS workflow test loads the actual Main.qml, sends native Qt window keyboard/pointer events, and captures the chooser and batch result. It uses isolated settings and fixture photos, disables editor persistence, points catalog helpers at a test URL, and restores the previous system clipboard. Its report and screenshots are under `build/release-macos/tests/photo-recipe-clipboard-proof/`. Run it in a local GUI session.

Validated on September 10, 2026: 31 native workflow checks passed with a 200-photo fixture, plus the recipe-state and save-queue tests. The Release app built and relaunched successfully.

The test covers selective copy, Ctrl/Cmd shortcuts, batch and single-photo paste, unchanged groups, masks with layers, unsaved edits, cached-photo reopening, neutral recipes, undo/redo, non-photo targets, duplicate targets, and text-field clipboard behavior. The queue test checks that an outstanding older save cannot replace the pasted revision.
