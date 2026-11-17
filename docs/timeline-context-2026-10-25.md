# Timeline channel context actions

This follows the [animation channels and precision trim pass](timeline-channels-trim-2026-10-24.md).

## Editing behavior

Right-click a key in a channel lane to open the same context menu used by the detailed Value/Speed graph. The menu provides add, easing, copy, paste, select-all, and delete. Right-clicking an already selected key retains the entire selection; clicking a different channel first targets that channel. Right-clicking empty curve space clears the old selection and offers Add at the clicked authored frame. Opening the menu does not seek or edit the project.

Shift+F10 or the Menu key opens the channel menu from the keyboard. After a menu action, keyboard focus returns to the invoking graph; outside dismissal keeps ordinary focus behavior. The active lane scrolls into view first. The popup uses the common application menu and item components, maps the originating pointer position into the visible editor, and stays within the window. Switching the curve channel, clip/track target, or graph mode dismisses the old context menu. Closing or hiding the panel also dismisses it.

Context easing now uses the same batch operation as the interpolation control. The native test exposed that the old context helper pushed one Undo snapshot per clip key, so one Undo could restore only the last changed key. Routing through the shared selection operation restores the whole easing change with one Undo and preserves the other channel.

Locked channel lanes remain available for inspection, scrolling, selection, and copying. Numeric and mutation controls stay disabled, and the locked-target status remains visible in short panels. Copying a selected locked clip uses the read-only clipboard path. A pinned locked clip must be selected before its keys can be copied; the unavailable action explains this restriction. Cut does not change the clipboard on a locked target. Add, easing, paste, and delete remain unavailable while locked.

## Verification

Evidence is retained under [timeline-context-2026-10-25](evidence/timeline-context-2026-10-25/). Native macOS foreground activation is available again: the earlier channel/trim workflow passed its active-window gate and native interaction assertions on this follow-up. The final follow-up receipts and source/binary hashes distinguish that historical run from the final context implementation.

The extended production Main workflow exercises right-click selection preservation, grouped easing/Undo, keyboard invocation, copy/paste, adding at a different time from the playhead, targeting an inactive lane, compact popup bounds, locked mutation states and actual read-only Copy, stale-target dismissal, and context delete/Undo. It retains the channel live-edit/save-reopen and decoded trim/source-limit checks from the preceding pass.

The final app and driver built successfully. Both final native workflows passed with identical tracked source/executable hashes that remained stable during each run: seven channel/context/trim captures and thirteen shared-graph/crop/Color/filter/docking captures. The final channel workflow explicitly asserts that menu actions return keyboard ownership to the channel graph. All four focused registered tests passed, and the shared style audit reported no violations across 296 QML files. The [verification manifest](evidence/timeline-context-2026-10-25/verification.json) records exact inputs, logs, receipts, and capture hashes.

Reviewed native views:

- [Channel context menu](evidence/timeline-context-2026-10-25/native-verified/channels-context-menu.png)
- [Compact channel menu](evidence/timeline-context-2026-10-25/native-verified/channels-compact-context-menu.png)
- [Compact detailed graph](evidence/timeline-context-2026-10-25/native-graph-verified/timeline-keyframe-narrow-dock.png)
- [Trim comparison](evidence/timeline-context-2026-10-25/native-verified/trim-before-after-rate-aware.png)

Intermediate failures remain in the evidence. They exposed the grouped easing Undo path, the locked ancestor that blocked inspection, and one keyboard-menu reopening failure before explicit action focus restoration. The final native run includes actual clipboard replacement while locked, active-channel focus ownership, stale-target dismissal, and context delete/Undo. Earlier build logs also retain unrelated generated ExportView integer-conversion and Gaussian shader-length warnings; no Timeline warning was introduced by this pass.

This pass does not add cross-channel grouped selection, serialized channel comparison presets, or new render/export behavior. Reverse/nonlinear retiming combinations, audio, sustained realtime playback, and broader release-readiness qualifications retain their previous limits.

## Manual review

1. Select several Scale keys, right-click one, choose Ease out, and Undo once. Confirm all keys restore and Position stays unchanged.
2. Right-click a Position key without activating that lane first. Confirm its context action changes Position alone.
3. Put the playhead at a different time, right-click empty curve space, and add a key. Confirm its timing follows the pointer. Check Copy/Paste and Delete/Undo.
4. Narrow the dock and use Shift+F10. Check menu bounds, keyboard navigation, Escape, and reopening the menu after scrolling.
5. Lock the track. Inspect and copy keys, verify mutation actions and numeric inputs are disabled, then unlock and repeat Delete/Undo. Change the target while a menu is open and confirm it closes.
