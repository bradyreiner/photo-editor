# Import visual and interaction refinement

The photographic review now uses larger full-frame previews, lighter selected
borders, more space between cards, and quiet underline tabs for media filters.
The header is unboxed, with selection actions secondary to the primary import
action. Picked files show a concise source summary instead of a long filesystem
path across the header; folder browsing still shows its current path.

The source rail has named folder/file actions and compact location rows with
icons and readable paths. It drops redundant location descriptions and gives
the location list more room before scrolling.

Review uses balanced columns and a plain readiness heading. Zero exception
counts are omitted; actual blockers, duplicates, unsupported files, and
unreadable files remain visible. Optional backup configuration is neutral, and
referenced imports omit irrelevant copy estimates and organization text.

New project entry opens on request. Managed Copy and Leave in Place are directly
available. File handling and presets opens custom policies, and Use defaults
restores managed copies, skipping duplicates, date/camera organization, and both
RAW/JPEG files. The internal personal/studio profile state is retained for
compatibility, but users no longer need to choose a profile to reach file
handling controls.

Validation uses the real Main.qml shell with isolated stock-photo fixtures.
The native Import shard checks decoded previews, keyboard media selection,
managed/referenced preflight, scan-time confirmation blocking, layout bounds at
1440x900, 1280x760, and 900x700, and activation of file handling, defaults,
Leave in Place, and new-project disclosure. The Import-specific source guard and
desktop app build also pass. Screenshots, logs, and hashes are under
`docs/evidence/import-polish-2026-10-23`.

The fixture does not confirm an import against a real catalog. Existing receipt
checks use a synthetic completed job. This pass establishes the edited UI and
preflight interactions, not end-to-end import or release qualification. Broader
Timeline and job-polling failures documented in the previous pass were not
retested here.
